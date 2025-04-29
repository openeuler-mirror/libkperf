/*
 * Optimization Notes:
 *
 * This program constructs a multi-threaded task, where each task consists of three phases:
 *
 *   1. on-CPU computation:
 *      Two modes are provided:
 *      - inefficient: Simulates inefficient computation using heavy floating-point operations (default).
 *      - efficient: Uses integers instead of floating-point numbers for optimized computation 
 *        (though more efficient, overall time remains almost unchanged as off-CPU phase (synchronous IO) is the bottleneck).
 *
 *   2. IO operation phase:
 *      Three modes are provided:
 *        - global: Write to a single file protected by a global lock (baseline).
 *        - split: Each thread writes to its own file (reduces lock contention).
 *        - async: Asynchronous IO, enqueues data for background batch writing (previous version lacked batching, causing worse performance).
 *
 *   3. Supplemental on-CPU computation.
 *
 * Usage (command-line argument order):
 *   [numThreads] [tasksPerThread] [cpuIterations] [ioDataSize] [ioWrites] [ioMode] [onCpuMode]
 *
 * Example (your given test parameters, plus onCpuMode parameter):
 *   ./blocked_sample_io 4 50 100000 5000 3000 global inefficient
 *
 * Where:
 *    ioMode: global|split|async
 *    onCpuMode: inefficient (inefficient implementation) or efficient (optimized implementation)
 *
 * Note: If the user attempts to optimize the CPU computation part using the efficient on-CPU mode, 
 *       the overall runtime remains almost unchanged, proving that the bottleneck lies mainly in the off-CPU part (synchronous IO and lock contention).
 */
#include <iostream>
#include <fstream>
#include <sstream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <vector>
#include <functional>
#include <chrono>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <string>
#include <cmath>
using namespace std;
using namespace std::chrono;

// Define IO mode enumeration
enum class IOMode { GLOBAL, SPLIT, ASYNC };
IOMode currentIOMode = IOMode::GLOBAL; // Default IO mode

//-------------------------------------------------------------
// on-CPU simulation: Implementation of two computation methods
//-------------------------------------------------------------
// Inefficient CPU work: Heavy loop computation to prevent compiler optimization
void doOnCpuWorkInefficient(int iterations) {
    volatile double dummy = 1.0;
    for (int i = 0; i < iterations; i++) {
        dummy = dummy * 1.000001 + 0.000001;
    }
    (void)dummy;
}

// Efficient CPU work: Use integers to simulate decimals for optimized computation
void doOnCpuWorkEfficient(int iterations) {
    long long dummy = 1000000; // Use integers to simulate decimals, assuming precision of 1e-6
    for (int i = 0; i < iterations; i++) {
        dummy = dummy * 1000001 / 1000000 + 1;
    }
    (void)dummy;
}

// Global flag to decide which on-CPU computation method to use (default is inefficient)
bool efficientOnCpu = false;

// Encapsulated on-CPU work interface, calls corresponding implementation based on efficientOnCpu
void doOnCpuWork(int iterations) {
    if (efficientOnCpu) {
        doOnCpuWorkEfficient(iterations);
    } else {
        doOnCpuWorkInefficient(iterations);
    }
}

//-------------------------------------------------------------
// GLOBAL mode: Global file and mutex
//-------------------------------------------------------------
mutex globalFileMutex;
ofstream globalSyncFile;  // Global file

//-------------------------------------------------------------
// Asynchronous IO Manager (optimized): Batch writing to reduce flush frequency
//-------------------------------------------------------------
class AsyncIOManager {
private:
    queue<string> msgQueue;
    mutex mtx;
    condition_variable cv;
    atomic<bool> stop;
    thread worker;
    ofstream outFile;
    const size_t batchSize; // Number of messages written per batch

public:
    AsyncIOManager(const string& filename, size_t batchSize = 50)
      : stop(false), batchSize(batchSize) 
    {
        outFile.open(filename, ios::out | ios::trunc);
        if (!outFile.is_open()){
            cerr << "Failed to open file: " << filename << endl;
        }
        worker = thread([this]() { this->process(); });
    }

    ~AsyncIOManager(){
        {
            lock_guard<mutex> lock(mtx);
            stop = true;
        }
        cv.notify_one();
        if(worker.joinable()){
            worker.join();
        }
        if(outFile.is_open()){
            outFile.close();
        }
    }

    // Push message to be written into the queue
    void push(const string &msg) {
         {
             lock_guard<mutex> lock(mtx);
             msgQueue.push(msg);
         }
         cv.notify_one();
    }

private:
    // Background thread processes batch writes
    void process() {
         while (true) {
             vector<string> localBatch;
             {
                 unique_lock<mutex> lock(mtx);
                 cv.wait(lock, [this]() { return stop || !msgQueue.empty(); });
                 while (!msgQueue.empty() && localBatch.size() < batchSize) {
                     localBatch.push_back(msgQueue.front());
                     msgQueue.pop();
                 }
                 if (stop && localBatch.empty()) {
                     break;
                 }
             }
             // Merge and write batch, then flush
             if (outFile.is_open()) {
                 string batchStr;
                 for (const auto &msg : localBatch) {
                     batchStr.append(msg);
                 }
                 outFile << batchStr;
                 outFile.flush();
             }
         }
    }
};

AsyncIOManager *asyncIO = nullptr;  // Global pointer to asynchronous IO manager

//-------------------------------------------------------------
// Thread Pool: Manages worker threads and task queue
//-------------------------------------------------------------
class ThreadPool {
public:
    ThreadPool(size_t threads);
    ~ThreadPool();
    void enqueue(function<void()> task);
    void wait();

private:
    vector<thread> workers;
    queue<function<void()>> tasks;
    mutex queue_mutex;
    condition_variable condition;
    atomic<bool> stop;
    atomic<int> active_tasks;
    condition_variable cv_finished;
};

ThreadPool::ThreadPool(size_t threads) : stop(false), active_tasks(0) {
    for (size_t i = 0; i < threads; i++) {
        workers.emplace_back([this, i]() {
            while (true) {
                function<void()> task;
                {
                    unique_lock<mutex> lock(this->queue_mutex);
                    this->condition.wait(lock, [this]() {
                        return this->stop.load() || !this->tasks.empty();
                    });
                    if (this->stop.load() && this->tasks.empty())
                        return;
                    task = move(this->tasks.front());
                    this->tasks.pop();
                    active_tasks++;
                }
                task();
                {
                    lock_guard<mutex> lock(this->queue_mutex);
                    active_tasks--;
                    if (tasks.empty() && active_tasks == 0) {
                        cv_finished.notify_all();
                    }
                }
            }
        });
    }
}

ThreadPool::~ThreadPool() {
    {
        lock_guard<mutex> lock(queue_mutex);
        stop.store(true);
    }
    condition.notify_all();
    for (thread &worker : workers) {
        if (worker.joinable())
            worker.join();
    }
}

void ThreadPool::enqueue(function<void()> task) {
    {
        lock_guard<mutex> lock(queue_mutex);
        tasks.push(move(task));
    }
    condition.notify_one();
}

void ThreadPool::wait() {
    unique_lock<mutex> lock(queue_mutex);
    cv_finished.wait(lock, [this]() {
        return tasks.empty() && active_tasks == 0;
    });
}

//-------------------------------------------------------------
// Helper functions: Print divider and usage instructions
//-------------------------------------------------------------
void printDivider() {
    cout << string(60, '-') << endl;
}

void printUsage(const char* programName) {
    cout << "Usage: " << programName << " [numThreads] [tasksPerThread] [cpuIterations] [ioDataSize] [ioWrites] [ioMode] [onCpuMode]" << endl;
    cout << "    numThreads:      Number of worker threads (default: 4)" << endl;
    cout << "    tasksPerThread:  Number of tasks per thread (default: 50)" << endl;
    cout << "    cpuIterations:   Number of on-CPU computation iterations (default: 100000)" << endl;
    cout << "    ioDataSize:      Number of characters written per synchronous IO operation (default: 5000)" << endl;
    cout << "    ioWrites:        Number of IO operations per task (default: 3000)" << endl;
    cout << "    ioMode:          IO mode, options: global, split, async (default: global)" << endl;
    cout << "    onCpuMode:       on-CPU mode, options: inefficient, efficient (default: inefficient)" << endl;
}

//-------------------------------------------------------------
// GLOBAL mode IO operation: Write to global file with global lock
//-------------------------------------------------------------
void doGlobalIOWork(int taskId, int ioDataSize, int ioWrites) {
    stringstream ss;
    ss << "Task " << taskId << " data: ";
    for (int i = 0; i < ioDataSize; i++) {
        ss << "X";
    }
    ss << "\n";
    string data = ss.str();
    for (int i = 0; i < ioWrites; i++) {
        {
            lock_guard<mutex> lock(globalFileMutex);
            if (globalSyncFile.is_open()) {
                globalSyncFile << data;
                globalSyncFile.flush();
            }
        }
        doOnCpuWork(1000);
    }
}

//-------------------------------------------------------------
// SPLIT mode IO operation: Each thread writes to its own file
//-------------------------------------------------------------
void doSplitIOWork(int taskId, int ioDataSize, int ioWrites) {
    stringstream ss;
    ss << "Task " << taskId << " data: ";
    for (int i = 0; i < ioDataSize; i++) {
        ss << "X";
    }
    ss << "\n";
    string data = ss.str();
    static thread_local ofstream localFile;
    static thread_local bool initialized = false;
    if (!initialized) {
        auto tid = this_thread::get_id();
        hash<thread::id> hasher;
        size_t id_hash = hasher(tid);
        string filename = "split_output_" + to_string(id_hash) + ".txt";
        localFile.open(filename, ios::out | ios::trunc);
        if (!localFile.is_open()) {
            cerr << "Failed to open file: " << filename << endl;
        }
        initialized = true;
    }
    for (int i = 0; i < ioWrites; i++) {
        localFile << data;
        localFile.flush();
        doOnCpuWork(1000);
    }
}

//-------------------------------------------------------------
// ASYNC mode IO operation: Push data into asynchronous queue
//-------------------------------------------------------------
void doAsyncIOWork(int taskId, int ioDataSize, int ioWrites) {
    stringstream ss;
    ss << "Task " << taskId << " data: ";
    for (int i = 0; i < ioDataSize; i++) {
        ss << "X";
    }
    ss << "\n";
    string data = ss.str();
    for (int i = 0; i < ioWrites; i++) {
        if (asyncIO) {
            asyncIO->push(data);
        }
        doOnCpuWork(1000);
    }
}

//-------------------------------------------------------------
// Task processing: on-CPU computation -> IO operation -> small amount of on-CPU computation
//-------------------------------------------------------------
void processTask(int taskId, int cpuIterations, int ioDataSize, int ioWrites) {
    // Phase 1: on-CPU computation (choose implementation based on onCpuMode)
    doOnCpuWork(cpuIterations);

    // Phase 2: IO operation, choose execution method based on current IO mode
    if (currentIOMode == IOMode::GLOBAL) {
        doGlobalIOWork(taskId, ioDataSize, ioWrites);
    } else if (currentIOMode == IOMode::SPLIT) {
        doSplitIOWork(taskId, ioDataSize, ioWrites);
    } else if (currentIOMode == IOMode::ASYNC) {
        doAsyncIOWork(taskId, ioDataSize, ioWrites);
    }

    // Phase 3: Small amount of additional on-CPU computation
    doOnCpuWork(cpuIterations / 10);
}

//-------------------------------------------------------------
// main function: Parse arguments, initialize IO & on-CPU modes, start thread pool, and measure elapsed time
//-------------------------------------------------------------
int main(int argc, char* argv[]) {
    // Default parameters
    int numThreads    = 4;
    int tasksPerThread = 50;
    int cpuIterations  = 100000;
    int ioDataSize     = 5000;
    int ioWrites       = 3000;
    string ioModeStr   = "global";       // Default IO mode
    string onCpuModeStr = "inefficient";  // Default on-CPU mode

    // Argument check and help information
    if (argc > 1) {
        if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
            printUsage(argv[0]);
            return 0;
        }
    }
    if (argc > 1) { numThreads = atoi(argv[1]); }
    if (argc > 2) { tasksPerThread = atoi(argv[2]); }
    if (argc > 3) { cpuIterations = atoi(argv[3]); }
    if (argc > 4) { ioDataSize = atoi(argv[4]); }
    if (argc > 5) { ioWrites = atoi(argv[5]); }
    if (argc > 6) { ioModeStr = argv[6]; }
    if (argc > 7) { onCpuModeStr = argv[7]; }

    // Determine current IO mode based on ioMode parameter
    if (ioModeStr == "global") {
        currentIOMode = IOMode::GLOBAL;
        cout << "Using GLOBAL mode: Writing to global file with global mutex protection" << endl;
    } else if (ioModeStr == "split") {
        currentIOMode = IOMode::SPLIT;
        cout << "Using SPLIT mode: Each thread writes to its own file, reducing lock granularity" << endl;
    } else if (ioModeStr == "async") {
        currentIOMode = IOMode::ASYNC;
        cout << "Using ASYNC mode: Asynchronous IO, background thread performs batch writes" << endl;
    } else {
        cout << "Unknown IO mode, defaulting to GLOBAL mode" << endl;
        currentIOMode = IOMode::GLOBAL;
    }

    // Determine on-CPU mode based on onCpuMode parameter
    if (onCpuModeStr == "efficient") {
        efficientOnCpu = true;
        cout << "Using efficient on-CPU implementation" << endl;
    } else {
        efficientOnCpu = false;
        cout << "Using inefficient on-CPU implementation (default)" << endl;
    }

    int totalTasks = numThreads * tasksPerThread;
    printDivider();
    cout << "Program configuration:" << endl;
    cout << "    Number of worker threads (numThreads):          " << numThreads << endl;
    cout << "    Number of tasks per thread (tasksPerThread):    " << tasksPerThread << endl;
    cout << "    Total number of tasks:                          " << totalTasks << endl;
    cout << "    On-CPU computation iterations (cpuIterations):  " << cpuIterations << endl;
    cout << "    Characters written per IO operation (ioDataSize): " << ioDataSize << endl;
    cout << "    Number of IO operations per task (ioWrites):    " << ioWrites << endl;
    cout << "    IO mode (ioMode):                               " << ioModeStr << endl;
    cout << "    on-CPU mode (onCpuMode):                        " << onCpuModeStr << endl;
    printDivider();

    // Perform necessary initialization based on IO mode
    if (currentIOMode == IOMode::GLOBAL) {
        globalSyncFile.open("global_output.txt", ios::out | ios::trunc);
        if (!globalSyncFile.is_open()){
            cerr << "Failed to open global_output.txt file. Please check permissions or path." << endl;
            return 1;
        }
    } else if (currentIOMode == IOMode::ASYNC) {
        asyncIO = new AsyncIOManager("async_output.txt", 50);
    }

    // Create thread pool, distribute tasks, and measure total elapsed time
    ThreadPool pool(numThreads);
    auto startTime = high_resolution_clock::now();
    for (int i = 0; i < totalTasks; i++) {
        pool.enqueue([=]() {
            processTask(i, cpuIterations, ioDataSize, ioWrites);
        });
    }
    pool.wait();
    auto endTime = high_resolution_clock::now();
    duration<double> elapsed = endTime - startTime;

    // Cleanup resources
    if (currentIOMode == IOMode::GLOBAL) {
        globalSyncFile.close();
    } else if (currentIOMode == IOMode::ASYNC) {
        delete asyncIO;
        asyncIO = nullptr;
    }

    printDivider();
    cout << "Completed " << totalTasks << " tasks in " 
         << fixed << setprecision(2) << elapsed.count() << " seconds." << endl;
    cout << "Current IO mode: " << ioModeStr << ", on-CPU mode: " << onCpuModeStr << endl;
    cout << "Optimization direction: Reducing lock granularity/scattered writes or adopting batch asynchronous IO can effectively alleviate off-CPU bottlenecks;" << endl;
    cout << "         Even with an efficient on-CPU implementation, there will be no significant impact on overall runtime." << endl;
    printDivider();

    return 0;
}
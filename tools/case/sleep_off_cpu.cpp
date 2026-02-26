#include <iostream>
#include <vector>
#include <chrono>
#include <thread>
#include <unistd.h> // 添加这个头文件以使用 getpid()

void on_cpu_task(int workload)
{
    volatile int res = 0;
    for (int i = 0; i < workload; i++) {
        res += i;
    }
}

void off_cpu_task(std::chrono::milliseconds duration)
{
    std::this_thread::sleep_for(duration);
}

int main()
{
    std::cout << "Process ID: " << getpid() << std::endl;
    int workload = 100000000;
    auto duration = std::chrono::milliseconds(500);

    auto on_cpu_start = std::chrono::high_resolution_clock::now();
    on_cpu_task(workload);
    auto on_cpu_end = std::chrono::high_resolution_clock::now();

    auto off_cpu_start = std::chrono::high_resolution_clock::now();
    off_cpu_task(duration);
    auto off_cpu_end = std::chrono::high_resolution_clock::now();

    auto on_cpu_time = std::chrono::duration_cast<std::chrono::nanoseconds>(on_cpu_end - on_cpu_start);
    auto off_cpu_time = std::chrono::duration_cast<std::chrono::nanoseconds>(off_cpu_end - off_cpu_start);

    std::cout << "On CPU time: " << on_cpu_time.count() << " ns" << std::endl;
    std::cout << "Off CPU time: " << off_cpu_time.count() << " ns" << std::endl;

    return 0;
}


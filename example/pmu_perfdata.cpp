/******************************************************************************
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 * libkperf licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *     http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR
 * PURPOSE.
 * See the Mulan PSL v2 for more details.
 * Author: Mr.Gan
 * Create: 2025-07-08
 * Description: Generate perf.data for sampling.
 ******************************************************************************/
// g++ -g pmu_perfdata.cpp -I /path/to/install/include/ -L /path/to/install/lib/ -lkperf -lsym -O3 -o pmu_perfdata
#include "pmu.h"
#include "symbol.h"
#include "pcerrc.h"
#include <vector>
#include <string>
#include <string.h>
#include <sstream>
#include <algorithm>
#include <iostream>
#include <signal.h>
#include <sys/wait.h>
#include <chrono>
#include <atomic>

using namespace std;

int pd = 0;
atomic<bool> toRead(false);
bool running = true;
PmuFile file = NULL;
int lastErr = SUCCESS;
bool verbose = false;
int64_t startTs = 0;
bool interrupt = false;

struct Param {
    vector<string> events;
    bool useBrbe = false;
    vector<string> command;
    string dataPath = "./libkperf.data";
    vector<pid_t> pidList;
    unsigned duration = UINT32_MAX;
    unsigned freq = 4000;
    unsigned interval = 1000; // ms
};

int64_t GetTime()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch())
            .count();
}

void Ts(const string msg)
{
    if (verbose) {
        cout << "[" << GetTime() - startTs << "]" << msg << "\n";
    }
}

void Split(const string &eventStr, vector<string> &events)
{
    stringstream ss(eventStr);
    string item;
    
    while (getline(ss, item, ',')) {
        item.erase(remove_if(item.begin(), item.end(), ::isspace), item.end());
        if (!item.empty()) {
            events.push_back(item);
        }
    }
}

static void PrintHelp()
{
    std::cerr << "Samping and generate perf.data: pmu_perfdata -e <event1>,<event2> -d <duration> -o <perf_data_path> -- <command>\n";
    std::cerr << "Sampling an existed pid:         pmu_perfdata -e <event1>,<event2> -p <pid> -o <perf_data_path>\n";
    std::cerr << "Use brbe:                       pmu_perfdata -e <event1>,<event2> -b -p <pid> -o <perf_data_path>\n";
    std::cerr << "Options:\n";
    std::cerr << "      -e <event1>,<event2>        event list. default: cycles\n";
    std::cerr << "      -b                          whether to use brbe.\n";
    std::cerr << "      -o <path>                   output file path. default: ./libkperf.data\n";
    std::cerr << "      -d <count>                  count of reading: default: UINT32_MAX\n";
    std::cerr << "      -I <milliseconds>           interval for reading buffer. unit: ms. default: 1000\n";
    std::cerr << "      -p <pid>                    pid of process to attach.\n";
    std::cerr << "      -F <freq>                   sampling frequency. default: 4000\n";
    std::cerr << "      -v                          print verbose log.\n";
}

static int ToInt(const char *str)
{
    char *endptr;
    long num = strtol(str, &endptr, 10);
    if (*endptr != 0) {
        throw invalid_argument("invalid arg: " + string(str));
    }

    return static_cast<int>(num);
}

static Param ParseArgs(int argc, char** argv)
{
    Param param;
    bool inCmd = false;
    for (int i = 1; i < argc; ++i) {
        if (inCmd) {
            param.command.push_back(argv[i]);
        } else if (strcmp(argv[i], "-e") == 0 && i+1 < argc) {
            Split(argv[i+1], param.events);
            ++i;
        } else if (strcmp(argv[i], "-b") == 0) {
            param.useBrbe = true;
        } else if (strcmp(argv[i], "--") == 0) {
            inCmd = true;
        } else if (strcmp(argv[i], "-o") == 0 && i+1 < argc) {
            param.dataPath = argv[i+1];
            ++i;
        } else if (strcmp(argv[i], "-d") == 0 && i+1 < argc) {
            param.duration = ToInt(argv[i+1]);
            ++i;
        } else if (strcmp(argv[i], "-p") == 0 && i+1 < argc) {
            param.pidList.push_back(ToInt(argv[i+1]));
            ++i;
        } else if (strcmp(argv[i], "-F") == 0 && i+1 < argc) {
            param.freq = ToInt(argv[i+1]);
            ++i;
        } else if (strcmp(argv[i], "-I") == 0 && i+1 < argc) {
            param.interval = ToInt(argv[i+1]);
            ++i;
        } else if (strcmp(argv[i], "-v") == 0) {
            verbose = true;
        } else {
            PrintHelp();
            exit(0);
        }
    }

    if (param.command.empty() && param.pidList.empty()) {
        PrintHelp();
        exit(0);
    }

    if (param.events.empty()) {
        param.events.push_back("cycles");
    }

    return param;
}

bool AllPidExit(const Param &param)
{
    bool allExit = true;
    for (auto &pid : param.pidList) {
        if (kill(pid, 0) == 0) {
            allExit = false;
            break;
        }
    }
    return allExit;
}

void FreeEvtList(char **evtlist, size_t size)
{
    for (int i = 0;i < size; ++i) {
        free(evtlist[i]);
    }
}

void AsyncReadAndWrite()
{
    while(running) {
        if (!toRead.load(memory_order_acquire)) {
            usleep(1000);
            continue;
        }

        Ts("to read samples");
        PmuData *data = nullptr;
        int len = PmuRead(pd, &data);
        Ts("read samples");
        if (len < 0) {
            lastErr = Perrorno();
            continue;
        }
        if (PmuWriteData(file, data, len) != SUCCESS) {
            lastErr = Perrorno();
            PmuDataFree(data);
            continue;
        }
        PmuDataFree(data);
        Ts("write data");
        toRead.store(false, memory_order_release);
    }
}

int Collect(const Param &param)
{
    pthread_t t;
    pthread_create(&t, NULL, (void*(*)(void*))AsyncReadAndWrite, NULL);

    PmuAttr attr = {0};
    char *evtlist[param.events.size()];
    for (int i = 0;i < param.events.size(); ++i) {
        evtlist[i] = strdup(param.events[i].c_str());
    }
    int pidlist[param.pidList.size()];
    for (int i = 0;i < param.pidList.size(); ++i) {
        pidlist[i] = param.pidList[i];
    }
    attr.evtList = evtlist;
    attr.numEvt = param.events.size();
    attr.pidList = pidlist;
    attr.numPid = param.pidList.size();
    attr.excludeKernel = 1;
    attr.useFreq = 1;
    attr.freq = param.freq;
    attr.symbolMode = RESOLVE_ELF;

    if (param.useBrbe) {
        attr.branchSampleFilter = KPERF_SAMPLE_BRANCH_ANY | KPERF_SAMPLE_BRANCH_USER;
    }

    pd = PmuOpen(SAMPLING, &attr);
    if (pd < 0) {
        FreeEvtList(evtlist, param.events.size());
        return Perrorno();
    }
    file = PmuBeginWrite(param.dataPath.c_str(), &attr);
    if (file == NULL) {
        FreeEvtList(evtlist, param.events.size());
        return Perrorno();
    }
    PmuEnable(pd);
    Ts("pmu open");
    for (int i = 0; i < param.duration; ++i) {
        usleep(1000 * param.interval);
        if (lastErr != SUCCESS) {
            cerr << Perror() << "\n";
            break;
        }
        if (interrupt) {
            cout << "Ctrl+C\n";
            break;
        }
        while(toRead.load(memory_order_acquire));
        toRead.store(true, memory_order_release);

        if (AllPidExit(param)) {
            break;
        }
    }
    Ts("finish sampling");
    PmuDisable(pd);
    while(toRead.load(memory_order_acquire));
    Ts("finish read samples");
    PmuEndWrite(file);
    PmuClose(pd);
    running = false;
    pthread_join(t, NULL);

    FreeEvtList(evtlist, param.events.size());
    Ts("pmu close");
    return 0;
}

bool ExecuteCommand(Param &param)
{
    auto &command = param.command;
    pid_t pid = fork();
    if (pid == -1) {
        perror("fork failed");
        return false;
    } else if (pid == 0) { // Child process
        char **argv = new char *[command.size() + 1];
        for (size_t i = 0; i < command.size(); ++i) {
            argv[i] = strdup(command[i].c_str());
        }
        argv[command.size()] = NULL;

        execve(argv[0], argv, NULL);

        perror("execve failed");
        for (size_t i = 0; i < command.size(); ++i) {
            free(argv[i]);
        }
        delete[] argv;
        exit(EXIT_FAILURE);
    } else {
        param.pidList.push_back(pid);
    }

    return true;
}

void TermBySignal(int sig) {
    interrupt = true;
}

int main(int argc, char** argv)
{
    try {
        startTs = GetTime();
        signal(SIGINT, TermBySignal);
        auto param = ParseArgs(argc, argv);
        if (!param.command.empty() && !ExecuteCommand(param)) {
            return 0;
        }
        auto err = Collect(param);
        if (err != 0) {
            cout << "Failed to collect: " << Perror() << "\n";
        }
        if (!param.command.empty() && !param.pidList.empty()) {
            kill(param.pidList[0], SIGTERM);
        }
        Ts("end");
    } catch (exception &ex) {
        cout << "Failed: " << ex.what() << "\n";
    }

    return 0;
}

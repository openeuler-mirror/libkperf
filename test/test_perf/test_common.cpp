/******************************************************************************
 * Copyright (c) Huawei Technologies Co., Ltd. 2024. All rights reserved.
 * libkperf licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *     http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR
 * PURPOSE.
 * See the Mulan PSL v2 for more details.
 * Author: Mr.Gan
 * Create: 2024-04-24
 * Description: Common functions for unit test.
 ******************************************************************************/
#include <thread>
#include <unistd.h>
#include "process_map.h"
#include "test_common.h"
using namespace std;

pid_t RunTestApp(const string &name)
{
    char myDir[PATH_MAX] = {0};
    readlink("/proc/self/exe", myDir, sizeof(myDir) - 1);
    auto pid = vfork();
    if (pid == 0) {
        // Bind test_perf to cpu 0 and bind test app to cpu 1.
        cpu_set_t mask;
        CPU_ZERO(&mask);
        CPU_SET(1, &mask);
        sched_setaffinity(0, sizeof(mask), &mask);
        setpgid(0, 0);
        string fullPath = string(dirname(myDir)) + "/case/" + name;
        char *const *dummy = nullptr;
        execvp(fullPath.c_str(), dummy);
        _exit(errno);
    }

    return pid;
}

void KillApp(pid_t pid)
{
    killpg(pid, SIGKILL);
}

void DumpPmuData(const PmuData *data)
{
    cout  << "evt: " << data->evt << " pid: " << data->pid << " comm: " << data->comm;
    auto stack = data->stack;
    if (stack != nullptr && stack->symbol != nullptr) {
        cout << " sym: " << stack->symbol->symbolName << " mod: " << stack->symbol->module
            << " src: " << stack->symbol->fileName << ":" << stack->symbol->lineNum;
    }
    cout << "\n";
}

bool FoundAllTids(PmuData *data, int len, pid_t pid)
{
    set<int> sampledTid;
    for (int i=0;i<len;++i) {
        sampledTid.insert(data[i].tid);
    }

    int numTid = 0;
    int *tids = GetChildTid(pid, &numTid);
    bool foundAllTid = true;
    for (int i=0;i<numTid;++i) {
        if (tids[i] == pid) {
            continue;
        }
        if (sampledTid.find(tids[i]) == sampledTid.end()) {
            foundAllTid = false;
            break;
        }
    }
    delete[] tids;

    return foundAllTid;
}

vector<pid_t> GetChildPid(pid_t pid)
{
    string childPath = "/proc/" + to_string(pid) + "/task/" + to_string(pid) + "/children";
    ifstream in(childPath);
    vector<pid_t> ret;
    while (!in.eof()) {
        string pidStr;
        in >> pidStr;
        if (pidStr.empty()) {
            continue;
        }
        ret.push_back(stoi(pidStr));
    }

    return ret;
}


bool FoundAllChildren(PmuData *data, int len, pid_t pid)
{
    // Find all subprocess pids from <pid> in pmu data.
    // Return true if all children are found.
    set<int> sampledPid;
    for (int i=0;i<len;++i) {
        sampledPid.insert(data[i].pid);
    }

    int numTid = 0;
    auto childPids = GetChildPid(pid);
    bool foundAllPid = true;
    for (auto child : childPids) {
        if (sampledPid.find(child) == sampledPid.end()) {
            foundAllPid = false;
            cout << "Cannot find pid: " << child << "\n";
            break;
        }
    }

    return foundAllPid;
}

static void DelayContinueThread(pid_t pid, int milliseconds)
{
    usleep(1000 * milliseconds);
    kill(pid, SIGCONT);
}

void DelayContinue(pid_t pid, int milliseconds)
{
    std::thread th(DelayContinueThread, pid, milliseconds);
    th.detach();
}

unsigned GetCpuNums()
{
    return sysconf(_SC_NPROCESSORS_ONLN);
}

bool CheckDataEvt(PmuData *data, int len, std::string evt)
{
    for (int i=0;i<len;++i) {
        if (string(data[i].evt) != evt) {
            return false;
        }
    }
    return true;
}

bool CheckDataPid(PmuData *data, int len, int pid)
{
    for (int i=0;i<len;++i) {
        if (data[i].pid != pid) {
            return false;
        }
    }
    return true;
}

bool HasEvent(PmuData *data, int len, std::string evt)
{
    for (int i=0;i<len;++i) {
        if (string(data[i].evt) == evt) {
            return true;
        }
    }
    return false;
}
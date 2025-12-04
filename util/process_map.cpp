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
 * Author: Mr.Wang
 * Create: 2024-04-03
 * Description: Get process and thread information.
 ******************************************************************************/
#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <cstring>
#include <dirent.h>
#include <ctype.h>
#include <memory>
#include "common.h"
#include "process_map.h"

using namespace std;
constexpr int PATH_LEN = 1024;

void FreeProcTopo(struct ProcTopology *procTopo)
{
    if (procTopo == nullptr) {
        return;
    }
    if (procTopo->childPid != nullptr) {
        free(procTopo->childPid);
        procTopo->childPid = nullptr;
    }
    if (procTopo->comm != nullptr) {
        free(procTopo->comm);
        procTopo->comm = nullptr;
    }
    if (procTopo->exe != nullptr) {
        free(procTopo->exe);
        procTopo->exe = nullptr;
    }
    delete procTopo;
}

int GetTgid(pid_t pid)
{
    if (pid == -1) {
        // for system sampling.
        return -1;
    }
    // Get tgid from /proc/<pid>/status.
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/status", pid);
    FILE* f = fopen(path, "r");
    if (!f) {
        return -1;
    }

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "Tgid:", 5) == 0) {
            char* endptr;
            long tgid = strtol(line + 5, &endptr, 10);
            fclose(f);
            if (endptr == line + 5) {
                return -1;
            }
            return static_cast<int>(tgid);
        }
    }
    // The file may be successfully opened before while loop,
    // but disappear before reading stream.
    fclose(f);
    return -1;
}

char *GetComm(pid_t pid)
{
    static thread_local char buffer[PATH_MAX];
    if (pid == -1) {
        return strdup("system");
    }

    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/comm", pid);
    FILE* f = fopen(path, "r");
    if (!f) {
        return nullptr;
    }

    if (!fgets(buffer, sizeof(buffer), f)) {
        fclose(f);
        return nullptr;
    }
    fclose(f);

    buffer[strcspn(buffer, "\n")] = '\0';
    return strdup(buffer);
}

struct ProcTopology *GetProcTopology(pid_t pid)
{
    unique_ptr<ProcTopology, void (*)(ProcTopology *)> procTopo(new ProcTopology{0}, FreeProcTopo);
    procTopo->tid = pid;
    if (pid == 0) {
        return procTopo.release();
    }
    try {
        // Get tgid, i.e., process id.
        procTopo->pid = GetTgid(pid);
        if (pid != -1 && procTopo->pid == -1) {
            return nullptr;
        }
        // Get command name.
        procTopo->comm = GetComm(procTopo->pid);
        if (procTopo->comm == nullptr) {
            return nullptr;
        }
    } catch (exception&) {
        return nullptr;
    }

    return procTopo.release();
}

// Check if a string represents a valid integer
int IsValidInt(const char *str)
{
    while (*str) {
        if (!isdigit(*str)) {
            return 0;
        }
        str++;
    }
    return 1;
}

void StoreThreadId(int** childTidList, int* count, const char* entryName)
{
    (*count)++;
    int* newChildTidList = new int[(*count)];
    if (newChildTidList != nullptr) {
        if (*childTidList != nullptr) {
            std::copy(*childTidList, *childTidList + (*count) - 1, newChildTidList);
            delete[] *childTidList;
        }
        newChildTidList[(*count) - 1] = atoi(entryName);
        *childTidList = newChildTidList;
    }
}

bool GetChildTidRecursive(const std::string& dirPath, int **childTidList, int *count)
{
    DIR *dir = opendir(dirPath.c_str());
    if (!dir) {
        return false;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_type == DT_DIR) {
            // Skip "." and ".." directories
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }

            // Check if the entry name is a valid thread ID
            if (IsValidInt(entry->d_name)) {
                // Store the thread ID in the array
                StoreThreadId(childTidList, count, entry->d_name);
            }
        }
    }

    closedir(dir);
    return true;
}

int *GetChildTid(int pid, int *numChild)
{
    int *childTidList = nullptr;
    if (pid == 0) {
        childTidList = new int[1];
        childTidList[0] = 0;
        *numChild = 1;
        return childTidList;
    }
    std::string dirPath = "/proc/" + std::to_string(pid) + "/task";
    *numChild = 0;
    if (!GetChildTidRecursive(dirPath, &childTidList, numChild)) {
        if (childTidList) {
            delete[] childTidList;
            childTidList = nullptr;
        }
        *numChild = 0;
    }
    return childTidList;
}
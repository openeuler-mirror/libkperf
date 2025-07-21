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
 * Description: Provide common file operation functions and system resource management functions.
 ******************************************************************************/

#include <cstring>
#include <vector>
#include <iostream>
#include <sstream>
#include <fstream>
#include <cstdlib>
#include <unistd.h>
#include <dirent.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <climits>
#include <sys/vfs.h>
#include "pcerrc.h"
#include "pcerr.h"
#include "common.h"

std::string GetRealPath(const std::string filePath)
{
    char resolvedPath[PATH_MAX];
    if (realpath(filePath.data(), resolvedPath) == nullptr) {
        return std::string{};
    }
    if (access(resolvedPath, R_OK) != 0) {
        return std::string{};
    }
    return resolvedPath;
}

bool IsValidPath(const std::string& filePath)
{
    if (filePath.empty()) {
        return false;
    }
    return true;
}

bool IsDirectory(const std::string& path)
{
    struct stat statbuf;
    return stat(path.c_str(), &statbuf) == 0 && S_ISDIR(statbuf.st_mode);
}

std::vector<std::string> ListDirectoryEntries(const std::string& dirPath)
{
    std::vector<std::string> entries;
    DIR* dir = opendir(dirPath.c_str());
    if (!dir) {
        pcerr::New(LIBPERF_ERR_OPEN_INVALID_FILE, "Failed to open directory: " + dirPath);
        return entries;
    }
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        entries.push_back(entry->d_name);
    }
    closedir(dir);
    return entries;
}

std::string ReadFileContent(const std::string& filePath)
{
    std::ifstream file(filePath);
    if (!file.is_open()) {
        pcerr::New(LIBPERF_ERR_OPEN_INVALID_FILE, "Failed to open File: " + filePath);
        return "";
    }
    std::string content;
    std::getline(file, content);
    file.close();
    return content;
}

int ConvertHexStrToInt(const std::string& hexStr, uint64_t& bus)
{
    try {
        bus = stoul(hexStr, nullptr, 16);
    } catch (const std::exception& e) {
        pcerr::New(LIBPERF_ERR_NOT_SUPPORT_PCIE_COUNTING, "hexStr: " + hexStr + " is invalid");
        return LIBPERF_ERR_NOT_SUPPORT_PCIE_COUNTING;
    }
    return SUCCESS;
}

int RaiseNumFd(uint64_t numFd)
{
    unsigned long extra = 50;
    unsigned long setNumFd = extra + numFd;
    struct rlimit currentlim;
    if (getrlimit(RLIMIT_NOFILE, &currentlim) == -1) {
        return LIBPERF_ERR_RAISE_FD;
    }
    if (currentlim.rlim_cur > setNumFd) {
        return SUCCESS;
    }
    if (currentlim.rlim_max < numFd) {
        return LIBPERF_ERR_TOO_MANY_FD;
    }
    struct rlimit rlim {
            .rlim_cur = currentlim.rlim_max, .rlim_max = currentlim.rlim_max,
    };
    if (setNumFd < currentlim.rlim_max) {
        rlim.rlim_cur = setNumFd;
    }
    if (setrlimit(RLIMIT_NOFILE, &rlim) != 0) {
        return LIBPERF_ERR_RAISE_FD;
    } else {
        return SUCCESS;
    }
}

bool ExistPath(const std::string &filePath) {
    struct stat statbuf{};
    return stat(filePath.c_str(), &statbuf) == 0;
}

std::vector<std::string> SplitStringByDelimiter(const std::string& str, char delimiter)
{
    std::vector<std::string> parts;
    std::stringstream ss(str);
    std::string part;
    while (std::getline(ss, part, delimiter)) {
        if (!part.empty()) {
            parts.push_back(part);
        }
    }
    return parts;
}

std::string GetTraceEventDir()
{
    if (ExistPath(TRACE_EVENT_PATH)) {
        return TRACE_EVENT_PATH;
    }
    if (ExistPath(TRACE_DEBUG_EVENT_PATH)) {
        return TRACE_DEBUG_EVENT_PATH;
    }
    return "";
}

bool StartWith(const std::string& str, const std::string& prefix)
{
    if (str.size() < prefix.size()) {
        return false;
    }
    return str.substr(0, prefix.size()) == prefix;
}

int CheckCgroupV2()
{
    const char *mnt = "/sys/fs/cgroup";
    struct statfs stbuf;

    if (statfs(mnt, &stbuf) < 0) {
        return -1;
    }

    return (stbuf.f_type == CGROUP2_SUPER_MAGIC);
}

bool ConvertStrToInt(const std::string &intValStr, int32_t &val)
{
    try {
        val = stoi(intValStr, nullptr, 10);
    } catch (const std::exception &e) {
        return false;
    }
    return true;
}

int GetParanoidVal()
{
    std::string paranoidValStr = ReadFileContent(PERF_EVENT_PARANOID_PATH);
    if (!paranoidValStr.empty()) {
        int val;
        if (ConvertStrToInt(paranoidValStr, val)) {
            return val;
        }
    }
    return INT32_MAX;
}
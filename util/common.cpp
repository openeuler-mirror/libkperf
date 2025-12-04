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
#include <sys/utsname.h>
#include <sys/stat.h>
#include <climits>
#include <map>
#include <sys/vfs.h>
#include "pcerrc.h"
#include "pcerr.h"
#include "common.h"

const std::string CUR_NS_PATH = "/proc/self/ns/mnt";
const std::string MOUNT_INFO_PATH = "/proc/self/mountinfo";

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

static int CheckCgroupV2()
{
    const char *mnt = "/sys/fs/cgroup";
    struct statfs stbuf;

    if (statfs(mnt, &stbuf) < 0) {
        return -1;
    }

    return (stbuf.f_type == CGROUP2_SUPER_MAGIC);
}

std::string GetCgroupPath(const std::string& cgroupName) {
    std::string cgroupRootPath = "/sys/fs/cgroup/";
    int cgroupIsV2 = CheckCgroupV2();
    if (cgroupIsV2) {
        cgroupRootPath += cgroupName;
    } else if (cgroupIsV2 == 0) {
        cgroupRootPath += "perf_event/" + cgroupName;
    } else {
        return "";
    }
    return cgroupRootPath;
}

bool CheckCurKernelConfig(const std::string& configName)
{
    std::string configPath;
    struct utsname buff;
    if (uname(&buff) == 0) {
        configPath = KERNEL_CONFIG_BASE_PATH + buff.release;
    } else {
        return false;
    }

    std::ifstream configFile(configPath.c_str());
    if (!configFile.is_open()) {
        return false;
    }

    std::string line;
    while(getline(configFile, line)) {
        if (line.find(configName) != std::string::npos) {
            return true;
        }
    }
    return false;
}

struct MountEntry {
    int mountId;
    int parentId;
    char device[32];
    char root[256];
    char mountPoint[256];
    char options[256];
    char optionFields[256];
    char fsType[32];
    char source[256];
    char superOptions[256];
};

void ParseMountInfo(const std::string& mntPath, std::map<std::string, MountEntry>& overlayMap)
{
    std::ifstream file(mntPath);
    if (!file.is_open()) {
        return;
    }
    std::string line;
    while (std::getline(file, line)){
        struct MountEntry entry;
        int parseLen = sscanf(line.c_str(), "%d %d %31s %255s %255s %255s %255[^-] - %31s %255s %255s", 
                        &entry.mountId, &entry.parentId, entry.device, entry.root, entry.mountPoint, entry.options, entry.optionFields, entry.fsType, entry.source, entry.superOptions);
        if (parseLen != 10) {
            continue;
        }
        if (strcmp(entry.fsType, "overlay") == 0) {
            overlayMap[entry.superOptions] = entry;
        }
    }
}

bool CheckIsSameMnt(int pid)
{
    std::string newMnt = "/proc/" + std::to_string(pid) + "/ns/mnt";
    struct stat curStat;
    struct stat newStat;
    if (stat(CUR_NS_PATH.c_str(), &curStat) < 0) {
        return true;
    }

    if (stat(newMnt.c_str(), &newStat) < 0) {
        return true;
    }

    return curStat.st_ino == newStat.st_ino;
}

std::string GetMntPoint(int pid)
{
    std::string mntPoint;
    if (CheckIsSameMnt(pid)) {
        return mntPoint;
    }
    std::map<std::string, MountEntry> overlayMap;
    std::map<std::string, MountEntry> newOverlayMap;
    ParseMountInfo(MOUNT_INFO_PATH, overlayMap);
    std::string newMntInfo = "/proc/" + std::to_string(pid) + "/mountinfo";
    ParseMountInfo(newMntInfo, newOverlayMap);
    for (const auto& item : newOverlayMap) {
        if (overlayMap.find(item.first) != overlayMap.end()) {
            auto entry = overlayMap[item.first];
            if (strcmp(entry.device, item.second.device) == 0) {
                 mntPoint = entry.mountPoint;
            }
        }
    }
    return mntPoint;
}




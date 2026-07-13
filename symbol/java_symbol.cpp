/******************************************************************************
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * libkperf licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *     http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR
 * PURPOSE.
 * See the Mulan PSL v2 for more details.
 * Author: RicardoJDLi
 * Create: 2026-05-18
 * Description: Java attach process Interface
 ******************************************************************************/
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/types.h>
#include <fcntl.h>
#include <limits.h>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <dlfcn.h>
#include <sstream>
#include <vector>

#include "common.h"
#include "java_symbol.h"

const static char* TMP_SOCKET_PREFIX = "/tmp/.java_pid";
const static char* TMP_ATTACH_PREFIX = "/tmp/.attach_pid";
const static char* ATTACH_FILE_PREFIX = "/.attach_pid";
const static char* PROTOCOL = "1";
const static char* CMD = "load";
const static char* ABSOLUTE = "true";
const static char* KPERF_MAP_NAME = "libkperfmap.so";
const static char* LD_LIBRARY_PATH = "LD_LIBRARY_PATH";
const static char* KPERF_MAP_SYMBOL_NAME = "perf_map_open";
const static char* PERF_MAP_OPTION_PREFIX = "file=";
const static size_t K_PROC_STAT_AFTER_COMM_OFFSET = 2;
const static int K_PROC_STAT_STATE_FIELD = 3;
const static int K_PROC_STAT_STARTTIME_FIELD = 22;
const static int K_PROC_STAT_STARTTIME_TOKEN_COUNT = K_PROC_STAT_STARTTIME_FIELD - K_PROC_STAT_STATE_FIELD + 1;
static std::string KPERF_MAP_LIB_PATH;

struct ProcessCredentials {
    uid_t euid;
    gid_t egid;
};

struct TargetAgentPath {
    std::string targetVisiblePath;
    std::string hostPath;
    bool created = false;
};

static inline bool FindPath(const std::string& path) {
    struct stat statbuf{};
    return stat(path.c_str(), &statbuf) == 0;
}

static inline std::string ProcRootPrefix(int pid)
{
    return "/proc/" + std::to_string(pid) + "/root";
}

static inline std::string ProcCwdPrefix(int pid)
{
    return "/proc/" + std::to_string(pid) + "/cwd";
}

static int GetNamespacePid(int pid)
{
    std::ifstream status("/proc/" + std::to_string(pid) + "/status");
    if (!status.is_open()) {
        return pid;
    }

    std::string line;
    while (std::getline(status, line)) {
        if (line.compare(0, 6, "NSpid:") != 0) {
            continue;
        }
        std::istringstream iss(line.substr(6));
        int nsPid = pid;
        int tmpPid = 0;
        while (iss >> tmpPid) {
            nsPid = tmpPid;
        }
        return nsPid;
    }
    return pid;
}

static bool ParseEffectiveId(const std::string& line, const char* key, unsigned long& id)
{
    size_t keyLen = strlen(key);
    if (line.compare(0, keyLen, key) != 0) {
        return false;
    }

    std::istringstream iss(line.substr(keyLen));
    unsigned long realId = 0;
    unsigned long effectiveId = 0;
    if (!(iss >> realId >> effectiveId)) {
        return false;
    }
    id = effectiveId;
    return true;
}

static ProcessCredentials GetProcessCredentials(int pid)
{
    ProcessCredentials credentials = {geteuid(), getegid()};
    std::ifstream status("/proc/" + std::to_string(pid) + "/status");
    if (!status.is_open()) {
        return credentials;
    }

    std::string line;
    while (std::getline(status, line)) {
        unsigned long id = 0;
        if (ParseEffectiveId(line, "Uid:", id)) {
            credentials.euid = static_cast<uid_t>(id);
            continue;
        }
        if (ParseEffectiveId(line, "Gid:", id)) {
            credentials.egid = static_cast<gid_t>(id);
        }
    }
    return credentials;
}

static std::string GetProcessStartTime(int pid)
{
    std::ifstream statFile("/proc/" + std::to_string(pid) + "/stat");
    std::string statLine;
    if (!std::getline(statFile, statLine)) {
        return "0";
    }

    size_t commEnd = statLine.rfind(')');
    if (commEnd == std::string::npos || commEnd + K_PROC_STAT_AFTER_COMM_OFFSET >= statLine.size()) {
        return "0";
    }

    std::istringstream fields(statLine.substr(commEnd + K_PROC_STAT_AFTER_COMM_OFFSET));
    std::string value;
    // /proc/<pid>/stat starttime is field 22; after comm, the stream starts at field 3 (state).
    for (int i = 0; i < K_PROC_STAT_STARTTIME_TOKEN_COUNT; ++i) {
        if (!(fields >> value)) {
            return "0";
        }
    }
    return value;
}

static std::string BuildTargetTmpPath(int pid, const char* prefix, int nsPid, bool procRoot)
{
    std::string path = procRoot ? ProcRootPrefix(pid) : "";
    path += prefix;
    path += std::to_string(nsPid);
    return path;
}

static inline int ConnectSocket(int fd, const char* path)
{
    struct sockaddr_un addr;
    int err = 0;

    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, path);
    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        err = errno;
    }
    return err;
}

static bool FindKperfMap() {
    char* libPath = getenv(LD_LIBRARY_PATH);
    if (libPath == nullptr) {
        return false;
    }
    std::vector<std::string> libList = SplitStringByDelimiter(libPath, ':');
    if (libList.empty()) {
        return false;
    }

    for (const auto&path  : libList) {
        std::string kperfPath = path + "/" + KPERF_MAP_NAME;
        if (ExistPath(kperfPath)) {
            char realPath[PATH_MAX] = {};
            KPERF_MAP_LIB_PATH = realpath(kperfPath.c_str(), realPath) == nullptr ? kperfPath : realPath;
            return true;
        }
    }
    return false;
}

static bool CheckLib() {
    if (KPERF_MAP_LIB_PATH.empty()) {
        return false;
    }
    // avoid CheckLib() to affect '.so' load status
    void* handle = dlopen(KPERF_MAP_LIB_PATH.c_str(), RTLD_LAZY | RTLD_LOCAL);
    if (!handle) {
        return false;
    }

    void* symbol = dlsym(handle, KPERF_MAP_SYMBOL_NAME);
    if (!symbol) {
        dlclose(handle);
        return false;
    }
    dlclose(handle);
    return true;
}

static inline void WriteDataToSocket(int fd, const std::string& data) {
    write(fd, data.c_str(), data.size());
    unsigned char zero[1] = {0};
    write(fd, zero, 1);
}

static bool CopyFile(const std::string& src, const std::string& dst)
{
    std::ifstream input(src, std::ios::binary);
    if (!input.is_open()) {
        return false;
    }
    std::ofstream output(dst, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        return false;
    }
    output << input.rdbuf();
    return output.good();
}

static bool IsRegularFile(const std::string& path)
{
    struct stat statbuf {};
    return lstat(path.c_str(), &statbuf) == 0 && S_ISREG(statbuf.st_mode);
}

static void SetAgentFilePermission(const std::string& path, const ProcessCredentials& credentials)
{
    chmod(path.c_str(), S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
    chown(path.c_str(), credentials.euid, credentials.egid);
}

static bool CopyAgentToStablePath(const std::string& stablePath, const ProcessCredentials& credentials,
                                  bool& createdAgentPath)
{
    std::string templ = stablePath + ".copy." + std::to_string(getpid()) + ".XXXXXX";
    std::vector<char> pathBuf(templ.begin(), templ.end());
    pathBuf.push_back('\0');

    int fd = mkstemp(pathBuf.data());
    if (fd < 0) {
        return false;
    }
    close(fd);

    std::string tmpPath = pathBuf.data();
    if (!CopyFile(KPERF_MAP_LIB_PATH, tmpPath)) {
        unlink(tmpPath.c_str());
        return false;
    }
    SetAgentFilePermission(tmpPath, credentials);

    if (link(tmpPath.c_str(), stablePath.c_str()) == 0) {
        createdAgentPath = true;
        unlink(tmpPath.c_str());
        return true;
    }

    int savedErrno = errno;
    unlink(tmpPath.c_str());
    if (savedErrno == EEXIST && IsRegularFile(stablePath)) {
        SetAgentFilePermission(stablePath, credentials);
        return true;
    }
    return false;
}

static bool PrepareTargetAgentPath(int pid, int nsPid, const ProcessCredentials& credentials,
                                   TargetAgentPath* agentPath)
{
    if (agentPath == nullptr) {
        return false;
    }
    *agentPath = TargetAgentPath{};
    std::string targetTmpDir = ProcRootPrefix(pid) + "/tmp";
    agentPath->hostPath = targetTmpDir + "/" + KPERF_MAP_NAME + "." + std::to_string(nsPid) + "." +
                          GetProcessStartTime(pid);
    if (IsRegularFile(agentPath->hostPath)) {
        SetAgentFilePermission(agentPath->hostPath, credentials);
        agentPath->targetVisiblePath = agentPath->hostPath.substr(ProcRootPrefix(pid).size());
        return true;
    }
    if (!CopyAgentToStablePath(agentPath->hostPath, credentials, agentPath->created)) {
        agentPath->hostPath.clear();
        return false;
    }
    agentPath->targetVisiblePath = agentPath->hostPath.substr(ProcRootPrefix(pid).size());
    return true;
}

static void CleanupAgentPathOnFailure(const TargetAgentPath& agentPath)
{
    if (agentPath.created && !agentPath.hostPath.empty()) {
        unlink(agentPath.hostPath.c_str());
    }
}

static bool CreateAttachFile(const std::string& attachPath, const ProcessCredentials& credentials)
{
    int fd = open(attachPath.c_str(), O_CREAT | O_EXCL, S_IWUSR | S_IRUSR);
    if (fd < 0 && errno != EEXIST) {
        return false;
    }
    if (fd >= 0) {
        close(fd);
    }

    struct stat statbuf {};
    if (lstat(attachPath.c_str(), &statbuf) != 0 || !S_ISREG(statbuf.st_mode)) {
        return false;
    }
    if (statbuf.st_uid != credentials.euid &&
        chown(attachPath.c_str(), credentials.euid, credentials.egid) != 0) {
        return false;
    }
    return true;
}

int attach_java_process(int pid, JavaAttachInfo* info) {
    if (KPERF_MAP_LIB_PATH.empty() && !FindKperfMap()) {
        return -1;
    }

    if (!CheckLib()) {
        return -1;
    }

    int nsPid = GetNamespacePid(pid);
    ProcessCredentials credentials = GetProcessCredentials(pid);
    std::string socketPath = BuildTargetTmpPath(pid, TMP_SOCKET_PREFIX, nsPid, true);
    std::string attachPath = ProcCwdPrefix(pid) + ATTACH_FILE_PREFIX + std::to_string(nsPid);
    std::string tmpAttachPath = BuildTargetTmpPath(pid, TMP_ATTACH_PREFIX, nsPid, true);
    std::string targetPerfMapPath = "/tmp/perf-" + std::to_string(nsPid) + ".map";
    std::string perfMapPath = ProcRootPrefix(pid) + "/tmp/perf-" + std::to_string(nsPid) + ".map";
    TargetAgentPath agentPath;
    if (!PrepareTargetAgentPath(pid, nsPid, credentials, &agentPath)) {
        return -1;
    }

    if (!FindPath(socketPath)) {
        if (!CreateAttachFile(attachPath, credentials) && !CreateAttachFile(tmpAttachPath, credentials)) {
            CleanupAgentPathOnFailure(agentPath);
            return -1;
        }
        kill((pid_t)pid, SIGQUIT);
        int i = 0;
        long sleepTime = 200 * 1000;
        do {usleep(sleepTime);
            i++;
        } while(!FindPath(socketPath) && i < 100);
    }

    int fd = socket(PF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        CleanupAgentPathOnFailure(agentPath);
        return -1;
    }
    int err = ConnectSocket(fd, socketPath.c_str());
    if (err != 0) {
        close(fd);
        CleanupAgentPathOnFailure(agentPath);
        return -1;
    }
    WriteDataToSocket(fd, PROTOCOL);
    WriteDataToSocket(fd, CMD);
    WriteDataToSocket(fd, agentPath.targetVisiblePath);
    WriteDataToSocket(fd, ABSOLUTE);
    WriteDataToSocket(fd, std::string(PERF_MAP_OPTION_PREFIX) + targetPerfMapPath);
    unsigned char buf[128];
    ssize_t readLen = read(fd, buf, 1);
    if (readLen > 0) {
        if (buf[0] == '0') {
            if (info != nullptr) {
                info->nspid = nsPid;
                info->perfMapPath = perfMapPath;
            }
            close(fd);
            return 0;
        }
    }
    close(fd);
    CleanupAgentPathOnFailure(agentPath);
    return -1;
}

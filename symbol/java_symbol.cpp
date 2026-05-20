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
#include <fcntl.h>
#include <iostream>
#include <iostream>
#include <dlfcn.h>

#include "common.h"
#include "java_symbol.h"

const static char* TMP_SOCKET_PREFIX = "/tmp/.java_pid";
const static char* TMP_ATTACH_PREFIX = "/tmp/.attach_pid";
const static char* PROTOCOL = "1";
const static char* CMD = "load";
const static char* ABSOLUTE = "true";
const static char* KPERF_MAP_NAME = "libkperfmap.so";
const static char* LD_LIBRARY_PATH = "LD_LIBRARY_PATH";
const static char* KPERF_MAP_SYMBOL_NAME = "perf_map_open";
static std::string KPERF_MAP_LIB_PATH;

static inline bool FindSocketFile(int pid) {
    std::string path = TMP_ATTACH_PREFIX + std::to_string(pid);
    struct stat statbuf{};
    return stat(path.c_str(), &statbuf) == 0;
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
            KPERF_MAP_LIB_PATH = kperfPath;
            return true;
        }
    }
}

static bool CheckLib() {
    if (KPERF_MAP_LIB_PATH.empty()) {
        return false;
    }
    void* handle = dlopen(KPERF_MAP_LIB_PATH.c_str(), RTLD_LAZY | RTLD_NOLOAD);
    if (!handle) {
        handle = dlopen(KPERF_MAP_LIB_PATH.c_str(), RTLD_LAZY | RTLD_LOCAL);
        if (!handle) {
            return false;
        }
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

int attach_java_process(int pid) {
    if (KPERF_MAP_LIB_PATH.empty() && !FindKperfMap()) {
        return -1;
    }

    if (!CheckLib()) {
        return -1;
    }

    std::string socketPath = TMP_SOCKET_PREFIX + std::to_string(pid);
    if (!FindSocketFile(pid)) {
        std::string attachPath = TMP_ATTACH_PREFIX + std::to_string(pid);
        open(attachPath.c_str(), O_CREAT | O_EXCL, S_IWUSR | S_IRUSR);
        chown(attachPath.c_str(), geteuid(), getegid());
        kill((pid_t)pid, SIGQUIT);
        int i = 0; 
        long sleepTime = 200 * 1000;
        do {usleep(sleepTime);
            i++;
        } while(!FindSocketFile(pid) && i < 100);
    }

    int fd = socket(PF_UNIX, SOCK_STREAM, 0);
    if (fd <= 0) {
        return -1;
    }
    int err = ConnectSocket(fd, socketPath.c_str());
    if (err != 0) {
        return -1;
    }
    WriteDataToSocket(fd, PROTOCOL);
    WriteDataToSocket(fd, CMD);
    WriteDataToSocket(fd, KPERF_MAP_LIB_PATH);
    WriteDataToSocket(fd, ABSOLUTE);
    WriteDataToSocket(fd, "");
    unsigned char buf[128];
    size_t readLen = read(fd, buf, 1);
    if (readLen > 0) {
        if (buf[0] == '0') {
            return 0;
        }
    }
    return -1;
}

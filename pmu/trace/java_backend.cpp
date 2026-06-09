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
 * Author: Wu
 * Create: 2026-04-27
 * Description: Java trace backend implementation: shared memory layout, agent injection command building, and ring-buffer slot reading for Java trace events.
 ******************************************************************************/
#include "java_backend.h"
#include "pmu.h"
#ifdef JAVA_TRACE
#include "java_trace_config.h"
#endif

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <cstring>
#include <cstdint>
#include <atomic>
#include <fcntl.h>
#include <string>
#include <cerrno>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <dlfcn.h>
#include <limits.h>

#ifndef LIBKPERF_JAVA_DIR_ENV
#define LIBKPERF_JAVA_DIR_ENV "LIBKPERF_JAVA_DIR"
#endif

#ifndef LIBKPERF_JAVA_REL_DIR
#define LIBKPERF_JAVA_REL_DIR "lib/java"
#endif

#ifndef LIBKPERF_JAVA_CONF_REL_DIR
#define LIBKPERF_JAVA_CONF_REL_DIR "conf"
#endif

#ifndef LIBKPERF_JAVA_CLI_JAR_NAME
#define LIBKPERF_JAVA_CLI_JAR_NAME "trace_cli.jar"
#endif

#ifndef LIBKPERF_JAVA_AGENT_JAR_NAME
#define LIBKPERF_JAVA_AGENT_JAR_NAME "trace_agent.jar"
#endif

#ifndef LIBKPERF_JAVA_NATIVE_LIB_NAME
#define LIBKPERF_JAVA_NATIVE_LIB_NAME "libtracex_threadinfo.so"
#endif

#ifndef LIBKPERF_JAVA_HOME_FALLBACK
#define LIBKPERF_JAVA_HOME_FALLBACK ""
#endif

#ifndef LIBKPERF_JAVA_FILTER_CONFIG_ENV
#define LIBKPERF_JAVA_FILTER_CONFIG_ENV "LIBKPERF_JAVA_FILTER_CONFIG"
#endif

#ifndef LIBKPERF_JAVA_FILTER_CONFIG_NAME
#define LIBKPERF_JAVA_FILTER_CONFIG_NAME "trace_filter.conf"
#endif

#ifndef LIBKPERF_JAVA_CONTEXT_DEPTH_ENV
#define LIBKPERF_JAVA_CONTEXT_DEPTH_ENV "LIBKPERF_JAVA_CONTEXT_DEPTH"
#endif

#ifndef LIBKPERF_JAVA_CONTEXT_MAX_METHODS_ENV
#define LIBKPERF_JAVA_CONTEXT_MAX_METHODS_ENV "LIBKPERF_JAVA_CONTEXT_MAX_METHODS"
#endif

#ifndef LIBKPERF_JAVA_DEFAULT_CONTEXT_DEPTH
#define LIBKPERF_JAVA_DEFAULT_CONTEXT_DEPTH -1
#endif

#ifndef LIBKPERF_JAVA_DEFAULT_CONTEXT_MAX_METHODS
#define LIBKPERF_JAVA_DEFAULT_CONTEXT_MAX_METHODS -1
#endif

#ifndef LIBKPERF_JAVA_SLOT_COUNT_ENV
#define LIBKPERF_JAVA_SLOT_COUNT_ENV "LIBKPERF_JAVA_SLOT_COUNT"
#endif

#ifndef LIBKPERF_JAVA_DEFAULT_SLOT_COUNT
#define LIBKPERF_JAVA_DEFAULT_SLOT_COUNT 262144U  // 128MB
#endif

#ifndef LIBKPERF_JAVA_MAX_SLOT_COUNT
#define LIBKPERF_JAVA_MAX_SLOT_COUNT 1048576U  // 512MB
#endif

namespace {
static constexpr uint64_t kMagic = 0x5554524356415731ULL;
static constexpr uint32_t kVersion = 1;
static constexpr size_t kHeaderSize = 64;
static constexpr size_t kHeaderMagic = 0;
static constexpr size_t kHeaderVersion = 8;
static constexpr size_t kHeaderActive = 12;
static constexpr size_t kHeaderSlotCount = 16;
static constexpr size_t kHeaderSlotSize = 20;
static constexpr size_t kHeaderWriteSeq = 24;
static constexpr size_t kHeaderDropped = 32;
static constexpr size_t kHeaderDeadlineNs = 40;

static constexpr size_t kSlotSize = 512;
static constexpr size_t kSlotSeq = 0;
static constexpr size_t kSlotAddr = 8;
static constexpr size_t kSlotTid = 16;
static constexpr size_t kSlotCpu = 20;
static constexpr size_t kSlotTimestamp = 24;
static constexpr size_t kSlotGPtr = 32;
static constexpr size_t kSlotIsRet = 40;
static constexpr size_t kSlotComm = 48;
static constexpr size_t kSlotCommLen = 32;
static constexpr size_t kSlotModule = 80;
static constexpr size_t kSlotModuleLen = 160;
static constexpr size_t kSlotFunc = 240;
static constexpr size_t kSlotFuncLen = 256;

static constexpr uint32_t kDefaultSlotCount = 4096;

static uint32_t JavaSlotCount()
{
    uint32_t value = LIBKPERF_JAVA_DEFAULT_SLOT_COUNT;
    const char *env = getenv(LIBKPERF_JAVA_SLOT_COUNT_ENV);
    if (env != nullptr && env[0] != '\0') {
        char *end = nullptr;
        unsigned long parsed = std::strtoul(env, &end, 10);
        if (end != env && *end == '\0' && parsed > 0) {
            value = parsed > LIBKPERF_JAVA_MAX_SLOT_COUNT
                ? LIBKPERF_JAVA_MAX_SLOT_COUNT
                : static_cast<uint32_t>(parsed);
        } else {
            std::fprintf(stderr, "[trace-java] ignore invalid %s=%s\n", LIBKPERF_JAVA_SLOT_COUNT_ENV, env);
        }
    }
    if (value < kDefaultSlotCount) {
        value = kDefaultSlotCount;
    }
    if (value > LIBKPERF_JAVA_MAX_SLOT_COUNT) {
        value = LIBKPERF_JAVA_MAX_SLOT_COUNT;
    }
    return value;
}
static constexpr const char *kDefaultJavaBin = "java";

static int ReadEnvInt(const char *envName, int fallback)
{
    const char *env = getenv(envName);
    if (env == nullptr || env[0] == '\0') {
        return fallback;
    }
    char *end = nullptr;
    long parsed = std::strtol(env, &end, 10);
    if (end != env && *end == '\0' && parsed >= 0 && parsed <= INT_MAX) {
        return static_cast<int>(parsed);
    }
    std::fprintf(stderr, "[trace-java] ignore invalid %s=%s\n", envName, env);
    return fallback;
}

static bool FileExists(const std::string &path)
{
    return !path.empty() && access(path.c_str(), R_OK) == 0;
}

static std::string JoinPath(const std::string &a, const std::string &b)
{
    if (a.empty()) {
        return b;
    }
    return a.back() == '/' ? a + b : a + "/" + b;
}

static std::string DirName(const std::string &path)
{
    size_t pos = path.rfind('/');
    if (pos == std::string::npos) {
        return ".";
    }
    if (pos == 0) {
        return "/";
    }
    return path.substr(0, pos);
}

static std::string SelfExeDir()
{
    char buf[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) {
        return "";
    }
    buf[n] = '\0';
    return DirName(std::string(buf));
}

static std::string CurrentLibDir()
{
    Dl_info info;
    if (dladdr(reinterpret_cast<void *>(&CurrentLibDir), &info) == 0 || info.dli_fname == nullptr) {
        return "";
    }
    return DirName(std::string(info.dli_fname));
}

static std::string FindJavaAsset(const char *fileName)
{
    const char *envDir = getenv(LIBKPERF_JAVA_DIR_ENV);
    if (envDir != nullptr && envDir[0] != '\0') {
        std::string p = JoinPath(envDir, fileName);
        if (FileExists(p)) {
            return p;
        }
    }

    std::string exeDir = SelfExeDir();
    if (!exeDir.empty()) {
        std::string toolRoot = DirName(exeDir);
        std::string p = JoinPath(JoinPath(toolRoot, LIBKPERF_JAVA_REL_DIR), fileName);
        if (FileExists(p)) {
            return p;
        }
    }

    std::string libDir = CurrentLibDir();
    if (!libDir.empty()) {
        std::string libRoot = DirName(libDir);
        std::string p = JoinPath(JoinPath(libRoot, LIBKPERF_JAVA_REL_DIR), fileName);
        if (FileExists(p)) {
            return p;
        }
    }
    return "";
}

static std::string CliJarPath()
{
    std::string p = FindJavaAsset(LIBKPERF_JAVA_CLI_JAR_NAME);
    return p;
}

static std::string AgentJarPath()
{
    std::string p = FindJavaAsset(LIBKPERF_JAVA_AGENT_JAR_NAME);
    return p;
}

static std::string NativeLibPath()
{
    std::string p = FindJavaAsset(LIBKPERF_JAVA_NATIVE_LIB_NAME);
    return p;
}

static std::string FilterConfigPath()
{
    const char *env = getenv(LIBKPERF_JAVA_FILTER_CONFIG_ENV);
    if (env != nullptr && env[0] != '\0' && FileExists(env)) {
        return env;
    }

    std::string exeDir = SelfExeDir();
    if (!exeDir.empty()) {
        std::string toolRoot = DirName(exeDir);
        std::string confPath = JoinPath(JoinPath(toolRoot, LIBKPERF_JAVA_CONF_REL_DIR), LIBKPERF_JAVA_FILTER_CONFIG_NAME);
        if (FileExists(confPath)) {
            return confPath;
        }
    }

    std::string libDir = CurrentLibDir();
    if (!libDir.empty()) {
        std::string libRoot = DirName(libDir);
        std::string confPath = JoinPath(JoinPath(libRoot, LIBKPERF_JAVA_CONF_REL_DIR), LIBKPERF_JAVA_FILTER_CONFIG_NAME);
        if (FileExists(confPath)) {
            return confPath;
        }
    }

    return FindJavaAsset(LIBKPERF_JAVA_FILTER_CONFIG_NAME);
}

static void EnsureDir(const std::string &path)
{
    if (!path.empty()) {
        mkdir(path.c_str(), 0700);
    }
}

static std::string EscapeShell(const std::string &s)
{
    std::string out;
    out.reserve(s.size() + 2);
    out.push_back('\'');
    for (char c : s) {
        if (c == '\'') {
            out += "'\\''";
        } else {
            out.push_back(c);
        }
    }
    out.push_back('\'');
    return out;
}

static std::string TimestampSuffix()
{
    std::time_t t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y%m%d%H%M%S", std::localtime(&t));
    return std::string(buf);
}

template <typename T> static T LoadAt(const uint8_t *p, size_t offset)
{
    T v{};
    std::memcpy(&v, p + offset, sizeof(T));
    return v;
}

template <typename T> static T LoadAtAcquire(const uint8_t *p, size_t offset)
{
    static_assert(std::is_integral<T>::value || std::is_enum<T>::value,
                  "LoadAtAcquire only supports integral or enum types");
    const volatile T *addr = reinterpret_cast<const volatile T *>(p + offset);
    return __atomic_load_n(addr, __ATOMIC_ACQUIRE);
}

template <typename T> static void StoreAtRelease(uint8_t *p, size_t offset, T v)
{
    static_assert(std::is_integral<T>::value || std::is_enum<T>::value,
                  "StoreAtRelease only supports integral or enum types");
    volatile T *addr = reinterpret_cast<volatile T *>(p + offset);
    __atomic_store_n(addr, v, __ATOMIC_RELEASE);
}

static std::string ReadFixedString(const uint8_t *base, size_t offset, size_t cap)
{
    const char *p = reinterpret_cast<const char *>(base + offset);
    size_t n = 0;
    while (n < cap && p[n] != '\0') {
        ++n;
    }
    return std::string(p, p + n);
}

static char *DupString(const std::string &s)
{
    char *p = static_cast<char *>(std::malloc(s.size() + 1));
    if (!p) {
        return nullptr;
    }
    std::memcpy(p, s.c_str(), s.size() + 1);
    return p;
}

static void FreeTraceDataFields(UTraceData &data)
{
    std::free(const_cast<char *>(data.comm));
    std::free(const_cast<char *>(data.module));
    std::free(const_cast<char *>(data.func));
    data.comm = nullptr;
    data.module = nullptr;
    data.func = nullptr;
}

static void FreeJavaTraceBlockRaw(uint8_t *raw, UTraceData *block, size_t count)
{
    if (block != nullptr) {
        for (size_t i = 0; i < count; ++i) {
            FreeTraceDataFields(block[i]);
        }
    }
    std::free(raw);
}

static bool FillTraceDataStrings(UTraceData &data, const uint8_t *slot)
{
    data.comm = DupString(ReadFixedString(slot, kSlotComm, kSlotCommLen));
    if (data.comm == nullptr) {
        return false;
    }
    data.module = DupString(ReadFixedString(slot, kSlotModule, kSlotModuleLen));
    if (data.module == nullptr) {
        FreeTraceDataFields(data);
        return false;
    }
    data.func = DupString(ReadFixedString(slot, kSlotFunc, kSlotFuncLen));
    if (data.func == nullptr) {
        FreeTraceDataFields(data);
        return false;
    }
    return true;
}

static int RunCommand(const std::string &cmd)
{
    int status = std::system(cmd.c_str());
    if (status == -1) {
        return -2;
    }
    if (!(WIFEXITED(status) && WEXITSTATUS(status) == 0)) {
        return status;
    }
    return 0;
}

static std::string WriteIncludeFile(const JavaBackendImpl &impl)
{
    if (impl.include_rules.empty()) {
        return "";
    }
    std::string path = impl.shm_path + ".includes";
    FILE *fp = std::fopen(path.c_str(), "wb");
    if (fp == nullptr) {
        std::fprintf(stderr, "[trace-java] open include file failed: %s, errno=%d(%s)\n", path.c_str(), errno, std::strerror(errno));
        return "";
    }

    size_t expected = impl.include_rules.size();
    size_t written = std::fwrite(impl.include_rules.data(), 1, expected, fp);
    if (written != expected) {
        int err = errno;
        std::fprintf(stderr, "[trace-java] write include file incomplete: %s, written=%zu, expected=%zu, errno=%d(%s)\n",
                     path.c_str(), written, expected, err, std::strerror(err));
        if (std::fclose(fp) != 0) {
            std::fprintf(stderr, "[trace-java] close include file after write failure failed: %s, errno=%d(%s)\n",
                         path.c_str(), errno, std::strerror(errno));
        }
        std::remove(path.c_str());
        return "";
    }
    if (std::fclose(fp) != 0) {
        int err = errno;
        std::fprintf(stderr, "[trace-java] close include file failed: %s, errno=%d(%s)\n", path.c_str(), err, std::strerror(err));
        std::remove(path.c_str());
        return "";
    }
    return path;
}

static std::string BuildEnableCommand(const JavaBackendImpl &impl)
{
    std::string cliJar = CliJarPath();
    std::string agentJar = AgentJarPath();
    std::string nativeLib = NativeLibPath();

    if (cliJar.empty()) {
        std::fprintf(stderr, "[trace-java] error: trace java cli jar not found\n");
        return "";
    }

    if (agentJar.empty()) {
        std::fprintf(stderr, "[trace-java] error: trace java agent jar not found\n");
        return "";
    }

    std::string cmd;
    cmd.reserve(2048);
    cmd += EscapeShell(kDefaultJavaBin);
    cmd += " -jar ";
    cmd += EscapeShell(cliJar);
    cmd += " -p ";
    cmd += std::to_string(impl.pid);
    cmd += " --agent-jar ";
    cmd += EscapeShell(agentJar);
    cmd += " --action start";
    cmd += " --shm-path ";
    cmd += EscapeShell(impl.shm_path);
    cmd += " --slot-count ";
    cmd += std::to_string(impl.slot_count > 0 ? impl.slot_count : kDefaultSlotCount);
    cmd += " --native-lib ";
    cmd += EscapeShell(nativeLib);
    
    if (!impl.filter_config_path.empty()) {
        cmd += " --config-file ";
        cmd += EscapeShell(impl.filter_config_path);
    }
    if (impl.contextDepth >= 0) {
        cmd += " --context-depth ";
        cmd += std::to_string(impl.contextDepth);
    }
    if (impl.contextMaxMethods >= 0) {
        cmd += " --context-max-methods ";
        cmd += std::to_string(impl.contextMaxMethods);
    }
    if (!impl.include_rules.empty()) {
        std::string includeFile = WriteIncludeFile(impl);
        if (includeFile.empty()) {
            std::fprintf(stderr, "[trace-java] error: include file create failed\n");
            return "";
        }

        cmd += " --include-file ";
        cmd += EscapeShell(includeFile);
    }
    return cmd;
}

static std::string BuildActionCommand(const JavaBackendImpl &impl, const char *action)
{
    std::string cliJar = CliJarPath();
    std::string agentJar = AgentJarPath();

    if (cliJar.empty() || agentJar.empty() || impl.pid <= 0 || action == nullptr) {
        return "";
    }

    std::string cmd;
    cmd.reserve(1024);

    cmd += EscapeShell(kDefaultJavaBin);
    cmd += " -jar ";
    cmd += EscapeShell(cliJar);
    cmd += " -p ";
    cmd += std::to_string(impl.pid);
    cmd += " --agent-jar ";
    cmd += EscapeShell(agentJar);
    cmd += " --action ";
    cmd += EscapeShell(action);

    return cmd;
}

static int JavaBackendRunAction(JavaBackendImpl *impl, const char *action)
{
    if (!impl || !action) {
        return -1;
    }

    std::string cmd = BuildActionCommand(*impl, action);
    if (cmd.empty()) {
        return -1;
    }

    int ret = RunCommand(cmd);
    if (ret != 0) {
        std::fprintf(stderr, "[trace-java] action=%s failed, ret=%d, cmd=%s\n",
                     action, ret, cmd.c_str());
        return ret;
    }

    return 0;
}

static int JavaBackendStopRuntime(JavaBackendImpl *impl)
{
    if (!impl) {
        return -1;
    }
    if (impl->runtimeStopped) {
        return 0;
    }

    int ret = JavaBackendRunAction(impl, "stop");
    if (ret == 0) {
        impl->runtimeStopped = true;
    }
    return ret;
}

static int JavaBackendRestoreRuntime(JavaBackendImpl *impl)
{
    if (!impl) {
        return -1;
    }
    if (impl->runtimeRestored) {
        return 0;
    }

    int ret = JavaBackendRunAction(impl, "restore");
    if (ret == 0) {
        impl->runtimeRestored = true;
        impl->runtimeStopped = true;
    }
    return ret;
}
} // namespace

int JavaBackendOpen(JavaBackendImpl *impl, int pid, const char *includeRules)
{
    if (!impl) {
        return -1;
    }

    impl->pid = pid;
    impl->runtimeStopped = true;
    impl->runtimeRestored = true;

    if (includeRules) {
        impl->include_rules = includeRules;
    }
    impl->filter_config_path = FilterConfigPath();
    impl->contextDepth = ReadEnvInt(LIBKPERF_JAVA_CONTEXT_DEPTH_ENV, LIBKPERF_JAVA_DEFAULT_CONTEXT_DEPTH);
    impl->contextMaxMethods = ReadEnvInt(LIBKPERF_JAVA_CONTEXT_MAX_METHODS_ENV, LIBKPERF_JAVA_DEFAULT_CONTEXT_MAX_METHODS);
    impl->shm_name = "/utrace_java_" + std::to_string(pid) + "_" + TimestampSuffix();
    impl->shm_path = "/dev/shm" + impl->shm_name;
    impl->shm_fd = shm_open(impl->shm_name.c_str(), O_CREAT | O_RDWR, 0600);
    if (impl->shm_fd < 0) {
        return -2;
    }

    impl->slot_count = JavaSlotCount();
    impl->shm_size = kHeaderSize + static_cast<size_t>(impl->slot_count) * kSlotSize;
    std::fprintf(stderr, "[trace-java] slotCount=%u, shmSize=%zu\n",
        impl->slot_count, impl->shm_size);
    if (ftruncate(impl->shm_fd, static_cast<off_t>(impl->shm_size)) != 0) {
        close(impl->shm_fd);
        shm_unlink(impl->shm_name.c_str());
        return -3;
    }

    impl->mapped = mmap(nullptr, impl->shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, impl->shm_fd, 0);
    if (impl->mapped == MAP_FAILED) {
        impl->mapped = nullptr;
        close(impl->shm_fd);
        shm_unlink(impl->shm_name.c_str());
        return -4;
    }

    std::memset(impl->mapped, 0, impl->shm_size);
    impl->read_seq = 0;
    return 0;
}

int JavaBackendEnable(JavaBackendImpl *impl)
{
    if (!impl || !impl->mapped) {
        return -1;
    }

    auto *mem = static_cast<uint8_t *>(impl->mapped);
    StoreAtRelease<uint32_t>(mem, kHeaderActive, 1);

   std::string cmd = BuildEnableCommand(*impl);
    if (cmd.empty()) {
        StoreAtRelease<uint32_t>(mem, kHeaderActive, 0);
        return -1;
    }
    int ret = RunCommand(cmd);
    if (ret != 0) {
        StoreAtRelease<uint32_t>(mem, kHeaderActive, 0);
        (void)JavaBackendRestoreRuntime(impl);
        return ret;
    }

    impl->runtimeStopped = false;
    impl->runtimeRestored = false;
    return 0;
}

int JavaBackendDisable(JavaBackendImpl *impl)
{
    if (!impl || !impl->mapped) {
        return -1;
    }
    auto *mem = static_cast<uint8_t *>(impl->mapped);
    StoreAtRelease<uint32_t>(mem, kHeaderActive, 0);
    uint64_t writeSeq = LoadAtAcquire<uint64_t>(mem, kHeaderWriteSeq);
    uint64_t dropped = LoadAtAcquire<uint64_t>(mem, kHeaderDropped);
    uint32_t slotCount = LoadAtAcquire<uint32_t>(mem, kHeaderSlotCount);
    uint64_t overwrittenByRing = (slotCount > 0 && writeSeq > slotCount) ? (writeSeq - slotCount) : 0;
    std::fprintf(stderr, "[trace-java] disabled shm writing, active=0, writeSeq=%llu, slotCount=%u, overwrittenByRing=%llu, dropped=%llu\n",
        static_cast<unsigned long long>(writeSeq), slotCount,
        static_cast<unsigned long long>(overwrittenByRing),
        static_cast<unsigned long long>(dropped));
    return 0;
}

int JavaBackendRead(JavaBackendImpl *impl, UTraceData **out_data, size_t *out_count)
{
    if (!impl || !out_data || !out_count) {
        return -1;
    }

    *out_data = nullptr;
    *out_count = 0;

    if (!impl->mapped) {
        return -2;
    }

    const uint8_t *mem = static_cast<const uint8_t *>(impl->mapped);

    if (LoadAtAcquire<uint64_t>(mem, kHeaderMagic) != kMagic) {
        return -3;
    }
    if (LoadAtAcquire<uint32_t>(mem, kHeaderVersion) != kVersion) {
        return -4;
    }

    const uint32_t slotCount = LoadAtAcquire<uint32_t>(mem, kHeaderSlotCount);
    const uint32_t slotSize = LoadAtAcquire<uint32_t>(mem, kHeaderSlotSize);
    if (slotCount == 0 || slotSize != kSlotSize) {
        return -5;
    }

    const uint64_t writeSeq = LoadAtAcquire<uint64_t>(mem, kHeaderWriteSeq);
    const uint64_t prevReadSeq = impl->read_seq;
    const uint64_t overwrittenByRing = (slotCount > 0 && writeSeq > slotCount) ? (writeSeq - slotCount) : 0;
    if (writeSeq <= impl->read_seq) {
        return 0;
    }

    const uint64_t oldest = writeSeq > slotCount ? (writeSeq - slotCount + 1) : 1;
    const uint64_t startSeq = std::max<uint64_t>(impl->read_seq + 1, oldest);
    if (startSeq > writeSeq) {
        impl->read_seq = writeSeq;
        return 0;
    }

    size_t n = static_cast<size_t>(writeSeq - startSeq + 1);
    size_t allocSize = sizeof(size_t) + n * sizeof(UTraceData);
    uint8_t *raw = static_cast<uint8_t *>(std::calloc(1, allocSize));
    if (!raw) {
        return -6;
    }

    size_t storedCount = 0;
    std::memcpy(raw, &storedCount, sizeof(size_t));
    UTraceData *block = reinterpret_cast<UTraceData *>(raw + sizeof(size_t));

    size_t outIdx = 0;
    size_t seqMismatch = 0;
    for (uint64_t seq = startSeq; seq <= writeSeq; ++seq) {
        size_t slotIndex = static_cast<size_t>((seq - 1) % slotCount);
        const uint8_t *slot = mem + kHeaderSize + slotIndex * kSlotSize;
        uint64_t slotSeq = LoadAtAcquire<uint64_t>(slot, kSlotSeq);
        if (slotSeq != seq) {
            ++seqMismatch;
            continue;
        }

        block[outIdx].addr = static_cast<unsigned long>(LoadAt<uint64_t>(slot, kSlotAddr));
        block[outIdx].tid = LoadAt<int32_t>(slot, kSlotTid);
        block[outIdx].cpu = LoadAt<int32_t>(slot, kSlotCpu);
        block[outIdx].timestamp = LoadAt<int64_t>(slot, kSlotTimestamp);
        block[outIdx].gPtr = LoadAt<uint64_t>(slot, kSlotGPtr);
        block[outIdx].isRet = LoadAt<uint32_t>(slot, kSlotIsRet);
        if (!FillTraceDataStrings(block[outIdx], slot)) {
            std::fprintf(stderr, "[trace-java] JavaBackendRead failed: duplicate string failed, outIdx=%zu\n", outIdx);
            FreeJavaTraceBlockRaw(raw, block, outIdx + 1);
            return -6;
        }
        ++outIdx;
    }

    impl->read_seq = writeSeq;
    std::fprintf(stderr, "[trace-java] JavaBackendRead summary: prevReadSeq=%llu, writeSeq=%llu, startSeq=%llu, slotCount=%u, overwrittenByRing=%llu, seqMismatch=%zu, out=%zu\n",
        static_cast<unsigned long long>(prevReadSeq),
        static_cast<unsigned long long>(writeSeq),
        static_cast<unsigned long long>(startSeq),
        slotCount,
        static_cast<unsigned long long>(overwrittenByRing),
        seqMismatch, outIdx);
    if (outIdx == 0) {
        std::free(raw);
        return 0;
    }

    std::memcpy(raw, &outIdx, sizeof(size_t));
    *out_data = block;
    *out_count = outIdx;
    return 0;
}

void JavaBackendDataFree(UTraceData *data)
{
    if (data == nullptr) {
        return;
    }
    uint8_t *raw = reinterpret_cast<uint8_t *>(data) - sizeof(size_t);
    size_t count = 0;
    std::memcpy(&count, raw, sizeof(size_t));
    FreeJavaTraceBlockRaw(raw, data, count);
}

void JavaBackendClose(JavaBackendImpl *impl)
{
    if (!impl) {
        return;
    }
    // Close is the final cleanup point: restore bytecode if possible.
    int ret = JavaBackendRestoreRuntime(impl);
    if (ret != 0) {
        std::fprintf(stderr, "[trace-java] warning: action=restore failed, ret=%d\n", ret);
    }
    if (impl->mapped && impl->shm_size) {
        auto *mem = static_cast<uint8_t *>(impl->mapped);
        StoreAtRelease<uint32_t>(mem, kHeaderActive, 0);
        munmap(impl->mapped, impl->shm_size);
        impl->mapped = nullptr;
    }
    if (impl->shm_fd >= 0) {
        close(impl->shm_fd);
        impl->shm_fd = -1;
    }
    if (!impl->shm_path.empty()) {
        std::string includeFile = impl->shm_path + ".includes";
        std::remove(includeFile.c_str());
    }
    if (!impl->shm_name.empty()) {
        shm_unlink(impl->shm_name.c_str());
        impl->shm_name.clear();
    }
    if (!impl->shm_path.empty()) {
        std::remove(impl->shm_path.c_str());
        impl->shm_path.clear();
    }
}

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
 * Description: Java trace backend implementation: shared memory layout, agent injection command building,
 * and ring-buffer slot reading for Java trace events.
 ******************************************************************************/
#include "java_backend.h"
#include "java_trace_util.h"
#include "pmu.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <atomic>
#include <fcntl.h>
#include <string>
#include <type_traits>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

namespace {
static constexpr uint64_t K_MAGIC = 0x5554524356415731ULL; // UTRCVAW1
static constexpr uint32_t K_VERSION = 1;
static constexpr size_t K_HEADER_SIZE = 64;
static constexpr size_t K_HEADER_MAGIC = 0;
static constexpr size_t K_HEADER_VERSION = 8;
static constexpr size_t K_HEADER_ACTIVE = 12;
static constexpr size_t K_HEADER_SLOT_COUNT = 16;
static constexpr size_t K_HEADER_SLOT_SIZE = 20;
static constexpr size_t K_HEADER_WRITE_SEQ = 24;
static constexpr size_t K_HEADER_DROPPED = 32;
static constexpr size_t K_HEADER_DEADLINE_NS = 40;

static constexpr size_t K_SLOT_SIZE = 512;
static constexpr size_t K_SLOT_SEQ = 0;
static constexpr size_t K_SLOT_ADDR = 8;
static constexpr size_t K_SLOT_TID = 16;
static constexpr size_t K_SLOT_CPU = 20;
static constexpr size_t K_SLOT_TIMESTAMP = 24;
static constexpr size_t K_SLOT_GPTR = 32;
static constexpr size_t K_SLOT_IS_RET = 40;
static constexpr size_t K_SLOT_COMM = 48;
static constexpr size_t K_SLOT_COMM_LEN = 32;
static constexpr size_t K_SLOT_MODULE = 80;
static constexpr size_t K_SLOT_MODULE_LEN = 160;
static constexpr size_t K_SLOT_FUNC = 240;
static constexpr size_t K_SLOT_FUNC_LEN = 256;

static constexpr mode_t K_TRACE_FILE_MODE = 0600;

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
    data.comm = TraceDupString(ReadFixedString(slot, K_SLOT_COMM, K_SLOT_COMM_LEN));
    if (data.comm == nullptr) {
        return false;
    }
    data.module = TraceDupString(ReadFixedString(slot, K_SLOT_MODULE, K_SLOT_MODULE_LEN));
    if (data.module == nullptr) {
        FreeTraceDataFields(data);
        return false;
    }
    data.func = TraceDupString(ReadFixedString(slot, K_SLOT_FUNC, K_SLOT_FUNC_LEN));
    if (data.func == nullptr) {
        FreeTraceDataFields(data);
        return false;
    }
    return true;
}

static bool HasAvailableSpace(const std::string &path, size_t bytes)
{
    std::string dir = path;
    size_t pos = dir.rfind('/');
    if (pos != std::string::npos) {
        dir = pos == 0 ? "/" : dir.substr(0, pos);
    }
    struct statvfs st {};
    if (statvfs(dir.c_str(), &st) != 0) {
        int error = errno;
        JavaTraceLog(MakeLogMessage("[trace-java] statvfs failed for ", dir,
                                    ", errno=", error, "(", std::strerror(error), ")\n"));
        return false;
    }
    unsigned long long available =
        static_cast<unsigned long long>(st.f_bavail) * static_cast<unsigned long long>(st.f_frsize);
    unsigned long long required = static_cast<unsigned long long>(bytes);
    if (available < required) {
        JavaTraceLog(MakeLogMessage("[trace-java] insufficient tmp space: dir=", dir,
                                    ", available=", available, ", required=", required, "\n"));
        return false;
    }
    return true;
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
    JavaTraceFlushTargetLog(*impl);
    if (ret != 0) {
        JavaTraceLog(MakeLogMessage("[trace-java] action=", action, " failed, ret=", ret,
                                    ", cmd=", cmd, "\n"));
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
static void CleanupFailedStorage(JavaBackendImpl *impl, bool removeShmFile)
{
    if (impl == nullptr) {
        return;
    }
    if (impl->shmFd >= 0) {
        close(impl->shmFd);
        impl->shmFd = -1;
    }
    if (removeShmFile && !impl->shmPath.empty()) {
        std::remove(impl->shmPath.c_str());
    }
    JavaTraceCleanupTargetAssets(impl);
}

static int PrepareJavaBackendOpen(JavaBackendImpl *impl, int pid, const char *includeRules)
{
    impl->pid = pid;
    impl->runtimeStopped = true;
    impl->runtimeRestored = true;

    if (includeRules != nullptr) {
        impl->includeRules = includeRules;
    }
    impl->filterConfigPath = FilterConfigPath();
    JavaTraceLocalConfig localConfig = LoadLocalConfig(impl->filterConfigPath);
    if (!localConfig.valid) {
        return -1;
    }
    impl->slotCount = localConfig.slotCount;
    impl->shmName = "utrace_java_" + std::to_string(pid) + "_" + TimestampSuffix();
    impl->shmSize = K_HEADER_SIZE + static_cast<size_t>(impl->slotCount) * K_SLOT_SIZE;

    if (!JavaTracePrepareTargetFiles(*impl)) {
        JavaTraceCleanupTargetAssets(impl);
        return -1;
    }
    if (!HasAvailableSpace(impl->shmPath, impl->shmSize)) {
        JavaTraceCleanupTargetAssets(impl);
        return -1;
    }
    return 0;
}

static int OpenJavaTraceStorage(JavaBackendImpl *impl)
{
    JavaTraceLog(MakeLogMessage("[trace-java] slotCount=", impl->slotCount,
                                ", shmSize=", impl->shmSize,
                                ", shmPath=", impl->shmPath,
                                ", targetShmPath=", impl->target.shmPath, "\n"));
    impl->shmFd = open(impl->shmPath.c_str(), O_CREAT | O_RDWR | O_TRUNC, K_TRACE_FILE_MODE);
    if (impl->shmFd < 0) {
        int error = errno;
        JavaTraceLog(MakeLogMessage("[trace-java] open tmp trace file failed: ", impl->shmPath,
                                    ", errno=", error, "(", std::strerror(error), ")\n"));
        CleanupFailedStorage(impl, false);
        return -1;
    }

    if (ftruncate(impl->shmFd, static_cast<off_t>(impl->shmSize)) != 0) {
        int error = errno;
        JavaTraceLog(MakeLogMessage("[trace-java] ftruncate trace file failed: ", impl->shmPath,
                                    ", errno=", error, "(", std::strerror(error), ")\n"));
        CleanupFailedStorage(impl, true);
        return -1;
    }

    impl->mapped = mmap(nullptr, impl->shmSize, PROT_READ | PROT_WRITE, MAP_SHARED, impl->shmFd, 0);
    if (impl->mapped == MAP_FAILED) {
        impl->mapped = nullptr;
        int error = errno;
        JavaTraceLog(MakeLogMessage("[trace-java] mmap trace file failed: ", impl->shmPath,
                                    ", errno=", error, "(", std::strerror(error), ")\n"));
        CleanupFailedStorage(impl, true);
        return -1;
    }

    std::memset(impl->mapped, 0, K_HEADER_SIZE);
    impl->readSeq = 0;
    return 0;
}
} // namespace

int JavaBackendOpen(JavaBackendImpl *impl, int pid, const char *includeRules)
{
    if (impl == nullptr) {
        return -1;
    }
    if (PrepareJavaBackendOpen(impl, pid, includeRules) != 0) {
        return -1;
    }
    return OpenJavaTraceStorage(impl);
}

int JavaBackendEnable(JavaBackendImpl *impl)
{
    if (!impl || !impl->mapped) {
        return -1;
    }
    auto *mem = static_cast<uint8_t *>(impl->mapped);
    // mark active before attach so TraceRuntime can write after TraceAgent configures the sink
    StoreAtRelease<uint32_t>(mem, K_HEADER_ACTIVE, 1);

    std::string cmd = BuildEnableCommand(*impl);
    if (cmd.empty()) {
        StoreAtRelease<uint32_t>(mem, K_HEADER_ACTIVE, 0);
        return -1;
    }
    int ret = RunCommand(cmd);
    JavaTraceFlushTargetLog(*impl);
    if (ret != 0) {
        StoreAtRelease<uint32_t>(mem, K_HEADER_ACTIVE, 0);
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
    StoreAtRelease<uint32_t>(mem, K_HEADER_ACTIVE, 0);

    uint64_t writeSeq = LoadAtAcquire<uint64_t>(mem, K_HEADER_WRITE_SEQ);
    uint64_t dropped = LoadAtAcquire<uint64_t>(mem, K_HEADER_DROPPED);
    uint32_t slotCount = LoadAtAcquire<uint32_t>(mem, K_HEADER_SLOT_COUNT);
    uint64_t overwrittenByRing = (slotCount > 0 && writeSeq > slotCount) ? (writeSeq - slotCount) : 0;
    JavaTraceLog(MakeLogMessage("[trace-java] disabled shm writing, active=0, writeSeq=", writeSeq,
                                ", slotCount=", slotCount,
                                ", overwrittenByRing=", overwrittenByRing,
                                ", dropped=", dropped, "\n"));
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
    if (LoadAtAcquire<uint64_t>(mem, K_HEADER_MAGIC) != K_MAGIC) {
        return -3;
    }
    if (LoadAtAcquire<uint32_t>(mem, K_HEADER_VERSION) != K_VERSION) {
        return -4;
    }

    const uint32_t slotCount = LoadAtAcquire<uint32_t>(mem, K_HEADER_SLOT_COUNT);
    const uint32_t slotSize = LoadAtAcquire<uint32_t>(mem, K_HEADER_SLOT_SIZE);
    // shared memory layout is abnormal
    if (slotCount == 0 || slotSize != K_SLOT_SIZE) {
        return -5;
    }

    const uint64_t writeSeq = LoadAtAcquire<uint64_t>(mem, K_HEADER_WRITE_SEQ);
    const uint64_t prevReadSeq = impl->readSeq;
    const uint64_t overwrittenByRing = (slotCount > 0 && writeSeq > slotCount) ? (writeSeq - slotCount) : 0;
    // no new readSeq index to read
    if (writeSeq <= impl->readSeq) {
        return 0;
    }

    const uint64_t oldest = writeSeq > slotCount ? (writeSeq - slotCount + 1) : 1;
    const uint64_t startSeq = std::max<uint64_t>(impl->readSeq + 1, oldest);
    if (startSeq > writeSeq) {
        impl->readSeq = writeSeq;
        return 0;
    }

    // return data in [startSeq, writeSeq]
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
        const uint8_t *slot = mem + K_HEADER_SIZE + slotIndex * K_SLOT_SIZE;
        uint64_t slotSeq = LoadAtAcquire<uint64_t>(slot, K_SLOT_SEQ);
        if (slotSeq != seq) {
            ++seqMismatch;
            continue;
        }
        block[outIdx].addr = static_cast<unsigned long>(LoadAt<uint64_t>(slot, K_SLOT_ADDR));
        block[outIdx].tid = LoadAt<int32_t>(slot, K_SLOT_TID);
        block[outIdx].cpu = LoadAt<int32_t>(slot, K_SLOT_CPU);
        block[outIdx].timestamp = LoadAt<int64_t>(slot, K_SLOT_TIMESTAMP);
        block[outIdx].gPtr = LoadAt<uint64_t>(slot, K_SLOT_GPTR);
        block[outIdx].isRet = LoadAt<uint32_t>(slot, K_SLOT_IS_RET);
        if (!FillTraceDataStrings(block[outIdx], slot)) {
            JavaTraceLog(MakeLogMessage(
                "[trace-java] JavaBackendRead failed: duplicate string failed, outIdx=", outIdx, "\n"));
            FreeJavaTraceBlockRaw(raw, block, outIdx + 1);
            return -7;
        }
        ++outIdx;
    }

    // advance readSeq to the current writeSeq. Missed or overwritten records will not be attempted.
    impl->readSeq = writeSeq;
    JavaTraceLog(MakeLogMessage("[trace-java] JavaBackendRead summary: prevReadSeq=", prevReadSeq,
                                ", writeSeq=", writeSeq,
                                ", startSeq=", startSeq,
                                ", slotCount=", slotCount,
                                ", overwrittenByRing=", overwrittenByRing,
                                ", seqMismatch=", seqMismatch,
                                ", out=", outIdx, "\n"));
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
        JavaTraceLog(MakeLogMessage("[trace-java] warning: action=restore failed, ret=", ret, "\n"));
    }
    if (impl->mapped && impl->shmSize) {
        auto *mem = static_cast<uint8_t *>(impl->mapped);
        StoreAtRelease<uint32_t>(mem, K_HEADER_ACTIVE, 0);
        munmap(impl->mapped, impl->shmSize);
        impl->mapped = nullptr;
    }
    if (impl->shmFd >= 0) {
        close(impl->shmFd);
        impl->shmFd = -1;
    }
    JavaTraceFlushTargetLog(*impl);
    if (!impl->shmPath.empty()) {
        std::string includeFile = impl->shmPath + ".includes";
        std::remove(includeFile.c_str());
    }
    if (!impl->shmPath.empty()) {
        std::remove(impl->shmPath.c_str());
        impl->shmPath.clear();
    }
    JavaTraceCleanupTargetAssets(impl);
    impl->shmName.clear();
    impl->target.shmPath.clear();
}

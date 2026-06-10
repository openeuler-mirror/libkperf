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
#include "java_trace_util.h"
#include "pmu.h"

#include <algorithm>
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
#include <unistd.h>

namespace {
static constexpr uint64_t kMagic = 0x5554524356415731ULL; // UTRCVAW1
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
    data.comm = TraceDupString(ReadFixedString(slot, kSlotComm, kSlotCommLen));
    if (data.comm == nullptr) {
        return false;
    }
    data.module = TraceDupString(ReadFixedString(slot, kSlotModule, kSlotModuleLen));
    if (data.module == nullptr) {
        FreeTraceDataFields(data);
        return false;
    }
    data.func = TraceDupString(ReadFixedString(slot, kSlotFunc, kSlotFuncLen));
    if (data.func == nullptr) {
        FreeTraceDataFields(data);
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
    JavaTraceLocalConfig localConfig = LoadLocalConfig(impl->filter_config_path);
    impl->slot_count = localConfig.slotCount;
    impl->shm_name = "/utrace_java_" + std::to_string(pid) + "_" + TimestampSuffix();
    impl->shm_path = "/dev/shm" + impl->shm_name;
    impl->shm_fd = shm_open(impl->shm_name.c_str(), O_CREAT | O_RDWR, 0600);
    if (impl->shm_fd < 0) {
        return -2;
    }

    impl->shm_size = kHeaderSize + static_cast<size_t>(impl->slot_count) * kSlotSize;
    std::fprintf(stderr, "[trace-java] slotCount=%u, shmSize=%zu\n", impl->slot_count, impl->shm_size);
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

    std::memset(impl->mapped, 0, kHeaderSize);
    impl->read_seq = 0;
    return 0;
}

int JavaBackendEnable(JavaBackendImpl *impl)
{
    if (!impl || !impl->mapped) {
        return -1;
    }
    auto *mem = static_cast<uint8_t *>(impl->mapped);
    // mark the segment active before attach so TraceRuntime can start writing immediately after it is configured by TraceAgent
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
    // shared memory layout is abnormal
    if (slotCount == 0 || slotSize != kSlotSize) {
        return -5;
    }

    const uint64_t writeSeq = LoadAtAcquire<uint64_t>(mem, kHeaderWriteSeq);
    const uint64_t prevReadSeq = impl->read_seq;
    const uint64_t overwrittenByRing = (slotCount > 0 && writeSeq > slotCount) ? (writeSeq - slotCount) : 0;
    // no new read_seq index to read
    if (writeSeq <= impl->read_seq) {
        return 0;
    }

    // ring buffer 只能保存最近 slotCount 条记录。若生产端已经绕环覆盖，
    // oldest 表示当前仍可能读到的最老序号，startSeq 取“未读序号”和“仍可访问序号”两者中的较大值。
    // 后面仍会逐 slot 校验 slotSeq，避免读到被并发写覆盖或尚未完全发布的槽位。
    const uint64_t oldest = writeSeq > slotCount ? (writeSeq - slotCount + 1) : 1;
    const uint64_t startSeq = std::max<uint64_t>(impl->read_seq + 1, oldest);
    if (startSeq > writeSeq) {
        impl->read_seq = writeSeq;
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
            return -7;
        }
        ++outIdx;
    }

    // advance read_seq to the current writeSeq. Missed or overwritten records will not be attempted.
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

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
#include <climits>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <thread>
#include <vector>
#include <fcntl.h>
#include <signal.h>
#include <string>
#include <type_traits>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

namespace {
static constexpr uint64_t K_MAGIC = 0x5554524356415731ULL; // UTRCVAW1
static constexpr uint32_t K_VERSION = 4;
static constexpr size_t K_HEADER_SIZE = 128;
static constexpr size_t K_HEADER_MAGIC = 0;
static constexpr size_t K_HEADER_VERSION = 8;
static constexpr size_t K_HEADER_ACTIVE = 12;
// Offsets 16 and 20 are reserved (removed ring mode / group size fields).
static constexpr size_t K_HEADER_RING_COUNT = 24;
static constexpr size_t K_HEADER_SLOTS_PER_RING = 28;
static constexpr size_t K_HEADER_DROPPED = 32;
static constexpr size_t K_HEADER_METHOD_CAPACITY = 40;
static constexpr size_t K_HEADER_METHOD_ENTRY_SIZE = 44;
static constexpr size_t K_HEADER_THREAD_CAPACITY = 48;
static constexpr size_t K_HEADER_THREAD_ENTRY_SIZE = 52;
static constexpr size_t K_HEADER_METHOD_COUNT = 56;
static constexpr size_t K_HEADER_THREAD_COUNT = 60;
static constexpr size_t K_HEADER_EVENT_OFFSET = 64;
static constexpr size_t K_HEADER_DESC_OFFSET = 72;
static constexpr size_t K_HEADER_RING_HIGH_WATER = 80;

static constexpr uint32_t K_METHOD_CAPACITY = 131072;
static constexpr size_t K_METHOD_ENTRY_SIZE = 448;
static constexpr size_t K_METHOD_SEQ = 0;
static constexpr size_t K_METHOD_ADDR = 8;
static constexpr size_t K_METHOD_MODULE = 16;
static constexpr size_t K_METHOD_MODULE_LEN = 160;
static constexpr size_t K_METHOD_FUNC = 176;
static constexpr size_t K_METHOD_FUNC_LEN = 256;

static constexpr uint32_t K_THREAD_CAPACITY = 65536;
static constexpr size_t K_THREAD_ENTRY_SIZE = 48;
static constexpr size_t K_THREAD_SEQ = 0;
static constexpr size_t K_THREAD_TID = 4;
static constexpr size_t K_THREAD_COMM = 8;
static constexpr size_t K_THREAD_COMM_LEN = 32;

// ABI v4 ring descriptor table (one padded 128-byte entry per ring).
static constexpr size_t K_DESC_SIZE = 128;
static constexpr size_t K_DESC_WRITE_SEQ = 0;
static constexpr size_t K_DESC_DROPPED = 8;
static constexpr uint32_t K_MAX_RING_COUNT = 65536;
static constexpr uint32_t K_MAX_SLOTS_PER_RING = 1U << 22;
static constexpr uint64_t K_MAX_SHM_SIZE = 64ULL << 30;

static constexpr size_t K_SLOT_SIZE = 64;
static constexpr size_t K_SLOT_SEQ = 0;
static constexpr size_t K_SLOT_DATA_TYPE = 8;

// METHOD record field group (K_DATA_TYPE_METHOD).
static constexpr size_t K_SLOT_METHOD_IS_RET = 12;
static constexpr size_t K_SLOT_METHOD_ADDR = 16;
static constexpr size_t K_SLOT_METHOD_METHOD_ID = 24;
static constexpr size_t K_SLOT_METHOD_COMM_ID = 28;
static constexpr size_t K_SLOT_METHOD_TID = 32;
static constexpr size_t K_SLOT_METHOD_CPU = 36;
static constexpr size_t K_SLOT_METHOD_TIMESTAMP = 40;
static constexpr size_t K_SLOT_METHOD_GPTR = 48;

// VT_PHASE record field group (K_DATA_TYPE_VT_PHASE): only the lifecycle fields are written.
static constexpr size_t K_SLOT_VT_CARRIER_TID = 16;
static constexpr size_t K_SLOT_VT_VT_ID = 24;
static constexpr size_t K_SLOT_VT_PHASE_ID = 32;
static constexpr size_t K_SLOT_VT_CPU = 36;
static constexpr size_t K_SLOT_VT_TIMESTAMP = 40;
static constexpr size_t K_SLOT_VT_FD = 48;
static constexpr size_t K_SLOT_VT_BYTES = 52;
static constexpr size_t K_SLOT_VT_OS_TID = 60;
static constexpr size_t K_SLOT_VT_COMM_ID = 12;

///< Java agent wire record kind (SharedEventSink SLOT_DATA_TYPE): 0=method, 1=vt-phase.
///< Kept separate from enum UTraceFormat so the wire protocol can evolve independently.
static constexpr int16_t K_DATA_TYPE_METHOD = 0;
static constexpr int16_t K_DATA_TYPE_VT_PHASE = 1;


static constexpr size_t K_METHOD_BYTES = static_cast<size_t>(K_METHOD_CAPACITY) * K_METHOD_ENTRY_SIZE;
static constexpr size_t K_THREAD_BYTES = static_cast<size_t>(K_THREAD_CAPACITY) * K_THREAD_ENTRY_SIZE;
// ABI v4: ring descriptor table right after the dictionaries, ring data after it.
static constexpr size_t K_DESC_OFFSET = K_HEADER_SIZE + K_METHOD_BYTES + K_THREAD_BYTES;

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

static bool ParsePositiveTid(const char *text, int *tid)
{
    if (text == nullptr || tid == nullptr || *text == '\0') {
        return false;
    }
    char *end = nullptr;
    errno = 0;
    long value = std::strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value <= 0 || value > INT_MAX) {
        return false;
    }
    *tid = static_cast<int>(value);
    return true;
}

static bool ReadInnermostNamespaceTid(const std::string &statusPath, int *namespaceTid)
{
    std::ifstream status(statusPath);
    std::string line;
    while (std::getline(status, line)) {
        if (line.compare(0, 6, "NSpid:") != 0) {
            continue;
        }
        std::istringstream values(line.substr(6));
        int value = 0;
        bool found = false;
        while (values >> value) {
            if (value > 0) {
                *namespaceTid = value;
                found = true;
            }
        }
        return found;
    }
    return false;
}

static void RefreshHostTidMap(JavaBackendImpl *impl)
{
    if (impl == nullptr || impl->pid <= 0) {
        return;
    }
    const std::string taskPath = "/proc/" + std::to_string(impl->pid) + "/task";
    DIR *taskDir = opendir(taskPath.c_str());
    if (taskDir == nullptr) {
        return;
    }
    struct dirent *entry = nullptr;
    while ((entry = readdir(taskDir)) != nullptr) {
        int hostTid = 0;
        if (!ParsePositiveTid(entry->d_name, &hostTid)) {
            continue;
        }
        int namespaceTid = hostTid;
        if (!ReadInnermostNamespaceTid(taskPath + "/" + entry->d_name + "/status", &namespaceTid)) {
            continue;
        }
        impl->hostTidByNamespaceTid[namespaceTid] = hostTid;
    }
    closedir(taskDir);
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

static void ResolveTraceDataStrings(std::string &comm, std::string &module, std::string &func,
                                    const uint8_t *mem, uint64_t addr, int32_t namespaceTid,
                                    uint32_t methodId, int32_t commId,
                                    bool &metadataMismatch)
{
    module = "<unknown>";
    func = "<unknown>";
    comm = "unknown";

    if (methodId < K_METHOD_CAPACITY) {
        const uint8_t *method = mem + K_HEADER_SIZE + static_cast<size_t>(methodId) * K_METHOD_ENTRY_SIZE;
        if (LoadAtAcquire<uint32_t>(method, K_METHOD_SEQ) == methodId + 1 &&
            LoadAt<uint64_t>(method, K_METHOD_ADDR) == addr) {
            module = ReadFixedString(method, K_METHOD_MODULE, K_METHOD_MODULE_LEN);
            func = ReadFixedString(method, K_METHOD_FUNC, K_METHOD_FUNC_LEN);
        } else {
            metadataMismatch = true;
        }
    } else {
        metadataMismatch = true;
    }

    if (commId >= 0 && static_cast<uint32_t>(commId) < K_THREAD_CAPACITY) {
        const uint32_t threadId = static_cast<uint32_t>(commId);
        const uint8_t *thread = mem + K_HEADER_SIZE + K_METHOD_BYTES +
                                static_cast<size_t>(threadId) * K_THREAD_ENTRY_SIZE;
        if (LoadAtAcquire<uint32_t>(thread, K_THREAD_SEQ) == threadId + 1 &&
            LoadAt<int32_t>(thread, K_THREAD_TID) == namespaceTid) {
            comm = ReadFixedString(thread, K_THREAD_COMM, K_THREAD_COMM_LEN);
        } else {
            metadataMismatch = true;
        }
    } else {
        metadataMismatch = true;
    }
}

// Resolve the carrier thread name from the thread dictionary for VT_PHASE events.
// commId < 0 means the event carries no carrier name (not a mismatch); empty string on mismatch.
static std::string ResolveCarrierComm(const uint8_t *mem, int32_t commId, int32_t namespaceTid,
                                      bool &metadataMismatch)
{
    if (commId < 0) {
        return std::string();
    }
    if (static_cast<uint32_t>(commId) < K_THREAD_CAPACITY) {
        const uint32_t threadId = static_cast<uint32_t>(commId);
        const uint8_t *thread = mem + K_HEADER_SIZE + K_METHOD_BYTES +
                                static_cast<size_t>(threadId) * K_THREAD_ENTRY_SIZE;
        if (LoadAtAcquire<uint32_t>(thread, K_THREAD_SEQ) == threadId + 1 &&
            LoadAt<int32_t>(thread, K_THREAD_TID) == namespaceTid) {
            return ReadFixedString(thread, K_THREAD_COMM, K_THREAD_COMM_LEN);
        }
    }
    metadataMismatch = true;
    return std::string();
}

// Counts the CPUs the target process may actually run on, mirroring the JVM's
// availableProcessors() affinity semantics. Falls back to online CPUs.
static uint32_t CountTargetCpus(int pid)
{
    if (pid > 0) {
        std::ifstream status("/proc/" + std::to_string(pid) + "/status");
        std::string line;
        while (std::getline(status, line)) {
            if (line.compare(0, 16, "Cpus_allowed_list:") != 0) {
                continue;
            }
            const std::string list = line.substr(16);
            size_t pos = list.find_first_of("0123456789");
            if (pos == std::string::npos) {
                break;
            }
            uint64_t count = 0;
            while (pos < list.size()) {
                char *end = nullptr;
                unsigned long long lo = std::strtoull(list.c_str() + pos, &end, 10);
                if (end == nullptr || end == list.c_str() + pos) {
                    break;
                }
                unsigned long long hi = lo;
                if (*end == '-') {
                    hi = std::strtoull(end + 1, &end, 10);
                }
                if (hi >= lo) {
                    count += hi - lo + 1;
                }
                if (*end != ',') {
                    break;
                }
                pos = static_cast<size_t>(end - list.c_str()) + 1;
            }
            if (count > 0 && count <= K_MAX_RING_COUNT) {
                return static_cast<uint32_t>(count);
            }
            break;
        }
    }
    long online = sysconf(_SC_NPROCESSORS_ONLN);
    if (online <= 0) {
        return 1;
    }
    return static_cast<uint32_t>(std::min<long>(online, K_MAX_RING_COUNT));
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

// The agent sizes the file from its own availableProcessors(), which can exceed the
// backend's CPU estimate when affinity changed between open and attach. Re-map over the
// grown file once so consumers can still read every advertised ring.
static bool TryExtendShmMapping(JavaBackendImpl *impl)
{
    if (impl->shmFd < 0) {
        return false;
    }
    struct stat st {};
    if (fstat(impl->shmFd, &st) != 0) {
        return false;
    }
    size_t newSize = static_cast<size_t>(st.st_size);
    if (newSize <= impl->shmSize || newSize > K_MAX_SHM_SIZE) {
        return false;
    }
    void *base = mmap(nullptr, newSize, PROT_READ | PROT_WRITE, MAP_SHARED, impl->shmFd, 0);
    if (base == MAP_FAILED) {
        return false;
    }
    void *old = impl->mapped;
    size_t oldSize = impl->shmSize;
    impl->mapped = base;
    impl->shmSize = newSize;
    munmap(old, oldSize);
    JavaTraceLog(MakeLogMessage("[trace-java] extended trace mapping: bytes=", newSize, "\n"));
    return true;
}

static void DeactivateSharedMemory(JavaBackendImpl *impl)
{
    if (impl == nullptr || impl->mapped == nullptr || impl->shmSize < K_HEADER_SIZE) {
        return;
    }
    auto *mem = static_cast<uint8_t *>(impl->mapped);
    StoreAtRelease<uint32_t>(mem, K_HEADER_ACTIVE, 0);
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
        DeactivateSharedMemory(impl);
        JavaTraceLog(MakeLogMessage("[trace-java] action=", action, " failed, ret=", ret, ", cmd=", cmd, "\n"));
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
    bool targetExited = impl->pid <= 0 || (kill(static_cast<pid_t>(impl->pid), 0) != 0 && errno == ESRCH);
    if (targetExited) {
        JavaTraceLog(MakeLogMessage("[trace-java] skip restore: target pid no longer exists, pid=", impl->pid, "\n"));
        impl->runtimeRestored = true;
        impl->runtimeStopped = true;
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

static int PrepareJavaBackendOpen(JavaBackendImpl *impl, int pid, const char *includeRules, bool enableVtPhase)
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
    impl->slotsPerRing = localConfig.slotsPerRing;
    impl->consumerThreads = localConfig.consumerThreads;
    impl->enableVtPhase = localConfig.enableVtPhase || enableVtPhase;
    impl->shmName = "utrace_java_" + std::to_string(pid) + "_" + TimestampSuffix();
    // One ring per usable CPU of the target, matching the agent's availableProcessors().
    impl->ringCount = CountTargetCpus(pid);
    impl->descOffset = K_DESC_OFFSET;
    impl->eventOffset = K_DESC_OFFSET + static_cast<size_t>(impl->ringCount) * K_DESC_SIZE;
    impl->ringDataBytes = static_cast<size_t>(impl->slotsPerRing) * K_SLOT_SIZE;
    impl->shmSize = impl->eventOffset + static_cast<size_t>(impl->ringCount) * impl->ringDataBytes;

    if (impl->ringCount == 0 || impl->ringCount > K_MAX_RING_COUNT ||
        impl->slotsPerRing == 0 || impl->slotsPerRing > K_MAX_SLOTS_PER_RING ||
        impl->shmSize > K_MAX_SHM_SIZE) {
        JavaTraceLog(MakeLogMessage("[trace-java] invalid ring config: ringCount=", impl->ringCount,
            ", slotsPerRing=", impl->slotsPerRing, ", shmSize=", impl->shmSize, "\n"));
        return -1;
    }

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
    impl->shmFd = open(impl->shmPath.c_str(), O_CREAT | O_EXCL | O_RDWR | O_CLOEXEC | O_NOFOLLOW,
                       K_TRACE_FILE_MODE);
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

    if (!JavaTraceSetTargetFileAccess(*impl, impl->shmFd, impl->shmPath)) {
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
    impl->ringReadSeq.clear();
    return 0;
}

// One decoded event with its timestamp kept alongside for the cross-ring merge.
struct DecodedRecord {
    int64_t timestamp{0};
    UTraceData data{};
};

// Per-shard state for parallel ring scanning. Shards own disjoint ring ranges and
// disjoint ringReadSeq slots, so no additional synchronization is needed.
struct RingReadShard {
    const uint8_t *mem{nullptr};
    JavaBackendImpl *impl{nullptr};
    uint32_t beginRing{0};
    uint32_t endRing{0};
    uint64_t descOffset{0};
    uint64_t eventOffset{0};
    uint64_t ringDataBytes{0};
    uint32_t slotsPerRing{0};
    std::vector<DecodedRecord> records;
    std::vector<std::pair<uint32_t, uint64_t>> cursors;
    uint64_t totalWrite{0};
    uint64_t overwritten{0};
    uint64_t seqMismatch{0};
    uint64_t metadataMismatch{0};
    bool allocFailed{false};
};

// Decode one published slot into a UTraceData record. Returns false only when the
// payload allocation failed; the caller frees already decoded records in that case.
static bool DecodeSlotRecord(RingReadShard *shard, const uint8_t *slot, UTraceData *out, int64_t *outTs)
{
    const uint8_t *mem = shard->mem;
    const int16_t dataType = LoadAt<int16_t>(slot, K_SLOT_DATA_TYPE);

    unsigned format = UTRACE_FMT_METHOD;
    unsigned payloadSize = 0;
    void *payload = nullptr;
    if (dataType == K_DATA_TYPE_VT_PHASE) {
        // VT_PHASE field group: only the lifecycle fields are present.
        const uint64_t carrierTid = LoadAt<uint64_t>(slot, K_SLOT_VT_CARRIER_TID);
        const uint64_t vtId = LoadAt<uint64_t>(slot, K_SLOT_VT_VT_ID);
        const uint32_t phaseId = LoadAt<uint32_t>(slot, K_SLOT_VT_PHASE_ID);
        const int cpu = LoadAt<int32_t>(slot, K_SLOT_VT_CPU);
        const int64_t timestamp = LoadAt<int64_t>(slot, K_SLOT_VT_TIMESTAMP);
        const int fd = LoadAt<int32_t>(slot, K_SLOT_VT_FD);
        const int64_t bytes = LoadAt<int64_t>(slot, K_SLOT_VT_BYTES);
        const uint32_t osTid = LoadAt<uint32_t>(slot, K_SLOT_VT_OS_TID);
        const int32_t commId = LoadAt<int32_t>(slot, K_SLOT_VT_COMM_ID);
        bool vtMismatched = false;
        const std::string carrierComm = ResolveCarrierComm(mem, commId, static_cast<int32_t>(osTid),
                                                           vtMismatched);
        if (vtMismatched) {
            ++shard->metadataMismatch;
        }
        format = UTRACE_FMT_VT_PHASE;
        payload = BuildVtPhasePayload(timestamp, carrierTid, vtId, phaseId, cpu, fd, bytes, osTid,
                                      carrierComm.c_str(), &payloadSize);
        *outTs = timestamp;
    } else {
        // METHOD field group.
        const uint64_t addr = LoadAt<uint64_t>(slot, K_SLOT_METHOD_ADDR);
        const int32_t namespaceTid = LoadAt<int32_t>(slot, K_SLOT_METHOD_TID);
        const int cpu = LoadAt<int32_t>(slot, K_SLOT_METHOD_CPU);
        const int64_t timestamp = LoadAt<int64_t>(slot, K_SLOT_METHOD_TIMESTAMP);
        const uint64_t gPtr = LoadAt<uint64_t>(slot, K_SLOT_METHOD_GPTR);
        const int32_t isRet = LoadAt<int32_t>(slot, K_SLOT_METHOD_IS_RET);
        const uint32_t methodId = LoadAt<uint32_t>(slot, K_SLOT_METHOD_METHOD_ID);
        const int32_t commId = LoadAt<int32_t>(slot, K_SLOT_METHOD_COMM_ID);
        std::string comm;
        std::string module;
        std::string func;
        bool mismatched = false;
        ResolveTraceDataStrings(comm, module, func, mem, addr, namespaceTid, methodId, commId, mismatched);
        if (mismatched) {
            ++shard->metadataMismatch;
        }
        int tid = -1;
        auto hostTid = shard->impl->hostTidByNamespaceTid.find(namespaceTid);
        if (hostTid != shard->impl->hostTidByNamespaceTid.end()) {
            tid = hostTid->second;
        }
        // DATA_TYPE_METHOD: METHOD_IS_RET is the enter/exit or vthread-transition marker.
        payload = BuildMethodPayload(timestamp, static_cast<unsigned long>(addr), gPtr, tid, cpu,
                                     static_cast<unsigned>(isRet), comm.c_str(), module.c_str(), func.c_str(),
                                     &payloadSize);
        *outTs = timestamp;
    }
    if (payload == nullptr) {
        shard->allocFailed = true;
        return false;
    }
    out->format = format;
    out->size = payloadSize;
    out->payload = payload;
    return true;
}

// Scan [beginRing, endRing) of the per-ring descriptor table and decode new events.
static void DecodeRingRange(RingReadShard *shard)
{
    for (uint32_t ring = shard->beginRing; ring < shard->endRing; ++ring) {
        const uint8_t *desc = shard->mem + shard->descOffset + static_cast<size_t>(ring) * K_DESC_SIZE;
        const uint64_t writeSeq = LoadAtAcquire<uint64_t>(desc, K_DESC_WRITE_SEQ);
        shard->totalWrite += writeSeq;
        if (writeSeq == 0) {
            continue;
        }
        const uint64_t read = shard->impl->ringReadSeq[ring];
        if (writeSeq <= read) {
            continue;
        }
        const uint64_t slotsPerRing = shard->slotsPerRing;
        const uint64_t oldest = writeSeq > slotsPerRing ? writeSeq - slotsPerRing + 1 : 1;
        const uint64_t startSeq = std::max<uint64_t>(read + 1, oldest);
        if (oldest > read + 1) {
            shard->overwritten += oldest - (read + 1);
        }
        const uint8_t *ringBase = shard->mem + static_cast<size_t>(shard->eventOffset) +
                                  static_cast<size_t>(ring) * shard->ringDataBytes;
        for (uint64_t seq = startSeq; seq <= writeSeq; ++seq) {
            const uint8_t *slot = ringBase + static_cast<size_t>((seq - 1) % slotsPerRing) * K_SLOT_SIZE;
            const uint64_t slotSeq = LoadAtAcquire<uint64_t>(slot, K_SLOT_SEQ);
            if (slotSeq != seq) {
                ++shard->seqMismatch;
                continue;
            }
            DecodedRecord record;
            if (!DecodeSlotRecord(shard, slot, &record.data, &record.timestamp)) {
                return;
            }
            shard->records.push_back(record);
        }
        shard->cursors.emplace_back(ring, writeSeq);
    }
}

static void FreeShardRecords(RingReadShard *shard)
{
    for (auto &record : shard->records) {
        FreeTraceDataFields(record.data);
    }
    shard->records.clear();
}
} // namespace

int JavaBackendOpen(JavaBackendImpl *impl, int pid, const char *includeRules, bool enableVtPhase)
{
    if (impl == nullptr) {
        return -1;
    }
    if (PrepareJavaBackendOpen(impl, pid, includeRules, enableVtPhase) != 0) {
        return -1;
    }
    return OpenJavaTraceStorage(impl);
}

int JavaBackendPrepare(JavaBackendImpl *impl)
{
    if (!impl || !impl->mapped) {
        return -1;
    }
    if (impl->runtimePrepared) {
        return 0;
    }
    auto *mem = static_cast<uint8_t *>(impl->mapped);
    // The agent can retransform classes while the collection gate remains closed.
    StoreAtRelease<uint32_t>(mem, K_HEADER_ACTIVE, 0);

    std::string cmd = BuildEnableCommand(*impl);
    if (cmd.empty()) {
        return -1;
    }
    impl->runtimeStopped = false;
    impl->runtimeRestored = false;
    int ret = RunCommand(cmd);
    if (ret != 0) {
        DeactivateSharedMemory(impl);
        if (JavaBackendRestoreRuntime(impl) != 0) {
            (void)JavaBackendStopRuntime(impl);
        }
        return ret;
    }
    RefreshHostTidMap(impl);
    impl->runtimePrepared = true;
    return 0;
}

int JavaBackendEnable(JavaBackendImpl *impl)
{
    if (JavaBackendPrepare(impl) != 0) {
        return -1;
    }
    auto *mem = static_cast<uint8_t *>(impl->mapped);
    RefreshHostTidMap(impl);
    StoreAtRelease<uint32_t>(mem, K_HEADER_ACTIVE, 1);
    return 0;
}

int JavaBackendDisable(JavaBackendImpl *impl)
{
    if (!impl || !impl->mapped) {
        return -1;
    }
    RefreshHostTidMap(impl);
    DeactivateSharedMemory(impl);
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

    RefreshHostTidMap(impl);
    const uint8_t *mem = static_cast<const uint8_t *>(impl->mapped);
    if (LoadAtAcquire<uint64_t>(mem, K_HEADER_MAGIC) != K_MAGIC) {
        return -3;
    }
    if (LoadAtAcquire<uint32_t>(mem, K_HEADER_VERSION) != K_VERSION) {
        return -4;
    }

    const uint32_t ringCount = LoadAtAcquire<uint32_t>(mem, K_HEADER_RING_COUNT);
    const uint32_t slotsPerRing = LoadAtAcquire<uint32_t>(mem, K_HEADER_SLOTS_PER_RING);
    const uint64_t dropped = LoadAtAcquire<uint64_t>(mem, K_HEADER_DROPPED);
    const uint32_t methodCapacity = LoadAtAcquire<uint32_t>(mem, K_HEADER_METHOD_CAPACITY);
    const uint32_t methodEntrySize = LoadAtAcquire<uint32_t>(mem, K_HEADER_METHOD_ENTRY_SIZE);
    const uint32_t threadCapacity = LoadAtAcquire<uint32_t>(mem, K_HEADER_THREAD_CAPACITY);
    const uint32_t threadEntrySize = LoadAtAcquire<uint32_t>(mem, K_HEADER_THREAD_ENTRY_SIZE);
    const uint32_t methodCount = LoadAtAcquire<uint32_t>(mem, K_HEADER_METHOD_COUNT);
    const uint32_t threadCount = LoadAtAcquire<uint32_t>(mem, K_HEADER_THREAD_COUNT);
    const uint64_t eventOffset = LoadAtAcquire<uint64_t>(mem, K_HEADER_EVENT_OFFSET);
    const uint64_t descOffset = LoadAtAcquire<uint64_t>(mem, K_HEADER_DESC_OFFSET);
    // shared memory layout is abnormal
    if (ringCount == 0 || ringCount > K_MAX_RING_COUNT ||
        slotsPerRing == 0 || slotsPerRing > K_MAX_SLOTS_PER_RING ||
        methodCapacity != K_METHOD_CAPACITY || methodEntrySize != K_METHOD_ENTRY_SIZE ||
        threadCapacity != K_THREAD_CAPACITY || threadEntrySize != K_THREAD_ENTRY_SIZE ||
        methodCount > methodCapacity || threadCount > threadCapacity ||
        descOffset != K_DESC_OFFSET ||
        eventOffset != descOffset + static_cast<uint64_t>(ringCount) * K_DESC_SIZE) {
        return -5;
    }

    // The agent sizes the file from its own CPU count; if it sees more CPUs than the
    // backend did at open time, extend the mapping once before validating the range.
    const uint64_t neededSize = eventOffset +
        static_cast<uint64_t>(ringCount) * slotsPerRing * K_SLOT_SIZE;
    if (neededSize > impl->shmSize && !TryExtendShmMapping(impl)) {
        return -5;
    }
    if (mem != impl->mapped) {
        mem = static_cast<const uint8_t *>(impl->mapped);
    }

    // Every ring owns a padded descriptor; unbound rings have writeSeq 0 and cost one
    // acquire load, so scanning the whole configured range stays cheap and removes the
    // need for a globally published ring high-water marker.
    const uint32_t scanRings = ringCount;
    if (impl->ringReadSeq.size() < ringCount) {
        impl->ringReadSeq.resize(ringCount, 0);
    }

    // Split the active ring range over the configured consumer threads.
    size_t shardCount = impl->consumerThreads > 0 ? impl->consumerThreads : 1;
    if (shardCount > scanRings) {
        shardCount = scanRings == 0 ? 1 : scanRings;
    }
    std::vector<RingReadShard> shards(shardCount);
    for (size_t i = 0; i < shardCount; ++i) {
        RingReadShard &shard = shards[i];
        shard.mem = mem;
        shard.impl = impl;
        shard.beginRing = static_cast<uint32_t>(static_cast<uint64_t>(scanRings) * i / shardCount);
        shard.endRing = static_cast<uint32_t>(static_cast<uint64_t>(scanRings) * (i + 1) / shardCount);
        shard.descOffset = descOffset;
        shard.eventOffset = eventOffset;
        shard.ringDataBytes = static_cast<uint64_t>(slotsPerRing) * K_SLOT_SIZE;
        shard.slotsPerRing = slotsPerRing;
    }
    if (shardCount == 1) {
        DecodeRingRange(&shards[0]);
    } else {
        std::vector<std::thread> pool;
        pool.reserve(shardCount - 1);
        for (size_t i = 1; i < shardCount; ++i) {
            pool.emplace_back(DecodeRingRange, &shards[i]);
        }
        DecodeRingRange(&shards[0]);
        for (auto &worker : pool) {
            worker.join();
        }
    }

    uint64_t totalWrite = 0;
    uint64_t overwritten = 0;
    uint64_t seqMismatch = 0;
    uint64_t metadataMismatch = 0;
    size_t total = 0;
    bool allocFailed = false;
    for (auto &shard : shards) {
        totalWrite += shard.totalWrite;
        overwritten += shard.overwritten;
        seqMismatch += shard.seqMismatch;
        metadataMismatch += shard.metadataMismatch;
        total += shard.records.size();
        if (shard.allocFailed) {
            allocFailed = true;
        }
    }

    if (allocFailed) {
        for (auto &shard : shards) {
            FreeShardRecords(&shard);
        }
        JavaTraceLog(MakeLogMessage(
            "[trace-java] JavaBackendRead failed: payload alloc failed, decoded=", total, "\n"));
        return -7;
    }

    // No new events in any ring yet: keep the original single-shot empty diagnostic.
    if (total == 0) {
        for (auto &shard : shards) {
            for (const auto &cursor : shard.cursors) {
                impl->ringReadSeq[cursor.first] = cursor.second;
            }
        }
        if (impl->lastEmptyReadTotalSeq != totalWrite) {
            JavaTraceLog(MakeLogMessage("[trace-java] collected: events=0", ", methods=", methodCount,
                ", threads=", threadCount, ", dropped=", dropped, "\n"));
            impl->lastEmptyReadTotalSeq = totalWrite;
        }
        return 0;
    }

    // Merge shards by timestamp: records inside one ring keep issue order and the
    // final stable sort restores a globally time-ordered stream for the pipeline.
    std::vector<DecodedRecord> flat;
    flat.reserve(total);
    for (auto &shard : shards) {
        for (auto &record : shard.records) {
            flat.push_back(record);
        }
        shard.records.clear();
    }
    std::stable_sort(flat.begin(), flat.end(),
                     [](const DecodedRecord &left, const DecodedRecord &right) {
                         return left.timestamp < right.timestamp;
                     });

    size_t allocSize = sizeof(size_t) + flat.size() * sizeof(UTraceData);
    uint8_t *raw = static_cast<uint8_t *>(std::calloc(1, allocSize));
    if (raw == nullptr) {
        for (auto &record : flat) {
            FreeTraceDataFields(record.data);
        }
        return -6;
    }
    UTraceData *block = reinterpret_cast<UTraceData *>(raw + sizeof(size_t));
    for (size_t i = 0; i < flat.size(); ++i) {
        block[i] = flat[i].data;
    }

    // Advance per-ring cursors only after the output block exists; alloc failures above
    // keep the old cursors so the next poll re-reads the same range.
    for (auto &shard : shards) {
        for (const auto &cursor : shard.cursors) {
            impl->ringReadSeq[cursor.first] = cursor.second;
        }
    }

    size_t stored = flat.size();
    std::memcpy(raw, &stored, sizeof(size_t));
    *out_data = block;
    *out_count = stored;
    impl->lastEmptyReadTotalSeq = totalWrite;
    JavaTraceLog(MakeLogMessage("[trace-java] collected: events=", stored,
                                ", methods=", methodCount,
                                ", threads=", threadCount,
                                ", rings=", scanRings,
                                ", dropped=", dropped,
                                ", overwritten=", overwritten,
                                ", seqMismatch=", seqMismatch,
                                ", metadataMismatch=", metadataMismatch, "\n"));
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
    DeactivateSharedMemory(impl);
    // restore stops the runtime before restoring bytecode
    int ret = JavaBackendRestoreRuntime(impl);
    if (ret != 0) {
        JavaTraceLog(MakeLogMessage("[trace-java] warning: action=restore failed, ret=", ret, "\n"));
        int stopRet = JavaBackendStopRuntime(impl);
        if (stopRet != 0) {
            JavaTraceLog(MakeLogMessage("[trace-java] warning: fallback action=stop failed, ret=", stopRet, "\n"));
        }
    }
    if (impl->mapped && impl->shmSize) {
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

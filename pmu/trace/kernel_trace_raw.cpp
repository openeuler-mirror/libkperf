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
 * Create: 2026-08-07
 * Description: Implementation for raw kernel trace session, process and data
 ******************************************************************************/

#include "kernel_trace_util.h"
#include "common.h"
#include "trace_log.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <exception>
#include <fcntl.h>
#include <fstream>
#include <poll.h>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <utility>
#include <unistd.h>
#include <vector>

namespace kernel_trace {
namespace {
constexpr uint32_t K_RINGBUF_TYPE_PADDING = 29;
constexpr uint32_t K_RINGBUF_TYPE_TIME_EXTEND = 30;
constexpr uint32_t K_RINGBUF_TYPE_TIME_STAMP = 31;
constexpr uint32_t K_RINGBUF_DATA_TYPE_MAX = 28;
constexpr uint32_t K_TIME_SHIFT = 27;
constexpr uint64_t K_TIMESTAMP_BITS = 59;
constexpr uint64_t K_TIMESTAMP_RANGE = 1ULL << K_TIMESTAMP_BITS;
constexpr uint64_t K_TIMESTAMP_LOW_MASK = K_TIMESTAMP_RANGE - 1;
constexpr uint64_t K_PAGE_MISSED_EVENTS = 1ULL << 31;
constexpr uint64_t K_PAGE_MISSED_STORED = 1ULL << 30;
constexpr uint64_t K_PAGE_COMMIT_MASK = K_PAGE_MISSED_STORED - 1;
constexpr size_t K_MAX_RAW_SUBBUFFER_SIZE = 16U * 1024U * 1024U;

struct TraceField {
    size_t offset = 0;
    size_t size = 0;
    bool valid = false;
};

struct EventFormat {
    uint32_t id = 0;
    TraceField commonType;
    TraceField commonPid;
    TraceField function;
};

struct PageFormat {
    TraceField timestamp;
    TraceField commit;
    TraceField data;
};

struct CpuPipe {
    int cpu = -1;
    int fd = -1;
};

static bool ReadText(const std::string &path, std::string &content)
{
    std::ifstream input(path);
    if (!input.is_open()) {
        return false;
    }
    std::ostringstream output;
    output << input.rdbuf();
    content = output.str();
    return true;
}

static bool ParseUnsigned(const std::string &text, uint64_t &value)
{
    std::string trimmed = Trim(text);
    if (trimmed.empty()) {
        return false;
    }
    char *end = nullptr;
    errno = 0;
    unsigned long long parsed = std::strtoull(trimmed.c_str(), &end, 0);
    if (errno != 0 || end == trimmed.c_str() || *end != '\0') {
        return false;
    }
    value = parsed;
    return true;
}

static bool ParseProperty(const std::string &line, const char *name, uint64_t &value)
{
    std::string marker = std::string(name) + ":";
    size_t begin = line.find(marker);
    if (begin == std::string::npos) {
        return false;
    }
    begin += marker.size();
    size_t end = line.find(';', begin);
    return ParseUnsigned(line.substr(begin, end == std::string::npos ? end : end - begin), value);
}

static std::string ParseFieldName(const std::string &line)
{
    size_t field = line.find("field:");
    size_t semicolon = line.find(';', field == std::string::npos ? 0 : field + 6);
    if (field == std::string::npos || semicolon == std::string::npos) {
        return "";
    }
    std::string declaration = Trim(line.substr(field + 6, semicolon - field - 6));
    size_t space = declaration.find_last_of(" \t");
    std::string name = space == std::string::npos ? declaration : declaration.substr(space + 1);
    while (!name.empty() && name[0] == '*') {
        name.erase(name.begin());
    }
    size_t array = name.find('[');
    if (array != std::string::npos) {
        name.erase(array);
    }
    return name;
}

static bool ParseTraceField(const std::string &line, TraceField &field)
{
    uint64_t offset = 0;
    uint64_t size = 0;
    if (!ParseProperty(line, "offset", offset) || !ParseProperty(line, "size", size) || size == 0) {
        return false;
    }
    field.offset = static_cast<size_t>(offset);
    field.size = static_cast<size_t>(size);
    field.valid = true;
    return true;
}

static bool LoadEventFormat(const std::string &path, EventFormat &format,
    std::string *error)
{
    std::string text;
    if (!ReadText(path, text)) {
        if (error != nullptr) {
            *error = "Cannot read raw ftrace event format: " + path;
        }
        return false;
    }
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        std::string trimmed = Trim(line);
        if (trimmed.compare(0, 3, "ID:") == 0) {
            uint64_t id = 0;
            if (ParseUnsigned(trimmed.substr(3), id) && id <= UINT32_MAX) {
                format.id = static_cast<uint32_t>(id);
            }
            continue;
        }
        std::string name = ParseFieldName(trimmed);
        if (name == "common_type") {
            ParseTraceField(trimmed, format.commonType);
        } else if (name == "common_pid") {
            ParseTraceField(trimmed, format.commonPid);
        } else if (name == "func") {
            ParseTraceField(trimmed, format.function);
        }
    }
    bool valid = format.id != 0 && format.commonType.valid && format.commonPid.valid &&
        format.function.valid;
    if (!valid && error != nullptr) {
        *error = "Incomplete raw ftrace event format: " + path;
    }
    return valid;
}

static bool LoadPageFormat(const std::string &path, PageFormat &format, std::string *error)
{
    std::string text;
    if (!ReadText(path, text)) {
        if (error != nullptr) {
            *error = "Cannot read trace ring-buffer page format: " + path;
        }
        return false;
    }
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        std::string name = ParseFieldName(line);
        if (name == "timestamp") {
            ParseTraceField(line, format.timestamp);
        } else if (name == "commit") {
            ParseTraceField(line, format.commit);
        } else if (name == "data") {
            ParseTraceField(line, format.data);
        }
    }
    if (format.timestamp.valid && format.commit.valid && format.data.valid) {
        return true;
    }
    if (error != nullptr) {
        *error = "Incomplete trace ring-buffer page format: " + path;
    }
    return false;
}

static bool ReadNumber(const uint8_t *data, size_t length, const TraceField &field, uint64_t &value)
{
    if (!field.valid || field.offset > length || field.size > length - field.offset || field.size > 8) {
        return false;
    }
    value = 0;
    std::memcpy(&value, data + field.offset, field.size);
    return true;
}

static uint32_t ReadU32(const uint8_t *data)
{
    uint32_t value = 0;
    std::memcpy(&value, data, sizeof(value));
    return value;
}

static bool IsCpuDirectory(const char *name, int &cpu)
{
    if (name == nullptr || std::strncmp(name, "cpu", 3) != 0 || name[3] == '\0') {
        return false;
    }
    for (const char *cursor = name + 3; *cursor != '\0'; ++cursor) {
        if (*cursor < '0' || *cursor > '9') {
            return false;
        }
    }
    cpu = std::atoi(name + 3);
    return cpu >= 0;
}


static std::string ReadThreadComm(int tid)
{
    std::string comm;
    if (ReadText("/proc/" + std::to_string(tid) + "/comm", comm)) {
        comm = Trim(comm);
    }
    return comm.empty() ? "tid-" + std::to_string(tid) : comm;
}

static size_t ReadSubbufferSize(const std::string &traceFsPath)
{
    std::string text;
    uint64_t sizeKb = 0;
    if (ReadText(traceFsPath + "/buffer_subbuf_size_kb", text) && ParseUnsigned(text, sizeKb) &&
        sizeKb > 0 && sizeKb <= K_MAX_RAW_SUBBUFFER_SIZE / 1024U) {
        return static_cast<size_t>(sizeKb * 1024U);
    }
    long pageSize = sysconf(_SC_PAGESIZE);
    return pageSize > 0 ? static_cast<size_t>(pageSize) : 4096U;
}
} // namespace

struct RawTraceStream {
    PageFormat pageFormat;
    EventFormat entryFormat;
    EventFormat returnFormat;
    std::unordered_map<uint64_t, uint64_t> canonicalAddressByTraceAddress;
    std::unordered_map<int, std::string> commByTid;
    std::vector<CpuPipe> pipes;
    std::vector<RawFunctionGraphEvent> events;
    std::thread worker;
    std::atomic<bool> stop {false};
    int wakeRead = -1;
    int wakeWrite = -1;
    size_t subbufferSize = 4096;
    std::vector<uint8_t> readBuffer;
    uint64_t rawBytes = 0;
    uint64_t rawPages = 0;
    uint64_t pageDataBytes = 0;
    uint64_t ringRecords = 0;
    uint64_t dataRecords = 0;
    uint64_t entryRecords = 0;
    uint64_t returnRecords = 0;
    uint64_t otherEventRecords = 0;
    uint64_t invalidPayloadRecords = 0;
    uint64_t adjustedAddressRecords = 0;
    uint64_t addressMissRecords = 0;
    uint64_t truncatedRecords = 0;
    uint64_t missedPages = 0;
    uint64_t missedEvents = 0;
    std::unordered_map<uint64_t, uint64_t> observedEventTypes;
    std::vector<uint8_t> firstPagePrefix;
    std::string error;

    ~RawTraceStream()
    {
        stop.store(true);
        if (wakeWrite >= 0) {
            char value = 1;
            (void)write(wakeWrite, &value, sizeof(value));
        }
        if (worker.joinable()) {
            worker.join();
        }
        CloseDescriptors();
    }

    void CloseDescriptors()
    {
        for (CpuPipe &pipe : pipes) {
            if (pipe.fd >= 0) {
                close(pipe.fd);
                pipe.fd = -1;
            }
        }
        if (wakeRead >= 0) {
            close(wakeRead);
            wakeRead = -1;
        }
        if (wakeWrite >= 0) {
            close(wakeWrite);
            wakeWrite = -1;
        }
    }

    void SetError(const std::string &message)
    {
        if (error.empty()) {
            error = message;
        }
    }

    void DecodePayload(const uint8_t *payload, size_t payloadLength, uint64_t timestamp, int cpu)
    {
        ++dataRecords;
        uint64_t type = 0;
        if (!ReadNumber(payload, payloadLength, entryFormat.commonType, type)) {
            ++invalidPayloadRecords;
            return;
        }
        auto observed = observedEventTypes.find(type);
        if (observed != observedEventTypes.end()) {
            ++observed->second;
        } else if (observedEventTypes.size() < 16) {
            observedEventTypes.emplace(type, 1);
        }

        const EventFormat *format = nullptr;
        unsigned isRet = 0;
        if (type == entryFormat.id) {
            format = &entryFormat;
            ++entryRecords;
        } else if (type == returnFormat.id) {
            format = &returnFormat;
            isRet = 1;
            ++returnRecords;
        } else {
            ++otherEventRecords;
            return;
        }

        uint64_t pidValue = 0;
        uint64_t functionAddress = 0;
        if (!ReadNumber(payload, payloadLength, format->commonPid, pidValue) ||
            !ReadNumber(payload, payloadLength, format->function, functionAddress) ||
            pidValue == 0 || pidValue > INT_MAX || timestamp > static_cast<uint64_t>(INT64_MAX)) {
            ++invalidPayloadRecords;
            return;
        }
        uint64_t canonicalAddress = functionAddress;
        auto functionIt = canonicalAddressByTraceAddress.find(canonicalAddress);
        if (functionIt == canonicalAddressByTraceAddress.end()) {
            // Convert the architecture-specific fentry call-site IP to the
            // symbol start. Exact matching remains the primary path.
#if defined(__aarch64__)
            // ARM64 records func+4 normally, or func+8 with BTI/CALL_OPS.
            static constexpr uint64_t K_FTRACE_IP_ADJUSTMENTS[] = {4, 8};
#elif defined(__x86_64__) || defined(__i386__)
            // x86 normally records the symbol start; IBT may put fentry after ENDBR64.
            static constexpr uint64_t K_FTRACE_IP_ADJUSTMENTS[] = {4};
#endif
#if defined(__aarch64__) || defined(__x86_64__) || defined(__i386__)
            for (uint64_t adjustment : K_FTRACE_IP_ADJUSTMENTS) {
                if (functionAddress < adjustment) {
                    continue;
                }
                uint64_t candidate = functionAddress - adjustment;
                functionIt = canonicalAddressByTraceAddress.find(candidate);
                if (functionIt != canonicalAddressByTraceAddress.end()) {
                    canonicalAddress = functionIt->second;
                    ++adjustedAddressRecords;
                    break;
                }
            }
#endif
        }
        if (functionIt == canonicalAddressByTraceAddress.end()) {
            ++addressMissRecords;
            return;
        }
        canonicalAddress = functionIt->second;

        int tid = static_cast<int>(pidValue);
        if (commByTid.find(tid) == commByTid.end()) {
            commByTid.emplace(tid, ReadThreadComm(tid));
        }

        RawFunctionGraphEvent event;
        event.timestamp = static_cast<int64_t>(timestamp);
        event.address = canonicalAddress;
        event.tid = tid;
        event.cpu = cpu;
        event.isRet = isRet;
        events.push_back(std::move(event));
    }

    void DecodePage(const uint8_t *page, size_t length, int cpu)
    {
        ++rawPages;
        if (firstPagePrefix.empty()) {
            size_t prefixLength = std::min<size_t>(length, 128);
            firstPagePrefix.assign(page, page + prefixLength);
        }
        uint64_t timestamp = 0;
        uint64_t commit = 0;
        if (!ReadNumber(page, length, pageFormat.timestamp, timestamp) ||
            !ReadNumber(page, length, pageFormat.commit, commit) || pageFormat.data.offset >= length) {
            SetError("Invalid trace_pipe_raw ring-buffer page header");
            return;
        }

        bool pageMissedEvents = (commit & K_PAGE_MISSED_EVENTS) != 0;
        bool pageMissedStored = (commit & K_PAGE_MISSED_STORED) != 0;
        size_t dataLength = static_cast<size_t>(commit & K_PAGE_COMMIT_MASK);
        size_t available = length - pageFormat.data.offset;
        if (pageMissedEvents) {
            ++missedPages;
            size_t missedSize = pageFormat.commit.size;
            if (pageMissedStored && missedSize <= sizeof(uint64_t) &&
                dataLength <= available - std::min(available, missedSize)) {
                uint64_t missed = 0;
                std::memcpy(&missed, page + pageFormat.data.offset + dataLength, missedSize);
                missedEvents += missed;
            }
        }
        if (dataLength > available) {
            dataLength = available;
        }
        pageDataBytes += dataLength;
        const uint8_t *data = page + pageFormat.data.offset;
        size_t offset = 0;
        while (offset + sizeof(uint32_t) <= dataLength) {
            ++ringRecords;
            uint32_t header = ReadU32(data + offset);
            uint32_t typeLen = header & 0x1fU;
            uint32_t timeDelta = header >> 5;

            if (typeLen <= K_RINGBUF_DATA_TYPE_MAX) {
                timestamp += timeDelta;
                size_t payloadOffset = offset + sizeof(uint32_t);
                size_t payloadLength = static_cast<size_t>(typeLen) * sizeof(uint32_t);
                size_t totalLength = sizeof(uint32_t) + payloadLength;
                if (typeLen == 0) {
                    if (offset + 2 * sizeof(uint32_t) > dataLength) {
                        break;
                    }
                    uint32_t storedLength = ReadU32(data + offset + sizeof(uint32_t));
                    if (storedLength < sizeof(uint32_t)) {
                        break;
                    }
                    payloadOffset += sizeof(uint32_t);
                    payloadLength = storedLength - sizeof(uint32_t);
                    totalLength = sizeof(uint32_t) + storedLength;
                }
                if (totalLength > dataLength - offset || payloadOffset > dataLength ||
                    payloadLength > dataLength - payloadOffset) {
                    ++truncatedRecords;
                    break;
                }
                DecodePayload(data + payloadOffset, payloadLength, timestamp, cpu);
                offset += totalLength;
                continue;
            }

            if (typeLen == K_RINGBUF_TYPE_PADDING) {
                if (timeDelta == 0 || offset + 2 * sizeof(uint32_t) > dataLength) {
                    break;
                }
                uint32_t padding = ReadU32(data + offset + sizeof(uint32_t));
                size_t totalLength = sizeof(uint32_t) + static_cast<size_t>(padding);
                if (totalLength > dataLength - offset) {
                    break;
                }
                timestamp += timeDelta;
                offset += totalLength;
                continue;
            }

            if (offset + 2 * sizeof(uint32_t) > dataLength) {
                break;
            }
            uint64_t high = ReadU32(data + offset + sizeof(uint32_t));
            uint64_t extended = (high << K_TIME_SHIFT) | timeDelta;
            if (typeLen == K_RINGBUF_TYPE_TIME_EXTEND) {
                timestamp += extended;
            } else if (typeLen == K_RINGBUF_TYPE_TIME_STAMP) {
                // Absolute timestamp records store only 59 bits. Restore the high
                // bits from the preceding timestamp and account for wraparound.
                uint64_t highBits = timestamp & ~K_TIMESTAMP_LOW_MASK;
                uint64_t absolute = highBits | (extended & K_TIMESTAMP_LOW_MASK);
                if (highBits != 0 && absolute < timestamp) {
                    absolute += K_TIMESTAMP_RANGE;
                }
                timestamp = absolute;
            } else {
                break;
            }
            offset += 2 * sizeof(uint32_t);
        }
    }

    bool ReadAvailable(CpuPipe &pipe)
    {
        bool consumed = false;
        while (true) {
            ssize_t count = read(pipe.fd, readBuffer.data(), readBuffer.size());
            if (count > 0) {
                consumed = true;
                rawBytes += static_cast<uint64_t>(count);
                DecodePage(readBuffer.data(), static_cast<size_t>(count), pipe.cpu);
                continue;
            }
            if (count == 0 || errno == EAGAIN || errno == EWOULDBLOCK) {
                return consumed;
            }
            if (errno == EINTR) {
                continue;
            }
            SetError("read trace_pipe_raw for cpu" + std::to_string(pipe.cpu) +
                " failed: " + std::strerror(errno));
            return consumed;
        }
    }

    void Run()
    {
        std::vector<struct pollfd> descriptors(pipes.size() + 1);
        descriptors[0].fd = wakeRead;
        descriptors[0].events = POLLIN;
        for (size_t index = 0; index < pipes.size(); ++index) {
            descriptors[index + 1].fd = pipes[index].fd;
            descriptors[index + 1].events = POLLIN;
        }

        while (!stop.load()) {
            int result = poll(descriptors.data(), descriptors.size(), 100);
            if (result < 0 && errno != EINTR) {
                SetError("poll trace_pipe_raw failed: " + std::string(std::strerror(errno)));
                break;
            }
            if (descriptors[0].revents & POLLIN) {
                char wakeBuffer[32];
                while (read(wakeRead, wakeBuffer, sizeof(wakeBuffer)) > 0) {
                }
            }
            for (size_t index = 0; index < pipes.size(); ++index) {
                if (descriptors[index + 1].revents & (POLLIN | POLLERR | POLLHUP)) {
                    ReadAvailable(pipes[index]);
                }
            }
        }

        // tracing_on is disabled before stop is requested. Drain partial final pages.
        bool consumed = false;
        do {
            consumed = false;
            for (CpuPipe &pipe : pipes) {
                consumed = ReadAvailable(pipe) || consumed;
            }
        } while (consumed);
    }
};

bool StartRawTraceStream(KernelTraceManager::Session &session, std::string *error)
{
    if (session.rawStream != nullptr) {
        if (error != nullptr) {
            *error = "trace_pipe_raw stream is already running";
        }
        return false;
    }

    std::shared_ptr<RawTraceStream> stream(new RawTraceStream());
    std::string root = session.traceFsPath;
    if (!LoadPageFormat(root + "/events/header_page", stream->pageFormat, error) ||
        !LoadEventFormat(root + "/events/ftrace/funcgraph_entry/format",
            stream->entryFormat, error) ||
        !LoadEventFormat(root + "/events/ftrace/funcgraph_exit/format",
            stream->returnFormat, error)) {
        return false;
    }

    for (const auto &symbol : session.addresses) {
        if (symbol.second != 0) {
            stream->canonicalAddressByTraceAddress.emplace(symbol.second, symbol.second);
        }
    }
    for (const auto &patchSite : session.patchAddresses) {
        auto canonical = session.addresses.find(patchSite.first);
        if (canonical != session.addresses.end() && canonical->second != 0 && patchSite.second != 0) {
            stream->canonicalAddressByTraceAddress[patchSite.second] = canonical->second;
        }
    }
    if (stream->canonicalAddressByTraceAddress.empty()) {
        if (error != nullptr) {
            *error = "trace_pipe_raw requires readable non-zero kernel addresses from /proc/kallsyms";
        }
        return false;
    }
    for (const auto &thread : session.tgidByTid) {
        stream->commByTid.emplace(thread.first, ReadThreadComm(thread.first));
    }
    stream->subbufferSize = ReadSubbufferSize(root);
    if (stream->subbufferSize == 0 || stream->subbufferSize > K_MAX_RAW_SUBBUFFER_SIZE) {
        if (error != nullptr) {
            *error = "Invalid trace ring-buffer subbuffer size";
        }
        return false;
    }
    stream->readBuffer.resize(stream->subbufferSize);

    DIR *directory = opendir((root + "/per_cpu").c_str());
    if (directory == nullptr) {
        if (error != nullptr) {
            *error = "Cannot open tracefs per_cpu directory: " + std::string(std::strerror(errno));
        }
        return false;
    }
    std::vector<int> onlineCpus;
    if (!ReadOnlineCpus(onlineCpus)) {
        closedir(directory);
        if (error != nullptr) {
            *error = "Cannot determine online CPUs for trace_pipe_raw";
        }
        return false;
    }
    std::vector<std::string> openFailures;
    struct dirent *entry = nullptr;
    while ((entry = readdir(directory)) != nullptr) {
        int cpu = -1;
        if (!IsCpuDirectory(entry->d_name, cpu) ||
            !std::binary_search(onlineCpus.begin(), onlineCpus.end(), cpu)) {
            continue;
        }
        std::string path = root + "/per_cpu/" + entry->d_name + "/trace_pipe_raw";
        int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) {
            openFailures.push_back("cpu" + std::to_string(cpu) + ": " + std::strerror(errno));
            continue;
        }
        CpuPipe cpuPipe;
        cpuPipe.cpu = cpu;
        cpuPipe.fd = fd;
        stream->pipes.push_back(cpuPipe);
    }
    closedir(directory);
    if (!openFailures.empty() || stream->pipes.size() != onlineCpus.size()) {
        if (error != nullptr) {
            std::ostringstream detail;
            for (size_t index = 0; index < openFailures.size(); ++index) {
                if (index != 0) {
                    detail << ", ";
                }
                detail << openFailures[index];
            }
            *error = "Cannot open trace_pipe_raw for every online CPU (opened=" +
                std::to_string(stream->pipes.size()) + ", online=" +
                std::to_string(onlineCpus.size()) + ")";
            if (!openFailures.empty()) {
                *error += ": " + detail.str();
            }
        }
        stream->CloseDescriptors();
        return false;
    }
    std::sort(stream->pipes.begin(), stream->pipes.end(), [](const CpuPipe &left, const CpuPipe &right) {
        return left.cpu < right.cpu;
    });
    if (stream->pipes.empty()) {
        if (error != nullptr) {
            *error = "No per-CPU trace_pipe_raw file could be opened";
        }
        return false;
    }

    int wakePipe[2] = {-1, -1};
    if (pipe(wakePipe) != 0) {
        if (error != nullptr) {
            *error = "Cannot create trace_pipe_raw wake pipe: " + std::string(std::strerror(errno));
        }
        return false;
    }
    int readFlags = fcntl(wakePipe[0], F_GETFL, 0);
    int writeFlags = fcntl(wakePipe[1], F_GETFL, 0);
    if (readFlags >= 0) {
        (void)fcntl(wakePipe[0], F_SETFL, readFlags | O_NONBLOCK);
    }
    if (writeFlags >= 0) {
        (void)fcntl(wakePipe[1], F_SETFL, writeFlags | O_NONBLOCK);
    }
    (void)fcntl(wakePipe[0], F_SETFD, FD_CLOEXEC);
    (void)fcntl(wakePipe[1], F_SETFD, FD_CLOEXEC);
    stream->wakeRead = wakePipe[0];
    stream->wakeWrite = wakePipe[1];
    try {
        stream->worker = std::thread(&RawTraceStream::Run, stream.get());
    } catch (const std::exception &exception) {
        if (error != nullptr) {
            *error = "Cannot start trace_pipe_raw reader: " + std::string(exception.what());
        }
        return false;
    }
    session.rawStream = stream;
    return true;
}

bool StopRawTraceStream(KernelTraceManager::Session &session, std::string *error)
{
    if (session.rawStream == nullptr) {
        return true;
    }
    session.rawStream->stop.store(true);
    if (session.rawStream->wakeWrite >= 0) {
        char value = 1;
        (void)write(session.rawStream->wakeWrite, &value, sizeof(value));
    }
    if (session.rawStream->worker.joinable()) {
        session.rawStream->worker.join();
    }
    // tracefs refuses to replace current_tracer while trace_pipe_raw readers
    // are still open. Keep the decoded events, but release all tracefs fds now.
    session.rawStream->CloseDescriptors();
    if (!session.rawStream->error.empty()) {
        if (error != nullptr) {
            *error = session.rawStream->error;
        }
        return false;
    }
    return true;
}

bool TakeRawTraceEvents(KernelTraceManager::Session &session, std::vector<RawFunctionGraphEvent> &events,
    std::unordered_map<int, std::string> *commByTid, uint64_t *rawBytes, std::string *error)
{
    if (session.rawStream == nullptr) {
        if (error != nullptr) {
            *error = "trace_pipe_raw stream was not started";
        }
        return false;
    }
    if (session.rawStream->worker.joinable()) {
        if (error != nullptr) {
            *error = "trace_pipe_raw stream must be stopped before reading";
        }
        return false;
    }
    if (!session.rawStream->error.empty()) {
        if (error != nullptr) {
            *error = session.rawStream->error;
        }
        return false;
    }

    if (session.rawStream->missedPages != 0) {
        if (error != nullptr) {
            *error = "trace_pipe_raw reported lost ring-buffer events: pages=" +
                std::to_string(session.rawStream->missedPages) + ", events=" +
                std::to_string(session.rawStream->missedEvents);
        }
        return false;
    }
    if (session.rawStream->truncatedRecords != 0 || session.rawStream->invalidPayloadRecords != 0 ||
        session.rawStream->addressMissRecords != 0 || session.rawStream->otherEventRecords != 0) {
        if (error != nullptr) {
            *error = "trace_pipe_raw decode was incomplete: truncated=" +
                std::to_string(session.rawStream->truncatedRecords) + ", invalid=" +
                std::to_string(session.rawStream->invalidPayloadRecords) + ", address_miss=" +
                std::to_string(session.rawStream->addressMissRecords) + ", other_events=" +
                std::to_string(session.rawStream->otherEventRecords);
        }
        return false;
    }
    if (session.rawStream->events.empty() && !session.rawStream->firstPagePrefix.empty()) {
        static constexpr char K_HEX[] = "0123456789abcdef";
        std::string prefixHex;
        prefixHex.reserve(session.rawStream->firstPagePrefix.size() * 2);
        for (uint8_t byte : session.rawStream->firstPagePrefix) {
            prefixHex.push_back(K_HEX[byte >> 4]);
            prefixHex.push_back(K_HEX[byte & 0xf]);
        }
        TraceLog("[trace-kernel] first raw page prefix: " + prefixHex + "\n");
    }
    events.swap(session.rawStream->events);
    if (commByTid != nullptr) {
        *commByTid = std::move(session.rawStream->commByTid);
    }
    if (rawBytes != nullptr) {
        *rawBytes = session.rawStream->rawBytes;
    }
    return true;
}
} // namespace kernel_trace

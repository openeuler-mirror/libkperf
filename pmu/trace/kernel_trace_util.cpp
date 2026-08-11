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
 * Description: Internal tracefs and raw kernel trace helpers
 ******************************************************************************/

#include "kernel_trace_util.h"
#include "common.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

#include "pcerr.h"
#include "trace_log.h"

using namespace pcerr;

namespace kernel_trace {
static constexpr const char *K_GLOBAL_TRACE_LOCK = "/tmp/libkperf_trace.lock";

static constexpr const char *K_CHANGED_TRACE_OPTIONS[] = {
    "funcgraph-irqs",
    "function-fork",
    "overwrite",
    "funcgraph-retaddr",
    "funcgraph-args",
    "funcgraph-retval",
    "stacktrace",
    "userstacktrace",
    "branch",
    "func_stack_trace"
};


static bool IsRegularOrVirtualFile(const std::string &path)
{
    struct stat info {};
    return stat(path.c_str(), &info) == 0;
}

static bool WriteAll(int fd, const std::string &value)
{
    size_t written = 0;
    while (written < value.size()) {
        ssize_t count = write(fd, value.data() + written, value.size() - written);
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (count == 0) {
            errno = EIO;
            return false;
        }
        written += static_cast<size_t>(count);
    }
    return true;
}

static bool WriteTraceFile(const std::string &path, const std::string &value, std::string *error)
{
    int fd = open(path.c_str(), O_WRONLY | O_TRUNC | O_CLOEXEC);
    if (fd < 0) {
        if (error != nullptr) {
            *error = "open " + path + " failed: " + std::strerror(errno);
        }
        return false;
    }
    bool ok = WriteAll(fd, value);
    int savedErrno = errno;
    close(fd);
    if (!ok && error != nullptr) {
        *error = "write " + path + " failed: " + std::strerror(savedErrno);
    }
    return ok;
}

static bool AppendTraceFile(const std::string &path, const std::string &value, std::string *error)
{
    int fd = open(path.c_str(), O_WRONLY | O_APPEND | O_CLOEXEC);
    if (fd < 0) {
        if (error != nullptr) {
            *error = "open " + path + " for append failed: " + std::strerror(errno);
        }
        return false;
    }
    bool ok = WriteAll(fd, value);
    int savedErrno = errno;
    close(fd);
    if (!ok && error != nullptr) {
        *error = "append " + path + " failed: " + std::strerror(savedErrno);
    }
    return ok;
}

static bool ClearTraceFile(const std::string &path, std::string *error)
{
    return WriteTraceFile(path, "\n", error);
}

static bool ClearTraceBuffer(const std::string &path, std::string *error)
{
    int fd = open(path.c_str(), O_WRONLY | O_TRUNC | O_CLOEXEC);
    if (fd < 0) {
        if (error != nullptr) {
            *error = "open " + path + " for ring-buffer clear failed: " + std::strerror(errno);
        }
        return false;
    }
    close(fd);
    return true;
}

static bool ReadTextFile(const std::string &path, std::string &content)
{
    std::ifstream input(path);
    if (!input.is_open()) {
        return false;
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    content = buffer.str();
    return true;
}

static bool ReadOptionalTraceFile(const std::string &path, std::string &content, std::string *error)
{
    if (!IsRegularOrVirtualFile(path)) {
        content.clear();
        return true;
    }
    if (ReadTextFile(path, content)) {
        return true;
    }
    if (error != nullptr) {
        *error = "Cannot read global tracefs state from " + path;
    }
    return false;
}

static bool WriteOptionalTraceFile(const std::string &path, const std::string &value, std::string *error)
{
    return !IsRegularOrVirtualFile(path) || WriteTraceFile(path, value, error);
}

static bool ClearOptionalTraceFile(const std::string &path, std::string *error)
{
    return !IsRegularOrVirtualFile(path) || ClearTraceFile(path, error);
}

bool ReadOnlineCpus(std::vector<int> &cpus)
{
    cpus.clear();
    std::string online;
    if (!ReadTextFile("/sys/devices/system/cpu/online", online)) {
        return false;
    }
    std::istringstream ranges(Trim(online));
    std::string range;
    while (std::getline(ranges, range, ',')) {
        size_t dash = range.find('-');
        std::string firstText = dash == std::string::npos ? range : range.substr(0, dash);
        std::string lastText = dash == std::string::npos ? range : range.substr(dash + 1);
        char *firstEnd = nullptr;
        char *lastEnd = nullptr;
        long first = std::strtol(firstText.c_str(), &firstEnd, 10);
        long last = std::strtol(lastText.c_str(), &lastEnd, 10);
        if (firstEnd == firstText.c_str() || *firstEnd != '\0' ||
            lastEnd == lastText.c_str() || *lastEnd != '\0' || first < 0 || last < first || last > INT_MAX) {
            cpus.clear();
            return false;
        }
        for (long cpu = first; cpu <= last; ++cpu) {
            cpus.push_back(static_cast<int>(cpu));
        }
    }
    std::sort(cpus.begin(), cpus.end());
    cpus.erase(std::unique(cpus.begin(), cpus.end()), cpus.end());
    return !cpus.empty();
}

static bool BuildOnlineCpuMask(std::string &mask)
{
    std::vector<int> cpus;
    if (!ReadOnlineCpus(cpus)) {
        return false;
    }
    std::vector<uint32_t> groups(static_cast<size_t>(cpus.back()) / 32U + 1U, 0);
    for (int cpu : cpus) {
        groups[static_cast<size_t>(cpu) / 32U] |= 1U << (static_cast<unsigned>(cpu) % 32U);
    }
    std::ostringstream output;
    output << std::hex << groups.back();
    for (size_t index = groups.size() - 1; index > 0; --index) {
        output << ',' << std::setw(8) << std::setfill('0') << groups[index - 1];
    }
    mask = output.str();
    return true;
}

std::string FindTraceFsRoot()
{
    static constexpr const char *K_TRACEFS_ROOTS[] = {"/sys/kernel/tracing", "/sys/kernel/debug/tracing"};
    for (const char *root : K_TRACEFS_ROOTS) {
        if (IsRegularOrVirtualFile(std::string(root) + "/available_tracers") &&
            IsRegularOrVirtualFile(std::string(root) + "/available_filter_functions")) {
            return root;
        }
    }
    return "";
}

bool HasRawKernelTraceSupport(const std::string &traceFsRoot)
{
    std::string tracers;
    if (!ReadTextFile(traceFsRoot + "/available_tracers", tracers)) {
        return false;
    }
    std::istringstream fields(tracers);
    std::string tracer;
    while (fields >> tracer) {
        if (tracer == "function_graph") {
            return true;
        }
    }
    return false;
}

static std::string SelectedTraceClock(const std::string &value)
{
    size_t begin = value.find('[');
    size_t end = begin == std::string::npos ? std::string::npos : value.find(']', begin + 1);
    if (begin == std::string::npos || end == std::string::npos || end == begin + 1) {
        return "";
    }
    return value.substr(begin + 1, end - begin - 1);
}

static bool RestoreSavedFilter(const std::string &path, const std::string &value, bool pidFilter, std::string *error)
{
    if (!ClearTraceFile(path, error)) {
        return false;
    }
    std::istringstream lines(value);
    std::string line;
    while (std::getline(lines, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == '#' || (pidFilter && line == "no pid")) {
            continue;
        }
        if (!AppendTraceFile(path, line + "\n", error)) {
            return false;
        }
    }
    return true;
}

static bool ReadPerCpuTraceEntries(const std::string &statsPath, unsigned long long &entries)
{
    std::ifstream input(statsPath);
    if (!input.is_open()) {
        return false;
    }
    std::string line;
    while (std::getline(input, line)) {
        line = Trim(line);
        static constexpr const char *K_ENTRIES_PREFIX = "entries:";
        if (line.compare(0, std::strlen(K_ENTRIES_PREFIX), K_ENTRIES_PREFIX) != 0) {
            continue;
        }
        const char *begin = line.c_str() + std::strlen(K_ENTRIES_PREFIX);
        while (*begin != '\0' && std::isspace(static_cast<unsigned char>(*begin))) {
            ++begin;
        }
        errno = 0;
        char *end = nullptr;
        entries = std::strtoull(begin, &end, 10);
        return errno == 0 && end != begin;
    }
    return false;
}

static bool IsCpuDirectory(const char *name)
{
    if (name == nullptr || std::strncmp(name, "cpu", 3) != 0 || name[3] == '\0') {
        return false;
    }
    for (const char *digit = name + 3; *digit != '\0'; ++digit) {
        if (!std::isdigit(static_cast<unsigned char>(*digit))) {
            return false;
        }
    }
    return true;
}

static bool TraceBufferIsEmpty(const std::string &tracePath, std::string *detail = nullptr)
{
    // Prefer per-CPU statistics. Unlike the human-readable trace header, the
    // "entries:" field has no dependency on the selected tracer or output options.
    size_t slash = tracePath.rfind('/');
    std::string traceRoot = slash == std::string::npos ? std::string() : tracePath.substr(0, slash);
    std::string perCpuPath = traceRoot + "/per_cpu";
    DIR *perCpu = opendir(perCpuPath.c_str());
    if (perCpu != nullptr) {
        bool foundCpu = false;
        bool statsComplete = true;
        struct dirent *entry = nullptr;
        while ((entry = readdir(perCpu)) != nullptr) {
            if (!IsCpuDirectory(entry->d_name)) {
                continue;
            }
            foundCpu = true;
            unsigned long long entries = 0;
            std::string cpuName = entry->d_name;
            std::string statsPath = perCpuPath + "/" + cpuName + "/stats";
            if (!ReadPerCpuTraceEntries(statsPath, entries)) {
                statsComplete = false;
                if (detail != nullptr) {
                    *detail = "cannot read entries from " + statsPath;
                }
                break;
            }
            if (entries != 0) {
                closedir(perCpu);
                if (detail != nullptr) {
                    *detail = cpuName + " entries=" + std::to_string(entries);
                }
                return false;
            }
        }
        closedir(perCpu);
        if (foundCpu && statsComplete) {
            if (detail != nullptr) {
                *detail = "all per-CPU entries are zero";
            }
            return true;
        }
    }

    // Compatibility fallback for kernels without per_cpu/*/stats.
    std::ifstream input(tracePath);
    if (!input.is_open()) {
        if (detail != nullptr && detail->empty()) {
            *detail = "cannot open " + tracePath;
        }
        return false;
    }
    std::string line;
    while (std::getline(input, line)) {
        size_t marker = line.find("entries-in-buffer/entries-written:");
        if (marker == std::string::npos) {
            continue;
        }
        size_t colon = line.find(':', marker);
        if (colon == std::string::npos) {
            if (detail != nullptr) {
                *detail = "malformed entries-in-buffer header in " + tracePath;
            }
            return false;
        }
        const char *begin = line.c_str() + colon + 1;
        while (*begin != '\0' && std::isspace(static_cast<unsigned char>(*begin))) {
            ++begin;
        }
        char *end = nullptr;
        unsigned long long entries = std::strtoull(begin, &end, 10);
        if (detail != nullptr) {
            *detail = end == begin ? "cannot parse entries-in-buffer header in " + tracePath :
                "global entries=" + std::to_string(entries);
        }
        return end != begin && entries == 0;
    }
    if (detail != nullptr && detail->empty()) {
        *detail = "no per-CPU stats or entries-in-buffer header available";
    }
    return false;
}

static void ReleaseGlobalTraceLock(KernelTraceManager::Session &session)
{
    if (session.traceLockFd < 0) {
        return;
    }
    flock(session.traceLockFd, LOCK_UN);
    close(session.traceLockFd);
    session.traceLockFd = -1;
}

bool AcquireGlobalTraceFs(KernelTraceManager::Session &session, std::string *globalNotraceFilter, std::string *error)
{
    int flags = O_RDWR | O_CREAT | O_CLOEXEC;
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    int lockFd = open(K_GLOBAL_TRACE_LOCK, flags, 0600);
    if (lockFd < 0) {
        if (error != nullptr) {
            *error = "open global kernel trace lock failed: " + std::string(std::strerror(errno));
        }
        return false;
    }
    struct stat lockInfo {};
    if (fstat(lockFd, &lockInfo) != 0 || !S_ISREG(lockInfo.st_mode) || lockInfo.st_uid != geteuid()) {
        if (error != nullptr) {
            *error = "global kernel trace lock is not a regular file owned by the current user";
        }
        close(lockFd);
        return false;
    }
    if (flock(lockFd, LOCK_EX | LOCK_NB) != 0) {
        if (error != nullptr) {
            *error = "kernel trace is already used by another libkperf process";
        }
        close(lockFd);
        return false;
    }

    auto fail = [&session, lockFd, error](const std::string &message) {
        if (error != nullptr) {
            *error = message;
        }
        flock(lockFd, LOCK_UN);
        close(lockFd);
        session.traceLockFd = -1;
        return false;
    };

    const std::string &root = session.traceFsPath;
    std::string currentTracer;
    std::string tracingOn;
    std::string functionFilter;
    std::string pidFilter;
    std::string traceClock;
    if (!ReadTextFile(root + "/current_tracer", currentTracer) || !ReadTextFile(root + "/tracing_on", tracingOn) ||
        !ReadTextFile(root + "/set_ftrace_filter", functionFilter) || !ReadTextFile(root + "/trace_clock", traceClock) ||
        !ReadTextFile(root + "/set_ftrace_pid", pidFilter) ||
        !ReadTextFile(root + "/buffer_size_kb", session.saved.bufferSizeKb)) {
        return fail("Cannot read global tracefs state for raw kernel tracing");
    }

    currentTracer = Trim(currentTracer);
    tracingOn = Trim(tracingOn);
    session.saved.functionFilter = functionFilter;
    session.saved.pidFilter = pidFilter;
    const std::string notracePath = root + "/set_ftrace_notrace";
    if (IsRegularOrVirtualFile(notracePath) && !ReadTextFile(notracePath, *globalNotraceFilter)) {
        return fail("Cannot read global set_ftrace_notrace for raw kernel tracing");
    }
    std::string optionalError;
    if (!ReadOptionalTraceFile(root + "/set_graph_function", session.saved.graphFunctionFilter, &optionalError) ||
        !ReadOptionalTraceFile(root + "/set_graph_notrace", session.saved.graphNotraceFilter, &optionalError) ||
        !ReadOptionalTraceFile(root + "/set_ftrace_notrace_pid", session.saved.notracePidFilter, &optionalError) ||
        !ReadOptionalTraceFile(root + "/max_graph_depth", session.saved.maxGraphDepth, &optionalError) ||
        !ReadOptionalTraceFile(root + "/tracing_thresh", session.saved.tracingThreshold, &optionalError) ||
        !ReadOptionalTraceFile(root + "/tracing_cpumask", session.saved.tracingCpuMask, &optionalError)) {
        return fail(optionalError);
    }
    std::string enabledEvents;
    if (!ReadOptionalTraceFile(root + "/events/enable", enabledEvents, &optionalError)) {
        return fail(optionalError);
    }
    if (!enabledEvents.empty() && Trim(enabledEvents) != "0") {
        return fail("Global tracefs has enabled trace events; disable them before raw kernel tracing");
    }
    if (tracingOn != "0") {
        return fail("Global tracefs is actively tracing: current_tracer=" + currentTracer +
            ", tracing_on=" + tracingOn + "; stop the existing tracer before starting kernel UTrace");
    }
    if (currentTracer != "nop" && currentTracer != "function_graph") {
        return fail("Global tracefs has an incompatible paused tracer: current_tracer=" +
            currentTracer + "; switch it to nop before starting kernel UTrace");
    }
    session.saved.tracer = currentTracer;
    std::string traceBufferDetail;
    if (!TraceBufferIsEmpty(root + "/trace", &traceBufferDetail)) {
        return fail("Global tracefs contains unread trace data; preserve or clear "
            + root + "/trace before starting kernel UTrace (" + traceBufferDetail + ")");
    }

    session.saved.clock = SelectedTraceClock(traceClock);
    session.saved.bufferSizeKb = Trim(session.saved.bufferSizeKb);
    if (session.saved.clock.empty() || session.saved.bufferSizeKb.empty()) {
        return fail("Cannot determine global tracefs clock or buffer size");
    }
    for (const char *option : K_CHANGED_TRACE_OPTIONS) {
        std::string path = root + "/options/" + option;
        if (!IsRegularOrVirtualFile(path)) {
            continue;
        }
        std::string value;
        if (!ReadTextFile(path, value)) {
            return fail("Cannot save global trace option " + std::string(option));
        }
        session.saved.options[option] = Trim(value);
    }
    session.traceLockFd = lockFd;
    session.traceFsPath = root;
    return true;
}

bool LoadAvailableKernelFunctions(const std::string &path, std::unordered_set<std::string> &functions)
{
    std::ifstream input(path);
    if (!input.is_open()) {
        return false;
    }
    std::string line;
    while (std::getline(input, line)) {
        std::istringstream fields(line);
        std::string function;
        if (fields >> function) {
            functions.insert(std::move(function));
        }
    }
    return !functions.empty();
}

static bool MatchesKernelRule(const std::string &pattern, const std::string &function)
{
    if (fnmatch(pattern.c_str(), function.c_str(), 0) == 0) {
        return true;
    }
    static constexpr const char *K_MODULES[] = {"[kernel]", "kernel", "vmlinux"};
    for (const char *module : K_MODULES) {
        std::string qualified = std::string(module) + "::" + function;
        if (fnmatch(pattern.c_str(), qualified.c_str(), 0) == 0) {
            return true;
        }
    }
    return false;
}

static bool MatchesAnyKernelRule(const std::vector<std::string> &patterns, const std::string &function)
{
    return std::any_of(patterns.begin(), patterns.end(), [&function](const std::string &pattern) {
        return MatchesKernelRule(pattern, function);
    });
}

static std::string SummarizeFunctions(const std::vector<std::string> &functions)
{
    static constexpr size_t K_LIMIT = 16;
    std::string summary;
    size_t count = std::min(functions.size(), K_LIMIT);
    for (size_t i = 0; i < count; ++i) {
        if (!summary.empty()) {
            summary += ", ";
        }
        summary += functions[i];
    }
    if (functions.size() > count) {
        summary += ", ... (+" + std::to_string(functions.size() - count) + ")";
    }
    return summary;
}

static std::vector<std::string> FindGlobalNotraceExcludedFunctions( const std::string &notraceFilter,
    const std::vector<std::string> &functions)
{
    std::vector<std::string> rules;
    std::istringstream input(notraceFilter);
    std::string line;
    while (std::getline(input, line)) {
        line = Trim(line);
        if (line.empty() || line.front() == '#') {
            continue;
        }
        size_t whitespace = line.find_first_of(" \t");
        if (whitespace != std::string::npos) {
            line.erase(whitespace);
        }
        if (!line.empty()) {
            rules.push_back(std::move(line));
        }
    }

    std::vector<std::string> excluded;
    for (const std::string &function : functions) {
        if (MatchesAnyKernelRule(rules, function)) {
            excluded.push_back(function);
        }
    }
    return excluded;
}

bool ApplyGlobalNotraceFilter(std::vector<std::string> &functions,
    const std::string &globalNotraceFilter, std::string *error)
{
    std::vector<std::string> excluded = FindGlobalNotraceExcludedFunctions(globalNotraceFilter, functions);
    if (excluded.empty()) {
        return true;
    }

    std::unordered_set<std::string> excludedSet(excluded.begin(), excluded.end());
    std::vector<std::string> effective;
    effective.reserve(functions.size() - excluded.size());
    for (const std::string &function : functions) {
        if (excludedSet.find(function) == excludedSet.end()) {
            effective.push_back(function);
        }
    }
    functions.swap(effective);

    std::string message = "Global set_ftrace_notrace excludes selected functions: count=" +
        std::to_string(excluded.size()) + ", functions=[" + SummarizeFunctions(excluded) + "]";
    SetWarn(LIBPERF_WARN_UTRACE_KERNEL_FAILED, message);
    TraceLog("[trace-kernel] warning: " + message + "\n");
    if (!functions.empty()) {
        return true;
    }
    if (error != nullptr) {
        *error = "No kernel function remains after applying global set_ftrace_notrace";
    }
    return false;
}

bool SelectTraceableFunctions(const std::vector<std::string> &requested, const std::vector<std::string> &includePatterns,
    const std::vector<std::string> &excludePatterns, const std::unordered_set<std::string> &available,
    const std::string &availablePath, std::vector<std::string> &selected, FunctionSelectionStats *stats)
{
    if (stats != nullptr) {
        *stats = {};
    }
    std::unordered_set<std::string> seen;
    std::vector<std::string> candidates;
    for (const std::string &function : requested) {
        if (!function.empty() && seen.insert(function).second) {
            candidates.push_back(function);
        }
    }
    for (const std::string &function : available) {
        if (MatchesAnyKernelRule(includePatterns, function)) {
            if (stats != nullptr) {
                ++stats->includeMatched;
            }
            if (seen.insert(function).second) {
                candidates.push_back(function);
            }
        }
    }

    std::vector<std::string> excluded;
    std::vector<std::string> unavailable;
    for (const std::string &function : candidates) {
        if (MatchesAnyKernelRule(excludePatterns, function)) {
            excluded.push_back(function);
            if (stats != nullptr) {
                ++stats->excludeMatched;
            }
        } else if (available.find(function) == available.end()) {
            unavailable.push_back(function);
        } else {
            selected.push_back(function);
        }
    }
    if (!excluded.empty()) {
        TraceLog("[trace-kernel] skipped by exclude rules: count=" + std::to_string(excluded.size()) +
            ", functions=[" + SummarizeFunctions(excluded) + "]\n");
    }
    if (!unavailable.empty()) {
        std::string message = "Skipped functions not listed in " + availablePath +
            ": count=" + std::to_string(unavailable.size()) + ", functions=[" + SummarizeFunctions(unavailable) + "]";
        SetWarn(LIBPERF_WARN_UTRACE_KERNEL_FAILED, message);
        TraceLog("[trace-kernel] warning: " + message + "\n");
    }
    if (!selected.empty()) {
        return true;
    }
    std::string message = "No requested or included kernel function is traceable in " + availablePath;
    New(LIBPERF_ERR_KERNEL_TRACE_FAILED, message);
    TraceLog("[trace-kernel] error: " + message + "\n");
    return false;
}

std::unordered_map<std::string, uint64_t> ResolveKernelSymbols(const std::vector<std::string> &functions)
{
    std::unordered_set<std::string> unresolved(functions.begin(), functions.end());
    std::unordered_map<std::string, uint64_t> addresses;
    std::ifstream kallsyms("/proc/kallsyms");
    std::string line;
    while (!unresolved.empty() && std::getline(kallsyms, line)) {
        std::istringstream fields(line);
        std::string addressText;
        std::string type;
        std::string name;
        if (!(fields >> addressText >> type >> name) || unresolved.find(name) == unresolved.end()) {
            continue;
        }
        char *end = nullptr;
        uint64_t address = std::strtoull(addressText.c_str(), &end, 16);
        if (end != addressText.c_str() && *end == '\0' && address != 0) {
            addresses.emplace(name, address);
        }
        unresolved.erase(name);
    }
    return addresses;
}

std::unordered_map<std::string, uint64_t> ResolveKernelPatchSites(const std::string &traceFsRoot,
    const std::vector<std::string> &functions)
{
    std::unordered_set<std::string> wanted(functions.begin(), functions.end());
    std::unordered_map<std::string, uint64_t> addresses;
    std::ifstream input(traceFsRoot + "/available_filter_functions_addrs");
    std::string line;
    while (!wanted.empty() && std::getline(input, line)) {
        std::istringstream fields(line);
        std::vector<std::string> tokens;
        std::string token;
        while (fields >> token) {
            tokens.push_back(token);
        }
        std::string function;
        for (const std::string &candidate : tokens) {
            if (wanted.find(candidate) != wanted.end()) {
                function = candidate;
                break;
            }
        }
        if (function.empty()) {
            continue;
        }
        for (const std::string &candidate : tokens) {
            const char *begin = candidate.c_str();
            if (candidate.compare(0, 2, "0x") == 0 || candidate.compare(0, 2, "0X") == 0) {
                begin += 2;
            }
            errno = 0;
            char *end = nullptr;
            uint64_t address = std::strtoull(begin, &end, 16);
            if (errno == 0 && end != begin && *end == '\0' && address != 0) {
                addresses.emplace(function, address);
                wanted.erase(function);
                break;
            }
        }
    }
    return addresses;
}

static bool IsUnsignedNumber(const char *value)
{
    if (value == nullptr || *value == '\0') {
        return false;
    }
    for (const char *cursor = value; *cursor != '\0'; ++cursor) {
        if (!std::isdigit(static_cast<unsigned char>(*cursor))) {
            return false;
        }
    }
    return true;
}

static void LoadTargetThreads(KernelTraceManager::Session &session)
{
    session.tgidByTid.clear();
    for (int pid : session.targetPids) {
        std::string taskPath = "/proc/" + std::to_string(pid) + "/task";
        DIR *directory = opendir(taskPath.c_str());
        if (directory == nullptr) {
            continue;
        }
        struct dirent *entry = nullptr;
        while ((entry = readdir(directory)) != nullptr) {
            if (!IsUnsignedNumber(entry->d_name)) {
                continue;
            }
            int tid = std::atoi(entry->d_name);
            if (tid > 0) {
                session.tgidByTid[tid] = pid;
            }
        }
        closedir(directory);
    }
}

bool WritePidFilter(KernelTraceManager::Session &session, std::string *error)
{
    LoadTargetThreads(session);
    if (session.tgidByTid.empty()) {
        if (error != nullptr) {
            *error = "No live target threads are available for raw kernel tracing";
        }
        return false;
    }
    std::vector<int> tids;
    tids.reserve(session.tgidByTid.size());
    for (const auto &entry : session.tgidByTid) {
        tids.push_back(entry.first);
    }
    std::sort(tids.begin(), tids.end());
    std::string path = session.traceFsPath + "/set_ftrace_pid";
    if (!ClearTraceFile(path, error)) {
        return false;
    }
    for (int tid : tids) {
        if (!AppendTraceFile(path, std::to_string(tid) + "\n", error)) {
            return false;
        }
    }
    return true;
}

static bool WriteFunctionFilter(const KernelTraceManager::Session &session,
    const std::vector<std::string> &functions, std::string *error)
{
    std::string path = session.traceFsPath + "/set_ftrace_filter";
    if (!ClearTraceFile(path, error)) {
        return false;
    }
    for (const std::string &function : functions) {
        if (!AppendTraceFile(path, function + "\n", error)) {
            return false;
        }
    }
    return true;
}

static bool SetTraceOption(const KernelTraceManager::Session &session, const std::string &name,
    bool enabled, bool required, std::string *error)
{
    std::string path = session.traceFsPath + "/options/" + name;
    if (!IsRegularOrVirtualFile(path)) {
        if (required && error != nullptr) {
            *error = "Required raw kernel trace option is unavailable: " + path;
        }
        return !required;
    }
    std::string optionError;
    if (WriteTraceFile(path, enabled ? "1\n" : "0\n", &optionError)) {
        return true;
    }
    if (required && error != nullptr) {
        *error = optionError;
    } else {
        TraceLog("[trace-kernel] warning: " + optionError + "\n");
    }
    return !required;
}

bool ConfigureGlobalTraceFs(KernelTraceManager::Session &session,
    const std::vector<std::string> &functions, uint32_t bufferSizeKb, std::string *error)
{
    std::string onlineCpuMask;
    if (!BuildOnlineCpuMask(onlineCpuMask)) {
        if (error != nullptr) {
            *error = "Cannot determine online CPUs for tracing_cpumask";
        }
        return false;
    }
    if (!WriteTraceFile(session.traceFsPath + "/tracing_on", "0\n", error) ||
        !WriteTraceFile(session.traceFsPath + "/current_tracer", "nop\n", error) ||
        !ClearTraceBuffer(session.traceFsPath + "/trace", error) ||
        !WriteTraceFile(session.traceFsPath + "/trace_clock", "mono\n", error) ||
        !WriteTraceFile(session.traceFsPath + "/buffer_size_kb", std::to_string(bufferSizeKb) + "\n", error) ||
        !ClearOptionalTraceFile(session.traceFsPath + "/set_graph_function", error) ||
        !ClearOptionalTraceFile(session.traceFsPath + "/set_graph_notrace", error) ||
        !ClearOptionalTraceFile(session.traceFsPath + "/set_ftrace_notrace_pid", error) ||
        !WriteOptionalTraceFile(session.traceFsPath + "/max_graph_depth", "0\n", error) ||
        !WriteOptionalTraceFile(session.traceFsPath + "/tracing_thresh", "0\n", error) ||
        !WriteOptionalTraceFile(session.traceFsPath + "/tracing_cpumask", onlineCpuMask + "\n", error) ||
        !WriteFunctionFilter(session, functions, error)) {
        return false;
    }

    if (!SetTraceOption(session, "funcgraph-irqs", false, false, error) ||
        !SetTraceOption(session, "function-fork", true, true, error) ||
        !SetTraceOption(session, "overwrite", false, true, error) ||
        !SetTraceOption(session, "funcgraph-retaddr", false, false, error) ||
        !SetTraceOption(session, "funcgraph-args", false, false, error) ||
        !SetTraceOption(session, "funcgraph-retval", false, false, error) ||
        !SetTraceOption(session, "stacktrace", false, false, error) ||
        !SetTraceOption(session, "userstacktrace", false, false, error) ||
        !SetTraceOption(session, "branch", false, false, error) ||
        !SetTraceOption(session, "func_stack_trace", false, false, error)) {
        return false;
    }
    return WriteTraceFile(session.traceFsPath + "/current_tracer", "nop\n", error);
}

void ResetGlobalTraceFs(KernelTraceManager::Session &session)
{
    if (session.traceFsPath.empty()) {
        ReleaseGlobalTraceLock(session);
        return;
    }
    std::string ignored;
    WriteTraceFile(session.traceFsPath + "/tracing_on", "0\n", &ignored);
    WriteTraceFile(session.traceFsPath + "/current_tracer", "nop\n", &ignored);
    bool traceCleared = ClearTraceBuffer(session.traceFsPath + "/trace", &ignored);
    if (!traceCleared || !TraceBufferIsEmpty(session.traceFsPath + "/trace")) {
        traceCleared = ClearTraceBuffer(session.traceFsPath + "/trace", &ignored) &&
            TraceBufferIsEmpty(session.traceFsPath + "/trace");
    }
    if (!traceCleared) {
        TraceLog("[trace-kernel] warning: failed to clear trace buffer before close: " + ignored + "\n");
    }

    for (const auto &option : session.saved.options) {
        if (!WriteTraceFile(session.traceFsPath + "/options/" + option.first, option.second + "\n", &ignored)) {
            TraceLog("[trace-kernel] warning: failed to restore option " + option.first + ": " + ignored + "\n");
        }
    }
    if (!session.saved.bufferSizeKb.empty() &&
        !WriteTraceFile(session.traceFsPath + "/buffer_size_kb", session.saved.bufferSizeKb + "\n", &ignored)) {
        TraceLog("[trace-kernel] warning: failed to restore buffer size: " + ignored + "\n");
    }
    if (!session.saved.clock.empty() &&
        !WriteTraceFile(session.traceFsPath + "/trace_clock", session.saved.clock + "\n", &ignored)) {
        TraceLog("[trace-kernel] warning: failed to restore trace clock: " + ignored + "\n");
    }
    if (!RestoreSavedFilter(session.traceFsPath + "/set_ftrace_filter", session.saved.functionFilter, false, &ignored)) {
        TraceLog("[trace-kernel] warning: failed to restore function filter: " + ignored + "\n");
    }
    if (!RestoreSavedFilter(session.traceFsPath + "/set_ftrace_pid", session.saved.pidFilter, true, &ignored)) {
        TraceLog("[trace-kernel] warning: failed to restore PID filter: " + ignored + "\n");
    }
    if (IsRegularOrVirtualFile(session.traceFsPath + "/set_graph_function") &&
        !RestoreSavedFilter(session.traceFsPath + "/set_graph_function",
            session.saved.graphFunctionFilter, false, &ignored)) {
        TraceLog("[trace-kernel] warning: failed to restore graph function filter: " + ignored + "\n");
    }
    if (IsRegularOrVirtualFile(session.traceFsPath + "/set_graph_notrace") &&
        !RestoreSavedFilter(session.traceFsPath + "/set_graph_notrace",
            session.saved.graphNotraceFilter, false, &ignored)) {
        TraceLog("[trace-kernel] warning: failed to restore graph notrace filter: " + ignored + "\n");
    }
    if (IsRegularOrVirtualFile(session.traceFsPath + "/set_ftrace_notrace_pid") &&
        !RestoreSavedFilter(session.traceFsPath + "/set_ftrace_notrace_pid",
            session.saved.notracePidFilter, true, &ignored)) {
        TraceLog("[trace-kernel] warning: failed to restore notrace PID filter: " + ignored + "\n");
    }
    if (!session.saved.maxGraphDepth.empty() &&
        !WriteOptionalTraceFile(session.traceFsPath + "/max_graph_depth",
            Trim(session.saved.maxGraphDepth) + "\n", &ignored)) {
        TraceLog("[trace-kernel] warning: failed to restore graph depth: " + ignored + "\n");
    }
    if (!session.saved.tracingThreshold.empty() &&
        !WriteOptionalTraceFile(session.traceFsPath + "/tracing_thresh",
            Trim(session.saved.tracingThreshold) + "\n", &ignored)) {
        TraceLog("[trace-kernel] warning: failed to restore tracing threshold: " + ignored + "\n");
    }
    if (!session.saved.tracingCpuMask.empty() &&
        !WriteOptionalTraceFile(session.traceFsPath + "/tracing_cpumask",
            Trim(session.saved.tracingCpuMask) + "\n", &ignored)) {
        TraceLog("[trace-kernel] warning: failed to restore tracing CPU mask: " + ignored + "\n");
    }
    if (!session.saved.tracer.empty() &&
        !WriteTraceFile(session.traceFsPath + "/current_tracer", session.saved.tracer + "\n", &ignored)) {
        TraceLog("[trace-kernel] warning: failed to restore current tracer: " + ignored + "\n");
    }
    bool finalBufferEmpty = TraceBufferIsEmpty(session.traceFsPath + "/trace");
    if (!finalBufferEmpty) {
        finalBufferEmpty = ClearTraceBuffer(session.traceFsPath + "/trace", &ignored) &&
            TraceBufferIsEmpty(session.traceFsPath + "/trace");
    }
    ReleaseGlobalTraceLock(session);
    session.traceFsPath.clear();
}

bool PrepareCollection(KernelTraceManager::Session &session, std::string *error)
{
    return WriteTraceFile(session.traceFsPath + "/tracing_on", "0\n", error) &&
        WriteTraceFile(session.traceFsPath + "/current_tracer", "nop\n", error) &&
        ClearTraceBuffer(session.traceFsPath + "/trace", error) &&
        WritePidFilter(session, error) &&
        WriteTraceFile(session.traceFsPath + "/current_tracer", "function_graph\n", error);
}

bool SetTracing(KernelTraceManager::Session &session, bool enabled, std::string *error)
{
    return WriteTraceFile(session.traceFsPath + "/tracing_on", enabled ? "1\n" : "0\n", error);
}

void SetCurrentTracer(KernelTraceManager::Session &session, const std::string &tracer)
{
    WriteTraceFile(session.traceFsPath + "/current_tracer", tracer + "\n", nullptr);
}

uint64_t ReadLostEvents(const KernelTraceManager::Session &session)
{
    std::string perCpuPath = session.traceFsPath + "/per_cpu";
    DIR *directory = opendir(perCpuPath.c_str());
    if (directory == nullptr) {
        return 0;
    }
    uint64_t total = 0;
    struct dirent *entry = nullptr;
    while ((entry = readdir(directory)) != nullptr) {
        if (std::strncmp(entry->d_name, "cpu", 3) != 0 ||
            !IsUnsignedNumber(entry->d_name + 3)) {
            continue;
        }
        std::ifstream stats(perCpuPath + "/" + entry->d_name + "/stats");
        std::string line;
        while (std::getline(stats, line)) {
            size_t colon = line.find(':');
            if (colon == std::string::npos) {
                continue;
            }
            std::string key = Trim(line.substr(0, colon));
            if (key != "overrun" && key != "commit overrun" &&
                key != "dropped events") {
                continue;
            }
            try {
                total += std::stoull(Trim(line.substr(colon + 1)));
            } catch (...) {
            }
        }
    }
    closedir(directory);
    return total;
}

int ResolveTgid(const KernelTraceManager::Session &session, int tid)
{
    auto known = session.tgidByTid.find(tid);
    if (known != session.tgidByTid.end()) {
        return known->second;
    }
    std::ifstream status("/proc/" + std::to_string(tid) + "/status");
    std::string key;
    while (status >> key) {
        if (key == "Tgid:") {
            int tgid = tid;
            status >> tgid;
            return tgid;
        }
        std::string ignored;
        std::getline(status, ignored);
    }
    return session.targetPids.empty() ? tid : session.targetPids.front();
}

} // namespace kernel_trace

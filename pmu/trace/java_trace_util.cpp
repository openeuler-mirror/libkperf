/******************************************************************************
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * libkperf licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *     http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR
 * PURPOSE.
 * See the Mulan PSL v2 for more details.
 * Author: Wu
 * Create: 2026-06-12
 * Description: Java trace utility functions for symbol parsing, config loading, command building and UTraceData process
 ******************************************************************************/

#include "java_trace_util.h"

#ifdef JAVA_TRACE
#include "java_trace_config.h"
#endif

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cctype>
#include <dlfcn.h>
#include <limits.h>
#include <regex>
#include <sys/wait.h>
#include <unistd.h>
#include <unordered_set>

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

#ifndef LIBKPERF_JAVA_FILTER_CONFIG_NAME
#define LIBKPERF_JAVA_FILTER_CONFIG_NAME "trace_filter.conf"
#endif

#ifndef LIBKPERF_JAVA_DEFAULT_SLOT_COUNT
#define LIBKPERF_JAVA_DEFAULT_SLOT_COUNT 1048576U
#endif

#ifndef LIBKPERF_JAVA_MAX_SLOT_COUNT
#define LIBKPERF_JAVA_MAX_SLOT_COUNT 67108864U
#endif

namespace {
static constexpr const char *kDefaultJavaBin = "java";
static constexpr uint32_t kDefaultSlotCount = LIBKPERF_JAVA_DEFAULT_SLOT_COUNT;
static constexpr uint32_t kMaxSlotCount = LIBKPERF_JAVA_MAX_SLOT_COUNT;

struct JavaFunc {
    bool valid = false;
    std::string className;
    std::string methodName;
};

static std::string Trim(const std::string &s)
{
    size_t first = 0;
    while (first < s.size() && std::isspace(static_cast<unsigned char>(s[first]))) {
        ++first;
    }
    size_t last = s.size();
    while (last > first && std::isspace(static_cast<unsigned char>(s[last - 1]))) {
        --last;
    }
    return s.substr(first, last - first);
}

static std::string StripComment(const std::string &line)
{
    size_t end = line.size();
    size_t hash = line.find('#');
    if (hash != std::string::npos) {
        end = std::min(end, hash);
    }
    size_t slashes = line.find("//");
    if (slashes != std::string::npos) {
        end = std::min(end, slashes);
    }
    return line.substr(0, end);
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
    return FindJavaAsset(LIBKPERF_JAVA_CLI_JAR_NAME);
}

static std::string AgentJarPath()
{
    return FindJavaAsset(LIBKPERF_JAVA_AGENT_JAR_NAME);
}

static std::string NativeLibPath()
{
    return FindJavaAsset(LIBKPERF_JAVA_NATIVE_LIB_NAME);
}

static uint32_t ClampSlotCount(uint64_t value)
{
    if (value < kDefaultSlotCount) {
        return kDefaultSlotCount;
    }
    if (value > kMaxSlotCount) {
        return kMaxSlotCount;
    }
    return static_cast<uint32_t>(value);
}

static uint32_t ParseSlotCountConfig(const std::string &value, uint32_t fallback)
{
    char *end = nullptr;
    unsigned long long parsed = std::strtoull(value.c_str(), &end, 10);
    if (end != value.c_str() && *end == '\0' && parsed > 0) {
        return ClampSlotCount(parsed);
    }
    return fallback;
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

static JavaFunc ParseJavaFunction(const SymbolSource &src)
{
    JavaFunc out;
    const char *moduleName = src.moduleName;
    const char *symbolName = src.symbolName;

    if (symbolName != nullptr && symbolName[0] != '\0') {
        static const std::regex javaFuncRe(R"(L([^;]+);::([^()\s]+))");
        std::cmatch match;
        if (std::regex_search(symbolName, match, javaFuncRe) && match.size() >= 3) {
            out.valid = true;
            out.className = match[1].str();
            out.methodName = match[2].str();
            return out;
        }
    }

    if (moduleName != nullptr && moduleName[0] == 'L' && symbolName != nullptr && symbolName[0] != '\0') {
        std::string cls = StripJavaClassName(moduleName);
        if (!cls.empty()) {
            out.valid = true;
            out.className = cls;
            out.methodName = symbolName;
            return out;
        }
    }

    return out;
}

static void AddSplitSymbol(std::vector<std::string> &modules, std::vector<std::string> &symbols,
                           std::vector<SymbolSource> &out, const std::string &module, const std::string &symbol)
{
    modules.emplace_back(module);
    symbols.emplace_back(symbol);
    SymbolSource src = {0};
    src.moduleName = const_cast<char *>(modules.back().c_str());
    src.symbolName = const_cast<char *>(symbols.back().c_str());
    out.emplace_back(src);
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
} // namespace

std::string StripJavaClassName(const std::string &s)
{
    if (s.size() >= 2 && s.front() == 'L' && s.back() == ';') {
        return s.substr(1, s.size() - 2);
    }
    return s;
}

SplitTraceAttr SplitSymbolsByRegex(const UTraceAttr *attr)
{
    SplitTraceAttr out;
    if (attr == nullptr || attr->symSrc == nullptr || attr->numSym == 0) {
        return out;
    }
    out.javaModules.reserve(attr->numSym);
    out.javaSymbols.reserve(attr->numSym);
    out.javaSymSrc.reserve(attr->numSym);
    out.nativeModules.reserve(attr->numSym);
    out.nativeSymbols.reserve(attr->numSym);
    out.nativeSymSrc.reserve(attr->numSym);

    for (unsigned i = 0; i < attr->numSym; ++i) {
        SymbolSource &src = attr->symSrc[i];
        JavaFunc javaFunc = ParseJavaFunction(src);
        if (javaFunc.valid) {
            if (javaFunc.className.empty() || javaFunc.methodName.empty()) {
                continue;
            }
            AddSplitSymbol(out.javaModules, out.javaSymbols, out.javaSymSrc, javaFunc.className, javaFunc.methodName);
            continue;
        }
        std::string module = src.moduleName == nullptr ? "" : src.moduleName;
        std::string symbol = src.symbolName == nullptr ? "" : src.symbolName;
        if (module.empty() || symbol.empty()) {
            continue;
        }
        AddSplitSymbol(out.nativeModules, out.nativeSymbols, out.nativeSymSrc, module, symbol);
    }
    return out;
}

UTraceAttr MakeSubAttr(const UTraceAttr *src, std::vector<SymbolSource> &symSrc)
{
    UTraceAttr out = *src;
    out.symSrc = symSrc.empty() ? nullptr : symSrc.data();
    out.numSym = static_cast<unsigned>(symSrc.size());
    return out;
}

std::string BuildJavaSymSrc(const UTraceAttr *attr)
{
    if (attr == nullptr || attr->symSrc == nullptr || attr->numSym == 0) {
        return "";
    }

    std::unordered_set<std::string> includes;
    for (unsigned i = 0; i < attr->numSym; ++i) {
        const char *module = attr->symSrc[i].moduleName;
        const char *symbol = attr->symSrc[i].symbolName;
        if (module == nullptr || module[0] == '\0') {
            continue;
        }
        std::string mod = StripJavaClassName(module);
        std::string sym = symbol == nullptr ? "" : symbol;
        if (!sym.empty() && sym != "*") {
            includes.emplace(mod + "/" + sym);
        } else {
            includes.emplace(mod);
        }
    }

    std::string result;
    for (const auto &include : includes) {
        if (!result.empty()) {
            result += ",";
        }
        result += include;
    }
    return result;
}

std::string FilterConfigPath()
{
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

JavaTraceLocalConfig LoadLocalConfig(const std::string &path)
{
    JavaTraceLocalConfig out{LIBKPERF_JAVA_DEFAULT_SLOT_COUNT};
    if (path.empty()) {
        out.slotCount = ClampSlotCount(out.slotCount);
        return out;
    }

    FILE *fp = std::fopen(path.c_str(), "rb");
    if (fp == nullptr) {
        out.slotCount = ClampSlotCount(out.slotCount);
        return out;
    }

    char buf[1024];
    while (std::fgets(buf, sizeof(buf), fp) != nullptr) {
        std::string s = Trim(StripComment(buf));
        if (s.empty() || (s.front() == '[' && s.back() == ']')) {
            continue;
        }
        size_t eq = s.find('=');
        if (eq == std::string::npos || eq == 0) {
            continue;
        }
        std::string key = Trim(s.substr(0, eq));
        std::string value = Trim(s.substr(eq + 1));
        if (key == "slot_count") {
            out.slotCount = ParseSlotCountConfig(value, out.slotCount);
        }
    }
    std::fclose(fp);
    out.slotCount = ClampSlotCount(out.slotCount);
    return out;
}

std::string TimestampSuffix()
{
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y%m%d%H%M%S", std::localtime(&t));
    unsigned long long usec = static_cast<unsigned long long>(
        std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count() % 1000000);
    char suffix[48];
    std::snprintf(suffix, sizeof(suffix), "%s%06llu", buf, usec);
    return std::string(suffix);
}

std::string BuildEnableCommand(const JavaBackendImpl &impl)
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
    cmd += " --native-lib ";
    cmd += EscapeShell(nativeLib);

    if (!impl.filter_config_path.empty()) {
        cmd += " --config-file ";
        cmd += EscapeShell(impl.filter_config_path);
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

std::string BuildActionCommand(const JavaBackendImpl &impl, const char *action)
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

int RunCommand(const std::string &cmd)
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

char *TraceDupCString(const char *s)
{
    if (s == nullptr) {
        return nullptr;
    }
    size_t len = std::strlen(s);
    char *p = static_cast<char *>(std::malloc(len + 1));
    if (p == nullptr) {
        return nullptr;
    }
    std::memcpy(p, s, len + 1);
    return p;
}

char *TraceDupString(const std::string &s)
{
    char *p = static_cast<char *>(std::malloc(s.size() + 1));
    if (p == nullptr) {
        return nullptr;
    }
    std::memcpy(p, s.c_str(), s.size() + 1);
    return p;
}

UTraceData DeepCopyTraceData(const UTraceData &src)
{
    UTraceData dst = src;
    dst.comm = TraceDupCString(src.comm);
    dst.module = TraceDupCString(src.module);
    dst.func = TraceDupCString(src.func);
    return dst;
}

void FreeTraceDataFields(UTraceData &data)
{
    std::free(const_cast<char *>(data.comm));
    std::free(const_cast<char *>(data.module));
    std::free(const_cast<char *>(data.func));
    data.comm = nullptr;
    data.module = nullptr;
    data.func = nullptr;
}

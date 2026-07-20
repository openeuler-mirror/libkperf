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
 * Description: Java trace utility functions for symbol parsing, config loading, command building and UTraceData
 * process
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
#include <exception>
#include <fstream>
#include <limits.h>
#include <regex>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <unordered_set>

namespace {
static constexpr const char *K_JAVA_REL_DIR = "lib/java";
static constexpr const char *K_JAVA_CONF_REL_DIR = "conf";
static constexpr const char *K_JAVA_CLI_JAR_NAME = "trace_cli.jar";
static constexpr const char *K_JAVA_AGENT_JAR_NAME = "trace_agent.jar";
static constexpr const char *K_JAVA_NATIVE_LIB_NAME = "libtracex_threadinfo.so";
static constexpr const char *K_JAVA_FILTER_CONFIG_NAME = "trace_filter.conf";
static constexpr const char *K_LOG_REL_DIR = "logs";
static constexpr const char *K_TRACE_LOG_NAME = "trace.log";
static constexpr uint32_t K_DEFAULT_SLOT_COUNT = 524288U;
static constexpr uint32_t K_MAX_SLOT_COUNT = 67108864U;
static constexpr const char *K_DEFAULT_JAVA_BIN = "java";
static constexpr mode_t K_PRIVATE_DIR_MODE = 0700;
static constexpr size_t K_EXTRA_CHARS = 2;
static constexpr size_t K_JAVA_FUNCTION_MATCH_SIZE = 3;
static constexpr size_t K_ENABLE_COMMAND_RESERVE = 2048;
static constexpr size_t K_ACTION_COMMAND_RESERVE = 1024;

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
static std::string BaseName(const std::string &path)
{
    size_t pos = path.rfind('/');
    return pos == std::string::npos ? path : path.substr(pos + 1);
}

static bool EnsureDir(const std::string &path)
{
    if (path.empty()) {
        return false;
    }
    struct stat st {};
    if (stat(path.c_str(), &st) == 0) {
        return S_ISDIR(st.st_mode);
    }
    if (mkdir(path.c_str(), K_PRIVATE_DIR_MODE) == 0) {
        return true;
    }
    return errno == EEXIST && stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

static bool CopyFile(const std::string &src, const std::string &dst)
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
    return input.good() && output.good();
}

static bool AppendFile(const std::string &src, const std::string &dst)
{
    std::ifstream input(src, std::ios::binary);
    if (!input.is_open()) {
        return false;
    }
    std::ofstream output(dst, std::ios::binary | std::ios::app);
    if (!output.is_open()) {
        return false;
    }
    output << input.rdbuf();
    return input.good() && output.good();
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
        std::string p = JoinPath(JoinPath(toolRoot, K_JAVA_REL_DIR), fileName);
        if (FileExists(p)) {
            return p;
        }
    }

    std::string libDir = CurrentLibDir();
    if (!libDir.empty()) {
        std::string libRoot = DirName(libDir);
        std::string p = JoinPath(JoinPath(libRoot, K_JAVA_REL_DIR), fileName);
        if (FileExists(p)) {
            return p;
        }
    }
    return "";
}

static std::string BuildLogPathFromInstallRoot(const std::string &installRoot)
{
    if (installRoot.empty()) {
        return "";
    }
    std::string logDir = JoinPath(installRoot, K_LOG_REL_DIR);
    (void)EnsureDir(logDir);
    return JoinPath(logDir, K_TRACE_LOG_NAME);
}

static std::string CliJarPath()
{
    return FindJavaAsset(K_JAVA_CLI_JAR_NAME);
}

static std::string AgentJarPath()
{
    return FindJavaAsset(K_JAVA_AGENT_JAR_NAME);
}

static std::string NativeLibPath()
{
    return FindJavaAsset(K_JAVA_NATIVE_LIB_NAME);
}

static std::string EscapeShell(const std::string &s)
{
    std::string out;
    out.reserve(s.size() + K_EXTRA_CHARS);
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
    const std::regex javaFuncRe(R"(L([^;]+);::([^()\s]+))");

    if (symbolName != nullptr && symbolName[0] != '\0') {
        std::cmatch match;
        if (std::regex_search(symbolName, match, javaFuncRe) && match.size() >= K_JAVA_FUNCTION_MATCH_SIZE) {
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
    if (impl.includeRules.empty()) {
        return "";
    }

    std::string path = impl.shmPath + ".includes";
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        JavaTraceLog(MakeLogMessage("[trace-java] open include file failed: ", path,
                                    ", errno=", errno, "(", std::strerror(errno), ")\n"));
        return "";
    }

    output.write(impl.includeRules.data(), static_cast<std::streamsize>(impl.includeRules.size()));
    if (!output.good()) {
        JavaTraceLog(MakeLogMessage("[trace-java] write include file failed: ", path, "\n"));
        output.close();
        std::remove(path.c_str());
        return "";
    }
    output.close();
    if (!output.good()) {
        JavaTraceLog(MakeLogMessage("[trace-java] close include file failed: ", path, "\n"));
        std::remove(path.c_str());
        return "";
    }
    return path;
}

} // namespace

std::string StripJavaClassName(const std::string &s)
{
    if (s.size() >= K_EXTRA_CHARS && s.front() == 'L' && s.back() == ';') {
        return s.substr(1, s.size() - K_EXTRA_CHARS);
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
        std::string confPath = JoinPath(JoinPath(toolRoot, K_JAVA_CONF_REL_DIR),
                                        K_JAVA_FILTER_CONFIG_NAME);
        if (FileExists(confPath)) {
            return confPath;
        }
    }

    std::string libDir = CurrentLibDir();
    if (!libDir.empty()) {
        std::string libRoot = DirName(libDir);
        std::string confPath = JoinPath(JoinPath(libRoot, K_JAVA_CONF_REL_DIR),
                                        K_JAVA_FILTER_CONFIG_NAME);
        if (FileExists(confPath)) {
            return confPath;
        }
    }

    return FindJavaAsset(K_JAVA_FILTER_CONFIG_NAME);
}

std::string JavaTraceLogPath()
{
    static std::string path;
    if (!path.empty()) {
        return path;
    }

    std::string libDir = CurrentLibDir();
    if (!libDir.empty()) {
        path = BuildLogPathFromInstallRoot(DirName(libDir));
        if (!path.empty()) {
            return path;
        }
    }

    std::string exeDir = SelfExeDir();
    if (!exeDir.empty()) {
        path = BuildLogPathFromInstallRoot(DirName(exeDir));
        if (!path.empty()) {
            return path;
        }
    }

    path = K_TRACE_LOG_NAME;
    return path;
}

void JavaTraceLog(const std::string &message)
{
    if (message.empty()) {
        return;
    }

    std::ofstream output(JavaTraceLogPath(), std::ios::binary | std::ios::app);
    if (!output.is_open()) {
        return;
    }
    output << message;
}

static bool IsDifferentRoot(int pid)
{
    struct stat selfRoot {};
    struct stat targetRoot {};
    std::string target = "/proc/" + std::to_string(pid) + "/root";
    if (stat("/", &selfRoot) != 0 || stat(target.c_str(), &targetRoot) != 0) {
        return false;
    }
    return selfRoot.st_dev != targetRoot.st_dev || selfRoot.st_ino != targetRoot.st_ino;
}

static bool CopyToTargetDir(const std::string &src, const std::string &targetDirHost, std::string *targetHostPath,
                            std::string *targetPath)
{
    if (src.empty()) {
        if (targetHostPath != nullptr) {
            targetHostPath->clear();
        }
        if (targetPath != nullptr) {
            targetPath->clear();
        }
        return true;
    }
    std::string base = BaseName(src);
    std::string dstHost = JoinPath(targetDirHost, base);
    if (!CopyFile(src, dstHost)) {
        JavaTraceLog(MakeLogMessage("[trace-java] copy target file failed: ", src, " -> ", dstHost,
                                    ", errno=", errno, "(", std::strerror(errno), ")\n"));
        return false;
    }
    if (targetHostPath != nullptr) {
        *targetHostPath = dstHost;
    }
    if (targetPath != nullptr) {
        *targetPath = JoinPath("/tmp/" + BaseName(targetDirHost), base);
    }
    return true;
}


static void RemoveTargetAsset(const std::string &dir, const std::string &targetPath)
{
    if (dir.empty() || targetPath.empty()) {
        return;
    }
    const std::string base = BaseName(targetPath);
    if (!base.empty()) {
        std::remove(JoinPath(dir, base).c_str());
    }
}

void JavaTraceCleanupTargetAssets(JavaBackendImpl *impl)
{
    if (impl == nullptr) {
        return;
    }
    if (!impl->target.assetDirHost.empty()) {
        RemoveTargetAsset(impl->target.assetDirHost, impl->target.agentJarPath);
        RemoveTargetAsset(impl->target.assetDirHost, impl->target.nativeLibPath);
        RemoveTargetAsset(impl->target.assetDirHost, impl->target.filterConfigPath);
        std::remove(JoinPath(impl->target.assetDirHost, "trace.log").c_str());
        rmdir(impl->target.assetDirHost.c_str());
    }
    impl->target.assetDirHost.clear();
    impl->target.agentJarPath.clear();
    impl->target.nativeLibPath.clear();
    impl->target.filterConfigPath.clear();
}

static void InitTargetPaths(JavaBackendImpl &impl, const std::string &agentJar, const std::string &nativeLib)
{
    impl.target.logPath = JavaTraceLogPath();
    impl.target.filterConfigPath = impl.filterConfigPath;
    impl.target.agentJarPath = agentJar;
    impl.target.nativeLibPath = nativeLib;
}

static bool CopyTargetAssets(JavaBackendImpl &impl, const std::string &targetDirHost,
                             const std::string &agentJar, const std::string &nativeLib)
{
    std::string ignored;
    if (!CopyToTargetDir(agentJar, targetDirHost, &ignored,
                         &impl.target.agentJarPath)) {
        return false;
    }
    if (!nativeLib.empty() &&
        !CopyToTargetDir(nativeLib, targetDirHost, &ignored, &impl.target.nativeLibPath)) {
        return false;
    }
    if (!impl.filterConfigPath.empty() &&
        !CopyToTargetDir(impl.filterConfigPath, targetDirHost, &ignored, &impl.target.filterConfigPath)) {
        return false;
    }
    return true;
}

static bool PrepareCrossRootTargetFiles(JavaBackendImpl &impl, const std::string &agentJar,
                                        const std::string &nativeLib, const std::string &fileName)
{
    std::string dirName = "libkperf_java_trace_" + std::to_string(impl.pid) + "_" + TimestampSuffix();
    std::string hostRoot = "/proc/" + std::to_string(impl.pid) + "/root/tmp";
    std::string targetDirHost = JoinPath(hostRoot, dirName);
    std::string targetDir = JoinPath("/tmp", dirName);
    if (!EnsureDir(targetDirHost)) {
        JavaTraceLog(MakeLogMessage("[trace-java] create target tmp dir failed: ", targetDirHost,
                                    ", errno=", errno, "(", std::strerror(errno), ")\n"));
        return false;
    }

    impl.target.assetDirHost = targetDirHost;
    if (!CopyTargetAssets(impl, targetDirHost, agentJar, nativeLib)) {
        JavaTraceCleanupTargetAssets(&impl);
        return false;
    }

    impl.shmPath = JoinPath(targetDirHost, fileName);
    impl.target.shmPath = JoinPath(targetDir, fileName);
    impl.target.logPath = JoinPath(targetDir, "trace.log");

    JavaTraceLog(MakeLogMessage("[trace-java] target process uses different root, assets copied to ",
                                targetDirHost, "\n"));
    return true;
}

bool JavaTracePrepareTargetFiles(JavaBackendImpl &impl)
{
    std::string cliJar = CliJarPath();
    std::string agentJar = AgentJarPath();
    std::string nativeLib = NativeLibPath();

    if (cliJar.empty() || agentJar.empty()) {
        JavaTraceLog(MakeLogMessage("[trace-java] trace cli or agent jar not found, cli=", cliJar,
                                    ", agent=", agentJar, "\n"));
        return false;
    }
    InitTargetPaths(impl, agentJar, nativeLib);
    std::string fileName = impl.shmName + ".shm";
    if (!IsDifferentRoot(impl.pid)) {
        impl.shmPath = JoinPath("/tmp", fileName);
        impl.target.shmPath = impl.shmPath;
        return true;
    }
    return PrepareCrossRootTargetFiles(impl, agentJar, nativeLib, fileName);
}

void JavaTraceFlushTargetLog(const JavaBackendImpl &impl)
{
    std::string logPath = JavaTraceLogPath();
    if (impl.target.assetDirHost.empty() || impl.target.logPath == logPath || logPath.empty()) {
        return;
    }
    std::string hostLog = JoinPath(impl.target.assetDirHost, "trace.log");
    if (FileExists(hostLog)) {
        (void)AppendFile(hostLog, logPath);
    }
}
struct LocalConfigParseContext {
    LocalConfigParseContext(const std::string &configPath, JavaTraceLocalConfig *localConfig)
        : path(configPath), config(localConfig), lineNo(0)
    {
    }

    const std::string &path;
    JavaTraceLocalConfig *config;
    std::string section;
    int lineNo;
};

static constexpr size_t K_MIN_SECTION_LINE_LEN = 3;
static constexpr int K_NUMBER_BASE_DECIMAL = 10;
static constexpr unsigned long long K_MAX_CONTEXT_DEPTH = 5;
static constexpr unsigned long long K_MAX_CONTEXT_METHODS = 4096;
static constexpr const char *K_JAVA_INCLUDE_SECTION = "java_include";
static constexpr const char *K_JAVA_EXCLUDE_SECTION = "java_exclude";
static constexpr const char *K_DIGITS = "0123456789";
static constexpr const char *K_SLOT_COUNT_KEY = "slot_count";
static constexpr const char *K_CONTEXT_DEPTH_KEY = "context_depth";
static constexpr const char *K_CONTEXT_MAX_METHODS_KEY = "context_max_methods";

static bool IsUnsignedInteger(const std::string &value)
{
    return !value.empty() && value.find_first_not_of(K_DIGITS) == std::string::npos;
}

static void LogInvalidConfigValue(const LocalConfigParseContext &context, const char *name,
                                  const std::string &value)
{
    JavaTraceLog(MakeLogMessage("[trace-java] invalid ", name, " at ", context.path, ":",
                                context.lineNo, ": ", value, "\n"));
}

static bool ParseSlotCountValue(const LocalConfigParseContext &context, const std::string &value,
                                uint32_t *slotCount)
{
    if (!IsUnsignedInteger(value)) {
        LogInvalidConfigValue(context, K_SLOT_COUNT_KEY, value);
        return false;
    }

    unsigned long long parsed = 0;
    try {
        parsed = std::stoull(value, nullptr, K_NUMBER_BASE_DECIMAL);
    } catch (const std::exception &) {
        LogInvalidConfigValue(context, K_SLOT_COUNT_KEY, value);
        return false;
    }

    if (parsed == 0 || parsed > K_MAX_SLOT_COUNT) {
        LogInvalidConfigValue(context, K_SLOT_COUNT_KEY, value);
        return false;
    }
    *slotCount = static_cast<uint32_t>(parsed);
    return true;
}

static bool ParseBoundedUnsignedValue(const LocalConfigParseContext &context, const char *key,
                                      const std::string &value, unsigned long long maxValue)
{
    if (!IsUnsignedInteger(value)) {
        LogInvalidConfigValue(context, key, value);
        return false;
    }

    unsigned long long parsed = 0;
    try {
        parsed = std::stoull(value, nullptr, K_NUMBER_BASE_DECIMAL);
    } catch (const std::exception &) {
        LogInvalidConfigValue(context, key, value);
        return false;
    }

    if (parsed > maxValue) {
        LogInvalidConfigValue(context, key, value);
        return false;
    }
    return true;
}

static bool ParseLocalConfigKeyValue(LocalConfigParseContext *context, const std::string &key,
                                     const std::string &value)
{
    if (key == K_SLOT_COUNT_KEY) {
        if (!ParseSlotCountValue(*context, value, &context->config->slotCount)) {
            return false;
        }
        return true;
    }
    if (key == K_CONTEXT_DEPTH_KEY) {
        return ParseBoundedUnsignedValue(*context, K_CONTEXT_DEPTH_KEY, value, K_MAX_CONTEXT_DEPTH);
    }
    if (key == K_CONTEXT_MAX_METHODS_KEY) {
        return ParseBoundedUnsignedValue(*context, K_CONTEXT_MAX_METHODS_KEY, value, K_MAX_CONTEXT_METHODS);
    }
    JavaTraceLog(MakeLogMessage("[trace-java] unknown trace filter key at ", context->path, ":",
                                context->lineNo, ": ", key, "\n"));
    return false;
}

static bool ParseLocalConfigSection(LocalConfigParseContext *context, const std::string &line)
{
    if (line.size() < K_MIN_SECTION_LINE_LEN || line.front() != '[' || line.back() != ']') {
        JavaTraceLog(MakeLogMessage("[trace-java] invalid trace filter section at ", context->path,
                                    ":", context->lineNo, ": ", line, "\n"));
        return false;
    }

    context->section = line.substr(1, line.size() - K_EXTRA_CHARS);
    if (context->section == K_JAVA_INCLUDE_SECTION) {
        return true;
    }
    if (context->section == K_JAVA_EXCLUDE_SECTION) {
        return true;
    }

    JavaTraceLog(MakeLogMessage("[trace-java] unknown trace filter section at ", context->path,
                                ":", context->lineNo, ": ", line, "\n"));
    return false;
}

static bool IsValidTraceRule(const std::string &rule)
{
    if (rule.empty() || rule.front() == '+' || rule.front() == '-') {
        return false;
    }
    return rule.find(',') == std::string::npos &&
           rule.find('=') == std::string::npos &&
           rule.find(' ') == std::string::npos &&
           rule.find('\t') == std::string::npos;
}

static bool ParseLocalConfigRule(const LocalConfigParseContext &context, const std::string &line)
{
    if (context.section != K_JAVA_INCLUDE_SECTION && context.section != K_JAVA_EXCLUDE_SECTION) {
        JavaTraceLog(MakeLogMessage("[trace-java] rule must be in [java_include] or [java_exclude] at ",
                                    context.path, ":", context.lineNo, ": ", line, "\n"));
        return false;
    }
    if (!IsValidTraceRule(line)) {
        JavaTraceLog(MakeLogMessage("[trace-java] invalid trace filter rule at ", context.path, ":",
                                    context.lineNo, ": ", line, "\n"));
        return false;
    }
    return true;
}

static bool ParseLocalConfigKeyValueItem(LocalConfigParseContext *context, const std::string &item,
                                         size_t equalPos)
{
    if (!context->section.empty()) {
        JavaTraceLog(MakeLogMessage("[trace-java] key/value is not allowed inside section at ",
                                    context->path, ":", context->lineNo, ": ", item, "\n"));
        return false;
    }

    std::string key = Trim(item.substr(0, equalPos));
    std::string value = Trim(item.substr(equalPos + 1));
    return ParseLocalConfigKeyValue(context, key, value);
}

static bool ParseLocalConfigItem(LocalConfigParseContext *context, const std::string &item)
{
    if (item.front() == '[' || item.back() == ']') {
        return ParseLocalConfigSection(context, item);
    }

    size_t equalPos = item.find('=');
    if (equalPos != std::string::npos) {
        return ParseLocalConfigKeyValueItem(context, item, equalPos);
    }
    return ParseLocalConfigRule(*context, item);
}

static bool ParseLocalConfigStream(std::ifstream &input, LocalConfigParseContext *context)
{
    std::string line;
    while (std::getline(input, line)) {
        ++context->lineNo;
        std::string item = Trim(StripComment(line));
        if (!item.empty() && !ParseLocalConfigItem(context, item)) {
            return false;
        }
    }
    return true;
}

JavaTraceLocalConfig LoadLocalConfig(const std::string &path)
{
    JavaTraceLocalConfig out{K_DEFAULT_SLOT_COUNT, false};
    if (path.empty()) {
        JavaTraceLog("[trace-java] filter config path is empty, skip java trace\n");
        return out;
    }

    std::ifstream input(path);
    if (!input.is_open()) {
        JavaTraceLog(MakeLogMessage("[trace-java] filter config not found: ", path, ", errno=",
                                    errno, "(", std::strerror(errno), ")\n"));
        return out;
    }

    LocalConfigParseContext context(path, &out);
    out.valid = ParseLocalConfigStream(input, &context);
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
    std::string agentJar = impl.target.agentJarPath.empty() ? AgentJarPath() : impl.target.agentJarPath;
    std::string nativeLib = impl.target.nativeLibPath.empty() ? NativeLibPath() : impl.target.nativeLibPath;

    if (cliJar.empty()) {
        JavaTraceLog("[trace-java] error: trace java cli jar not found\n");
        return "";
    }
    if (agentJar.empty()) {
        JavaTraceLog("[trace-java] error: trace java agent jar not found\n");
        return "";
    }

    std::string cmd;
    cmd.reserve(K_ENABLE_COMMAND_RESERVE);
    cmd += EscapeShell(K_DEFAULT_JAVA_BIN);
    cmd += " -jar ";
    cmd += EscapeShell(cliJar);
    cmd += " -p ";
    cmd += std::to_string(impl.pid);
    cmd += " --agent-jar ";
    cmd += EscapeShell(agentJar);
    cmd += " --action start";
    cmd += " --shm-path ";
    cmd += EscapeShell(impl.target.shmPath.empty() ? impl.shmPath : impl.target.shmPath);
    cmd += " --native-lib ";
    cmd += EscapeShell(nativeLib);
    if (!impl.target.logPath.empty()) {
        cmd += " --log-file ";
        cmd += EscapeShell(impl.target.logPath);
    }

    if (!impl.target.filterConfigPath.empty()) {
        cmd += " --config-file ";
        cmd += EscapeShell(impl.target.filterConfigPath);
    }
    if (!impl.includeRules.empty()) {
        std::string includeFile = WriteIncludeFile(impl);
        if (includeFile.empty()) {
            JavaTraceLog("[trace-java] error: include file create failed\n");
            return "";
        }
        std::string targetIncludeFile = includeFile;
        if (!impl.target.assetDirHost.empty()) {
            targetIncludeFile = JoinPath("/tmp/" + BaseName(impl.target.assetDirHost), BaseName(includeFile));
        }
        cmd += " --include-file ";
        cmd += EscapeShell(targetIncludeFile);
    }
    return cmd;
}

std::string BuildActionCommand(const JavaBackendImpl &impl, const char *action)
{
    std::string cliJar = CliJarPath();
    std::string agentJar = impl.target.agentJarPath.empty() ? AgentJarPath() : impl.target.agentJarPath;
    if (cliJar.empty() || agentJar.empty() || impl.pid <= 0 || action == nullptr) {
        return "";
    }

    std::string cmd;
    cmd.reserve(K_ACTION_COMMAND_RESERVE);
    cmd += EscapeShell(K_DEFAULT_JAVA_BIN);
    cmd += " -jar ";
    cmd += EscapeShell(cliJar);
    cmd += " -p ";
    cmd += std::to_string(impl.pid);
    cmd += " --agent-jar ";
    cmd += EscapeShell(agentJar);
    cmd += " --action ";
    cmd += EscapeShell(action);
    if (!impl.target.logPath.empty()) {
        cmd += " --log-file ";
        cmd += EscapeShell(impl.target.logPath);
    }
    return cmd;
}

int RunCommand(const std::string &cmd)
{
    std::string loggedCmd = cmd + " >> " + EscapeShell(JavaTraceLogPath()) + " 2>&1";
    int status = std::system(loggedCmd.c_str());
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

void SortTraceDataByTimestamp(UTraceData *data, size_t count)
{
    if (data == nullptr || count < K_EXTRA_CHARS) {
        return;
    }
    std::stable_sort(data, data + count, [](const UTraceData &left, const UTraceData &right) {
        return left.timestamp < right.timestamp;
    });
}

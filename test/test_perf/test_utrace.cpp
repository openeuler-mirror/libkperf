#include "common.h"
#include "test_common.h"
#include "elf_scanner.h"
#include "probe_registrar.h"
#include "trace_data_manager.h"

class ElfScannerTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ElfScanner::FormatFailures();
    }

    void TearDown() override
    {
        ElfScanner::FormatFailures();
    }
};

TEST_F(ElfScannerTest, ResolveElf_SuccessAndExactRetCount)
{
    std::vector<std::string> symbolsToFind = {
        "single_ret_func",
        "multiple_ret_func"};

    std::unordered_map<std::string, std::vector<std::string>> module2Symbols;
    auto testElfPath = GetTestBinaryPath("libtest_utrace_elf.so");
    module2Symbols[testElfPath] = symbolsToFind;
    auto resultsMap = ElfScanner::ResolveElfs(module2Symbols);

    ASSERT_EQ(resultsMap.count(testElfPath), 1) << "The test ELF file should be present in the results.";
    const auto &resultVec = resultsMap.at(testElfPath);

    ASSERT_EQ(resultVec.size(), 2) << "Should find all two specified functions.";

    std::map<std::string, const ProbePoints *> results;
    for (const auto &p : resultVec)
    {
        results[p.symbolName] = &p;
    }

    EXPECT_EQ(results.at("single_ret_func")->retOffsets.size(), 1);
#ifdef DEBUG
    // -pg enables gprof profiling instrumentation, which inserts extra calls and affects RET count
    EXPECT_EQ(results.at("multiple_ret_func")->retOffsets.size(), 1);
#else
    EXPECT_EQ(results.at("multiple_ret_func")->retOffsets.size(), 2);
#endif

    EXPECT_TRUE(ElfScanner::FormatFailures().empty());
}

TEST_F(ElfScannerTest, ResolveElf_FileDoesNotExist)
{
    std::unordered_map<std::string, std::vector<std::string>> module2Symbols;
    auto notExistentPath = GetTestBinaryPath("libnon_existent_file.so");
    module2Symbols[notExistentPath] = {"any_symbol"};
    auto resultsMap = ElfScanner::ResolveElfs(module2Symbols);

    EXPECT_TRUE(resultsMap.empty());

    std::string failureMsg = ElfScanner::FormatFailures();
    EXPECT_FALSE(failureMsg.empty());
    EXPECT_NE(failureMsg.find("Failed to open file"), std::string::npos);
}

TEST_F(ElfScannerTest, ResolveElf_SomeSymbolsNotFound)
{
    std::vector<std::string> symbolsToFind = {
        "single_ret_func",
        "non_existent_symbol"};

    std::unordered_map<std::string, std::vector<std::string>> module2Symbols;
    auto testElfPath = GetTestBinaryPath("libtest_utrace_elf.so");
    module2Symbols[testElfPath] = symbolsToFind;
    auto resultsMap = ElfScanner::ResolveElfs(module2Symbols);

    ASSERT_EQ(resultsMap.count(testElfPath), 1);
    const auto &resultVec = resultsMap.at(testElfPath);

    ASSERT_EQ(resultVec.size(), 1);
    EXPECT_EQ(resultVec[0].symbolName, "single_ret_func");

    std::string failureMsg = ElfScanner::FormatFailures();
    EXPECT_FALSE(failureMsg.empty());
    EXPECT_NE(failureMsg.find("Symbols not found"), std::string::npos);
    EXPECT_NE(failureMsg.find("libtest_utrace_elf.so [non_existent_symbol]"), std::string::npos);
}

TEST_F(ElfScannerTest, ResolveElfs_MultipleModules)
{
    auto testElfPath = GetTestBinaryPath("libtest_utrace_elf.so");
    auto notExistentPath = GetTestBinaryPath("libnon_existent_file.so");
    std::unordered_map<std::string, std::vector<std::string>> module2Symbols = {
        {testElfPath, {"single_ret_func", "non_existent_symbol"}},
        {notExistentPath, {"any_symbol"}}};

    auto result = ElfScanner::ResolveElfs(module2Symbols);

    ASSERT_EQ(result.size(), 1);
    ASSERT_TRUE(result.count(testElfPath));
    ASSERT_EQ(result[testElfPath].size(), 1);
    EXPECT_EQ(result[testElfPath][0].symbolName, "single_ret_func");

    std::string failureMsg = ElfScanner::FormatFailures();
    EXPECT_FALSE(failureMsg.empty());
    EXPECT_NE(failureMsg.find("ELF parsing failed"), std::string::npos);
    EXPECT_NE(failureMsg.find("libnon_existent_file.so"), std::string::npos);
    EXPECT_NE(failureMsg.find("Symbols not found"), std::string::npos);
    EXPECT_NE(failureMsg.find("libtest_utrace_elf.so [non_existent_symbol]"), std::string::npos);
}

class UTraceTest : public ::testing::Test
{
protected:
    static void SetUpTestCase()
    {
        pid = RunTestApp("test_utrace_app");
    }

    static void TearDownTestCase()
    {
        KillApp(pid);
    }

    void SetUp()
    {
        if (GetTraceEventDir() == "")
        {
            GTEST_SKIP();
        }
    }

    void TearDown()
    {
        if (data != nullptr)
        {
            UTraceDataFree(data);
            data = nullptr;
        }
        UTraceClose(pd);
    }

protected:
    int pd;
    static pid_t pid;
    UTraceData *data = nullptr;
};

pid_t UTraceTest::pid = 0;

TEST_F(UTraceTest, FullLifecycleSuccess)
{
    std::string exePath = GetTestBinaryPath("test_utrace_app");
    SymbolSource srcs[] = {
        {const_cast<char*>(exePath.c_str()), "my_function"}};
    int pidList[1] = {pid};

    struct UTraceAttr attr = {0};
    attr.symSrc = srcs;
    attr.numSym = 1;
    attr.pidList = pidList;
    attr.numPid = 1;

    int pd = UTraceOpen(&attr);
    ASSERT_GE(pd, 0) << Perror() << "\n" << GetWarnMsg();

    int ret = UTraceEnable(pd);
    EXPECT_EQ(ret, SUCCESS);

    sleep(1);

    ret = UTraceDisable(pd);
    EXPECT_EQ(ret, SUCCESS);

    int len = UTraceRead(pd, &data);

    ASSERT_NE(data, nullptr);
}

TEST_F(UTraceTest, OpenWithNullAttr)
{
    int pd = UTraceOpen(nullptr);
    EXPECT_EQ(pd, -1);
}

TEST_F(UTraceTest, OpenWithInvalidSymbol)
{
    std::string exePath = GetTestBinaryPath("test_utrace_app");
    SymbolSource srcs[] = {
        {const_cast<char*>(exePath.c_str()), "non_existent_function"}};
    int pidList[1] = {pid};

    struct UTraceAttr attr = {0};
    attr.symSrc = srcs;
    attr.numSym = 1;
    attr.pidList = pidList;
    attr.numPid = 1;

    int pd = UTraceOpen(&attr);
    EXPECT_EQ(pd, -1);
    EXPECT_EQ(Perrorno(), LIBPERF_ERR_UTRACE_ELF_SCAN_FAILED);
    EXPECT_EQ(GetWarn(), LIBPERF_WARN_UTRACE_ELF_SCAN_FAILED);
}
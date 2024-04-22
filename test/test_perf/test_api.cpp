#include <thread>
#include <sys/resource.h>
#include "util_time.h"
#include "process_map.h"
#include "common.h"
#include "test_common.h"

using namespace std;
class TestAPI : public testing::Test {
public:
    static void SetUpTestCase()
    {
        // Bind test_perf to cpu 0 and bind test app to cpu 1, for stable collection.
        cpu_set_t mask;
        CPU_ZERO(&mask);
        CPU_SET(0, &mask);
        sched_setaffinity(0, sizeof(mask), &mask);

        // Create a simple process.
        demoPid = RunTestApp(exePath);
        cout << "pid: " << demoPid << endl;
    }

    static void TearDownTestCase()
    {
        KillApp(demoPid);
    }

    void TearDown()
    {
        if (data != nullptr) {
            PmuDataFree(data);
            data = nullptr;
        }
        PmuClose(pd);
        for (int i = 0; i< pdNums; ++i) {
            PmuClose(pds[i]);
        }
    }

protected:
    bool HasExpectSource(PmuData *data, int len) const
    {
        // Check whether pmu data contains expected filename and line number.
        bool foundData = false;
        for (int i = 0; i < len; ++i) {
            auto stack = data[i].stack;
            while (stack) {
                if (stack->symbol) {
                    if (basename(stack->symbol->fileName) == expectFilename && stack->symbol->lineNum == expectLine) {
                        foundData = true;
                        break;
                    }
                }
                stack = stack->next;
            }
            if (foundData) {
                break;
            }
        }

        return foundData;
    }

    bool HasExpectSymbol(PmuData *data, int len) const
    {
        bool foundData = false;
        for (int i = 0; i < len; ++i) {
            auto stack = data[i].stack;
            while (stack) {
                if (stack->symbol) {
                    if (basename(stack->symbol->module) == exePath) {
                        foundData = true;
                        break;
                    }
                }
                stack = stack->next;
            }
            if (foundData) {
                break;
            }
        }

        return foundData;
    }

    bool CheckDataEvent(PmuData *data, int len, string evtName) const
    {
        for (int i = 0; i < len; ++i) {
            if (string(data[i].evt) != evtName) {
                return false;
            }
        }

        return true;
    }

    PmuAttr GetPmuAttribute()
    {
        PmuAttr attr = {0};
        attr.evtList = evtList;
        attr.numEvt = numEvt;
        attr.pidList = pidList;
        attr.numPid = numPid;
        attr.cpuList = cpuList;
        attr.numCpu = numCpu;
        attr.freq = 1000;
        attr.useFreq = 1;
        return attr;
    }

    PmuAttr GetSpeAttribute()
    {
        PmuAttr attr = {0};
        attr.pidList = pidList;
        attr.numPid = numPid;
        attr.cpuList = cpuList;
        attr.numCpu = numCpu;
        attr.period = 1000;
        attr.dataFilter = SPE_DATA_ALL;
        attr.evFilter = SPE_EVENT_RETIRED;
        attr.minLatency = 0x40;
        return attr;
    }

protected:
    static const string exePath;
    static pid_t demoPid;

    static const unsigned numCpu = 0;
    static const unsigned numPid = 1;
    static const unsigned numEvt = 1;
    static const string expectFilename;
    static const unsigned expectLine = 17;
    static const unsigned collectInterval = 100;

    int *cpuList = nullptr;
    char *evtList[numEvt] = {"cycles"};
    int pidList[numPid] = {demoPid};
    int pd = 0;
    static const int pdNums = 2;
    int pds[pdNums] = {0};
    PmuData *data = nullptr;
};

pid_t TestAPI::demoPid;
const string TestAPI::exePath = "simple";
const string TestAPI::expectFilename = "simple.cpp";

TEST_F(TestAPI, SampleInitSuccess)
{
    auto attr = GetPmuAttribute();
    pd = PmuOpen(SAMPLING, &attr);
    ASSERT_TRUE(pd != -1);
}

TEST_F(TestAPI, SampleCollectSuccess)
{
    auto attr = GetPmuAttribute();
    pd = PmuOpen(SAMPLING, &attr);
    int ret = PmuCollect(pd, 10, collectInterval);
    ASSERT_TRUE(ret == SUCCESS);
}

TEST_F(TestAPI, SampleReadSuccess)
{
    auto attr = GetPmuAttribute();
    pd = PmuOpen(SAMPLING, &attr);
    int ret = PmuCollect(pd, 1000, collectInterval);
    int len = PmuRead(pd, &data);
    EXPECT_TRUE(data != nullptr);
    ASSERT_TRUE(HasExpectSource(data, len));
}

TEST_F(TestAPI, SpeInitSuccess)
{
    auto attr = GetSpeAttribute();
    pd = PmuOpen(SPE_SAMPLING, &attr);
    ASSERT_TRUE(pd != -1);
}

TEST_F(TestAPI, SpeCollectSuccess)
{
    auto attr = GetSpeAttribute();
    pd = PmuOpen(SPE_SAMPLING, &attr);
    ASSERT_TRUE(pd != -1);
    int ret = PmuCollect(pd, 10, collectInterval);
    ASSERT_TRUE(ret == SUCCESS);
}

TEST_F(TestAPI, SpeReadSuccess)
{
    auto attr = GetSpeAttribute();
    pd = PmuOpen(SPE_SAMPLING, &attr);
    int ret = PmuCollect(pd, 1000, collectInterval);
    ASSERT_TRUE(pd != -1);
    int len = PmuRead(pd, &data);
    EXPECT_TRUE(data != nullptr);
    ASSERT_TRUE(HasExpectSymbol(data, len));
}

TEST_F(TestAPI, InitSampleNullEvt)
{
    auto attr = GetPmuAttribute();
    attr.evtList = nullptr;
    pd = PmuOpen(SAMPLING, &attr);
    ASSERT_EQ(pd, -1);
    ASSERT_EQ(Perrorno(), LIBPERF_ERR_INVALID_EVTLIST);
}

TEST_F(TestAPI, InitCountNullEvt)
{
    auto attr = GetPmuAttribute();
    attr.evtList = nullptr;
    pd = PmuOpen(COUNTING, &attr);
    ASSERT_EQ(pd, -1);
    ASSERT_EQ(Perrorno(), LIBPERF_ERR_INVALID_EVTLIST);
}

TEST_F(TestAPI, InitBadPid)
{
    auto attr = GetPmuAttribute();
    attr.pidList[0] = -1;
    pd = PmuOpen(COUNTING, &attr);
    ASSERT_EQ(pd, -1);
    ASSERT_EQ(Perrorno(), LIBPERF_ERR_INVALID_PIDLIST);
}

TEST_F(TestAPI, InitBadCpu)
{
    auto attr = GetPmuAttribute();
    attr.cpuList = new int[4];
    attr.numCpu = 4;
    attr.cpuList[0] = 5000;
    pd = PmuOpen(COUNTING, &attr);
    delete[] attr.cpuList;
    ASSERT_EQ(pd, -1);
    ASSERT_EQ(Perrorno(), LIBPERF_ERR_INVALID_CPULIST);
}

TEST_F(TestAPI, SampleCollectBadEvt)
{
    auto attr = GetPmuAttribute();
    attr.evtList[0] = "abc";
    pd = PmuOpen(SAMPLING, &attr);
    ASSERT_EQ(pd, -1);
    ASSERT_EQ(Perrorno(), LIBPERF_ERR_INVALID_EVENT);
}

TEST_F(TestAPI, SampleCollectBadPd)
{
    auto ret = PmuCollect(3, 1000, collectInterval);
    ASSERT_EQ(Perrorno(), LIBPERF_ERR_INVALID_PD);
}

TEST_F(TestAPI, SpeInitBusy)
{
    auto attr = GetSpeAttribute();
    pd = PmuOpen(SPE_SAMPLING, &attr);
    ASSERT_TRUE(pd != -1);
    int badPd = PmuOpen(SPE_SAMPLING, &attr);
    ASSERT_TRUE(badPd == -1);
    ASSERT_EQ(Perrorno(), LIBPERF_ERR_DEVICE_BUSY);
    PmuClose(badPd);
}

TEST_F(TestAPI, SampleSystem)
{
    auto attr = GetPmuAttribute();
    attr.pidList = nullptr;
    attr.numPid = 0;
    pd = PmuOpen(SAMPLING, &attr);
    int ret = PmuCollect(pd, 100, collectInterval);
    int len = PmuRead(pd, &data);
    EXPECT_TRUE(data != nullptr);
    ASSERT_TRUE(HasExpectSource(data, len));
}

TEST_F(TestAPI, SpeSystem)
{
    auto attr = GetSpeAttribute();
    attr.pidList = nullptr;
    attr.numPid = 0;
    pd = PmuOpen(SPE_SAMPLING, &attr);
    ASSERT_TRUE(pd != -1);
    int ret = PmuCollect(pd, 1000, collectInterval);
    int len = PmuRead(pd, &data);
    EXPECT_TRUE(data != nullptr);
    ASSERT_TRUE(HasExpectSymbol(data, len));
}

static void Stop(int pd)
{
    sleep(2);
    PmuStop(pd);
}

TEST_F(TestAPI, StopSuccess)
{
    auto attr = GetPmuAttribute();
    pd = PmuOpen(SAMPLING, &attr);
    thread th(Stop, pd);
    auto start = GetCurrentTime();
    int ret = PmuCollect(pd, 1000 * 10, collectInterval);
    auto end = GetCurrentTime();
    th.join();
    ASSERT_LE(end - start, 5000);
}

TEST_F(TestAPI, OpenInvalidTaskType)
{
    auto attr = GetPmuAttribute();
    pd = PmuOpen((PmuTaskType)99, &attr);
    ASSERT_TRUE(pd == -1);
    ASSERT_EQ(Perrorno(), LIBPERF_ERR_INVALID_TASK_TYPE);
}

TEST_F(TestAPI, CollectInvalidTime)
{
    auto attr = GetPmuAttribute();
    pd = PmuOpen(SAMPLING, &attr);
    int ret = PmuCollect(pd, -2, collectInterval);
    ASSERT_EQ(Perrorno(), LIBPERF_ERR_INVALID_TIME);
}

TEST_F(TestAPI, RaiseNumFd)
{
    struct rlimit currentlim;
    ASSERT_NE(getrlimit(RLIMIT_NOFILE, &currentlim), -1);
    auto err = RaiseNumFd(currentlim.rlim_max + 1);
    ASSERT_EQ(err, LIBPERF_ERR_TOO_MANY_FD);
    err = RaiseNumFd(currentlim.rlim_cur - 1);
    ASSERT_EQ(err, SUCCESS);
    err = RaiseNumFd(currentlim.rlim_max - 1);
    ASSERT_EQ(err, SUCCESS);
    err = RaiseNumFd(currentlim.rlim_max - 51);
    ASSERT_EQ(err, SUCCESS);
}
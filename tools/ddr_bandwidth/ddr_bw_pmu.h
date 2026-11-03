#ifndef DDR_BW_PMU_H
#define DDR_BW_PMU_H

#include <memory>
#include <string>
#include <vector>

#include "pmu.h"
#include "ddr_bw_types.h"

struct BwContext;
struct IntervalWeights;
struct Symbol;

constexpr const char* UNKNOWN = "UNKNOWN";

struct SampleInfo {
    bool hasExt = false;
    bool hasSym = false;
    bool hasSource = false;
    bool isLoad = false;
    bool isStore = false;
    unsigned short source = 0;
    unsigned long event = 0;
    unsigned long pc = 0;
    int pid = 0;
    Symbol* sym = nullptr;
};

struct PcieCollector {
    int pd = -1;
    std::string bdf;
    std::vector<std::unique_ptr<char[]>> eventStorage;
    std::vector<char*> eventList;
    double lastReadSec = 0.0;
    bool synchronized = false;
};

struct PcieReadResult {
    unsigned attempted = 0;
    unsigned succeeded = 0;
    unsigned failed = 0;
    unsigned resynchronized = 0;
    std::vector<std::string> incompleteBdfs;

    bool Complete() const
    {
        return attempted > 0 && failed == 0 && resynchronized == 0 &&
            succeeded == attempted;
    }
};

double NowSec();
int64_t NowNs();
void ConfigureSpeDebug(bool enabled);
void PrintSpeEventDebug();
void PrintSpeStorePcDebug();
void ProcessSpeChunk(PmuData* data, int len, BwContext& ctx,
                     const std::vector<int>& targetPids, SpeWindowState& windows,
                     int64_t readEndNs);
bool ReadDdrcInterval(int pd, PmuDeviceAttr* attrs, unsigned attrLen, double& readBytes, double& writeBytes);
bool IsPciBridge(const std::string& bdf);
bool PrimePmuRead(int pd, double& readSec, std::string& error);
bool OpenPcieCollector(const std::string& bdf, PcieCollector& collector, std::string& error);
PcieReadResult ReadPcieInterval(std::vector<PcieCollector>& collectors,
                                BwContext& ctx, PcieBytes& total,
                                double stoppedSec = 0.0);

#endif

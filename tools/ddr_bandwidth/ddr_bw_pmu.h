#ifndef DDR_BW_PMU_H
#define DDR_BW_PMU_H

#include <memory>
#include <string>
#include <vector>

#include "pmu.h"

struct BwContext;
struct IntervalWeights;
struct Symbol;

constexpr const char* UNKNOWN = "UNKNOWN";
constexpr int DEFAULT_ACCESS_BYTES = 8;

struct SampleInfo {
    bool hasExt = false;
    bool hasSym = false;
    bool isLoad = false;
    bool isStore = false;
    unsigned short source = 0;
    unsigned long pc = 0;
    int pid = 0;
    Symbol* sym = nullptr;
};

struct PcieBytes {
    double rxMwr = 0.0;
    double txCpl = 0.0;
    double elapsedSec = 0.0;
    double minRunningRatio = 1.0;
    bool hasRunningRatio = false;
};

struct PcieCollector {
    int pd = -1;
    std::string bdf;
    std::vector<std::unique_ptr<char[]>> eventStorage;
    std::vector<char*> eventList;
    double lastReadSec = 0.0;
};

double NowSec();
void ProcessSpeChunk(PmuData* data, int len, BwContext& ctx,
                     const std::vector<int>& targetPids, IntervalWeights& weights);
bool ReadDdrcInterval(int pd, PmuDeviceAttr* attrs, unsigned attrLen, double& readBytes, double& writeBytes);
bool IsPciBridge(const std::string& bdf);
bool PrimePmuRead(int pd, double& readSec, std::string& error);
bool OpenPcieCollector(const std::string& bdf, PcieCollector& collector, std::string& error);
bool ReadPcieInterval(std::vector<PcieCollector>& collectors, BwContext& ctx, PcieBytes& total);

#endif

/******************************************************************************
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 * libkperf licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *     http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR
 * PURPOSE.
 * See the Mulan PSL v2 for more details.
 * Author: Wu
 * Create: 2025-10-21
 * Description: Collecting summary info and hotspot info
 ******************************************************************************/
#include <iostream>
#include <iomanip>
#include <cstring>
#include <string.h>
#include <map>
#include <algorithm>
#include "pmu.h"
#include "symbol.h"
#include "cpu_map.h"
#include "collect.h"

const char* UNKNOWN = "UNKNOWN";
const int HEX_BUFFER_SIZE = 20;
const int FLOAT_PRECISION = 2;
static std::string ProcessSymbol(const Symbol* symbol)
{
    if (symbol == nullptr) {
        return UNKNOWN;
    }
    std::string res = UNKNOWN;
    if (symbol->symbolName != nullptr && strcmp(symbol->symbolName, UNKNOWN) != 0) {
        res = symbol->symbolName;
    } else if (symbol->codeMapAddr > 0) {
        char buf[HEX_BUFFER_SIZE];
        if (sprintf(buf, "0x%lx", symbol->codeMapAddr) < 0) {
            res = std::string();
        }
        res = buf;
    } else {
        char buf[HEX_BUFFER_SIZE];
        if (sprintf(buf, "0x%lx", symbol->addr) < 0) {
            res = std::string();
        }
        res = buf;
    }
    return res;
}

static bool IsUnknownSymbolName(const char* symbolName)
{
    return symbolName == nullptr || strncmp(symbolName, UNKNOWN, strlen(UNKNOWN)) == 0;
}

static bool IsKernelSymbol(const Symbol* symbol)
{
    return symbol != nullptr && symbol->module != nullptr && strcmp(symbol->module, "[kernel]") == 0;
}

static bool IsResolvedUserSymbol(const Symbol* symbol)
{
    return symbol != nullptr && !IsKernelSymbol(symbol) && !IsUnknownSymbolName(symbol->symbolName);
}

static bool IsJavaMethodSymbol(const Symbol* symbol)
{
    return IsResolvedUserSymbol(symbol) && symbol->symbolName[0] == 'L' &&
           strstr(symbol->symbolName, "::") != nullptr;
}

static Stack* FindValidNode(Stack* head)
{
    Stack* firstResolved = nullptr;
    Stack* firstUserAddr = nullptr;
    Stack* curr = head;
    while (curr != nullptr) {
        Symbol* symbol = curr->symbol;
        if (IsJavaMethodSymbol(symbol)) {
            return curr;
        }
        if (firstResolved == nullptr && IsResolvedUserSymbol(symbol)) {
            firstResolved = curr;
        }
        if (firstUserAddr == nullptr && symbol != nullptr && !IsKernelSymbol(symbol)) {
            firstUserAddr = curr;
        }
        curr = curr->next;
    }
    if (firstResolved != nullptr) {
        return firstResolved;
    }
    if (firstUserAddr != nullptr) {
        return firstUserAddr;
    }
    return head;
}

static HotspotFunc BuildHotspotFunc(const PmuData& data)
{
    HotspotFunc hs;
    hs.pid = data.pid;

    Stack* stack = instStat ? data.stack : FindValidNode(data.stack);
    if (stack == nullptr || stack->symbol == nullptr) {
        return hs;
    }

    const Symbol* symbol = stack->symbol;
    hs.symbolName = ProcessSymbol(symbol);
    hs.moduleName = symbol->module == nullptr ? UNKNOWN : symbol->module;
    hs.symbolKey = hs.symbolName + "@" + hs.moduleName;
    hs.addr = symbol->addr;
    hs.offset = symbol->offset;
    hs.codeMapAddr = symbol->codeMapAddr;
    hs.codeMapEndAddr = symbol->codeMapEndAddr;
    return hs;
}

static bool IsSameHotspot(const HotspotFunc& hs, const HotspotFunc& other)
{
    if (hs.pid != other.pid) {
        return false;
    }
    if (instStat) {
        return hs.addr == other.addr;
    }
    return !hs.symbolKey.empty() && hs.symbolKey == other.symbolKey;
}

static std::string GetPeriodPercent(unsigned pid, uint64_t period)
{
    std::ostringstream oss;
    double totalPeriod = pidPeriod[pid];
    if (totalPeriod == 0.0) {
        oss << std::fixed << std::setprecision(FLOAT_PRECISION) << 0.0;
    } else {
        oss << std::fixed << std::setprecision(FLOAT_PRECISION) << (static_cast<double>(period) / pidPeriod[pid] * 100.0);
    }
    return oss.str();
}

static void SortHotspotData(std::vector<HotspotFunc>& hotSpotData)
{
    std::sort(hotSpotData.begin(), hotSpotData.end(), [](const HotspotFunc &a, const HotspotFunc &b) {
        if (hotspotSortType == HotspotSortOption::L1) {
            return a.l1RefillPeriod > b.l1RefillPeriod;
        }
        if (hotspotSortType == HotspotSortOption::L2) {
            return a.l2RefillPeriod > b.l2RefillPeriod;
        }
        return a.cyclesPeriod > b.cyclesPeriod;
    });
}

static int GetPmuDataHotspot(PmuData* pmuData, int pmuDataLen, std::vector<HotspotFunc>& tmpData)
{
    if (!pmuData || pmuDataLen == 0) {
        return SUCCESS;
    }

    std::string l1CacheEvent = dataCollect ? "l1d_cache" : "l1i_cache";
    std::string l1RefillEvent = dataCollect ? "l1d_cache_refill" : "l1i_cache_refill";
    std::string l2CacheEvent = dataCollect ? "l2d_cache" : "l2i_cache";
    std::string l2RefillEvent = dataCollect ? "l2d_cache_refill" : "l2i_cache_refill";

    for (int i = 0; i < pmuDataLen; ++i) {
        PmuData& data = pmuData[i];
        if (!data.stack) {
            continue;
        }
        if (strcmp(data.evt, "cycles") == 0) {
            pidPeriod[data.pid] += data.period;
        }
        HotspotFunc current = BuildHotspotFunc(data);
        if (current.symbolKey.empty()) {
            continue;
        }
        bool found = false;
        for (auto &hs : tmpData) {
            if (IsSameHotspot(current, hs)) {
                if (strcmp(data.evt, l1CacheEvent.c_str()) == 0) {
                    hs.l1AccessPeriod += data.period;
                } else if (strcmp(data.evt, l1RefillEvent.c_str()) == 0) {
                    hs.l1RefillPeriod += data.period;
                } else if (strcmp(data.evt, l2CacheEvent.c_str()) == 0) {
                    hs.l2AccessPeriod += data.period;
                    hs.l2iAccessCount += 1;
                } else if (strcmp(data.evt, l2RefillEvent.c_str()) == 0) {
                    hs.l2RefillPeriod += data.period;
                    hs.l2iRefillCount += 1;
                } else if (strcmp(data.evt, "cycles") == 0) {
                    hs.cyclesPeriod += data.period;
                    hs.cyclesCount += 1;
                }
                found = true;
                break;
            }
        }

        if (!found) {
            HotspotFunc hs = current;
            if (strcmp(data.evt, l1CacheEvent.c_str()) == 0) {
                hs.l1AccessPeriod = data.period;
            } else if (strcmp(data.evt, l1RefillEvent.c_str()) == 0) {
                hs.l1RefillPeriod = data.period;
            } else if (strcmp(data.evt, l2CacheEvent.c_str()) == 0) {
                hs.l2AccessPeriod = data.period;
                hs.l2iAccessCount = 1;
            } else if (strcmp(data.evt, l2RefillEvent.c_str()) == 0) {
                hs.l2RefillPeriod = data.period;
                hs.l2iRefillCount = 1;
            } else if (strcmp(data.evt, "cycles") == 0) {
                hs.cyclesPeriod = data.period;
                hs.cyclesCount = 1;
            }
            tmpData.push_back(std::move(hs));
        }
    }

    SortHotspotData(tmpData);
    return SUCCESS;
}

static void PrintHeader(const std::string& refill, const std::string& cache)
{
    std::cout << std::setw(15) << refill
              << std::setw(12) << cache
              << std::setw(15) << "Cycles"
              << std::setw(12) << "Ratio(%)"
              << "\n";
}

static void PrintHotSpotTitle(int length) {
    std::cout << std::string(length, '=') << "\n";
    std::string title = instStat ? "HOTSPOT INST" : "HOTSPOT FUNC";
    std::cout << std::setw(80) << std::right << title << "\n";
    std::cout << std::string(length, '-') << "\n";
    std::cout << std::left;
    if (instStat) {
            std::cout << std::setw(20) << "Addr"
              << std::setw(50) << "FuncName"
              << std::setw(15) << "Pid";
    } else {
            std::cout<< std::setw(50) << "Function"
              << std::setw(12) << "Pid"
              << std::setw(18) << "Start Addr"
              << std::setw(18) << "End Addr"
              << std::setw(18) << "Length";
    }

    std::string l1Refill = dataCollect ? "l1 dcache refill" : "l1 icache refill";
    std::string l1Cache = dataCollect ? "l1 dcache" : "l1 icache";
    std::string l2Refill = dataCollect ? "l2 dcache refill" : "l2 icache refill";
    std::string l2Cache  = dataCollect ? "l2 dcache" : "l2 icache";
    std::cout << std::setw(20) << l1Refill
          << std::setw(15) << l1Cache
          << std::setw(20) << l2Refill
          << std::setw(15) << l2Cache
          << std::setw(15) << "Cycles"
          << std::setw(12) << "Ratio(%)"
          << "\n";
    std::cout << std::string(length, '-') << "\n";
}

static void WriteRecord(std::ofstream& out, const std::string& funcname, unsigned long offset, unsigned long value)
{
    if (value > 0) {
        out << 1 << ' ' << funcname << ' ' << std::hex << offset << ' ' << std::dec << value << '\n';
    }
}

static void InitOutputFiles(FileSet &fs, int pid, const std::string &timeStr)
{
    std::string baseName = std::to_string(pid) + "_" + timeStr;

    if (boltType == BoltOption::CYCLES || boltType == BoltOption::ALL) {
        fs.cyclesPath = baseName + "_cycles.txt";
        fs.cyclesFile.open(fs.cyclesPath);
        fs.cyclesFile << "no_lbr cycles:\n";
    }
    if (boltType == BoltOption::L2I_CACHE || boltType == BoltOption::ALL) {
        fs.l2iCachePath = baseName + "_l2i_cache.txt";
        fs.l2iCacheFile.open(fs.l2iCachePath);
        fs.l2iCacheFile << "no_lbr l2i_cache:\n";
    }
    if (boltType == BoltOption::L2I_CACHE_REFILL || boltType == BoltOption::ALL) {
        fs.l2iCacheRefillPath = baseName + "_l2i_cache_refill.txt";
        fs.l2iCacheRefillFile.open(fs.l2iCacheRefillPath);
        fs.l2iCacheRefillFile << "no_lbr l2i_cache_refill:\n";
    }
}

static void WriteOutputRecords(FileSet &fs, const std::string &funcName, uint64_t offset, const HotspotFunc &hs)
{
    if (boltType == BoltOption::L2I_CACHE_REFILL || boltType == BoltOption::ALL) {
        WriteRecord(fs.l2iCacheRefillFile, funcName, offset, hs.l2iRefillCount);
    }
    if (boltType == BoltOption::L2I_CACHE || boltType == BoltOption::ALL) {
        WriteRecord(fs.l2iCacheFile, funcName, offset, hs.l2iAccessCount);
    }
    if (boltType == BoltOption::CYCLES || boltType == BoltOption::ALL) {
        WriteRecord(fs.cyclesFile, funcName, offset, hs.cyclesCount);
    }
}

static void PrintOutputPaths(const FileSet &fs)
{
    if (boltType == BoltOption::CYCLES || boltType == BoltOption::ALL) {
        std::cout << "Bolt file: " << GetFullPath(fs.cyclesPath) << "\n";
    }
    if (boltType == BoltOption::L2I_CACHE_REFILL || boltType == BoltOption::ALL) {
        std::cout << "Bolt file: " << GetFullPath(fs.l2iCacheRefillPath) << "\n";
    }
    if (boltType == BoltOption::L2I_CACHE || boltType == BoltOption::ALL) {
        std::cout << "Bolt file: " << GetFullPath(fs.l2iCachePath) << "\n";
    }
}
static void PrintHotSpot(std::vector<HotspotFunc>& hotSpotData)
{
    std::map<int, std::vector<HotspotFunc>> grouped;  // grouping by pid
    for (auto& hs : hotSpotData) {
        grouped[hs.pid].push_back(hs);
    }

    std::string timeStr = GetTimeStr();
    int length = instStat ? 180 : 215;

    for (auto& kv : grouped) {
        int pid = kv.first;
        auto& funcs = kv.second;
        SortHotspotData(funcs);

        PrintHotSpotTitle(length);

        FileSet fs;
        fs.enabled = !dataCollect && (boltType != BoltOption::NONE);
        if (fs.enabled) {
            InitOutputFiles(fs, pid, timeStr);
        }

        bool longName = false;
        for (const auto& hs : funcs) {
            std::string funcName = hs.symbolName;
            std::string fullFuncName = ProcessFunctionString(funcName);

            if (!longName && funcName.size() > 48) {
                int halfLen = 48 / 2 - 1;
                int startPos = funcName.size() - 48 + halfLen + 3;
                funcName = funcName.substr(0, halfLen) + "..." + funcName.substr(startPos);
            }

            std::cout << std::left;
            if (instStat) {
                std::cout << std::hex << std::setw(20) << hs.addr << std::dec
                          << std::setw(50) << funcName
                          << std::setw(15) << pid;
            } else {
                unsigned long beginAddr = hs.codeMapAddr >= hs.offset ? hs.codeMapAddr - hs.offset : 0;
                unsigned long funcLength = hs.codeMapEndAddr == 0 || hs.codeMapEndAddr < beginAddr ?
                                           0 : hs.codeMapEndAddr - beginAddr;
                std::cout << std::setw(50) << funcName
                          << std::setw(12) << pid
                          << std::setw(18) << std::hex << beginAddr
                          << std::setw(18) << std::hex << hs.codeMapEndAddr
                          << std::setw(18) << std::dec << funcLength;
            }

            std::cout << std::setw(20) << hs.l1RefillPeriod
                      << std::setw(15) << hs.l1AccessPeriod
                      << std::setw(20) << hs.l2RefillPeriod
                      << std::setw(15) << hs.l2AccessPeriod
                      << std::setw(15) << hs.cyclesPeriod
                      << std::setw(12) << GetPeriodPercent(pid, hs.cyclesPeriod) << "\n";

            if (!dataCollect) {
                WriteOutputRecords(fs, fullFuncName, hs.offset, hs);
            }
        }

        std::cout << std::string(length, '_') << "\n";
        if (!dataCollect && fs.enabled) {
            PrintOutputPaths(fs);
        }
        std::cout << "\n";
    }
}

static uint64_t GetEventCount(const std::map<std::string, uint64_t>& events, const std::string& event)
{
    std::map<std::string, uint64_t>::const_iterator it = events.find(event);
    return it == events.end() ? 0 : it->second;
}

EventConfig buildEventConfig(bool dataCollect, bool summaryCollect)
{
    EventConfig cfg;
    if (summaryCollect) {
        cfg.baseEvents = {
            "cycles",
            "instructions",
            "l1i_cache_refill",
            "l1i_cache",
            "l1d_cache_refill",
            "l1d_cache",
            "l2i_cache_refill",
            "l2i_cache",
            "l2d_cache_refill",
            "l2d_cache"
        };
    } else if (dataCollect) {
        cfg.baseEvents = {
            "cycles",
            "l1d_cache_refill",
            "l1d_cache",
            "l2d_cache_refill",
            "l2d_cache"
        };
    } else {
        cfg.baseEvents = {
            "cycles",
            "l1i_cache_refill",
            "l1i_cache",
            "l2i_cache_refill",
            "l2i_cache"
        };
    }

    cfg.groupId.reserve(cfg.baseEvents.size());
    for (size_t i = 0; i < cfg.baseEvents.size(); ++i) {
        int groupId = summaryCollect && i >= 6 ? 2 : 1;
        cfg.groupId.push_back(EvtAttr{groupId});
    }
    cfg.evtStorage.reserve(cfg.baseEvents.size());
    cfg.evtList.reserve(cfg.baseEvents.size());
    for (const auto& evt : cfg.baseEvents) {
        auto buf = std::make_unique<char[]>(evt.size() + 1);
        std::strcpy(buf.get(), evt.c_str());
        cfg.evtList.push_back(buf.get());
        cfg.evtStorage.push_back(std::move(buf));
    }

    return cfg;
}

void collectMiss(CollectArgs& args)
{
    dataCollect = args.enableData;
    instStat = args.enableInst;
    boltType = args.boltOption;
    hotspotSortType = args.hotspotSortOption;
    bool summaryCollect = false;
    EventConfig cfg = buildEventConfig(dataCollect, summaryCollect);

    PmuAttr attr = {0};
    attr.evtList = cfg.evtList.data();
    attr.numEvt = cfg.baseEvents.size();
    attr.callStack = 1;
    attr.excludeKernel = true;
    attr.symbolMode = RESOLVE_ELF_DWARF;
    attr.freq = args.frequency;
    attr.useFreq = 1;
    attr.pidList = args.pids.data();
    attr.numPid = args.pids.size();

    int pd = PmuOpen(SAMPLING, &attr);
    if (pd == -1) {
        std::cerr << "PmuOpen failed. Error msg:" << Perror() << std::endl;
        return;
    }
    PmuEnable(pd);

    int len;
    PmuData* pmuData = nullptr;
    std::vector<HotspotFunc> hotSpotData;
    int loopCount = (args.duration * 1000) / args.interval;
    for (int i = 0; i < loopCount; ++i) {
        usleep(1000 * args.interval);
        len = PmuRead(pd, &pmuData);
        if (len == -1) {
            std::cerr << "PmuRead failed. error msg:" << Perror() << std::endl;
            return;
        }
        GetPmuDataHotspot(pmuData, len, hotSpotData);
        PmuDataFree(pmuData);
    }
    PrintHotSpot(hotSpotData);
    PmuDisable(pd);
    PmuClose(pd);
}

static void PrintSummaryData(const std::map<int, std::map<std::string, uint64_t>>& pidSummaryMap)
{
    std::vector<PidSummary> summaries;

    for (std::map<int, std::map<std::string, uint64_t>>::const_iterator it = pidSummaryMap.begin(); it != pidSummaryMap.end(); ++it) {
        int pid = it->first;
        const std::map<std::string, uint64_t>& events = it->second;

        uint64_t l1i_cache = GetEventCount(events, "l1i_cache");
        uint64_t l1i_cache_refill = GetEventCount(events, "l1i_cache_refill");
        uint64_t l1d_cache = GetEventCount(events, "l1d_cache");
        uint64_t l1d_cache_refill = GetEventCount(events, "l1d_cache_refill");
        uint64_t l2i_cache = GetEventCount(events, "l2i_cache");
        uint64_t l2i_cache_refill = GetEventCount(events, "l2i_cache_refill");
        uint64_t l2d_cache = GetEventCount(events, "l2d_cache");
        uint64_t l2d_cache_refill = GetEventCount(events, "l2d_cache_refill");
        uint64_t instructions = GetEventCount(events, "instructions");
        uint64_t cycles = GetEventCount(events, "cycles");

        double l1Icache_miss_rate = l1i_cache ? static_cast<double>(l1i_cache_refill) / l1i_cache : 0.0;
        double l1Dcache_miss_rate = l1d_cache ? static_cast<double>(l1d_cache_refill) / l1d_cache : 0.0;
        double l2Icache_miss_rate = l2i_cache ? static_cast<double>(l2i_cache_refill) / l2i_cache : 0.0;
        double l2Dcache_miss_rate = l2d_cache ? static_cast<double>(l2d_cache_refill) / l2d_cache : 0.0;
        double ipc = cycles ? static_cast<double>(instructions) / cycles : 0.0;

        PidSummary s;
        s.pid = pid;
        s.l1Icache_miss_rate = l1Icache_miss_rate;
        s.l1Dcache_miss_rate = l1Dcache_miss_rate;
        s.l2Icache_miss_rate = l2Icache_miss_rate;
        s.l2Dcache_miss_rate = l2Dcache_miss_rate;
        s.ipc = ipc;

        summaries.push_back(s);
    }

    std::sort(summaries.begin(), summaries.end(), [](const PidSummary& a, const PidSummary& b) {
        if (dataCollect) {
           return a.l1Dcache_miss_rate > b.l1Dcache_miss_rate;
        } else {
            return a.l1Icache_miss_rate > b.l1Icache_miss_rate;
        }
    });

    const int pid_width = 10;
    const int rate_width = 25;
    const int ipc_width = 10;
    const int total_width = pid_width + rate_width * 4 + ipc_width;

    std::cout << std::string(total_width, '=') << "\n";
    std::cout << std::setw((total_width + 7) / 2) << "SUMMARY" << "\n";
    std::cout << std::string(total_width, '-') << "\n";

    std::cout << std::left << std::setw(pid_width) << "Pid"
              << std::right << std::setw(rate_width) << "l1 icache Miss Rate"
              << std::setw(rate_width) << "l1 dcache Miss Rate"
              << std::setw(rate_width) << "l2 icache Miss Rate"
              << std::setw(rate_width) << "l2 dcache Miss Rate";
    std::cout  << std::setw(ipc_width) << "IPC" << "\n";
    std::cout << std::string(total_width, '-') << "\n";

    for (size_t i = 0; i < summaries.size(); ++i) {
        const PidSummary& s = summaries[i];

        std::ostringstream l1Icache_str;
        std::ostringstream l1Dcache_str;
        std::ostringstream l2Icache_str;
        std::ostringstream l2Dcache_str;
        l1Icache_str << std::fixed << std::setprecision(2) << (s.l1Icache_miss_rate * 100) << "%";
        l1Dcache_str << std::fixed << std::setprecision(2) << (s.l1Dcache_miss_rate * 100) << "%";
        l2Icache_str << std::fixed << std::setprecision(2) << (s.l2Icache_miss_rate * 100) << "%";
        l2Dcache_str << std::fixed << std::setprecision(2) << (s.l2Dcache_miss_rate * 100) << "%";

        std::cout << std::left << std::setw(pid_width) << s.pid
                  << std::right << std::setw(rate_width) << l1Icache_str.str()
                  << std::setw(rate_width) << l1Dcache_str.str()
                  << std::setw(rate_width) << l2Icache_str.str()
                  << std::setw(rate_width) << l2Dcache_str.str();
        std::cout<<std::setw(ipc_width) << std::fixed << std::setprecision(2) << s.ipc << "\n";
    }

    std::cout << std::string(total_width, '-') << "\n";
}

void collectSummaryData(CollectArgs& args)
{
    CHIP_TYPE chipType = GetCpuType();
    dataCollect = args.enableData;
    bool summaryCollect = true;
    bool dataSummaryCollect = true;
    EventConfig cfg = buildEventConfig(dataSummaryCollect, true);

    PmuAttr attr = {0};
    attr.evtAttr = cfg.groupId.data();
    attr.evtList = cfg.evtList.data();
    attr.numEvt = cfg.baseEvents.size();
    attr.numEvtAttr = cfg.baseEvents.size();
    attr.pidList = args.pids.data();
    attr.numPid = args.pids.size();

    int pd = PmuOpen(COUNTING, &attr);
    if (pd == -1) {
        std::cerr << "PmuOpen failed. Error msg:" << Perror() << std::endl;
        return;
    }

    PmuEnable(pd);
    sleep(args.summaryTime);
    PmuDisable(pd);

    PmuData* pmuData = nullptr;
    int len = PmuRead(pd, &pmuData);
    if (len == -1) {
        std::cerr << "PmuRead failed. error msg:" << Perror() << std::endl;
        return;
    }
    std::map<int, std::map<std::string, uint64_t>> pidSummaryMap;
    for (int i = 0; i < len; i++) {
        pidSummaryMap[pmuData[i].pid][pmuData[i].evt] += pmuData[i].count;
    }
    PrintSummaryData(pidSummaryMap);

    PmuDataFree(pmuData);
    PmuClose(pd);
}

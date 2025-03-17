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
 * Author: Mr.Lei
 * Create: 2025-03-13
 * Description: Pmu data hotspot analysis module.
 * Current capability: Analyze the original data of performance monitoring unit, and compute the hotspot data.
 ******************************************************************************/
#include <iostream>
#include <vector>
#include <algorithm>
#include <string>
#include <cstring>
#include <iomanip>
#include <signal.h>
#include "pcerrc.h"
#include "pmu.h"
#include "symbol.h"

using namespace std;

#define RED_TEXT "\033[31m"
#define RESET_COLOR "\033[0m"

const char* UNKNOWN = "UNKNOWN";
const int HEX_BUFFER_SIZE = 20;
const int FLOAT_PRECISION = 2;
uint64_t g_totalPeriod = 0;

std::string ProcessSymbol(const Symbol* symbol)
{
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

bool ComparePmuData(const PmuData &a, const PmuData &b)
{
    if (strcmp(a.evt, b.evt) != 0) {
        return false;
    }
    Stack* stackA = a.stack;
    Stack* stackB = b.stack;

    while (stackA != nullptr && stackB != nullptr) {
        std::string symbolA = ProcessSymbol(stackA->symbol);
        std::string symbolB = ProcessSymbol(stackB->symbol);

        if (symbolA.empty() || symbolB.empty() || symbolA != symbolB) {
            return false;
        }

        stackA = stackA->next;
        stackB = stackB->next;
    }

    // confirm the stackA and stackB are the same length.
    return stackA == nullptr && stackB == nullptr;
}

int GetPmuDataHotspot(PmuData* pmuData, int pmuDataLen, std::vector<PmuData>& tmpData)
{
    if (pmuData == nullptr || pmuDataLen == 0) {
        return SUCCESS;
    }

    for (int i = 0; i < pmuDataLen; ++i) {
        PmuData& data = pmuData[i];
        if (data.stack == nullptr) {
            continue;
        }
        g_totalPeriod += data.period;
        bool isExist = false;
        for (auto &tmp : tmpData) {
            if (ComparePmuData(data, tmp)) {
                isExist = true;
                tmp.period += data.period;
                break;
            }
        }
        if (!isExist) {
            tmpData.push_back(data);
        }
    }

    std::sort(tmpData.begin(), tmpData.end(), [](const PmuData &a, const PmuData &b) { return a.period > b.period; });
    return SUCCESS;
}

std::string GetPeriodPercent(uint64_t period)
{
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(FLOAT_PRECISION) << (static_cast<double>(period) * 100 / g_totalPeriod);
    return oss.str();
}

void PrintStack(const Stack* stack, int depth = 0, uint64_t period = 0)
{
    if (stack == nullptr) {
        return;
    }
    std::string symbolName = ProcessSymbol(stack->symbol);
    std::string moduleName = stack->symbol->module == nullptr ? UNKNOWN : stack->symbol->module;
    std::cout << std::string(depth * 2, ' ') << "|——";
    std::string outInfo = symbolName + " " + moduleName;
    std::cout << outInfo;
    if (depth == 0) {
        if (outInfo.size() < 110) {
            std::cout << std::string(110 - outInfo.size(), ' ') << GetPeriodPercent(period) << "%";
        } else {
            std::cout << "  " << GetPeriodPercent(period) << "%";
        }
    }
    std::cout << std::endl;
    PrintStack(stack->next, depth + 1, period);
}

void PrintHotSpotGraph(std::vector<PmuData>& hotSpotData)
{
    std::cout << std::string(140, '=') << std::endl;
    std::cout << std::string(140, '-') << std::endl;
    std::cout << std::setw(80) << std::left << " Function" << std::setw(20) << " Cycles"
        << std::setw(40) << " Module" << "cycles(%)" << std::endl;
    std::cout << std::string(140, '-') << std::endl;
    bool longName = false;
    for (int i = 0; i < hotSpotData.size(); ++i) {
        std::string moduleName = hotSpotData[i].stack->symbol->module == nullptr ? UNKNOWN : hotSpotData[i].stack->symbol->module;
        if (!longName) {
            std::size_t pos = moduleName.find_last_of("/");
            if (pos != std::string::npos) {
                moduleName = moduleName.substr(pos + 1);
            }
        }
        if (strcmp(hotSpotData[i].evt, "context-switches") == 0) {
            std::cout << RED_TEXT;
        }
        std::string funcName = ProcessSymbol(hotSpotData[i].stack->symbol);
        if (!longName && funcName.size() > 78) {
            int halfLen = 78 / 2 - 1;
            int startPos = funcName.size() - 78 + halfLen + 3;
            funcName = funcName.substr(0, halfLen) + "..." + funcName.substr(startPos);
        }
        std::cout << "  " << std::setw(78) << std::left << funcName
            << std::setw(20) << hotSpotData[i].period << std::setw(40) << moduleName
            << GetPeriodPercent(hotSpotData[i].period) << "%" << std::endl;
        if (strcmp(hotSpotData[i].evt, "context-switches") == 0) {
            std::cout << RESET_COLOR;
        }
    }
    std::cout << std::string(140, '_') << std::endl;
}

void BlockedSample(int pid)
{
    char* evtList[1];
    // evtList[0] = (char*)"cycles";
    struct PmuAttr attr = {0};
    attr.evtList = nullptr;
    attr.numEvt = 0;
    attr.blockedSample = 1;
    attr.pidList = &pid;
    attr.numPid = 1;
    attr.cpuList = nullptr;
    attr.numCpu = 0;
    attr.useFreq = 1;
    attr.freq = 4000;
    attr.callStack = 1;
    attr.symbolMode = RESOLVE_ELF_DWARF;

    int pd = PmuOpen(SAMPLING, &attr);
    if (pd == -1) {
        std::cerr << "PmuOpen failed" << std::endl;
        std::cerr << "error msg:" << Perror() << std::endl;
        return;
    }

    PmuEnable(pd);
    sleep(1);
    PmuDisable(pd);

    PmuData* pmuData = nullptr;
    int len = PmuRead(pd, &pmuData);
    if (len == -1) {
        std::cerr << "error msg:" << Perror() << std::endl;
        return;
    }

    std::vector<PmuData> hotSpotData;
    GetPmuDataHotspot(pmuData, len, hotSpotData);
    PrintHotSpotGraph(hotSpotData);
    std::cout << std::string(50, '=') << "Print the call stack of the hotspot function";
    std::cout << std::string(50, '=') << std::endl;
    std::cout << std::setw(40) << "@symbol" << std::setw(40) << "@module";
    std::cout << std::setw(40) << std::right << "@percent" << std::endl;
    for (int i = 0; i < hotSpotData.size(); ++i) {
        PrintStack(hotSpotData[i].stack, 0, hotSpotData[i].period);
    }
    PmuDataFree(pmuData);
    PmuClose(pd);
    return;
}

void StartProc(char *process, int &pid)
{
    if (process == nullptr) {
        return;
    }
    pid = fork();
    if (pid == 0) {
        execlp(process, process, nullptr);
        exit(0);
    }
}

void EndProc(int pid)
{
    if (pid > 0) {
        kill(pid, SIGKILL);
    }
}

int main(int argc, char** argv)
{
    int pid = 0;
    if (argc > 1) {
        StartProc(argv[1], pid);
    }
    BlockedSample(pid);
    EndProc(pid);
    
    return 0;
}
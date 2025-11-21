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
 * Description: parse and check collect args
 ******************************************************************************/
#include <iostream>
#include <getopt.h>
#include <sstream>
#include <stdexcept>
#include <vector>
#include <cstdlib>
#include <unordered_map>
#include "collect_args.h"


const std::unordered_map<std::string, BoltOption> boltOptionMap = {
    {"cycles", BoltOption::CYCLES},
    {"l2i_cache", BoltOption::L2I_CACHE},
    {"l2i_cache_refill", BoltOption::L2I_CACHE_REFILL},
    {"all", BoltOption::ALL}
};

BoltOption CollectArgs::ParseBoltOption(const std::string& value) {
    auto it = boltOptionMap.find(value);
    if (it != boltOptionMap.end()) {
        return it->second;
    }
    return BoltOption::NONE;
}


bool CollectArgs::ParsePositiveIntArg(const char* arg, const std::string& paramName, int& outValue, int minValue)
{
    try {
        int value = std::stoul(arg);
        if (value < minValue) {
            std::cerr << "Error: parameter '" << paramName << "' must bigger than 0, but got " << value << ".\n";
            return false;
        }
        outValue = static_cast<unsigned>(value);
        return true;
    } catch (const std::invalid_argument& e) {
        std::cerr << "Error: failed to parse parameter '" << paramName << "' from argument '" << arg
                  << "'. Reason: " << e.what() << ". Expected an integer.\n";
        return false;
    } catch (const std::out_of_range& e) {
        std::cerr << "Error: parameter '" << paramName << "' value '" << arg
                  << "' is out of valid range. Reason: " << e.what() << "\n";
        return false;
    }
}

bool CollectArgs::ParseOption(int argc, char* argv[])
{
    int opt;
    int option_index = 0;
    std::string level;
    std::string mode;
    std::string bolt;
    struct option long_options[] = {
        {"help",      no_argument,       0, 'h'},
        {"pid",       required_argument, 0, 'p'},
        {"duration",  required_argument, 0, 'd'},
        {"level",     required_argument, 0, 'l'},
        {"mode",      required_argument, 0, 'm'},
        {"interval",  required_argument, 0, 'i'},
        {"frequency", required_argument, 0, 'f'},
        {"bolt",      required_argument, 0, 'b'},
        {"summary",   required_argument, 0, 's'},
        {0, 0, 0, 0}
    };

    while ((opt = getopt_long(argc, argv, "hp:d:l:m:i:f:b:s:", long_options, &option_index)) != -1) {
        switch (opt) {
            case 'h':
                printUsage();
                exit(0);
            case 'd':
                if (!ParsePositiveIntArg(optarg, "hotspot duration", duration)) {
                    return false;
                }
                break;
            case 'p':
                pidList = optarg;
                break;
            case 'l':
                level = optarg;
                if (level == "inst") {
                    enableInst = true;
                } else {
                    std::cerr << "Error: invalid input for --level/-l, only support 'inst'" << "\n";
                    return false;
                }
                break;
            case 'm':
                mode = optarg;
                if (mode == "dcache") {
                    enableData = true;
                } else {
                    std::cerr << "Error: invalid input for --mode/-m, only support 'dcache'" << "\n";
                    return false;
                }
                break;
            case 'i':
                if (!ParsePositiveIntArg(optarg, "interval", interval)) {
                    return false;
                }
                break;
            case 'f':
                if (!ParsePositiveIntArg(optarg, "frequency", frequency)) {
                    return false;
                }
                break;
            case 'b':
                bolt = optarg;
                boltOption = ParseBoltOption(bolt);
                if (boltOption == BoltOption::NONE) {
                    std::cerr << "Error: invalid input for --bolt/-b, only support 'cycles', 'l2i_cache', 'l2i_cache_refill', and 'all'" << "\n";
                    return false;
                }
                break;
            case 's':
                if (!ParsePositiveIntArg(optarg, "summary duration", summaryTime)) {
                    return false;
                }
                break;
            default:
                printUsage();
                return false;
        }
    }
    if (boltOption != BoltOption::NONE && enableData == true) {
        std::cerr << "Error: bolt file can only be set in default collection mode\n";
        return false;
    }
    if (pidList.empty()) {
        std::cerr << "Error: missing required parameter --pid/-p\n";
        return false;
    }
    return ParsePidList();
}

bool CollectArgs::ParsePidList()
{
    std::stringstream ss(pidList);
    std::string pidStr;
    while (std::getline(ss, pidStr, ',')) {
        pid_t pid;
        try {
            pid = std::stoi(pidStr);
        } catch (const std::invalid_argument& e) {
            std::cerr << "Error: failed to parse pid from input parameter: '" << pidStr
                    << "' .Reason: " << e.what() << ". Expected an integer.\n";
            return false;
        }
        pids.push_back(pid);
    }
    return true;
}

void CollectArgs::printUsage()
{
    std::cerr << "Usage: cache_collect --pid/-p <pid> [options]\n\n";

    std::cerr << "Required:\n";
    std::cerr << "  --pid/-p <pid>           : Target process ID(s). Multiple IDs can be separated by ','\n";
    std::cerr << "Optional:\n";
    std::cerr << "  --duration/-d <seconds>  : Set collection time of hotspots. Unit: s, default: 10\n";
    std::cerr << "  --level/-l <level>       : Set to 'inst' for instruction-level summary. Default: function-level summary\n";
    std::cerr << "  --mode/-m <mode>         : Set to 'dcache' to collect L2D cache data. Default: L2I cache data\n";
    std::cerr << "  --interval/-i <ms>       : Interval for reading the ring buffer. Unit: ms, default: 1000\n";
    std::cerr << "  --frequency/-f <freq>    : Sampling frequency, default: 1000\n";
    std::cerr << "  --bolt/-b <option>       : Generate BOLT format output file. Options: 'cycles', 'l2i_cache', 'l2i_cache_refill', or 'all'. Only for default mode\n";
    std::cerr << "  --summary/-s <seconds>   : Set collection time of summary ratio and IPC collection. Unit: s, default: 5\n\n";
    std::cerr << "Examples:\n";
    std::cerr << "  cache_collect -p 125785 -d 10 -l inst -m dcache -i 2000\n";
    std::cerr << "  cache_collect -p 125785,143789 -m dcache -f 4000 -b cycles\n";
}
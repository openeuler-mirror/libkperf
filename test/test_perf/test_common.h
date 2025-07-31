#ifndef TEST_PERF_H
#define TEST_PERF_H

#include <gtest/gtest.h>
#include <vector>
#include <string>
#include <map>
#include <set>
#include <iostream>
#include <fstream>
#include <unistd.h>
#include <limits.h>
#include <libgen.h>
#include <linux/types.h>
#include "pcerrc.h"
#include "symbol.h"
#include "pmu.h"

pid_t RunTestApp(const std::string &name);

void KillApp(pid_t pid);

void DumpPmuData(const PmuData *data);

bool FoundAllTids(PmuData *data, int len, pid_t pid);

std::vector<pid_t> GetChildPid(pid_t pid);

bool FoundAllChildren(PmuData *data, int len, pid_t pid);

void DelayContinue(pid_t pid, int milliseconds);

unsigned GetCpuNums();

unsigned GetNumaNodeCount();

unsigned GetClusterCount();

// Check whether event names of all data are <evt>.
bool CheckDataEvt(PmuData *data, int len, std::string evt);

// Check whether pid of all data are <evt>.
bool CheckDataPid(PmuData *data, int len, int pid);

// Check whether at least one event name of data is <evt>.
bool HasEvent(PmuData *data, int len, std::string evt);

// Check whether there is arm_spe_0 device.
bool HasSpeDevice();

#endif

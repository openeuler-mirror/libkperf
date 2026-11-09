#ifndef DDR_BW_CLI_H
#define DDR_BW_CLI_H

#include <string>
#include <vector>
#include <unistd.h>

struct BwContext;
struct CollectionTiming;

struct ArgsContext {
    int pid = -1;
    std::vector<int> pids;
    int duration = 10;
    int interval = 100;
    int speReadInterval = 50;
    unsigned long minSamples = 1;
    unsigned period = 2048;
    unsigned long minLatency = 0;
    int topN = 20;
    bool debug = false;
    bool isLaunch = false;
    bool systemWide = false;
    int fd[2] = {-1, -1};
    std::vector<std::string> pcieBdfs;
    std::vector<std::string> command;
};

bool ParseArgs(int argc, char** argv, ArgsContext& act);
void PrintHelp();
void PrintSummary(BwContext& ctx, bool ddrcOpened, bool pcieOpened,
                  const CollectionTiming& timing, const ArgsContext& act);
int ddr_bw_run(int argc, char** argv);

#endif

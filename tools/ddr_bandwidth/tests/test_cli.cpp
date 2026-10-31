#include <cstdlib>
#include <getopt.h>
#include <iostream>
#include <signal.h>
#include <sstream>
#include <string>
#include <vector>
#include "ddr_bw_cli.h"
#include "ddr_bw_types.h"

// CLI tests never open PMUs or launch a target.
int RunDdrBandwidthCollection(ArgsContext&, volatile sig_atomic_t*, volatile int*)
{
    std::abort();
}

static void Check(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << message << "\n";
        std::exit(1);
    }
}

static bool Parse(std::vector<std::string> args, ArgsContext& act)
{
    std::vector<char*> argv;
    for (auto& arg : args) argv.push_back(&arg[0]);
    argv.push_back(nullptr);
    optind = 0;
    return ParseArgs(static_cast<int>(args.size()), argv.data(), act);
}

int main()
{
    ArgsContext defaults;
    Check(Parse({"ddr_bandwidth", "-p", "123"}, defaults), "default CLI");
    Check(defaults.speReadInterval == 50 && defaults.interval == 100 &&
          defaults.minSamples == 1 && defaults.period == 2048,
          "50 ms SPE drain and 2048 period defaults");
    ArgsContext custom;
    Check(Parse({"ddr_bandwidth", "-p", "123,456", "--spe-read-interval", "10",
                 "--min-samples", "1", "--period", "4096", "-i", "500"}, custom), "custom CLI");
    Check(custom.speReadInterval == 10 && custom.minSamples == 1 && custom.period == 4096 &&
          custom.interval == 500 && custom.pids.size() == 2, "custom values retained");
    for (const auto& option : {"--spe-read-interval", "--min-samples", "--period", "-i", "-d", "--top"}) {
        for (const auto& value : {"0", "-1", "999999999999999999999999999999", "x"}) {
            ArgsContext invalid;
            Check(!Parse({"ddr_bandwidth", "-p", "123", option, value}, invalid), "invalid number rejected");
        }
    }
    for (const auto& value : {"123,", "123,,456", "2147483648"}) {
        ArgsContext invalid;
        Check(!Parse({"ddr_bandwidth", "-p", value}, invalid), "invalid PID rejected");
    }
    ArgsContext launch;
    Check(Parse({"ddr_bandwidth", "--", "./demo", "--arg"}, launch) && launch.isLaunch,
          "launch parsing remains available");
    BwContext ctx;
    auto& stat = ctx.funcStats["A"];
    stat.funcKey = "A";
    stat.funcName = "A";
    stat.pid = 123;
    stat.readSamples = stat.readWeight = 100;
    ctx.spe.readSamples = ctx.spe.readWeight = 100;
    CollectionTiming timing;
    timing.durationSec = 1;
    std::ostringstream output;
    auto* saved = std::cout.rdbuf(output.rdbuf());
    PrintSummary(ctx, false, false, timing, defaults);
    std::cout.rdbuf(saved);
    Check(output.str().find("N/A") != std::string::npos, "missing DDRC must show N/A");
    Check(output.str().find("Estimated CPU DDR bandwidth") == std::string::npos,
          "sample count cannot masquerade as bytes");
    Check(output.str().find("DBG_WINDOW") == std::string::npos,
          "window diagnostics require debug mode");
    Check(output.str().find("CPU DDR BANDWIDTH BY USER FUNCTION") != std::string::npos &&
          output.str().find("PID") != std::string::npos &&
          output.str().find("123") != std::string::npos,
          "function table prints PID in a dedicated column");

    ArgsContext debugArgs = defaults;
    debugArgs.debug = true;
    debugArgs.minSamples = 1;
    BwContext debugCtx;
    auto& debugStat = debugCtx.funcStats["A"];
    debugStat.funcKey = "A";
    debugStat.funcName = "A";
    debugStat.pid = 123;
    WindowRecord window;
    window.startNs = 1000;
    window.endNs = 1100;
    window.speReadCostNs = 25;
    window.speDrains = 2;
    window.ddrcReadBytes = 100;
    window.ddrcWriteBytes = 100;
    window.residualReadBytes = 100;
    window.residualWriteBytes = 100;
    window.systemReadSamples = 10;
    window.systemWriteSamples = 2;
    window.kernelReadSamples = 2;
    window.kernelWriteSamples = 2;
    window.targetReadSamples = 4;
    window.funcReadSamples = {{"A", 4}};
    debugCtx.windows.push_back(window);
    debugCtx.traffic.ddrcReadBytes = 100;
    debugCtx.traffic.ddrcWriteBytes = 100;
    debugCtx.spe.readSamples = 4;
    debugCtx.spe.systemReadSamples = 10;
    debugCtx.spe.kernelReadSamples = 2;
    debugCtx.speTimestamps.late = 2;
    FinalizeAttribution(debugCtx, debugArgs.minSamples);
    output.str("");
    output.clear();
    saved = std::cout.rdbuf(output.rdbuf());
    PrintSummary(debugCtx, true, false, timing, debugArgs);
    std::cout.rdbuf(saved);
    Check(output.str().find("DBG_WINDOW window=0") != std::string::npos,
          "debug prints every window");
    Check(output.str().find("system_read_weight=10") != std::string::npos,
          "debug prints system denominator");
    Check(output.str().find("kernel_read_weight=2") != std::string::npos &&
          output.str().find("background_read_weight=6") != std::string::npos &&
          output.str().find("attributed_read_bytes=40.000000") != std::string::npos &&
          output.str().find("kernel_attributed_read_bytes=20.000000") != std::string::npos &&
          output.str().find("unattributed_read_bytes=40.000000") != std::string::npos &&
          output.str().find("kernel_observed_write_bytes=100.000000") != std::string::npos &&
          output.str().find("unattributed_write_bytes=100.000000") != std::string::npos &&
          output.str().find("read_state=ok write_state=sparse") != std::string::npos,
          "debug prints final window attribution state");
    Check(output.str().find("Target PID user attributed") != std::string::npos &&
          output.str().find("Kernel observed (read partition)") != std::string::npos &&
          output.str().find("Kernel observed (write overlap*)") != std::string::npos &&
          output.str().find("normalized within target PID stores") != std::string::npos &&
          output.str().find("do not add them") != std::string::npos &&
          output.str().find("Other user/system") != std::string::npos,
          "summary distinguishes kernel read partition from write overlap");
    Check(output.str().find("target_dram_loads=4") != std::string::npos &&
          output.str().find("system_dram_loads=10") != std::string::npos &&
          output.str().find("kernel_dram_loads=2") != std::string::npos,
          "SPE summary separates target, system and kernel samples");
    Check(output.str().find("DBG_FUNC window=0 pid=123") != std::string::npos &&
          output.str().find("read_weight=4 write_weight=0 "
                            "read_bytes=40.000000 write_bytes=0.000000") != std::string::npos,
          "debug prints deterministic per-window function data");
    Check(output.str().find("DBG_TIMESTAMP missing=0 before_start=0 after_stop=0 "
                            "after_read=0 late=2 unbucketed=0") != std::string::npos,
          "debug prints timestamp diagnostics");
    std::cout << "DDR CLI regression checks passed\n";
}

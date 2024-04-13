#include <chrono>
#include <unistd.h>
#include <vector>
#include <iostream>
#include <string.h>
#include <signal.h>
#include <thread>
#include <map>
#include <limits.h>
#include "symbol.h"
#include "pmu.h"
#include "pcerrc.h"

using namespace std;

static pid_t pid = 0;

void Sample2()
{
    int numCpu = 1;
    int numPid = 1;
    int numEvt = 1;
    int *cpuList = (int*)malloc(sizeof(int)*numCpu);

    int *pidList = (int*)malloc(sizeof(int)*numPid);
    pidList[0] = pid;
    char *evtList[1];
    evtList[0] = "cycles";

    PmuAttr attr = {0};
    attr.evtList = evtList;
    attr.numEvt = 1;
    attr.pidList = pidList;
    attr.numPid = 1;
    attr.freq = 1000;
    attr.useFreq = 1;

    int pd = PmuOpen(SAMPLING, &attr);
    if (pd == -1) {
        cout << Perror() << "\n";
        return;
    }

    int i=3;
    int err = PmuEnable(pd);
    if (err != SUCCESS) {
        cout << "Error pmuEnable\n";
        return;
    }

    PmuData *sum = nullptr;
    int total = 0;
    while(i>0) {
        sleep(1);
        struct PmuData *pmuData = nullptr;
        int len = PmuRead(pd, &pmuData);
        total = PmuAppendData(pmuData, &sum);

        for (int j=0;j<len;++j) {
            PmuData *data = &pmuData[j];
            cout << std::dec << "comm: " << data->comm << " cpu: " << data->cpu << " pid: " << data->pid << " tid: " << data->tid
                 << " ts: " << data->ts << endl;
            Stack *stack = data->stack;
            while( stack) {
                if (stack->symbol) {
                    cout << std::hex << stack->symbol->addr << std::dec << " " << stack->symbol->symbolName << " " << stack->symbol->module << endl;
                }
                stack = stack->next;
            }
            cout << endl;
            break;
        }

        printf("len: %d\n", len);
        PmuDataFree(pmuData);
        i--;
    }
    err = PmuDisable(pd);
    printf("total: %d\n", total);
    PmuClose(pd);

}

void SpeSample2()
{
    int numCpu = 1;
    int numPid = 1;
    int numEvt = 1;
    int *cpuList = (int*)malloc(sizeof(int)*numCpu);

    int *pidList = (int*)malloc(sizeof(int)*numPid);
    pidList[0] = pid;

    PmuAttr attr = {0};
    attr.evtList = nullptr;
    attr.numEvt = 0;
    attr.pidList = pidList;
    attr.numPid = 1;
    attr.period = 1000;
    attr.dataFilter = SPE_DATA_ALL;
    attr.evFilter = SPE_EVENT_RETIRED;
    attr.minLatency = 0x40;

    int pd = PmuOpen(SPE_SAMPLING, &attr);
    if (pd == -1) {
        cout << Perror() << "\n";
        return;
    }

    int i=3;
    int err = PmuEnable(pd);
    if (err != SUCCESS) {
        cout << "Error pmuEnable\n";
        return;
    }

    PmuData *sum = nullptr;
    int total = 0;
    while(i>0) {
        sleep(1);
        struct PmuData *pmuData = nullptr;
        int len = PmuRead(pd, &pmuData);
        total = PmuAppendData(pmuData, &sum);

        for (int j=0;j<len;++j) {
            PmuData *data = &pmuData[j];
            cout << std::dec << "comm: " << data->comm << " cpu: " << data->cpu << " pid: " << data->pid << " tid: " << data->tid
                 << " ts: " << data->ts << endl;
            Stack *stack = data->stack;
            while( stack) {
                if (stack->symbol) {
                    cout << std::hex << stack->symbol->addr << std::dec << " " << stack->symbol->symbolName << " " << stack->symbol->module << endl;
                }
                stack = stack->next;
            }
            cout << endl;
            break;
        }

        printf("len: %d\n", len);
        PmuDataFree(pmuData);
        i--;
    }
    err = PmuDisable(pd);
    printf("total: %d\n", total);
    PmuClose(pd);

}

void Waas()
{
    vector<char*> traceEvts = {
        "net:netif_rx",
        "hisi_sccl7_hha5/rx_outer/",
        "hisi_sccl7_hha5/rx_sccl/",
    };

    int cpuList[1];
    cpuList[0] = 0;
    PmuAttr attr = {0};
    attr.evtList = traceEvts.data();
    attr.numEvt = traceEvts.size();

    int pd = PmuOpen(COUNTING, &attr);

    for (int i=0;i<10;++i) {
        int ret = PmuCollect(pd, 1000);
        PmuData *data = nullptr;
        int len = PmuRead(pd, &data);
        map<string, size_t> evtMap;
        for (int j=0;j<len;++j) {
            evtMap[data[j].evt] += data[j].count;
        }

        for (auto p: evtMap) {
            cout << "evt: " << p.first << " count: " << p.second << "\n";
        }
    }
}

void StartProc(char *cmd)
{
    if (cmd == nullptr) return;
    pid = fork();
    if (pid == 0) {
        char * const * dummy = nullptr;
        execvp(cmd, dummy);
        _exit(errno);
    } else {
        cout << "Process Pid: " << pid << "\n";
    }
}

void EndProc()
{
    if (pid == 0) return;
    kill(pid, SIGTERM);
}

int main(int argc, char** argv)
{
    char *cmd = nullptr;
    if (argc == 2) {
        cmd = argv[1];
    }

}
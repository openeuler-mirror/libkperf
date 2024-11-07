import kperf

def TestAPI_InitCountNullEvt():
    evtList = []
    pmu_attr = kperf.PmuAttr(evtList=evtList)
    pd = kperf.open(kperf.PmuTaskType.COUNTING, pmu_attr)
    if pd == -1:
        print(f"error number: {kperf.errorno()} error message: {kperf.error()}")

def TestAPI_InitSampleNullEvt():
    evtList = []
    pmu_attr = kperf.PmuAttr(evtList=evtList)
    pd = kperf.open(kperf.PmuTaskType.SAMPLING, pmu_attr)
    if pd == -1:
        print(f"error number: {kperf.errorno()} error message: {kperf.error()}")

def TestAPI_InitBadPid():
    pidList = [-1]
    pmu_attr = kperf.PmuAttr(pidList=pidList)
    pd = kperf.open(kperf.PmuTaskType.COUNTING, pmu_attr)
    if pd == -1:
        print(f"error number: {kperf.errorno()} error message: {kperf.error()}")

def TestAPI_InitBadCpu():
    cpuList = [5000, 0, 0, 0]
    pmu_attr = kperf.PmuAttr(cpuList=cpuList)
    pd = kperf.open(kperf.PmuTaskType.COUNTING, pmu_attr)
    if pd == -1:
        print(f"error number: {kperf.errorno()} error message: {kperf.error()}")

def TestAPI_SampleCollectBadEvt():
    evtList = ["abc"]
    pmu_attr = kperf.PmuAttr(evtList=evtList)
    pd = kperf.open(kperf.PmuTaskType.SAMPLING, pmu_attr)
    if pd == -1:
        print(f"error number: {kperf.errorno()} error message: {kperf.error()}")

def TestAPI_SpeInitBusy():
    pmu_attr = kperf.PmuAttr(
        sampleRate = 1000,
        symbolMode = kperf.SymbolMode.RESOLVE_ELF,
        dataFilter = kperf.SpeFilter.SPE_DATA_ALL,
        evFilter = kperf.SpeEventFilter.SPE_EVENT_RETIRED,
        minLatency = 0x40
    )
    # need root privilege to run
    pd = kperf.open(kperf.PmuTaskType.SPE_SAMPLING, pmu_attr)
    if pd == -1:
        print(f"error number: {kperf.errorno()} error message: {kperf.error()}")
    badpd = kperf.open(kperf.PmuTaskType.SPE_SAMPLING, pmu_attr)
    if badpd == -1:
        print(f"badpd error number: {kperf.errorno()} badpd error message: {kperf.error()}")
    kperf.close(badpd)
    
def TestAPI_OpenInvalidTaskType():
    evtList = ["r11", "cycles"]
    pmu_attr = kperf.PmuAttr(evtList=evtList)
    pd = kperf.open(99, pmu_attr)
    if pd == -1:
        print(f"error number: {kperf.errorno()} error message: {kperf.error()}")

if __name__ == '__main__':
    TestAPI_InitCountNullEvt()
    TestAPI_InitSampleNullEvt()
    TestAPI_InitBadPid()
    TestAPI_InitBadCpu()
    TestAPI_SampleCollectBadEvt()
    TestAPI_SpeInitBusy()
    TestAPI_OpenInvalidTaskType()
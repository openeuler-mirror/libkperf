import time
import kperf
from ctypes import *

def TestBrBe():
    evtList = ["cycles"]
    pidList = [1] # The pid can be changed based on the ID of the collectd process.
    branchSampleMode = kperf.BranchSampleFilter.KPERF_SAMPLE_BRANCH_ANY | kperf.BranchSampleFilter.KPERF_SAMPLE_BRANCH_USER
    pmu_attr = kperf.PmuAttr(sampleRate=1000, useFreq=True, pidList=pidList, evtList=evtList, branchSampleFilter=branchSampleMode)
    pd = kperf.open(kperf.PmuTaskType.SAMPLING, pmu_attr)
    if pd == -1:
        print(kperf.error())
        return
    kperf.enable(pd)
    time.sleep(1)
    kperf.disable(pd)
    pmu_data = kperf.read(pd)
    for data in pmu_data.iter:
        if data.ext and data.ext.branchRecords:
            for item in data.ext.branchRecords.iter:
                predicted = 'P'
                if item.mispred:
                    predicted = 'M'
                print(f"{hex(item.fromAddr)}->{hex(item.toAddr)} {item.cycles} {predicted}")

if __name__ == "__main__":
    TestBrBe()

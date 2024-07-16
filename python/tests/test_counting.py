import time
from collections import defaultdict

import kperf

def Counting():
    evtList = ["r11", "cycles"]
    pmu_attr = kperf.PmuAttr(evtList=evtList)
    pd = kperf.open(kperf.PmuTaskType.COUNTING, pmu_attr)
    if pd == -1:
        print(kperf.error())
    kperf.enable(pd)
    for _ in range(3):
        time.sleep(1)
        pmu_data = kperf.read(pd)
        evtMap = defaultdict(int)
        for data in pmu_data.iter:
            print(f"evt:{data.evt} count:{data.count}")
            evtMap[data.evt] += data.count
        for evt, count in evtMap.items():
            print(f"evt:{evt} count:{count}")
    kperf.disable(pd)
    kperf.close(pd)

if __name__ == '__main__':
    Counting()
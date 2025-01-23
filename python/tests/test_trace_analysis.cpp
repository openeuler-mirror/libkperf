import time
import os
import subprocess

import kperf
import ksym

def run_test_exe(exe_path):
    process = subprocess.Popen(exe_path, shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    pid = process.pid

    return pid
    

def test_config_param_error():
    funcList = ["testName"]
    pmu_trace_data = kperf.PmuTraceAttr(funcs=funcList)

    pd = kperf.trace_open(kperf.TRACE_SYS_CALL, pmu_trace_data)
    if pd == -1:
        print(kperf.errorno())
        print(kperf.error())
    

def test_collect_single_trace_data():
    apppid = run_test_exe(../../_build/test/test_perf/case/test_12threads)
    funcList = ["futex"]
    pmu_trace_data = kperf.PmuTraceAttr(funcs=funcList, pidList=[apppid])
    pd = kperf.trace_open(kperf.TRACE_SYS_CALL, pmu_trace_data)

    if pd == -1:
        print(kperf.errorno())
        print(kperf.error())
        return
    
    kperf.trace_enable(pd)
    time.sleep(1)
    kperf.trace_disable(pd)

    pmu_trace_data = kperf.trace_read(pd)
    for data in pmu_trace_data.iter:
        print(f"funcName: {data.funcs} elapsedTime: {data.elapsedTime} pid: {data.pid} tid: {data.tid} cpu: {data.cpu} comm: {data.comm}")
    
    kperf.trace_close(pd)

def test_collect_all_syscall_trace_data():
    pmu_trace_data = kperf.PmuTraceAttr()
    pd = kperf.trace_open(kperf.TRACE_SYS_CALL, pmu_trace_data)
    if pd == -1:
        print(kperf.errorno())
        print(kperf.error())
        return
    
    kperf.trace_enable(pd)
    time.sleep(1)
    kperf.trace_disable(pd)

    pmu_trace_data = kperf.trace_read(pd)
    for data in pmu_trace_data.iter:
        print(f"funcName: {data.funcs} elapsedTime: {data.elapsedTime} pid: {data.pid} tid: {data.tid} cpu: {data.cpu} comm: {data.comm}")
    
    kperf.trace_close(pd)

if __name__ == "__main__":
    test_config_param_error()
    test_collect_single_trace_data()
    test_collect_all_syscall_trace_data()

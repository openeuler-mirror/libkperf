import time
import os
import subprocess

import kperf
import ksym

def run_test_exe(exe_path, cpu_list):
    cpu_mask = ",".join(map(str, cpu_list))
    process = subprocess.Popen(["taskset", "-c", cpu_mask, exe_path])
    pid = process.pid

    return pid
    

def test_config_param_error():
    print(f"============start to test config param error ===================")
    funcList = ["testName"]
    pmu_trace_data = kperf.PmuTraceAttr(funcs=funcList)

    pd = kperf.trace_open(kperf.PmuTraceType.TRACE_SYS_CALL, pmu_trace_data)
    if pd == -1:
        print(kperf.errorno())
        print(kperf.error())
    

def test_collect_single_trace_data():
    print(f"============start to test collect single syscall and single cpu and single process ===================")
    main_dir = os.path.dirname(os.path.dirname(os.getcwd()))
    sub_dir = "_build/test/test_perf/case/test_12threads"
    app_path = os.path.join(main_dir, sub_dir)
    cpuList = [1]
    apppid = run_test_exe(app_path, cpuList)
    pidList = [apppid]
    funcList = ["futex"]
    pmu_trace_data = kperf.PmuTraceAttr(funcs=funcList, pidList=pidList, cpuList=cpuList)
    pd = kperf.trace_open(kperf.PmuTraceType.TRACE_SYS_CALL, pmu_trace_data)

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
    print(f"============start to test collect all syscall and all cpu and all process ===================")
    pmu_trace_data = kperf.PmuTraceAttr()
    pd = kperf.trace_open(kperf.PmuTraceType.TRACE_SYS_CALL, pmu_trace_data)
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

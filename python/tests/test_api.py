import pytest
import kperf

def test_init_count_null_evt():
    """测试初始化计数任务时，事件列表为空的情况"""
    evtList = []
    pmu_attr = kperf.PmuAttr(evtList=evtList)
    pd = kperf.open(kperf.PmuTaskType.COUNTING, pmu_attr)
    assert pd == -1, f"Expected failure, but got pd={pd}"
    print(f"error number: {kperf.errorno()} error message: {kperf.error()}")

def test_init_sample_null_evt():
    """测试初始化采样任务时，事件列表为空的情况"""
    evtList = []
    pmu_attr = kperf.PmuAttr(evtList=evtList)
    pd = kperf.open(kperf.PmuTaskType.SAMPLING, pmu_attr)
    assert pd == -1, f"Expected failure, but got pd={pd}"
    print(f"error number: {kperf.errorno()} error message: {kperf.error()}")

def test_init_bad_pid():
    """测试初始化任务时，PID 列表无效的情况"""
    pidList = [-1]
    pmu_attr = kperf.PmuAttr(pidList=pidList)
    pd = kperf.open(kperf.PmuTaskType.COUNTING, pmu_attr)
    assert pd == -1, f"Expected failure, but got pd={pd}"
    print(f"error number: {kperf.errorno()} error message: {kperf.error()}")

def test_init_bad_cpu():
    """测试初始化任务时，CPU 列表无效的情况"""
    cpuList = [5000, 0, 0, 0]
    pmu_attr = kperf.PmuAttr(cpuList=cpuList)
    pd = kperf.open(kperf.PmuTaskType.COUNTING, pmu_attr)
    assert pd == -1, f"Expected failure, but got pd={pd}"
    print(f"error number: {kperf.errorno()} error message: {kperf.error()}")

def test_sample_collect_bad_evt():
    """测试采样任务时，事件列表无效的情况"""
    evtList = ["abc"]
    pmu_attr = kperf.PmuAttr(evtList=evtList)
    pd = kperf.open(kperf.PmuTaskType.SAMPLING, pmu_attr)
    assert pd == -1, f"Expected failure, but got pd={pd}"
    print(f"error number: {kperf.errorno()} error message: {kperf.error()}")

def test_spe_init_busy():
    """测试 SPE 初始化时资源忙的情况"""
    pmu_attr = kperf.PmuAttr(
                sampleRate=1000,
                symbolMode=kperf.SymbolMode.RESOLVE_ELF,
                dataFilter=kperf.SpeFilter.SPE_DATA_ALL,
                evFilter=kperf.SpeEventFilter.SPE_EVENT_RETIRED,
                minLatency=0x40
            )
    pd = kperf.open(kperf.PmuTaskType.SPE_SAMPLING, pmu_attr)
    assert pd != -1, f"Failed to open SPE sampling task: {kperf.error()}"
    badpd = kperf.open(kperf.PmuTaskType.SPE_SAMPLING, pmu_attr)
    assert badpd == -1, f"Expected failure, but got badpd={badpd}"
    print(f"badpd error number: {kperf.errorno()} badpd error message: {kperf.error()}")
    kperf.close(pd)

def test_open_invalid_task_type():
    """测试使用无效的任务类型初始化的情况"""
    evtList = ["r11", "cycles"]
    pmu_attr = kperf.PmuAttr(evtList=evtList)
    pd = kperf.open(99, pmu_attr)
    assert pd == -1, f"Expected failure, but got pd={pd}"
    print(f"error number: {kperf.errorno()} error message: {kperf.error()}")

if __name__ == '__main__':
    # 提示用户使用pytest 运行测试文件
    print("This is a pytest script. Run it using the 'pytest' command.")
    print("For example: pytest test_*.py -v")
    print("if need print the run log, use pytest test_*.py -s -v")

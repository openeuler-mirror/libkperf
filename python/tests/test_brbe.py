import time
import kperf
from ctypes import *
import subprocess
import os
import pytest


def read_branch_records(pd):
    """Helper function to read and process branch records."""
    pmu_data = kperf.read(pd)
    branch_records = []
    for data in pmu_data.iter:
        if data.ext and data.ext.branchRecords:
            for item in data.ext.branchRecords.iter:
                branch_records.append((item.fromAddr, item.toAddr, item.cycles))
    return branch_records

def test_branch_sampling():
    """Test case for branch sampling functionality."""
    current_file_path = os.path.abspath(__file__)
    print(f"current_file_path: {current_file_path}")
    main_dir = os.path.dirname(os.path.dirname(os.path.dirname(current_file_path)))
    sub_dir = "_build/test/test_perf/case/test_12threads"
    app_path = os.path.join(main_dir, sub_dir)
    process = subprocess.Popen([app_path])
    apppid = process.pid
    print(f"process.pid: {apppid}")
    pidList = [apppid]
    evtList = ["cycles"]
    branchSampleMode = (
        kperf.BranchSampleFilter.KPERF_SAMPLE_BRANCH_ANY
        | kperf.BranchSampleFilter.KPERF_SAMPLE_BRANCH_USER
    )
    pmu_attr = kperf.PmuAttr(
        sampleRate=1000,
        useFreq=True,
        pidList=pidList,
        evtList=evtList,
        branchSampleFilter=branchSampleMode,
    )
    pd = kperf.open(kperf.PmuTaskType.SAMPLING, pmu_attr)
    if pd == -1:
        pytest.fail(f"Failed to open PMU: {kperf.error()}")
    kperf.enable(pd)
    time.sleep(1)  # Allow some time for sampling
    kperf.disable(pd)
    branch_records = read_branch_records(pd)

    # Assert that at least one branch record is captured
    assert len(branch_records) > 0, "No branch records were captured"

    # Optionally, validate the structure of the branch records
    for from_addr, to_addr, cycles in branch_records:
        assert isinstance(from_addr, int), f"Invalid fromAddr: {from_addr}"
        assert isinstance(to_addr, int), f"Invalid toAddr: {to_addr}"
        assert isinstance(cycles, int), f"Invalid cycles: {cycles}"

    # Print the branch records for debugging purposes
    for from_addr, to_addr, cycles in branch_records:
        print(f"{hex(from_addr)}->{hex(to_addr)} {cycles}")
    kperf.disable(pd)  # Ensure PMU is disabled after the test
    kperf.close(pd)  # Close the PMU descriptor
    process.terminate()
    process.wait()

if __name__ == '__main__':
    # 提示用户使用pytest 运行测试文件
    print("This is a pytest script. Run it using the 'pytest' command.")
    print("For example: pytest test_*.py -v")
    print("if need print the run log, use pytest test_*.py -s -v")
    test_branch_sampling()
import time
from collections import defaultdict

import kperf
import pytest

def read_event_counts(pd):
    """Helper function to read and process event counts."""
    pmu_data = kperf.read(pd)
    evtMap = defaultdict(int)
    for data in pmu_data.iter:
        evtMap[data.evt] += data.count
    return evtMap

def test_event_counting():
    """Test case for event counting functionality."""
    evtList = ["r11", "cycles"]
    pmu_attr = kperf.PmuAttr(evtList=evtList)
    pd = kperf.open(kperf.PmuTaskType.COUNTING, pmu_attr)
    if pd == -1:
        pytest.fail(f"Failed to open PMU: {kperf.error()}")
    kperf.enable(pd)

    # Collect event counts over multiple iterations
    for _ in range(3):
        time.sleep(1)  # Allow some time for counting
        evtMap = read_event_counts(pd)

        # Assert that at least one event count is captured
        assert len(evtMap) > 0, "No event counts were captured"

        # Validate the structure of the event counts
        for evt, count in evtMap.items():
            assert isinstance(evt, str), f"Invalid event name: {evt}"
            assert isinstance(count, int), f"Invalid count for event {evt}: {count}"

        # Print the event counts for debugging purposes
        for evt, count in evtMap.items():
            print(f"evt:{evt} count:{count}")

    # Optionally, validate specific event counts (if expected values are known)
    # Example: assert evtMap["cycles"] > 0, "Cycle count should be greater than zero"
    kperf.disable(pd)  # Ensure PMU is disabled after the test
    kperf.close(pd)  # Close the PMU descriptor

if __name__ == '__main__':
    # 提示用户使用pytest 运行测试文件
    print("This is a pytest script. Run it using the 'pytest' command.")
    print("For example: pytest test_*.py -v")
    print("if need print the run log, use pytest test_*.py -s -v")
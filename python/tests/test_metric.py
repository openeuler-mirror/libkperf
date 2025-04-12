import pytest
from ctypes import *
import kperf
import time
import os

def get_cpu_nums():
    return os.cpu_count()

def hyper_thread_enabled() -> bool:
    """
    检查是否启用了超线程。
    :return: 如果启用超线程返回True，否则返回False。
    """
    sibling_file_path = "/sys/devices/system/cpu/cpu0/topology/thread_siblings_list"
    try:
        with open(sibling_file_path, "r") as sibling_file:
            siblings = sibling_file.read().strip()
            return siblings != "0"
    except FileNotFoundError:
        print("Failed to open thread_siblings_list file.")
        return False


def get_cluster_nums() -> int:
    clusters = set()

    cpu_index = 0
    while True:
        cluster_id_path = f"/sys/devices/system/cpu/cpu{cpu_index}/topology/cluster_id"
        if not os.path.exists(cluster_id_path):
            break

        try:
            with open(cluster_id_path, "r") as in_file:
                cluster_id = int(in_file.read().strip())
                clusters.add(cluster_id)
        except Exception as e:
            print(f"Failed to read cluster_id for cpu{cpu_index}: {e}")
            break

        cpu_index += 1

    cluster_count = len(clusters)
    if cluster_count == 0:
        enabled = hyper_thread_enabled()
        if not enabled:
            print("Failed to check hyper-threading status.")
            return 0

        cpu_nums = get_cpu_nums()
        if cpu_nums == 0:
            return 0

        if enabled:
            cluster_count = cpu_nums // 8
        else:
            cluster_count = cpu_nums // 4

    return cluster_count


def print_dev_data_details(dev_data):
    """打印设备数据的详细信息"""
    for dev_data_item in dev_data.iter:
        print(f"metric:{dev_data_item.metric} count:{dev_data_item.count} mode:{dev_data_item.mode}")
        if dev_data_item.mode == kperf.PmuMetricMode.PMU_METRIC_CORE:
            print(f"coreId:{dev_data_item.coreId}")
        elif dev_data_item.mode == kperf.PmuMetricMode.PMU_METRIC_NUMA:
            print(f"numaId:{dev_data_item.numaId}")
        elif dev_data_item.mode == kperf.PmuMetricMode.PMU_METRIC_CLUSTER:
            print(f"clusterId:{dev_data_item.clusterId}")
        elif dev_data_item.mode == kperf.PmuMetricMode.PMU_METRIC_BDF:
            print(f"bdf:{dev_data_item.bdf}")


def test_get_pcie_bdf_list():
    bdf_type = kperf.PmuBdfType.PMU_BDF_TYPE_PCIE
    bdf_list = kperf.device_bdf_list(bdf_type)
    print(kperf.error())
    print(len(bdf_list))
    assert bdf_list is not None, f"Expected non-null bdf_list, but got {bdf_list}"

def test_get_smmu_bdf_list():
    bdf_type = kperf.PmuBdfType.PMU_BDF_TYPE_SMMU
    bdf_list = kperf.device_bdf_list(bdf_type)
    print(kperf.error())
    print(len(bdf_list))
    assert bdf_list is not None, f"Expected non-null bdf_list, but got {bdf_list}"

def test_get_cpu_freq():
    core = 6
    cpu6_freq = kperf.get_cpu_freq(core)
    print(kperf.error())
    assert cpu6_freq != -1, f"Expected non-negative cpu6_freq, but got {cpu6_freq}"

def test_get_cluster_cores():
    clusterId = 1
    cluster_cores = kperf.get_cluster_core(clusterId)
    print(kperf.error())
    assert cluster_cores is not None, f"Expected non-negative cluster_cores, but got {cluster_cores}"
    print(cluster_cores)

def test_get_numa_cores():
    numaId = 1
    numa_cores = kperf.get_numa_core(numaId)
    print(kperf.error())
    assert numa_cores is not None, f"Expected non-negative numa_cores, but got {numa_cores}"
    print(numa_cores)

def test_collect_ddr_bandwidth():
    dev_attr = [
        kperf.PmuDeviceAttr(metric=kperf.PmuDeviceMetric.PMU_DDR_READ_BW)
    ]
    pd = kperf.device_open(dev_attr)
    print(kperf.error())
    assert pd != -1, f"Expected non-negative pd, but got {pd}"
    kperf.enable(pd)
    time.sleep(1)
    kperf.disable(pd)
    ori_data = kperf.read(pd)
    assert len(ori_data) != -1, f"Expected non-negative ori_len, but got {len(ori_data)}"

    dev_data = None
    dev_data = kperf.get_device_metric(ori_data, dev_attr)
    assert len(dev_data) == 4
    assert dev_data[0].numaId == 0
    assert dev_data[0].mode == kperf.PmuMetricMode.PMU_METRIC_NUMA
    print_dev_data_details(dev_data)

def test_collect_l3_latency():
    dev_attr = [
        kperf.PmuDeviceAttr(metric=kperf.PmuDeviceMetric.PMU_L3_LAT)
    ]
    pd = kperf.device_open(dev_attr)
    print(kperf.error())
    assert pd != -1, f"Expected non-negative pd, but got {pd}"
    kperf.enable(pd)
    time.sleep(1)
    kperf.disable(pd)
    ori_data = kperf.read(pd)
    assert len(ori_data) != -1, f"Expected non-negative ori_len, but got {len(ori_data)}"

    dev_data = kperf.get_device_metric(ori_data, dev_attr)
    assert len(dev_data) == get_cluster_nums()
    assert dev_data[0].clusterId == 0
    print_dev_data_details(dev_data)

def test_collect_l3_latency_and_ddr():
    dev_attr = [
        kperf.PmuDeviceAttr(metric=kperf.PmuDeviceMetric.PMU_L3_LAT),
        kperf.PmuDeviceAttr(metric=kperf.PmuDeviceMetric.PMU_DDR_WRITE_BW)
    ]
    pd = kperf.device_open(dev_attr)
    print(kperf.error())
    assert pd != -1, f"Expected non-negative pd, but got {pd}"
    kperf.enable(pd)
    time.sleep(1)
    kperf.disable(pd)
    ori_data = kperf.read(pd)
    assert len(ori_data) != -1, f"Expected non-negative ori_len, but got {len(ori_data)}"

    dev_data = kperf.get_device_metric(ori_data, dev_attr)
    assert len(dev_data) == get_cluster_nums() + 4
    print_dev_data_details(dev_data)


def test_collect_l3_traffic():
    dev_attr = [
        kperf.PmuDeviceAttr(metric=kperf.PmuDeviceMetric.PMU_L3_TRAFFIC)
    ]
    pd = kperf.device_open(dev_attr)
    print(kperf.error())
    assert pd != -1, f"Expected non-negative pd, but got {pd}"
    kperf.enable(pd)
    time.sleep(1)
    kperf.disable(pd)
    ori_data = kperf.read(pd)
    assert len(ori_data) != -1, f"Expected non-negative ori_len, but got {len(ori_data)}"

    dev_data = kperf.get_device_metric(ori_data, dev_attr)
    assert len(dev_data) == get_cpu_nums()
    assert dev_data[0].mode == kperf.PmuMetricMode.PMU_METRIC_CORE
    print_dev_data_details(dev_data)


def test_collect_l3_traffic_and_l3_ref():
    dev_attr = [
        kperf.PmuDeviceAttr(metric=kperf.PmuDeviceMetric.PMU_L3_TRAFFIC),
        kperf.PmuDeviceAttr(metric=kperf.PmuDeviceMetric.PMU_L3_REF)
    ]
    pd = kperf.device_open(dev_attr)
    print(kperf.error())
    assert pd != -1, f"Expected non-negative pd, but got {pd}"
    kperf.enable(pd)
    time.sleep(1)
    kperf.disable(pd)
    ori_data = kperf.read(pd)
    assert len(ori_data) != -1, f"Expected non-negative ori_len, but got {len(ori_data)}"

    dev_data = kperf.get_device_metric(ori_data, dev_attr)
    assert len(dev_data) == get_cpu_nums() * 2
    assert dev_data[0].metric == kperf.PmuDeviceMetric.PMU_L3_TRAFFIC
    assert dev_data[0].mode == kperf.PmuMetricMode.PMU_METRIC_CORE
    assert dev_data[get_cpu_nums()].metric == kperf.PmuDeviceMetric.PMU_L3_REF
    assert dev_data[get_cpu_nums()].mode == kperf.PmuMetricMode.PMU_METRIC_CORE
    print_dev_data_details(dev_data)


def test_collect_l3_latency_and_l3_miss():
    dev_attr = [
        kperf.PmuDeviceAttr(metric=kperf.PmuDeviceMetric.PMU_L3_LAT),
        kperf.PmuDeviceAttr(metric=kperf.PmuDeviceMetric.PMU_L3_MISS)
    ]
    pd = kperf.device_open(dev_attr)
    print(kperf.error())
    assert pd != -1, f"Expected non-negative pd, but got {pd}"
    kperf.enable(pd)
    time.sleep(1)
    kperf.disable(pd)
    ori_data = kperf.read(pd)
    assert len(ori_data) != -1, f"Expected non-negative ori_len, but got {len(ori_data)}"

    dev_data = kperf.get_device_metric(ori_data, dev_attr)
    data_len = get_cpu_nums() + get_cluster_nums()
    assert len(dev_data) == data_len
    print_dev_data_details(dev_data)

def test_get_metric_pcie_bandwidth():
    dev_attr = [
        kperf.PmuDeviceAttr(metric=kperf.PmuDeviceMetric.PMU_PCIE_RX_MRD_BW, bdf="01:03.0"),
        kperf.PmuDeviceAttr(metric=kperf.PmuDeviceMetric.PMU_PCIE_RX_MWR_BW, bdf="01:03.0")
    ]
    pd = kperf.device_open(dev_attr)
    print(kperf.error())
    assert pd != -1, f"Expected non-negative pd, but got {pd}"
    kperf.enable(pd)
    time.sleep(1)
    kperf.disable(pd)
    ori_data = kperf.read(pd)
    assert len(ori_data) != -1, f"Expected non-negative ori_len, but got {len(ori_data)}"

    dev_data = kperf.get_device_metric(ori_data, dev_attr)
    assert len(dev_data) == 2
    print_dev_data_details(dev_data)

def test_get_metric_smmu_transaction():
    bdf_list = kperf.device_bdf_list(kperf.PmuBdfType.PMU_BDF_TYPE_PCIE)
    dev_attr = [
        kperf.PmuDeviceAttr(metric=kperf.PmuDeviceMetric.PMU_SMMU_TRAN, bdf=bdf_list[0]),
        kperf.PmuDeviceAttr(metric=kperf.PmuDeviceMetric.PMU_SMMU_TRAN, bdf=bdf_list[1])
    ]
    pd = kperf.device_open(dev_attr)
    print(kperf.error())
    assert pd != -1, f"Expected non-negative pd, but got {pd}"
    kperf.enable(pd)
    time.sleep(1)
    kperf.disable(pd)
    ori_data = kperf.read(pd)
    assert len(ori_data) != -1, f"Expected non-negative ori_len, but got {len(ori_data)}"

    dev_data = kperf.get_device_metric(ori_data, dev_attr)
    assert len(dev_data) == 2
    print_dev_data_details(dev_data)

if __name__ == '__main__':
    # 提示用户使用pytest 运行测试文件
    print("This is a pytest script. Run it using the 'pytest' command.")
    print("For example: pytest test_*.py -v")
    print("if need print the run log, use pytest test_*.py -s -v")
import time
from ctypes import *
import kperf
from kperf import PointerEvt as evt
import pytest

STR_BUFFER_SIZE = 128
UTF_8 = "utf-8"


@pytest.fixture
def setup_pmu():
    """Fixture to set up and tear down PMU resources."""
    pd_list = []

    def _setup(event_name):
        pmu_attr = kperf.PmuAttr(
            evtList=[event_name],
            sampleRate=1000,
            symbolMode=kperf.SymbolMode.RESOLVE_ELF
        )
        pd = kperf.open(kperf.PmuTaskType.SAMPLING, pmu_attr)
        if pd == -1:
            pytest.fail(f"Failed to open PMU for event {event_name}: {kperf.error()}")
        pd_list.append(pd)
        return pd
    
    yield _setup

    # Cleanup all PMU resources after the test
    for pd in pd_list:
        kperf.disable(pd)
        kperf.close(pd)


class TraceEvt:
    def __init__(self, evtName):
        self.evtName = evtName

    def do_read_field(self, data):
        raise NotImplementedError("Subclasses must implement this method")

    def sample(self, pd):
        print(f"============start to test {self.evtName} ===================")
        err = kperf.enable(pd)
        if err != 0:
            pytest.fail(f"Failed to enable PMU for event {self.evtName}: {kperf.error()}")
        total = 0
        i = 3
        while i > 0:
            time.sleep(1)
            pmu_data = kperf.read(pd)
            for data in pmu_data.iter:
                self.do_read_field(data)
            pmu_data.free()
            i -= 1
        err = kperf.disable(pd)
        if err != 0:
            pytest.fail(f"Failed to disable PMU for event {self.evtName}: {kperf.error()}")


class NetRx(TraceEvt):
    def __init__(self):
        super().__init__(evt.NetNetifRx.event_name)

    def do_read_field(self, data):
        name = create_string_buffer(STR_BUFFER_SIZE)
        kperf.get_field(data, evt.NetApiGroReceiveEntry.name, name)
        skbaddr = c_ulong(0x0)
        kperf.get_field(data, evt.NetNetifRx.skbaddr, pointer(skbaddr))
        len = c_uint(0)
        kperf.get_field(data, evt.NetNetifRx.len, pointer(len))
        print("name={};skbaddr={};len={}".format(name.value.decode(UTF_8), hex(skbaddr.value), len.value))


class NetSkebCopy(TraceEvt):
    def __init__(self):
        super().__init__(evt.SkebSkbCopyDatagramIovec.event_name)
    
    def do_read_field(self, data):
        skbaddr = c_ulong(0x0)
        kperf.get_field(data, evt.SkebSkbCopyDatagramIovec.skbaddr, pointer(skbaddr))
        len = c_uint(0)
        kperf.get_field(data, evt.SkebSkbCopyDatagramIovec.len, pointer(len))
        print("skbaddr={};len={}".format(hex(skbaddr.value), len.value))


class NetNAPI(TraceEvt):
    def __init__(self):
        super().__init__(evt.NetApiGroReceiveEntry.event_name)

    def do_read_field(self, data):
        name = create_string_buffer(STR_BUFFER_SIZE)
        kperf.get_field(data, evt.NetApiGroReceiveEntry.name, name)
        name = name.value.decode(UTF_8)

        napi_id = c_uint(0)
        kperf.get_field(data, evt.NetApiGroReceiveEntry.napi_id, pointer(napi_id))
        napi_id = napi_id.value

        ip_summed = c_ubyte(0)
        kperf.get_field(data, evt.NetApiGroReceiveEntry.ip_summed, pointer(ip_summed))
        ip_summed = ip_summed.value

        protocol = c_ushort(0)
        kperf.get_field(data, evt.NetApiGroReceiveEntry.protocol, pointer(protocol))
        protocol = protocol.value

        mac_header_valid = c_bool(False)
        kperf.get_field(data, evt.NetApiGroReceiveEntry.mac_header_valid, pointer(mac_header_valid))
        mac_header_valid = mac_header_valid.value

        skbaddr = c_ulong(0x0)
        kperf.get_field(data, evt.SkebSkbCopyDatagramIovec.skbaddr, pointer(skbaddr))
        skbaddr = hex(skbaddr.value)

        print("name={} napi_id={} ip_summed={} protocol={} mac_header_valid={} skbaddr={}"
              .format(name, napi_id, ip_summed, protocol, mac_header_valid, skbaddr))


class NetNAPIExp(TraceEvt):
    def __init__(self):
        super().__init__(evt.NetApiGroReceiveEntry.event_name)
    
    def do_read_field(self, data):
        field = kperf.get_field_exp(data, evt.NetApiGroReceiveEntry.name)
        print("field_str={} field_name={} size={} offset={} isSigned={}"
              .format(field.field_name, field.field_str, field.size, field.offset, field.is_signed))


@pytest.mark.parametrize("trace_class", [
    NetRx,
    NetSkebCopy,
    NetNAPI,
    NetNAPIExp
])
def test_trace_events(setup_pmu, trace_class):
    """Test case for all trace events."""
    # Instantiate the trace class and get the event name
    trace_instance = trace_class()
    event_name = trace_instance.evtName

    # Set up PMU resources for the event
    pd = setup_pmu(event_name)

    # Run the sampling process
    trace_instance.sample(pd)

if __name__ == '__main__':
    # 提示用户使用pytest 运行测试文件
    print("This is a pytest script. Run it using the 'pytest' command.")
    print("For example: pytest test_*.py -v")
    print("if need print the run log, use pytest test_*.py -s -v")
import time
from ctypes import *
import kperf
from kperf import PointerEvt as evt

STR_BUFFER_SIZE = 128
UTF_8 = "utf-8"


class TraceEvt:
    def __init__(self, evtName):
        self.evtName = evtName
        self.pd = None

    def do_read_field(self, data):
        pass

    def sample(self):
        print(f"============start to test {self.evtName} ===================")
        evtList = [self.evtName]
        pmu_attr = kperf.PmuAttr(
            evtList=evtList,
            sampleRate=1000,
            symbolMode=kperf.SymbolMode.RESOLVE_ELF
        )
        self.pd = kperf.open(kperf.PmuTaskType.SAMPLING, pmu_attr)
        if self.pd == -1:
            print(kperf.error())
        err = kperf.enable(self.pd)
        if err != 0:
            print(kperf.error())
            return
        total = 0
        i = 3
        while i > 0:
            time.sleep(1)
            pmu_data = kperf.read(self.pd)
            for data in pmu_data.iter:
                self.do_read_field(data)
            pmu_data.free()
            i -= 1
        err = kperf.disable(self.pd)
        if err != 0:
            print("disable pmu err!")
            return
        kperf.close(self.pd)


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


if __name__ == '__main__':
    NetRx().sample()
    NetSkebCopy().sample()
    NetNAPI().sample()
    NetNAPIExp().sample()

package libkperf_test

import "testing"
import "time"
import "os"

import "libkperf/kperf"

func TestCount(t *testing.T) {
	attr := kperf.PmuAttr{EvtList:[]string{"cycles"}, SymbolMode:kperf.ELF}
	fd, err := kperf.PmuOpen(kperf.COUNT, attr)
	if err != nil {
		t.Fatalf("kperf pmuopen counting failed, expect err is nil, but is %v", err)
	}
	kperf.PmuEnable(fd)
	time.Sleep(time.Second)
	kperf.PmuDisable(fd)

	dataVo, err := kperf.PmuRead(fd)
	if err != nil {
		t.Fatalf("kperf pmuread failed, expect err is nil, but is %v", err)
	}

	for _, o := range dataVo.GoData {
		t.Logf("================================Get Couting data success================================")
		t.Logf("count base info comm=%v, evt=%v, pid=%v, tid=%v, coreId=%v, numaId=%v, sockedId=%v", o.Comm, o.Evt, o.Pid, o.Tid, o.CpuTopo.CoreId, o.CpuTopo.NumaId, o.CpuTopo.SocketId)
		t.Logf("count info count=%v, countPercent=%v", o.Count, o.CountPercent)
	}
	kperf.PmuDataFree(dataVo)
	kperf.PmuClose(fd)
}

func TestSample(t *testing.T) {
	attr := kperf.PmuAttr{EvtList:[]string{"cycles"}, SymbolMode:kperf.ELF_DWARF, CallStack:true, SampleRate: 1000, UseFreq:true}
	fd, err := kperf.PmuOpen(kperf.SAMPLE, attr)
	if err != nil {
		t.Fatalf("kperf pmuopen sample failed, expect err is nil, but is %v", err)
	}

	kperf.PmuEnable(fd)
	time.Sleep(time.Second)
	kperf.PmuDisable(fd)

	dataVo, err := kperf.PmuRead(fd)
	if err != nil {
		t.Fatalf("kperf pmuread failed, expect err is nil, but is %v", err)
	}

	for _, o := range dataVo.GoData {
		t.Logf("================================Get Sampling data success================================")
		t.Logf("sample base info comm=%v, evt=%v, pid=%v, tid=%v, coreId=%v, numaId=%v, sockedId=%v", o.Comm, o.Evt, o.Pid, o.Tid, o.CpuTopo.CoreId, o.CpuTopo.NumaId, o.CpuTopo.SocketId)
		t.Logf("sample info count=%v", o.Period)
		for _, s := range o.Symbols {
			t.Logf("symbol info module=%v, symbolName=%v, mangleName=%v, addr=%v, lineNum=%v fileName=%v", s.Module, s.SymbolName, s.MangleName, s.Addr, s.LineNum, s.FileName)
		}
	}
	kperf.PmuDataFree(dataVo)
	kperf.PmuClose(fd)
}

func TestSpe(t *testing.T) {
	attr := kperf.PmuAttr{MinLatency:0x40, CallStack: true, SymbolMode: kperf.ELF_DWARF, SampleRate: 1000, DataFilter: kperf.SPE_DATA_ALL, EvFilter: kperf.SPE_EVENT_RETIRED}
	fd, err := kperf.PmuOpen(kperf.SPE, attr)
	if err != nil {
		t.Fatalf("kperf pmuopen spe failed, expect err is nil, but is %v", err)
	}

	kperf.PmuEnable(fd)
	time.Sleep(time.Second)
	kperf.PmuDisable(fd)

	dataVo, err := kperf.PmuRead(fd)
	if err != nil {
		t.Fatalf("kperf pmuread failed, expect err is nil, but is %v", err)
	}

	for _, o := range dataVo.GoData {
		t.Logf("================================Get Spe data success================================")
		t.Logf("spe base info comm=%v, evt=%v, pid=%v, tid=%v, coreId=%v, numaId=%v, sockedId=%v", o.Comm, o.Evt, o.Pid, o.Tid, o.CpuTopo.CoreId, o.CpuTopo.NumaId, o.CpuTopo.SocketId)
		t.Logf("spe ext info pa=%v, va=%v, event=%v, latency=%v", o.SpeExt.Pa, o.SpeExt.Va, o.SpeExt.Event, o.SpeExt.Lat)
		for _, s := range o.Symbols {
			t.Logf("symbol info module=%v, symbolName=%v, mangleName=%v, addr=%v, lineNum=%v fileName=%v", s.Module, s.SymbolName, s.MangleName, s.Addr, s.LineNum, s.FileName)
		}
	}
	kperf.PmuDataFree(dataVo)
	kperf.PmuClose(fd)
}

func TestGetEvtList(t *testing.T) {
	evtList := kperf.PmuEventList(kperf.CORE_EVENT)
	if len(evtList) == 0 {
		t.Fatalf("core event can't be empty")
	}
	t.Logf("Get Core event list success!")
	for _, v := range evtList {
		t.Logf("%v", v)
	}
}

func TestDumpData(t *testing.T) {
	attr := kperf.PmuAttr{EvtList:[]string{"cycles"}, SymbolMode:kperf.ELF_DWARF, CallStack:true, SampleRate: 1000, UseFreq:true}
	fd, err := kperf.PmuOpen(kperf.SAMPLE, attr)
	if err != nil {
		t.Fatalf("kperf pmuopen sample failed, expect err is nil, but is %v", err)
	}

	kperf.PmuEnable(fd)
	time.Sleep(time.Second)
	kperf.PmuDisable(fd)

	dataVo, err := kperf.PmuRead(fd)
	if err != nil {
		t.Fatalf("kperf pmuread failed, expect err is nil, but is %v", err)
	}

	wd, err := os.Getwd()
	if err != nil {
		t.Fatalf("get pwd dir failed, expect err is nil, but is %v", err)
	}

	dumpFilePath := wd + "/dump.txt"

	dumpErr := kperf.PmuDumpData(dataVo, dumpFilePath, true)
	if dumpErr != nil {
		t.Fatalf("pmu dump data failed, expect err is nil, but is %v", dumpErr)
	}
	t.Logf("dump data to %v success!", dumpFilePath)
}

func TestSysCallTrace(t *testing.T) {
	syscallList := kperf.PmuSysCallFuncList()
	if syscallList == nil {
		t.Fatalf("sys call list is empty")
	} else {
		for _, funcName := range syscallList {
			t.Logf("func name %v", funcName)
		}
	}

	traceAttr := kperf.PmuTraceAttr{Funcs:[]string{"clone", "futex", "clock_gettime"}}

	taskId, err := kperf.PmuTraceOpen(kperf.TRACE_SYS_CALL, traceAttr)
	if err != nil {
		t.Fatalf("pmu trace open failed, expect err is nil, but is %v", err)
	}

	kperf.PmuTraceEnable(taskId)
	time.Sleep(time.Second)
	kperf.PmuTraceDisable(taskId)

	traceList, err := kperf.PmuTraceRead(taskId)

	if err != nil {
		t.Fatalf("pmu trace read failed, expect err is nil, but is %v", err)
	}

	t.Logf("==========================pmu get trace data success==========================")

	for _, v := range traceList.GoTraceData {
		t.Logf("comm=%v, func=%v, elapsedTime=%v, pid=%v, tid=%v, cpu=%v", v.Comm, v.FuncName, v.ElapsedTime, v.Pid, v.Tid, v.Cpu)
	}

	kperf.PmuTraceFree(traceList)
	kperf.PmuTraceClose(taskId)
}

func TestBrbe(t *testing.T) {
	attr := kperf.PmuAttr{EvtList:[]string{"cycles"}, SymbolMode:kperf.ELF_DWARF, CallStack:true, SampleRate: 1000, UseFreq:true, BranchSampleFilter: kperf.KPERF_SAMPLE_BRANCH_ANY}
	fd, err := kperf.PmuOpen(kperf.SAMPLE, attr)
	if err != nil {
		t.Fatalf("kperf pmuopen sample failed, expect err is nil, but is %v", err)
	}

	kperf.PmuEnable(fd)
	time.Sleep(time.Second)
	kperf.PmuDisable(fd)

	dataVo, err := kperf.PmuRead(fd)
	if err != nil {
		t.Fatalf("kperf pmuread failed, expect err is nil, but is %v", err)
	}

	for _, o := range dataVo.GoData {
		t.Logf("================================Get Sampling data success================================")
		t.Logf("sample base info comm=%v, evt=%v, pid=%v, tid=%v, coreId=%v, numaId=%v, sockedId=%v", o.Comm, o.Evt, o.Pid, o.Tid, o.CpuTopo.CoreId, o.CpuTopo.NumaId, o.CpuTopo.SocketId)
		t.Logf("sample info count=%v", o.Period)
		for _, s := range o.Symbols {
			t.Logf("symbol info module=%v, symbolName=%v, mangleName=%v, addr=%v, lineNum=%v fileName=%v", s.Module, s.SymbolName, s.MangleName, s.Addr, s.LineNum, s.FileName)
		}

		for _, b := range o.BranchRecords {
			t.Logf("branch record info fromAddr=%v, toAddr=%v cycles=%v", b.FromAddr, b.ToAddr, b.Cycles)
		}
	}
	kperf.PmuDataFree(dataVo)
	kperf.PmuClose(fd)
}
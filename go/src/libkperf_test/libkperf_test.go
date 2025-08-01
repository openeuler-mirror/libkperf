package libkperf_test

import "testing"
import "time"
import "os"
import "fmt"

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
		t.Logf("================================Get Counting data success================================")
		t.Logf("count base info comm=%v, evt=%v, pid=%v, tid=%v, coreId=%v, numaId=%v, sockedId=%v", o.Comm, o.Evt, o.Pid, o.Tid, o.CpuTopo.CoreId, o.CpuTopo.NumaId, o.CpuTopo.SocketId)
		t.Logf("count info count=%v, countPercent=%v", o.Count, o.CountPercent)
	}
	kperf.PmuDataFree(dataVo)
	kperf.PmuClose(fd)
}

func TestUserAccessCount(t *testing.T) {
	attr := kperf.PmuAttr{EvtList:[]string{"cycles"}, SymbolMode:kperf.ELF, PidList:[]int{0}, CpuList:[]int{-1}, EnableUserAccess:true}
	fd, err := kperf.PmuOpen(kperf.COUNT, attr)
	if err != nil {
		t.Fatalf("kperf pmuopen counting failed, expect err is nil, but is %v", err)
	}
	kperf.PmuEnable(fd)
	dataVo, err := kperf.PmuRead(fd)
	if err != nil {
		t.Fatalf("kperf pmuread failed, expect err is nil, but is %v", err)
	}
	for i, n := 0, 3; i < n; i++ {
		j := 0;
		for k := 0; k < 1000000000; k++ {
			j = j + 1
		}
		dataVo, err = kperf.PmuRead(fd)
		if err != nil {
			t.Fatalf("kperf pmuread failed, expect err is nil, but is %v", err)
		}
		for _, o := range dataVo.GoData {
			t.Logf("================================Get Counting data success================================")
			t.Logf("count base info comm=%v, evt=%v, pid=%v, tid=%v, coreId=%v, numaId=%v, sockedId=%v", o.Comm, o.Evt, o.Pid, o.Tid, o.CpuTopo.CoreId, o.CpuTopo.NumaId, o.CpuTopo.SocketId)
			t.Logf("count info count=%v, countPercent=%v", o.Count, o.CountPercent)
		}
		kperf.PmuDataFree(dataVo)
	}
	kperf.PmuDisable(fd)
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
			t.Logf("symbol info module=%v, symbolName=%v, mangleName=%v, addr=%#x, lineNum=%v fileName=%v", s.Module, s.SymbolName, s.MangleName, s.Addr, s.LineNum, s.FileName)
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
			t.Logf("symbol info module=%v, symbolName=%v, mangleName=%v, addr=%#x, lineNum=%v fileName=%v", s.Module, s.SymbolName, s.MangleName, s.Addr, s.LineNum, s.FileName)
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
		t.Logf("comm=%v, func=%v, elapsedTime=%v, startTs=%v, pid=%v, tid=%v, cpu=%v", v.Comm, v.FuncName, v.ElapsedTime, v.StartTs, v.Pid, v.Tid, v.Cpu)
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
			t.Logf("symbol info module=%v, symbolName=%v, mangleName=%v, addr=%#x, lineNum=%v fileName=%v", s.Module, s.SymbolName, s.MangleName, s.Addr, s.LineNum, s.FileName)
		}

		for _, b := range o.BranchRecords {
			t.Logf("branch record info fromAddr=%#x, toAddr=%#x cycles=%v mispred=%v predicted=%v", b.FromAddr, b.ToAddr, b.Cycles, b.MisPred, b.Predicted)
		}
	}
	kperf.PmuDataFree(dataVo)
	kperf.PmuClose(fd)
}

func TestGetDeviceBdfList(t *testing.T) {
	pcieBdfList, err := kperf.PmuDeviceBdfList(kperf.PMU_BDF_TYPE_PCIE)
	if err != nil {
		t.Fatalf("kperf GetDeviceBdfList failed, expect err is nil, but is %v", err)
	}
	t.Log("Get PCIE bdf list success!")
	for _, v := range pcieBdfList {
		t.Logf("bdf is %v", v)
	}

	smmuBdfList, err := kperf.PmuDeviceBdfList(kperf.PMU_BDF_TYPE_SMMU)
	if err != nil {
		t.Fatalf("kperf GetDeviceBdfList failed, expect err is nil, but is %v", err)
	}
	t.Log("Get SMMU bdf list success!")
	for _, v := range smmuBdfList {
		t.Logf("bdf is %v", v)
	}
}

func TestGetCpuFreq(t *testing.T) {
	coreId := uint(6)
	freq, err := kperf.PmuGetCpuFreq(coreId)
	if err != nil {
		t.Fatalf("kperf PmuGetCpuFreq failed, expect err is nil, but is %v", err)
	}
	t.Logf("coreId %v freq is %v", coreId, freq)
}

func TestGetMetric(t *testing.T) {
	deviceAttrs := []kperf.PmuDeviceAttr{kperf.PmuDeviceAttr{Metric: kperf.PMU_L3_LAT}}
	fd, err := kperf.PmuDeviceOpen(deviceAttrs)
	if err != nil {
		t.Fatalf("kperf PmuDeviceOpen failed, expect err is nil, but is %v", err)
	}
	kperf.PmuEnable(fd)
	time.Sleep(time.Second)
	kperf.PmuDisable(fd)

	dataVo, err := kperf.PmuRead(fd)
	if err != nil {
		t.Fatalf("kperf pmuread failed, expect err is nil, but is %v", err)
	}
	t.Logf("================================Get device data success================================")
	deivceDataVo, err := kperf.PmuGetDevMetric(dataVo, deviceAttrs)
	if err != nil {
		t.Fatalf("kperf PmuGetDevMetric failed, expect err is nil, but is %v", err)
	}
	for _, v := range deivceDataVo.GoDeviceData {
		t.Logf("get device data count=%v coreId=%v, numaId=%v bdf=%v clusterId=%v", v.Count, v.CoreId, v.NumaId, v.Bdf, v.ClusterId)
	}
	kperf.DevDataFree(deivceDataVo)
	kperf.PmuDataFree(dataVo)
	kperf.PmuClose(fd)
}

func TestPmuGetClusterCore(t *testing.T) {
	clusterId := uint(1)
	coreList, err := kperf.PmuGetClusterCore(clusterId)
	if err != nil {
		t.Fatalf("kperf PmuGetClusterCore failed, expect err is nil, but is %v", err)
	}
	for _, v := range coreList {
		t.Logf("coreId has:%v", v)
	}
}

func TestPmuGetNumaCore(t *testing.T) {
	nodeId := uint(0)
	coreList, err := kperf.PmuGetNumaCore(nodeId)
	if err != nil {
		t.Fatalf("kperf PmuGetNumaCore failed, expect err is nil, but is %v", err)
	}
	for _, v := range coreList {
		t.Logf("coreId has:%v", v)
	}
}

func TestPmuGetCpuFreqDetail(t *testing.T) {
	err := kperf.PmuOpenCpuFreqSampling(100)
	if err != nil {
		t.Fatalf("kperf PmuOpenCpuFreqSampling failed, expect err is nil, but is %v", err)
	}

    freqList := kperf.PmuReadCpuFreqDetail()
	for _, v := range freqList {
		t.Logf("cpuId=%v, minFreq=%d, maxFreq=%d, avgFreq=%d", v.CpuId, v.MinFreq, v.MaxFreq, v.AvgFreq)
	}

	kperf.PmuCloseCpuFreqSampling()
}

func TestResolvePmuDataSymbol(t *testing.T) {
	attr := kperf.PmuAttr{EvtList:[]string{"cycles"}, CallStack:true, SampleRate: 1000, UseFreq:true}
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
		if len(o.Symbols) != 0 {
			t.Fatalf("expect symbol data is empty, but is not")
		}
	}

	parseErr := kperf.ResolvePmuDataSymbol(dataVo)
	if parseErr != nil {
		t.Fatalf("kperf ResolvePmuDataSymbol failed, expect err is nil, but is %v", parseErr)
	}

	for _, o := range dataVo.GoData {
		if len(o.Symbols) == 0 {
			t.Fatalf("expect symbol data is not empty, but is empty")
		}
	}
	kperf.PmuDataFree(dataVo)
	kperf.PmuClose(fd)
}

func TestCgroupNameList(t *testing.T) {
    
	cgroupV2File := "/sys/fs/cgroup/cgroup.controllers"
	_, err := os.Stat(cgroupV2File)
	groupPath := "/sys/fs/cgroup"
	if os.IsNotExist(err) {
		groupPath += "/perf_event/testGocgroup" //cgroup v1
	} else {
		groupPath += "/testGocgroup"
	}
	_, statErr := os.Stat(groupPath)
	if statErr != nil {
		err := os.Mkdir(groupPath, 0755)
		if err != nil {
			t.Fatalf("failed to mkdir groupPath named %s, err is %v", groupPath, err)
		}
	}
	cgroupProcPath := groupPath + "/cgroup.procs"
	procFile, procOpenErr := os.OpenFile(cgroupProcPath, os.O_RDWR | os.O_CREATE|os.O_TRUNC, 0644)

	if procOpenErr != nil {
		t.Fatalf("failed to open file %v, err is %v", cgroupProcPath, procOpenErr)
	}

	defer procFile.Close()

	pid := os.Getpid()
	contentStr := fmt.Sprintf("%d\n", pid)
	_, writeErr := procFile.WriteString(contentStr)
	if writeErr != nil {
		t.Fatalf("failed to write pidinfo, err is %v", writeErr)
	}

	attr := kperf.PmuAttr{EvtList:[]string{"cycles"}, SymbolMode:kperf.ELF_DWARF, CallStack:true, SampleRate: 1000, UseFreq:true, CgroupNameList:[]string{"testGocgroup"}}
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
		if "testGocgroup" != o.CgroupName {
			t.Fatalf("kperf pmuread cgroupName err")
		}
	}
	kperf.PmuDataFree(dataVo)
	kperf.PmuClose(fd)
}
/******************************************************************************
 * Copyright (c) Huawei Technologies Co., Ltd. 2024. All rights reserved.
 * libkperf licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *     http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR
 * PURPOSE.
 * See the Mulan PSL v2 for more details.
 * Author: Mr.Li
 * Create: 2025-03-28
 * Description: libkperf go language interface 
 ******************************************************************************/

package kperf

/*
#cgo CFLAGS: -I ../include
#cgo LDFLAGS: -L ../lib -lkperf -lsym

#include "pmu.h"
#include "symbol.h"
#include "pcerrc.h"
#include <stdlib.h>
#include <string.h>

struct SpeDataExt {
 	unsigned long pa;    // physical address
	unsigned long va;    // virtual address
	unsigned long event; // event id, which is a bit map of mixed events, event bit is defined in SPE_EVENTS.
	unsigned short lat; // latency, Number of cycles between the time when an operation is dispatched and the time when the operation is executed.
};

void SetPeriod(struct PmuAttr* attr, unsigned period) {
	attr->period = period;
}

void SetFreq(struct PmuAttr* attr, unsigned freq) {
	attr->freq = freq;
}

void SetCallStack(struct PmuAttr* attr, unsigned callStack) {
	attr->callStack = callStack;
}

void SetExcludeUser(struct PmuAttr* attr, unsigned excludeUser) {
	attr->excludeUser = excludeUser;
}

void SetExcludeKernel(struct PmuAttr* attr, unsigned excludeKernel) {
	attr->excludeKernel = excludeKernel;
}

void SetUseFreq(struct PmuAttr* attr, unsigned useFreq) {
	attr->useFreq = useFreq;
}

void SetIncludeNewFork(struct PmuAttr* attr, unsigned includeNewFork) {
	attr->includeNewFork = includeNewFork;
}

void SetBlockedSample(struct PmuAttr* attr, unsigned blockedSample) {
	attr->blockedSample = blockedSample;
}

struct PmuData* IPmuRead(int fd, int* len) {
	struct PmuData* pmuData = NULL;
	*len = PmuRead(fd, &pmuData);
	return pmuData;
}

struct BranchSampleRecord* IPmuGetBranchRecord(struct PmuData* data, int *nr) {
	if (data->ext != NULL) {
		*nr = data->ext->nr;
		return data->ext->branchRecords;
	}
	*nr = 0;
	return NULL;
}

void IPmuGetSpeDataExt(struct PmuData* data, struct SpeDataExt* speExt) {
	if (data->ext != NULL) {
		speExt->pa = data->ext->pa;
		speExt->va = data->ext->va;
		speExt->event = data->ext->event;
		speExt->lat = data->ext->lat;
	}
}

struct PmuTraceData* IPmuTraceRead(int taskId, int *len) {
	struct PmuTraceData* traceData = NULL;
	*len = PmuTraceRead(taskId, &traceData);
	return traceData;
}

size_t GetPmuAttrSize() {
	return sizeof(struct PmuAttr);
}

size_t GetPmuTraceAttrSize() {
	return sizeof(struct PmuTraceAttr);
}

*/
import "C"
import "errors"
import "unsafe"
import "reflect"
import "libkperf/sym"

// pmu task type, for PmuOpen collectType
var (
	COUNT C.enum_PmuTaskType = C.COUNTING
	SAMPLE C.enum_PmuTaskType = C.SAMPLING
	SPE C.enum_PmuTaskType = C.SPE_SAMPLING
)

// pmu event type, for PmuEventList interface
var (
	CORE_EVENT C.enum_PmuEventType = C.CORE_EVENT
	UNCORE_EVENT C.enum_PmuEventType = C.UNCORE_EVENT
	TRACE_EVENT C.enum_PmuEventType = C.TRACE_EVENT
	ALL_EVENT C.enum_PmuEventType = C.ALL_EVENT
)

// symbol mode, for pmuAttr.SymbolMode
var (
	ELF C.enum_SymbolMode = C.RESOLVE_ELF
	ELF_DWARF C.enum_SymbolMode = C.RESOLVE_ELF_DWARF
)

// spe filter, for pmuAttr.DataFilter
var (
	TS_ENABLE C.enum_SpeFilter = C.TS_ENABLE
	PA_ENABLE C.enum_SpeFilter = C.PA_ENABLE
	PCT_ENABLE C.enum_SpeFilter = C.PCT_ENABLE
	JITTER C.enum_SpeFilter = C.JITTER
	BRANCH_FILTER C.enum_SpeFilter = C.BRANCH_FILTER
	LOAD_FILTER C.enum_SpeFilter = C.LOAD_FILTER
	STORE_FILTER C.enum_SpeFilter = C.STORE_FILTER
	SPE_DATA_ALL C.enum_SpeFilter = C.SPE_DATA_ALL
)

// spe event filter, for pmuAttr.EvFilter
var (
	SPE_EVENT_NONE C.enum_SpeEventFilter = C.SPE_EVENT_NONE
	SPE_EVENT_RETIRED C.enum_SpeEventFilter = C.SPE_EVENT_RETIRED
	SPE_EVENT_L1DMISS C.enum_SpeEventFilter = C.SPE_EVENT_L1DMISS
	SPE_EVENT_TLB_WALK C.enum_SpeEventFilter = C.SPE_EVENT_TLB_WALK
	SPE_EVENT_MISPREDICTED C.enum_SpeEventFilter = C.SPE_EVENT_MISPREDICTED
)

// branch sample type, for pmuAttr.BranchSampleFilter
var (
	/**
     * The first part of the value is the privilege level,which is a combination of 
     * one of the values listed below. If the user does not set privilege level explicitly,
     * the kernel will use the event's privilege level.Event and branch privilege levels do
     * not have to match.
     */
	 KPERF_SAMPLE_BRANCH_USER       uint64   = 1 << 0
	 KPERF_SAMPLE_BRANCH_KERNEL     uint64   = 1 << 1
	 KPERF_SAMPLE_BRANCH_HV         uint64   = 1 << 2
	 // In addition to privilege value , at least one or more of the following bits must be set.
	 KPERF_SAMPLE_BRANCH_ANY        uint64   = 1 << 3
	 KPERF_SAMPLE_BRANCH_ANY_CALL   uint64   = 1 << 4
	 KPERF_SAMPLE_BRANCH_ANY_RETURN uint64   = 1 << 5
	 KPERF_SAMPLE_BRANCH_IND_CALL   uint64   = 1 << 6
	 KPERF_SAMPLE_BRANCH_ABORT_TX   uint64   = 1 << 7
	 KPERF_SAMPLE_BRANCH_IN_TX      uint64   = 1 << 8
	 KPERF_SAMPLE_BRANCH_NO_TX      uint64   = 1 << 9
	 KPERF_SAMPLE_BRANCH_COND       uint64   = 1 << 10
	 KPERF_SAMPLE_BRANCH_CALL_STACK uint64   = 1 << 11
	 KPERF_SAMPLE_BRANCH_IND_JUMP   uint64   = 1 << 12
	 KPERF_SAMPLE_BRANCH_CALL       uint64   = 1 << 13
	 KPERF_SAMPLE_BRANCH_NO_FLAGES  uint64   = 1 << 14
	 KPERF_SAMPLE_BRANCH_NO_CYCLES  uint64   = 1 << 15
	 KPERF_SAMPLE_BRANCH_TYPE_SAVE  uint64   = 1 << 16
)

// trace type, for pmuTraceOpen
var (
	TRACE_SYS_CALL C.enum_PmuTraceType = C.TRACE_SYS_CALL
)
	
var fdModeMap map[int]C.enum_PmuTaskType = make(map[int]C.enum_PmuTaskType)

type PmuAttr struct {
	EvtList []string                   // evt list
	PidList []int                      // process id list
	CpuList []int                      // cpu id list
	EvtAttr []int                      // group id list
	SampleRate uint32                  // sample rate, if useFreq=true, set the freq=SampleRate
	UseFreq bool                       // Use sample frequency or not, if set to true, used frequency, otherwise, used period
	ExcludeUser bool                   // Don't count user
	ExcludeKernel bool                 // Don't count kernel
	SymbolMode C.enum_SymbolMode       // This indicates how to analyze symbols of samples.Refer to comments of SymbolMode
	CallStack bool                     // This indicates whether to collect whole callchains or only top frame
	DataFilter C.enum_SpeFilter        // Spe Data Filter.Refer to comments of SpeFilter 
	EvFilter C.enum_SpeEventFilter     // Spe Event filter.Refer to comments of SpeEventFilter
	MinLatency uint64                  // Collect only smaples with latency or higher
	IncludeNewFork bool                // enable it you can get the new child thread count, only in couting mode
	BranchSampleFilter uint64          // if the filering mode is set, branch_sample_stack data is collected in sampling mode
	BlockedSample bool                 // This indicates whether the blocked sample mode is enabled. In this mode, both on Cpu and off Cpu data is collectd
}

type CpuTopolopy struct {
	CoreId int         // cpu core id
	NumaId int		   // numa id
	SocketId int       // socket id
}

type SpeDataExt struct {
	Pa uint64          // physical address
	Va uint64		   // virtual address
	Event uint64       // event id, which is a bit map of mixed events, events bits 
	Lat uint16         // latency, Number of cycles between the time when an operation is dispatched and the time when the operation is executed
}

type BranchSampleRecord struct {
	FromAddr uint64    // from addr
	ToAddr uint64      // to addr
	Cycles uint64      // cycles
}

type PmuData struct {
	Evt string 						   // event name
	Ts uint64						   // time stamp. uint: ns
	Pid int				               // process id
	Tid int							   // thread id
	Cpu int						   // cpu id
	Comm string						   // process command 
	Period uint64                      // sample period
	Count uint64					   // event count. Only available for counting
	CountPercent float64               // event count Percent. when count = 0, countPercent = -1; Only available for counting
	CpuTopo CpuTopolopy 			   // cpu topolopy
	Symbols []sym.Symbol			   // symbol list
 	BranchRecords []BranchSampleRecord // branch record list
	SpeExt SpeDataExt                  // SPE data

	cPmuData C.struct_PmuData          // C.struct_PmuData
}

type PmuDataVo struct {
	GoData []PmuData            // PmuData list
	cData *C.struct_PmuData	    // Pointer to PmuData in inferface C
	fd int		                // fd
}

type SampleRawField struct {
	FieldName string   // the field name of this field
	FieldStr string    // the field line
	Offset uint32	   // the data offset
	Size uint32		   // the field size	
	IsSigned uint32    // is signed or is unsigned
}

type PmuTraceAttr struct {
	Funcs []string 	// system call function list, if funcs is empty, it will collect all the system call function elapsed time
	PidList []int   // pid list 
	CpuList []int   // cpu id list
}

// PmuTraceData info
type PmuTraceData struct {
	FuncName string        // function name
	ElapsedTime float64    // elapsed time
	Pid int				   // process id
	Tid int                // thread id
	Cpu int			   // cpu id
	Comm string			   // process command
}

// PmuTraceDataVo read from PmuTraceRead
type PmuTraceDataVo struct {
	GoTraceData []PmuTraceData           // PmuTraceData list
	cTraceData *C.struct_PmuTraceData	 // Pointer to PmuData in interface C
}

// Initialize the collection target
// On success, a task id is returned which is the unique identity for the task
// On error, -1 is returned
// Refer to comments of PmuAttr for details about settings
// param collectType task type
// param attr settings of the current task
// return task id
func PmuOpen(collectType C.enum_PmuTaskType, attr PmuAttr) (int, error) {
	attrSize := C.GetPmuAttrSize()
	ptr := C.malloc(C.size_t(int(attrSize)))
	if ptr == nil {
		return -1, errors.New("malloc failed")
	}
	defer C.free(ptr)
	C.memset(ptr, 0, attrSize)

	cAttr := (*C.struct_PmuAttr)(ptr)
	evtLen := len(attr.EvtList)
	if evtLen > 0 {
		evtList := make([]*C.char, evtLen)
		for i, evt := range attr.EvtList {
			evtList[i] = C.CString(evt)
			defer C.free(unsafe.Pointer(evtList[i]))
		}
		cAttr.numEvt = C.uint32_t(evtLen)
		cAttr.evtList = &evtList[0]
	}

	pidLen := len(attr.PidList)
	if pidLen > 0 {
		 pidList := make([]C.int, pidLen)
		 for i, pid := range attr.PidList {
			pidList[i] = C.int(pid)
		 }
		 cAttr.pidList = &pidList[0]
		 cAttr.numPid = C.uint32_t(pidLen)
	}

	cpuLen := len(attr.CpuList) 
	if cpuLen > 0 {
		cpuList := make([]C.int, cpuLen)
		for i, cpu := range attr.CpuList {
			cpuList[i] = C.int(cpu)
		}
		cAttr.cpuList = &cpuList[0]
		cAttr.numCpu = C.uint32_t(cpuLen)
	}

	evtAttrLen := len(attr.EvtAttr)
	if evtAttrLen > 0 {
		evtAttrList := make([]C.struct_EvtAttr, evtAttrLen)
		for i, groupId := range attr.EvtAttr {
			evtAttrList[i] = C.struct_EvtAttr{C.int(groupId)}
		}
		cAttr.evtAttr = &evtAttrList[0]
	}

	if attr.UseFreq {
		C.SetUseFreq(cAttr, C.uint(1))
		C.SetFreq(cAttr, C.uint(attr.SampleRate))
	} else {
		C.SetPeriod(cAttr, C.uint(attr.SampleRate))
	}

	if attr.IncludeNewFork {
		C.SetIncludeNewFork(cAttr, C.uint(1))
	}

	if attr.CallStack {
		C.SetCallStack(cAttr, C.uint(1))
	}

	if attr.ExcludeKernel {
		C.SetExcludeKernel(cAttr, C.uint(1))
	}

	if attr.ExcludeUser {
		C.SetExcludeUser(cAttr, C.uint(1))
	}

	if attr.BlockedSample {
		C.SetBlockedSample(cAttr, C.uint(1))
	}

	if attr.DataFilter > 0 {
		cAttr.dataFilter = attr.DataFilter
	}

	if attr.EvFilter > 0 {
		cAttr.evFilter = attr.EvFilter
	}

	if attr.BranchSampleFilter > 0 {
		cAttr.branchSampleFilter = C.ulong(attr.BranchSampleFilter)
	}

	if attr.SymbolMode > 0 {
		cAttr.symbolMode = attr.SymbolMode
	}

	fd := C.PmuOpen(collectType, cAttr)

	if int(fd) == -1 {
		return -1, errors.New(C.GoString(C.Perror()))
	}
	fdModeMap[int(fd)] = collectType
	return int(fd), nil
}

// Query all available event from system
// param eventType type of event chosen by user
// param numEvt length of event list
// return event list
func PmuEventList(eventType C.enum_PmuEventType) []string {
	numEvt := C.uint(0)
	evtList := C.PmuEventList(eventType, &numEvt)
	if int(numEvt) == 0 {
		return nil
	}
	goEvtList := make([]string, int(numEvt))
	ptr := unsafe.Pointer(evtList)
	slice := reflect.SliceHeader{
		Data: uintptr(ptr),
		Len: int(numEvt),
		Cap: int(numEvt),
	}
	stringList := *(*[]*C.char)(unsafe.Pointer(&slice))
	for i := 0; i < int(numEvt); i++ {
		goEvtList[i] = C.GoString(stringList[i])
	}
	return goEvtList
}

// Enable counting or sampling of task <pd>.
// On success, nil is returned.
// On error, error is returned.
// param pd task id
// return error
func PmuEnable(fd int) error {
	rs := C.PmuEnable(C.int(fd))
	if int(rs) != 0 {
		return errors.New(C.GoString(C.Perror()))
	}
	return nil
}

// Disable counting or sampling of task <pd>.
// On success, nil is returned.
// On error, error is returned.
// param pd task id
// return err
func PmuDisable(fd int) error {
	rs := C.PmuDisable(C.int(fd))
	if int(rs) != 0 {
		return errors.New(C.GoString(C.Perror()))
	}
	return nil
}

// Collect <milliseconds> milliseconds. If <milliseconds> is equal to - 1 and the PID list is not empty, the collection
// is performed until all processes are complete.
// param milliseconds
// param interval internal collect period. Unit: millisecond. Must be larger than or equal to 100
// return error
func PmuCollect(fd int, milliseconds int, interval uint32) error {
	rs := C.PmuCollect(C.int(fd), C.int(milliseconds), C.uint(interval))
	if int(rs) != 0 {
		return errors.New(C.GoString(C.Perror()))
	}
	return nil
}

// Similar to <PmuCollect>, and <PmuCollectV> accepts multiple pds.
// param milliseconds
// return error
func PmuCollectV(fds []int, milliseconds int) error {
	fdSize := len(fds)
	if fdSize == 0 {
		return errors.New("fds must not be empty")
	}
	var cFds []C.int = make([]C.int, fdSize)
	for i, fd := range fds {
		cFds[i] = C.int(fd)
	}
	inputFdPointer := (*C.int)(unsafe.Pointer(&cFds[0]))
	rs := C.PmuCollectV(inputFdPointer, C.uint(fdSize), C.int(milliseconds))
	if int(rs) != 0 {
		return errors.New(C.GoString(C.Perror()))
	}
	return nil
}


// Free PmuData pointer.
// param pmuDataVo
func PmuDataFree(data PmuDataVo) {
	C.PmuDataFree(data.cData)
}

// Close task with id <pd>
// After PmuClose is called, all pmu data related to the task become invalid
// param pd task id
func PmuClose(fd int) {
	if fd <= 0 {
		return
	}
	C.PmuClose(C.int(fd))
	_, modeOk := fdModeMap[fd]
	if !modeOk {
		return
	}
	delete(fdModeMap, fd)
}

// stop a sampling task in asynchronous mode
// param pd pmu descriptor.
func PmuStop(fd int) {
	if fd <= 0 {
		return
	}
	C.PmuStop(C.int(fd))
}


// Collect data
// Pmu data are collected starting from the last PmuEnable or PmuRead
// That is to say, for COUNTING, counts of all pmu event are reset to zero in PmuRead
// For SAMPLING and SPE_SAMPLING, samples collected are started from the last PmuEnable or PmuRead
// On success, PmuDataVo is returned
// param pd task id
// return PmuDataVo and error
func PmuRead(fd int) (PmuDataVo, error) {
	pmuDataVo := PmuDataVo{}
	dataLen := C.int(0)
	cDatas := C.IPmuRead(C.int(fd), &dataLen)
	if int(dataLen) == 0 {
		return pmuDataVo, errors.New("PmuData is empty")
	}
	goDatas := transferCPmuDataToGoData(cDatas, int(dataLen), fd)
	pmuDataVo.GoData = goDatas
	pmuDataVo.cData = cDatas
	pmuDataVo.fd = fd
	return pmuDataVo, nil
}

// Append data list <fromData> to another data list <*toData>
// The pointer of data list <*toData> will be refreshed after this function is called
// On success, nil is returned, to PmuDataVo GoData changed.
// On error, error is returned
// from data list which will be copied to <*toData>
// to pointer to target data list. <*to> can't be nil
// return error
func PmuAppendData(from *PmuDataVo, to *PmuDataVo) error {
	if len(to.GoData) == 0 {
		return errors.New("to PmuDataVo.GoData can't be empty")
	}
	cDataLen := C.PmuAppendData(from.cData, &to.cData)
	if int(cDataLen) == 0 {
		return errors.New(C.GoString(C.Perror()))
	}
	for _, v := range from.GoData {
		to.GoData = append(to.GoData, v)
	}
	return nil
}

// Dump pmu data to a specific file
// If file exists, then data will be appended to file
// If file does not exist, then file will be created
// Dump format: comm pid tid cpu period evt count addr symbolName offset module fileName lineNum
// param dataVo PmuDataVo
// param filepath path of the output file
// param dumpDwf if true, source file and line number of symbols will not be dumped, otherwise, they will be dumped to file
func PmuDumpData(dataVo PmuDataVo, filePath string, dumpDwf bool) error {
	if len(dataVo.GoData) == 0 {
		return errors.New("dataVo can't be empty")
	}
	cDumpDwfFlag := C.int(0)
	if dumpDwf {
		cDumpDwfFlag = C.int(1)
	}
	cFilePath := C.CString(filePath)
	rs := C.PmuDumpData(dataVo.cData, C.uint(len(dataVo.GoData)), cFilePath, cDumpDwfFlag)
	if int(rs) != 0 {
		return errors.New(C.GoString(C.Perror()))
	}
	return nil
}

// Initialize the trace collection target
// On success, a trace collect task id is returned which is the unique identity for the task
// On error, -1, error is returned
// Refer to comments of PmuTraceAttr for details about settings
// param PmuTraceType task type
// param PmuTraceAttr settings of the current trace collect task
// return trace collect task id
func PmuTraceOpen(traceType C.enum_PmuTraceType, traceAttr PmuTraceAttr) (int, error) {
	attrSize := C.GetPmuTraceAttrSize()
	ptr := C.malloc(C.size_t(int(attrSize)))
	if ptr == nil {
		return -1, errors.New("malloc failed")
	}
	defer C.free(ptr)
	C.memset(ptr, 0, attrSize)
	cAttr := (*C.struct_PmuTraceAttr)(ptr)
	pidLen := len(traceAttr.PidList)
	if pidLen > 0 {
		pidList := make([]C.int, pidLen)
		for i, pid := range(traceAttr.PidList) {
			pidList[i] = C.int(pid)
		}
		cAttr.pidList = &pidList[0]
		cAttr.numPid  = C.uint32_t(pidLen)
	}

	cpuLen := len(traceAttr.CpuList)
	if cpuLen > 0 {
		cpuList := make([]C.int, cpuLen)
		for i, cpu := range(traceAttr.CpuList) {
			cpuList[i] = C.int(cpu)
		}
		cAttr.cpuList = &cpuList[0]
		cAttr.numCpu  = C.uint32_t(cpuLen)
	}

	funcLen := len(traceAttr.Funcs)
	if funcLen > 0 {
		funcs := make([]*C.char, funcLen)
		for i, funcName := range traceAttr.Funcs {
			funcs[i] = C.CString(funcName)
			defer C.free(unsafe.Pointer(funcs[i]))
		}
		cAttr.numFuncs = C.uint32_t(funcLen)
		cAttr.funcs   = &funcs[0]
	}

	taskId := C.PmuTraceOpen(traceType, cAttr)
	if int(taskId) == -1 {
		return -1, errors.New(C.GoString(C.Perror()))
	}
	return int(taskId), nil
}

// Enable trace collection of task <pd>
// On success, nil is returned.
// On error, -1 is returned.
// param pd trace collect task id
// return error code
func PmuTraceEnable(taskId int) error {
	rs := C.PmuTraceEnable(C.int(taskId))
	if int(rs) != 0 {
		return errors.New(C.GoString(C.Perror()))
	}
	return nil
}

// Disable trace collection of task <pd>
// On success, nil is returned
// On error, error is returned
// param pd trace collect task id
// return error code
func PmuTraceDisable(taskId int) error {
	rs := C.PmuTraceDisable(C.int(taskId))
	if int(rs) != 0 {
		return errors.New(C.GoString(C.Perror()))
	}
	return nil
}

// Collect data.
// Pmu trace data are collected starting from the last PmuTraceEnable or PmuTraceRead
// On success, PmuTraceDataVo is returned
// param pd trace collect task id
// param PmuTraceDataVo pmu trace data
// return PmuTraceDataVo and error
func PmuTraceRead(taskId int) (PmuTraceDataVo, error) {
	res := PmuTraceDataVo{}
	traceLen := C.int(0)
	cTraceData := C.IPmuTraceRead(C.int(taskId), &traceLen)
	if int(traceLen) == 0 {
		return res, errors.New("trace data list is empty")
	}
	ptr := unsafe.Pointer(cTraceData)
	slice := reflect.SliceHeader {
		Data: uintptr(ptr),
		Len:  int(traceLen),
		Cap:  int(traceLen),
	}
	cDataList := *(*[]C.struct_PmuTraceData)(unsafe.Pointer(&slice))
	goTraceData := make([]PmuTraceData, int(traceLen))
	for i, v := range cDataList {
		goTraceData[i] = PmuTraceData{FuncName:C.GoString(v.funcs), ElapsedTime:float64(v.elapsedTime), Pid:int(v.pid), Tid: int(v.tid), Cpu: int(v.cpu), Comm: C.GoString(v.comm)}
	}
	res.GoTraceData = goTraceData
	res.cTraceData  = cTraceData
	return res, nil
}

// Close task with id <pd>.
// After PmuTraceClose is called, all pmu trace data related to the task become invalid
// param collect task id
func PmuTraceClose(taskId int) {
	C.PmuTraceClose(C.int(taskId))
}


// Free PmuTraceData pointer.
// param PmuTraceDataVo
func PmuTraceFree(data PmuTraceDataVo) {
	C.PmuTraceDataFree(data.cTraceData)
}

// Query all available system call function from system
// return system call function list
func PmuSysCallFuncList() []string {
	funcNum := C.uint32_t(0)
	funcs := C.PmuSysCallFuncList(&funcNum)
	if uint32(funcNum) == 0 {
		return nil
	}
	ptr := unsafe.Pointer(funcs)
	slice := reflect.SliceHeader{
		Data: uintptr(ptr),
		Len:  int(funcNum),
		Cap:  int(funcNum),
	}

	stringList := *(*[]*C.char)(unsafe.Pointer(&slice))
	goFuncList := make([]string, int(funcNum))
	for i, v := range stringList {
		goFuncList[i] = C.GoString(v)
	}
	return goFuncList
}

// Get the SampleRawField explation.
// param fieldName
func (data PmuData) GetRawFieldExp(fieldName string) (SampleRawField, error) {
	cFieldName := C.CString(fieldName)
	defer C.free(unsafe.Pointer(cFieldName))
	rs := C.PmuGetFieldExp(data.cPmuData.rawData, cFieldName)
	if rs == nil {
		return SampleRawField{}, errors.New(C.GoString(C.Perror()))
	}
	srf := SampleRawField{FieldName: C.GoString(rs.fieldName), FieldStr: C.GoString(rs.fieldStr), Offset: uint32(rs.offset), Size: uint32(rs.size), IsSigned: uint32(rs.isSigned)}
	return srf, nil	
}

// Get the pointer trace event raw field
// param fieldName the filed name of one field
// param value  the pointer of value
// return nil success otherwise failed
func (data PmuData) GetField(fieldName string, valuePointer unsafe.Pointer) error {
	srf, err := data.GetRawFieldExp(fieldName)
	if err != nil {
		return err
	}

	cFieldName := C.CString(fieldName)
	defer C.free(unsafe.Pointer(cFieldName))

	rs := C.PmuGetField(data.cPmuData.rawData, cFieldName, valuePointer, C.uint(srf.Size))
	if int(rs) != 0 {
		return errors.New(C.GoString(C.Perror()))
	}

	return nil
}

func transferCPmuDataToGoData(cPmuData *C.struct_PmuData, dataLen int, fd int) []PmuData {
	ptr := unsafe.Pointer(cPmuData)
	slice := reflect.SliceHeader {
		Data: uintptr(ptr),
		Len: dataLen,
		Cap: dataLen,
	}
	cPmuDatas := *(*[]C.struct_PmuData)(unsafe.Pointer(&slice))
	goDatas := make([]PmuData, dataLen)
	for i := 0; i < dataLen; i++ {
		dataObj := cPmuDatas[i]
		goDatas[i].Comm = C.GoString(dataObj.comm)
		goDatas[i].Evt = C.GoString(dataObj.evt)
		goDatas[i].Pid = int(dataObj.pid)
		goDatas[i].Tid = int(dataObj.tid)
		goDatas[i].Ts = uint64(dataObj.ts)
		goDatas[i].Period = uint64(dataObj.period)
		goDatas[i].Count = uint64(dataObj.count)
		goDatas[i].CountPercent = float64(dataObj.countPercent)
		goDatas[i].Cpu = int(dataObj.cpu)
		if dataObj.cpuTopo != nil {
			goDatas[i].CpuTopo = CpuTopolopy{CoreId: int(dataObj.cpuTopo.coreId), NumaId: int(dataObj.cpuTopo.numaId), SocketId: int(dataObj.cpuTopo.socketId)}
		}

		if dataObj.ext != nil {
			if fdModeMap[fd] == SPE {
				goDatas[i].appendSpeExt(dataObj)
			} else {
				goDatas[i].appendBranchRecords(dataObj)
			}
		}

		if dataObj.stack != nil {
			goDatas[i].appendSymbols(dataObj)
		}
		goDatas[i].cPmuData = dataObj
	}
	return goDatas
}

func (data *PmuData) appendSpeExt(pmuData C.struct_PmuData) {
	speDataExt := C.struct_SpeDataExt{}
	C.IPmuGetSpeDataExt(&pmuData, &speDataExt)
	data.SpeExt = SpeDataExt{Pa:uint64(speDataExt.pa), Va: uint64(speDataExt.va), Event: uint64(speDataExt.event), Lat: uint16(speDataExt.lat)}
}

func (data *PmuData) appendSymbols(pmuData C.struct_PmuData) {
	if pmuData.stack == nil {
		return
	}
	symbols := make([]sym.Symbol, 0, 10)
	curStack := pmuData.stack
	for curStack != nil {
		cSymbol := curStack.symbol
		if cSymbol != nil {
			oneSymbol := sym.Symbol{Addr:uint64(cSymbol.addr),
				 Module:C.GoString(cSymbol.module),
				 SymbolName:C.GoString(cSymbol.symbolName),
				 MangleName:C.GoString(cSymbol.mangleName),
				 FileName:C.GoString(cSymbol.fileName),
				 LineNum:uint32(cSymbol.lineNum),
				 Offset:uint64(cSymbol.offset),
				 CodeMapEndAddr:uint64(cSymbol.codeMapEndAddr),
				 CodeMapAddr:uint64(cSymbol.codeMapAddr)}
			symbols = append(symbols, oneSymbol)
		}
		curStack = curStack.next
	}
	data.Symbols = symbols
}

func (data *PmuData) appendBranchRecords(pmuData C.struct_PmuData) {
	nr := C.int(0)
	records := C.IPmuGetBranchRecord(&pmuData, &nr)
	if int(nr) == 0 {
		return
	}
	branchList := make([]BranchSampleRecord, int(nr))
	ptr := unsafe.Pointer(records)
	slice := reflect.SliceHeader {
		Data: uintptr(ptr),
		Len:  int(nr),
		Cap:  int(nr),
	}
	branchRecords := *(*[]C.struct_BranchSampleRecord)(unsafe.Pointer(&slice))
	for i := 0; i < int(nr); i++ {
		branchList[i].FromAddr = uint64(branchRecords[i].fromAddr)
		branchList[i].ToAddr   = uint64(branchRecords[i].toAddr)
		branchList[i].Cycles   = uint64(branchRecords[i].cycles)
	}
	data.BranchRecords = branchList
}

/******************************************************************************
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 * libkperf licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *     http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR
 * PURPOSE.
 * See the Mulan PSL v2 for more details.
 * Author: Mr.Li
 * Create: 2025-04-09
 * Description: Pmu data hotspot analysis module.
 * Current capability: Analyze the original data of performance monitoring unit, and compute the hotspot data.
 ******************************************************************************/
package main 

import "os"
import "os/exec"
import "fmt"
import "errors"
import "sort"
import "strings"
import "time"
import "strconv"
import "syscall"

import "libkperf/kperf"
import "libkperf/sym"

func printUsage() {
	fmt.Println("Usage: ./pmu_hotspot_of_go <interval> <count> <blockedSample> <process name>")
    fmt.Println(" interval: sample interval, unit s (must be a positive number)")
    fmt.Println(" count: sample print count (must be a positive integer)")
    fmt.Println(" blockedSample: blockedSample flag, 1 for enable, 0 for disable")
    fmt.Println(" process name: process path or input process number")
    fmt.Println(" example: ./pmu_hotspot_of_go 0.1 10 0 ./process")
    fmt.Println(" example: ./pmu_hotspot_of_go 1 100 1 ./process")
	fmt.Println(" example: ./pmu_hotspot_of_go 1 100 1 <pid>")
}

var GlobalPeriod uint64 = 0

func startProc(cmdName string) (int, error) {
	cmd := exec.Command(cmdName)
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	err := cmd.Start()
	if err != nil {
		return -1, err
	}
	return cmd.Process.Pid, nil
}

func processSymbol(symbol sym.Symbol) string {
	if len(symbol.SymbolName) > 0 && symbol.SymbolName != "UNKNOWN" {
		return symbol.SymbolName
	} else if (symbol.CodeMapAddr > 0) {
		return fmt.Sprintf("0x%x", symbol.CodeMapAddr)
	} else {
		return fmt.Sprintf("0x%x", symbol.Addr)
	}
}

func comparePmuData(p1 kperf.PmuData, p2 kperf.PmuData) bool {
	if p1.Evt != p2.Evt {
		return false
	}

	syms_p1 := p1.Symbols
	syms_p2 := p2.Symbols

	if len(syms_p1) != len(syms_p2) {
		return false
	}

	for i, v := range(syms_p1) {
		sym_a := processSymbol(v)
		sym_b := processSymbol(syms_p2[i])
		if sym_a != sym_b {
			return false
		}
	}

	return true
}

func GetPmuDataHotSpot(vo kperf.PmuDataVo) []kperf.PmuData {
	if len(vo.GoData) == 0 {
		return nil
	}

	tmpDataList := make([]kperf.PmuData, len(vo.GoData))

	for _, v := range vo.GoData {
		
		if v.Symbols == nil || len(v.Symbols) == 0 {
			continue
		}

		GlobalPeriod += v.Period
		isExist := false
		for i, tmpVo := range tmpDataList {
			if comparePmuData(v, tmpVo) {
				isExist = true
				tmpDataList[i].Period = tmpVo.Period + v.Period
				break
			}
		}

		if !isExist {
			tmpDataList = append(tmpDataList, v)
		}
	}

	sort.Slice(tmpDataList, func(i, j int) bool {
		return tmpDataList[i].Period > tmpDataList[j].Period
	})                                                                        
	return tmpDataList
}

func getPeriodPercent(period uint64) float64 {
	return float64(period) / float64(GlobalPeriod) * 100.00
}

func printHotSpotGraph(hotspotData []kperf.PmuData) {
	fmt.Println(strings.Repeat("=", 140))
	fmt.Println(strings.Repeat("-", 140))
	fmt.Printf("%-80s%-20s%-40scycles(%%)\n", " Function", "Cycles", "Module")
	fmt.Println(strings.Repeat("-", 140))
	for _, data := range hotspotData {

		if data.Symbols == nil {
			continue
		}

		moduleName := "UNKNOWN"
		if data.Symbols != nil {
			if len(data.Symbols[0].Module) > 0 {
				moduleName = data.Symbols[0].Module
			}
		}
		
		pos := strings.LastIndex(moduleName, "/")
		if pos != -1 {
			moduleName = moduleName[pos + 1:]
		}

		if data.Evt == "context-switches" {
			fmt.Printf("\x1B[31m")
		}

		funcName := processSymbol(data.Symbols[0])
		if len(funcName) > 78 {
			halfLen := 78 / 2 -1
			startPos := len(funcName) - 78 + halfLen + 3
			funcName = funcName[:halfLen] + "..." + funcName[startPos:]
		}

		fmt.Printf(" %-78s%-20v%-40s%.2f%%\n", funcName, data.Period, moduleName,   getPeriodPercent(data.Period))
		if data.Evt == "context-switches" {
			fmt.Printf("\x1B[0m")
		}
	}
	fmt.Println(strings.Repeat("_", 140))
}

func printStack(symbols []sym.Symbol, period uint64) {
	for i, v := range symbols {
		symbolName := processSymbol(v)
		moduleName := "UNKNOWN"
		if len(v.Module) > 0 {
			moduleName = v.Module
		}
		fmt.Printf("%s%s", strings.Repeat("  ", i), "|——")
		outInfo := fmt.Sprintf("%s %s", symbolName, moduleName)
		fmt.Printf(outInfo)
		if i == 0 {	
			padding := 0
			if len(outInfo) < 110 {
				padding = 110 - len(outInfo)
			}
			fmt.Printf("%s%.2f%%\n", strings.Repeat(" ", padding) , getPeriodPercent(period))
		} else {
			fmt.Println()
		}
	}
}

func blockSample(pid int, interval float64, count int, blockedSample int) {
	attr := kperf.PmuAttr{BlockedSample: false, EvtList: []string{"cycles"}, SymbolMode:kperf.ELF_DWARF, CallStack:true, SampleRate: 4000, UseFreq:true, PidList: []int{pid}}
	if blockedSample == 1 {
		attr.BlockedSample = true
		attr.EvtList = nil
	}
	
	fd, err := kperf.PmuOpen(kperf.SAMPLE, attr)
	if err != nil {
		fmt.Printf("kperf pmuopen failed, expect err is nil, but is %v\n", err)
		return
	}

	kperf.PmuEnable(fd)
	for i := 0; i < count; i++ {
		time.Sleep(time.Duration(interval) * time.Second)

		pmuDataVo, err := kperf.PmuRead(fd)
		if err != nil {
			fmt.Printf("kperf read failed, expect err is nil, but is %v\n", err)
			return
		}

		GlobalPeriod = 0
		hotspotData := GetPmuDataHotSpot(pmuDataVo)
		printHotSpotGraph(hotspotData)
		fmt.Printf(strings.Repeat("=", 50) + "Print the call stack of the hotspot function" + strings.Repeat("=", 50) + "\n")
		fmt.Printf("% -40s%-40s%+40s\n", "@symbol", "@module", "@percent")
		for _, data := range hotspotData {
			printStack(data.Symbols, data.Period)
		}
		GlobalPeriod = 0
	}
	kperf.PmuDisable(fd)
	kperf.PmuClose(fd)
}

func main() {

	pid := int(0)
	if len(os.Args) < 5 {
		printUsage()
		return
	}

	defer func() {
		err := recover()
		if err != nil {
			printUsage()
		}
	}()

	interval, err := strconv.ParseFloat(os.Args[1], 64)
	if err != nil {
		panic(err)
	}


	if interval <= 0 {
		panic(errors.New("Interval must be a positive number."))
	}

	count, err := strconv.ParseInt(os.Args[2], 10, 32)

	if err != nil {
		panic(err)
	}

	if count <= 0 {
		panic(errors.New("Count must be a positive integer."))
	}

	blockedSample, err := strconv.ParseInt(os.Args[3], 10, 32)

	if err != nil {
		panic(err)
	}

	if blockedSample != 0 && blockedSample != 1 {
		panic(errors.New("blockedSample must be 0 or 1"))
	}


	parsePid, err := strconv.ParseInt(os.Args[4], 10, 32)
	needKill := false
	if err != nil {
		parsePid, err := startProc(os.Args[4])
		if err != nil {
			panic(err)
		}
		pid = int(parsePid)
		needKill = true
	} else {
		pid = int(parsePid)
	}
	
	blockSample(pid, interval, int(count), int(blockedSample))
	if needKill {
		syscall.Kill(pid, syscall.SIGKILL)
	}
}
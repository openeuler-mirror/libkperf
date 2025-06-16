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
 * Description: libsym go language interface 
 ******************************************************************************/

package sym

/*
#cgo CFLAGS: -I ../include
#cgo !static LDFLAGS: -L ../lib -lsym
#cgo static LDFLAGS: -L ../static_lib -lsym -lstdc++ -lelf++ -ldwarf++

#include "symbol.h"
#include "pcerrc.h"
*/
import "C"
import "errors"

type Symbol struct {
	Addr uint64                // address (synamic allocated) of this symbol
	Module string			   // binary name of which the symbol belongs to
	SymbolName string		   // name of the symbol with demangle
	MangleName string		   // name of the symbol with no demangle
	FileName string			   // corresponding file of current symbol
	LineNum uint32			   // line number of a symbol in the file
	Offset uint64			   // offset relateive to the start address
	CodeMapEndAddr uint64	   // function end address
	CodeMapAddr uint64		   // real srcAddr of Asm Code or

	cSymbol *C.struct_Symbol   // pointer of C source symbol
}

type AsmCode struct {
	Addr uint64       // address of asm file
	Code string       // code of asm
	FileName string   // this source file name of this asm code
	LineNum uint32    // the real line of this addr
}

type Asm struct {
	FuncName string          // function name of void
	FuncStartAddr uint64     // start address of function
	FuncFileOffset uint64    // offset of function in this file
	CodeData AsmCode         // asm code
}

type AsmStack struct {
	GoAsm []Asm              // Asm list
	cAsm *C.struct_StackAsm  // // pointer of C source StackAsm
}

// init
func Init() {
	C.SymResolverInit()
}

// record kernel symbol
func RecordKernel() error {
	res := C.SymResolverRecordKernel()
	if int(res) != 0 {
		return errors.New(C.GoString(C.Perror()))
	}
	return nil
}

// record modules by pit
func RecordModule(pid int) error {
	res := C.SymResolverRecordModule(C.int(pid))
	if int(res) != 0 {
		return errors.New(C.GoString(C.Perror()))
	}
	return nil
}

// Collects symbols by process ID but does not collect dwarf information
func RecordModuleNoDwarf(pid int) error {
	res := C.SymResolverRecordModuleNoDwarf(C.int(pid))
	if int(res) != 0 {
		return errors.New(C.GoString(C.Perror()))
	}
	return nil
}

//  Incremental update modules of pid, i.e. record newly loaded dynamic libraries by pid.
func IncrUpdateModule(pid int) error {
	res := C.SymResolverIncrUpdateModule(C.int(pid))
	if int(res) != 0 {
		return errors.New(C.GoString(C.Perror()))
	}
	return nil
}

// incremental update modules of pid, i.e. record newly loaded dynamic libraries with no dwarf by pid.
func IncrUpdateModuleNoDwarf(pid int) error {
	res := C.SymResolverIncrUpdateModuleNoDwarf(C.int(pid))
	if int(res) != 0 {
		return errors.New(C.GoString(C.Perror()))
	}
	return nil
}

// update module info
func UpdateModule(pid int, moduleName string, startAddr  uint64) error {
	res := C.SymResolverUpdateModule(C.int(pid), C.CString(moduleName), C.ulong(startAddr))
	if int(res) != 0 {
		return errors.New(C.GoString(C.Perror()))
	}
	return nil
}

// update module info but dose not collect dwarf info
func UpdateModuleNoDwarf(pid int, moduleName string, startAddr  uint64) error {
	res := C.SymResolverUpdateModuleNoDwarf(C.int(pid), C.CString(moduleName), C.ulong(startAddr))
	if int(res) != 0 {
		return errors.New(C.GoString(C.Perror()))
	}
	return nil
}

// record ELF data for a binay
func RecordElf(fileName string) error {
	res := C.SymResolverRecordElf(C.CString(fileName))
	if int(res) != 0 {
		return errors.New(C.GoString(C.Perror()))
	}
	return nil
}

//  Record DWARF data for a binary
func RecordDwarf(fileName string) error {
	res := C.SymResolverRecordDwarf(C.CString(fileName))
	if int(res) != 0 {
		return errors.New(C.GoString(C.Perror()))
	}
	return nil
}

// Clean up resolver in the end after usage
func Destory() {
	C.SymResolverDestroy()
}

// Convert a callstack to a unsigned long long hashid
func StackToHash(pid int, address []uint64) ([]Symbol, error) {
	addrLen := len(address) 
	if addrLen == 0 {
		return nil, errors.New("addr list can't be empty")
	}
	addrList := make([]C.ulong, addrLen)
	for i, addr := range address {
		addrList[i] = C.ulong(addr)
	}
	stack := C.StackToHash(C.int(pid), &addrList[0], C.int(addrLen))
	syms := make([]Symbol, 0, addrLen)

	if stack == nil {
		return nil, errors.New("stack is empty")
	}

	curStack := stack 

	for curStack != nil {
		cSymbol := curStack.symbol
		if cSymbol != nil {
			oneSymbol := getGoSymbol(cSymbol)
			syms = append(syms, oneSymbol)
		}
		curStack = curStack.next
	}
	return syms, nil
}

// Map a specific address to a symbol
func MapAddr(pid int, addr uint64) (Symbol, error) {
	cSymbol := C.SymResolverMapAddr(C.int(pid), C.ulong(addr))
	if cSymbol == nil {
		return Symbol{}, errors.New("Corresponding symbol not found")
	}
	return getGoSymbol(cSymbol), nil
}

// Obtain assembly code from file and start address and end address
func GetAsmCode(moduleName string, startAddr uint64, endAddr uint64) (AsmStack, error) {
	stackAsm := C.SymResolverAsmCode(C.CString(moduleName), C.ulong(startAddr), C.ulong(endAddr))
	if stackAsm == nil {
		return AsmStack{}, errors.New(C.GoString(C.Perror()))
	}

	predictMaxLen := (endAddr - startAddr) / 4 + 1

	asmList := make([]Asm, 0, predictMaxLen)

	curStack := stackAsm

	for curStack != nil {
		oneAsm := Asm{FuncName: C.GoString(curStack.funcName), FuncFileOffset: uint64(curStack.functFileOffset), FuncStartAddr: uint64(curStack.funcStartAddr)}
		cAsmCode := curStack.asmCode
		if cAsmCode != nil {
			oneAsm.CodeData = AsmCode{Addr: uint64(cAsmCode.addr), Code: C.GoString(cAsmCode.code), FileName: C.GoString(cAsmCode.fileName), LineNum: uint32(cAsmCode.lineNum)}
		}
		asmList = append(asmList, oneAsm)
		curStack = curStack.next
	}
	asmStack := AsmStack{GoAsm: asmList, cAsm: stackAsm}
	return asmStack, nil
}

// Obtain the source code from the file and real start address.
func MapCodeAddr(moduleName string, startAddr uint64) (Symbol, error) {
	cSymbol := C.SymResolverMapCodeAddr(C.CString(moduleName), C.ulong(startAddr))
	if cSymbol == nil {
		return Symbol{}, errors.New("Corresponding symbol not found")
	}
	return getGoSymbol(cSymbol), nil
}

// free Symbol pointer 
func FreeSymbol(symbol Symbol) {
	C.FreeSymbolPtr(symbol.cSymbol)
}

// free pid module data
func FreeModuleData(pid int) {
	C.FreeModuleData(C.int(pid))
}

// free asm stack code
func FreeAsmStack(asmStack AsmStack) {
	C.FreeAsmStack(asmStack.cAsm)
}

func getGoSymbol(cSymbol *C.struct_Symbol) Symbol {
	return Symbol{Addr:uint64(cSymbol.addr),
		Module:C.GoString(cSymbol.module),
		SymbolName:C.GoString(cSymbol.symbolName),
		MangleName:C.GoString(cSymbol.mangleName),
		FileName:C.GoString(cSymbol.fileName),
		LineNum:uint32(cSymbol.lineNum),
		Offset:uint64(cSymbol.offset),
		CodeMapEndAddr:uint64(cSymbol.codeMapEndAddr),
		CodeMapAddr:uint64(cSymbol.codeMapAddr),
		cSymbol: cSymbol}
}


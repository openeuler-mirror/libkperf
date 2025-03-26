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
#cgo LDFLAGS: -L ../lib -lsym

#include "symbol.h"
*/
import "C"

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

	cSymbol *C.struct_symbol   // pointer of C source symbol
}
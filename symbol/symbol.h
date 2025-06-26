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
 * Create: 2024-04-03
 * Description: Provide the tool function and data structure of the complete symbol analysis and stack analysis.
 ******************************************************************************/
#ifndef SYMBOL_H
#define SYMBOL_H
#include <stdbool.h>
#include <linux/types.h>
#ifdef __cplusplus
extern "C" {
#endif

struct Symbol {
    unsigned long addr;    // address (dynamic allocated) of this symbol
    char* module;          // binary name of which the symbol belongs to
    char* symbolName;      // name of the symbol with demangle
    char* mangleName;      // name of the symbol with no demangle
    char* fileName;        // corresponding file of current symbol
    unsigned int lineNum;  // line number of a symbol in the file
    unsigned long offset;
    unsigned long codeMapEndAddr;  // function end address
    unsigned long codeMapAddr;     // real srcAddr of Asm Code or
    __u64 count;
};

struct Stack {
    struct Symbol* symbol;  // symbol info for current stack
    struct Stack* next;     // points to next position in stack
    struct Stack* prev;     // points to previous position in stack
    __u64 count;
} __attribute__((aligned(64)));

struct StackAsm {
    char* funcName;                 // function name of void
    unsigned long funcStartAddr;    // start address of function
    unsigned long functFileOffset;  // offset of function in this file
    struct StackAsm* next;          // points to next position in stack
    struct AsmCode* asmCode;        // asm code
};

struct AsmCode {
    unsigned long addr;    // address of asm file
    char* code;            // code of asm
    char* fileName;        // this source file name of this asm code
    unsigned int lineNum;  // the real line of this addr
};

void SymResolverInit();

int SymResolverRecordKernel();

int SymResolverRecordModule(int pid);

int SymResolverRecordModuleNoDwarf(int pid);
/**
 * Incremental update modules of pid, i.e. record newly loaded dynamic libraries by pid.
 */
int SymResolverIncrUpdateModule(int pid);

/**
 * Incremental update modules of pid, i.e. record newly loaded dynamic libraries with no dwarf by pid.
 */
int SymResolverIncrUpdateModuleNoDwarf(int pid);

int SymResolverUpdateModule(int pid, const char* moduleName, unsigned long startAddr);

int SymResolverUpdateModuleNoDwarf(int pid, const char* moduleName, unsigned long startAddr);

/**
 * Record ELF data for a binary
 */
int SymResolverRecordElf(const char* fileName);

/**
 * Record DWARF data for a binary
 */
int SymResolverRecordDwarf(const char* fileName);

/**
 * Clean up resolver in the end after usage
 */
void SymResolverDestroy();

/**
 * Convert a callstack to a unsigned long long hashid
 */
struct Stack* StackToHash(int pid, unsigned long* stack, int nr);

/**
 * Map a specific address to a symbol
 */
struct Symbol* SymResolverMapAddr(int pid, unsigned long addr);

/**
 * Obtain assembly code from file and start address and end address
 */
struct StackAsm* SymResolverAsmCode(const char* moduleName, unsigned long startAddr, unsigned long endAddr);

/**
 * Obtain the source code from the file and real start address.
 */
struct Symbol* SymResolverMapCodeAddr(const char* moduleName, unsigned long startAddr);

/**
 * free Symbol pointer
 */
void FreeSymbolPtr(struct Symbol* symbol);

/**
 * free pid module data
 * @param pid
 */
void FreeModuleData(int pid);

/**
 * free asm stack code
 */
void FreeAsmStack(struct StackAsm* stackAsm);

int SymGetBuildId(const char *moduleName, char **buildId);

struct ProcTopology {
    int pid;
    int tid;
    int ppid;
    int numChild;
    int* childPid;
    char* comm;
    char* exe;
    bool kernel;
    bool isMain;
};
#ifdef __cplusplus
}
#endif
#endif

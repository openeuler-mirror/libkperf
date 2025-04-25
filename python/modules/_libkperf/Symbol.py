"""
Copyright (c) Huawei Technologies Co., Ltd. 2024. All rights reserved.
libkperf licensed under the Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
    http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR
PURPOSE.
See the Mulan PSL v2 for more details.
Author: Victor Jin
Create: 2024-05-10
Description: ctype python Symbol module
"""
import ctypes
from typing import List, Any, Iterator
from  .Config import UTF_8, sym_so


class CtypesSymbol(ctypes.Structure):
    """
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
    """

    _fields_ = [
        ('addr',           ctypes.c_ulong),
        ('module',         ctypes.c_char_p),
        ('symbolName',     ctypes.c_char_p),
        ('mangleName',     ctypes.c_char_p),
        ('fileName',       ctypes.c_char_p),
        ('lineNum',        ctypes.c_uint),
        ('offset',         ctypes.c_ulong),
        ('codeMapEndAddr', ctypes.c_ulong),
        ('codeMapAddr',    ctypes.c_ulong),
        ('count',          ctypes.c_uint64)
    ]

    def __init__(self,
                 addr: int = 0,
                 module: str = '',
                 symbolName: str = '',
                 mangleName: str = '',
                 fileName: str = '',
                 lineNum: int = 0,
                 offset: int = 0,
                 codeMapEndAddr: int = 0,
                 codeMapAddr: int = 0,
                 count: int = 0,
                 *args: Any, **kw: Any) -> None:
        super().__init__(*args, **kw)
        self.addr = ctypes.c_ulong(addr)
        self.module = ctypes.c_char_p(module.encode(UTF_8))

        self.symbolName = ctypes.c_char_p(symbolName.encode(UTF_8))
        self.mangleName = ctypes.c_char_p(mangleName.encode(UTF_8))

        self.fileName = ctypes.c_char_p(fileName.encode(UTF_8))
        self.lineNum = ctypes.c_uint(lineNum)
        self.offset = ctypes.c_ulong(offset)

        self.codeMapEndAddr = ctypes.c_ulong(codeMapEndAddr)
        self.codeMapAddr = ctypes.c_ulong(codeMapAddr)
        self.count = ctypes.c_uint64(count)


class Symbol:

    __slots__ = ['__c_sym']

    def __init__(self,
                 addr: int = 0,
                 module: str = '',
                 symbolName: str = '',
                 mangleName: str = '',
                 fileName: str = '',
                 lineNum: int = 0,
                 offset: int = 0,
                 codeMapEndAddr: int = 0,
                 codeMapAddr: int = 0,
                 count: int = 0) -> None:
        self.__c_sym = CtypesSymbol(
            addr=addr,
            module=module,
            symbolName=symbolName,
            mangleName=mangleName,
            fileName=fileName,
            lineNum=lineNum,
            offset=offset,
            codeMapEndAddr=codeMapEndAddr,
            codeMapAddr=codeMapAddr,
            count=count
        )

    @property
    def c_sym(self) -> CtypesSymbol:
        return self.__c_sym

    @property
    def addr(self) -> int:
        return self.c_sym.addr

    @addr.setter
    def addr(self, addr: int) -> None:
        self.c_sym.addr = ctypes.c_ulong(addr)

    @property
    def module(self) -> str:
        return self.c_sym.module.decode(UTF_8)

    @module.setter
    def module(self, module: str) -> None:
        self.c_sym.module = ctypes.c_char_p(module.encode(UTF_8))

    @property
    def symbolName(self) -> str:
        return self.c_sym.symbolName.decode(UTF_8)

    @symbolName.setter
    def symbolName(self, symbolName: str) -> None:
        self.c_sym.symbolName = ctypes.c_char_p(symbolName.encode(UTF_8))
    
    @property
    def mangleName(self) -> str:
        return self.c_sym.mangleName.decode(UTF_8)

    @mangleName.setter
    def mangleName(self, mangleName: str) -> None:
        self.c_sym.mangleName = ctypes.c_char_p(mangleName.encode(UTF_8))

    @property
    def fileName(self) -> str:
        return self.c_sym.fileName.decode(UTF_8)

    @fileName.setter
    def fileName(self, fileName: str) -> None:
        self.c_sym.fileName = ctypes.c_char_p(fileName.encode(UTF_8))

    @property
    def lineNum(self) -> int:
        return self.c_sym.lineNum

    @lineNum.setter
    def lineNum(self, lineNum: int) -> None:
        self.c_sym.lineNum = ctypes.c_uint(lineNum)

    @property
    def offset(self) -> int:
        return self.c_sym.offset

    @offset.setter
    def offset(self, offset: int) -> None:
        self.c_sym.offset = ctypes.c_ulong(offset)

    @property
    def codeMapEndAddr(self) -> int:
        return self.c_sym.codeMapEndAddr

    @codeMapEndAddr.setter
    def codeMapEndAddr(self, codeMapEndAddr: int) -> None:
        self.c_sym.codeMapEndAddr = ctypes.c_ulong(codeMapEndAddr)

    @property
    def codeMapAddr(self) -> int:
        return self.c_sym.codeMapAddr

    @codeMapAddr.setter
    def codeMapAddr(self, codeMapAddr: int) -> None:
        self.c_sym.codeMapAddr = ctypes.c_ulong(codeMapAddr)

    @property
    def count(self) -> int:
        return self.c_sym.count

    @count.setter
    def count(self, count: int) -> None:
        self.c_sym.count = ctypes.c_uint64(count)

    @classmethod
    def from_c_sym(cls, c_sym: CtypesSymbol) -> 'Symbol':
        symbol = cls()
        symbol.__c_sym = c_sym
        return symbol


class CtypesStack(ctypes.Structure):
    """
    struct Stack {
        struct Symbol* symbol;  // symbol info for current stack
        struct Stack* next;     // points to next position in stack
        struct Stack* prev;     // points to previous position in stack
        __u64 count;
    } __attribute__((aligned(64)));
    """
    pass


CtypesStack._fields_ = [
        ('symbol', ctypes.POINTER(CtypesSymbol)),
        ('next',   ctypes.POINTER(CtypesStack)),
        ('prev',   ctypes.POINTER(CtypesStack)),
        ('count',  ctypes.c_uint64)
    ]


class Stack:

    __slots__ = ['__c_stack']

    def __init__(self,
                 symbol: Symbol = None,
                 next: 'Stack' = None,
                 prev: 'Stack' = None,
                 count: int = 0) -> None:
        self.__c_stack = CtypesStack(
            symbol=symbol.c_sym if symbol else None,
            next=next.c_stack if next else None,
            prev=prev.c_stack if prev else None,
            count=count
        )

    @property
    def c_stack(self) -> CtypesStack:
        return self.__c_stack

    @property
    def symbol(self) -> Symbol:
        return Symbol.from_c_sym(self.c_stack.symbol.contents) if self.c_stack.symbol else None

    @symbol.setter
    def symbol(self, symbol: Symbol) -> None:
        self.c_stack.symbol = symbol.c_sym if symbol else None


    @property
    def next(self) -> 'Stack':
        return self.from_c_stack(self.c_stack.next.contents) if self.c_stack.next else None

    @next.setter
    def next(self, next: 'Stack') -> None:
        self.c_stack.next = next.c_stack if next else None


    @property
    def prev(self) -> 'Stack':
        return self.from_c_stack(self.c_stack.prev.contents) if self.c_stack.prev else None

    @prev.setter
    def prev(self, prev: 'Stack') -> None:
        self.c_stack.prev = prev.c_stack if prev else None

    @property
    def count(self) -> int:
        return self.c_stack.count

    @count.setter
    def count(self, count: int) -> None:
        self.c_stack.count = ctypes.c_uint64(count)

    @classmethod
    def from_c_stack(cls, c_stack: CtypesStack) -> 'Stack':
        stack = cls()
        stack.__c_stack = c_stack
        return stack


class CtypesAsmCode(ctypes.Structure):
    """
    struct AsmCode {
        unsigned long addr;    // address of asm file
        char* code;            // code of asm
        char* fileName;        // this source file name of this asm code
        unsigned int lineNum;  // the real line of this addr
    };
    """

    _fields_ = [
        ('addr',     ctypes.c_ulong),
        ('code',     ctypes.c_char_p),
        ('fileName', ctypes.c_char_p),
        ('lineNum',  ctypes.c_uint)
    ]

    def __init__(self,
                 addr: int = 0,
                 code: str = '',
                 fileName: str = '',
                 lineNum: int = 0,
                 *args: Any, **kw: Any) -> None:
        super().__init__(*args, **kw)
        self.addr =  ctypes.c_ulong(addr)
        self.code = ctypes.c_char_p(code.encode(UTF_8))
        self.fileName = ctypes.c_char_p(fileName.encode(UTF_8))
        self.lineNum =  ctypes.c_uint(lineNum)


class AsmCode:

    __slots__ = ['__c_asm_code']

    def __init__(self,
                 addr: int = 0,
                 code: str = '',
                 fileName: str = '',
                 lineNum: int = 0) -> None:
        self.__c_asm_code = CtypesAsmCode(
            addr=addr,
            code=code,
            fileName=fileName,
            lineNum=lineNum
        )

    @property
    def c_asm_code(self) -> CtypesAsmCode:
        return self.__c_asm_code

    @property
    def addr(self) -> int:
        return self.c_asm_code.addr

    @addr.setter
    def addr(self, addr: int) -> None:
        self.c_asm_code.addr = ctypes.c_ulong(addr)

    @property
    def code(self) -> str:
        return self.c_asm_code.code.decode(UTF_8)

    @code.setter
    def code(self, code: str) -> None:
        self.c_asm_code.code = ctypes.c_char_p(code.encode(UTF_8))

    @property
    def fileName(self) -> str:
        return self.c_asm_code.fileName.decode(UTF_8)

    @fileName.setter
    def fileName(self, fileName: str) -> None:
        self.c_asm_code.fileName = ctypes.c_char_p(fileName.encode(UTF_8))

    @property
    def lineNum(self) -> int:
        return self.c_asm_code.lineNum

    @lineNum.setter
    def lineNum(self, lineNum: int) -> None:
        self.c_asm_code.lineNum = ctypes.c_uint(lineNum)

    @classmethod
    def from_c_asm_code(cls, c_asm_code: CtypesAsmCode) -> 'AsmCode':
        asm_code = cls()
        asm_code.__c_asm_code = c_asm_code
        return asm_code


class CtypesStackAsm(ctypes.Structure):
    """
    struct StackAsm {
        char* funcName;                 // function name of void
        unsigned long funcStartAddr;    // start address of function
        unsigned long functFileOffset;  // offset of function in this file
        struct StackAsm* next;          // points to next position in stack
        struct AsmCode* asmCode;        // asm code
    };
    """

    _fields_ = [
        ('fileName',        ctypes.c_char_p),
        ('funcStartAddr',   ctypes.c_ulong),
        ('functFileOffset', ctypes.c_ulong),
        ('next',            ctypes.POINTER('CtypesStackAsm')),
        ('asmCode',         ctypes.POINTER(CtypesAsmCode)),
    ]

    def __init__(self,
                 fileName: str = '',
                 funcStartAddr: int = 0,
                 functFileOffset: int = 0,
                 next: 'CtypesStackAsm' = None,
                 asmCode: CtypesAsmCode = None,
                 *args: Any, **kw: Any) -> None:
        super().__init__(*args, **kw)
        self.fileName = ctypes.c_char_p(fileName.encode(UTF_8))
        self.funcStartAddr =  ctypes.c_ulong(funcStartAddr)
        self.functFileOffset =  ctypes.c_ulong(functFileOffset)
        self.next = next
        self.asmCode = asmCode


class StackAsm:

    __slots__ = ['__c_stack_asm']

    def __init__(self,
                 fileName: str = '',
                 funcStartAddr: int = 0,
                 functFileOffset: int = 0,
                 next: 'StackAsm' = None,
                 asmCode: AsmCode = None) -> None:
        self.__c_stack_asm = CtypesStackAsm(
            fileName=fileName,
            funcStartAddr=funcStartAddr,
            functFileOffset=functFileOffset,
            next=next.c_stack_asm if next else None,
            asmCode=asmCode.c_asm_code if asmCode else None,
        )

    @property
    def c_stack_asm(self) -> CtypesStackAsm:
        return self.__c_stack_asm

    @property
    def fileName(self) -> str:
        return self.c_stack_asm.fileName.decode(UTF_8)

    @fileName.setter
    def fileName(self, fileName: str) -> None:
        self.c_stack_asm.fileName = ctypes.c_char_p(fileName.encode(UTF_8))

    @property
    def funcStartAddr(self) -> int:
        return self.c_stack_asm.funcStartAddr

    @funcStartAddr.setter
    def funcStartAddr(self, funcStartAddr: int) -> None:
        self.c_stack_asm.funcStartAddr = ctypes.c_ulong(funcStartAddr)
        
    @property
    def functFileOffset(self) -> int:
        return self.c_stack_asm.functFileOffset

    @functFileOffset.setter
    def functFileOffset(self, functFileOffset: int) -> None:
        self.c_stack_asm.functFileOffset = ctypes.c_ulong(functFileOffset)

    @property
    def next(self) -> 'StackAsm':
        return self.from_c_stack_asm(self.c_stack_asm.next.contents) if self.c_stack_asm.next else None

    @next.setter
    def next(self, next: 'StackAsm') -> None:
        self.c_stack_asm.next = next.c_stack_asm if next else None

    @property
    def asmCode(self) -> AsmCode:
        return AsmCode.from_c_asm_code(self.c_stack_asm.asmCode.contents) if self.c_stack_asm.asmCode else None

    @asmCode.setter
    def asmCode(self, asmCode: AsmCode) -> None:
        self.c_stack_asm.asmCode = asmCode.c_asm_code if asmCode else None

    @classmethod
    def from_c_stack_asm(cls, c_stack_asm: CtypesStackAsm) -> 'StackAsm':
        stack_asm = cls()
        stack_asm.__c_stack_asm = c_stack_asm
        return stack_asm


class CtypesProcTopology(ctypes.Structure):
    """
    struct ProcTopology {
        int pid;
        int tid;
        int ppid;
        int numChild;
        int* childPid;
        char* comm;
        char* exe;
        bool kernel;
    };
    """

    _fields_ = [
        ('pid',      ctypes.c_int),
        ('tid',      ctypes.c_int),
        ('ppid',     ctypes.c_int),
        ('numChild', ctypes.c_int),
        ('childPid', ctypes.POINTER(ctypes.c_int)),
        ('comm',     ctypes.c_char_p),
        ('exe',      ctypes.c_char_p),
        ('kernel',   ctypes.c_bool),
    ]

    def __init__(self,
                 pid: int = 0,
                 tid: int = 0,
                 ppid: int = 0,
                 childPid: List[int] = None,
                 comm: str = '',
                 exe: str = '',
                 kernel: bool = False,
                 *args: Any, **kw: Any) -> None:
        super().__init__(*args, **kw)
        self.pid = ctypes.c_int(pid)
        self.tid = ctypes.c_int(tid)
        self.ppid = ctypes.c_int(ppid)
        if childPid:
            numChildPid = len(childPid)
            self.childPid = (ctypes.c_int * numChildPid)(*childPid)
            self.numChild = ctypes.c_int(numChildPid)
        else:
            self.childPid = None
            self.numChild = ctypes.c_int(0)
        self.comm = ctypes.c_char_p(comm.encode(UTF_8))
        self.exe = ctypes.c_char_p(exe.encode(UTF_8))
        self.kernel = ctypes.c_bool(kernel)


class ProcTopology:

    __slots__ = ['__c_proc_topology']

    def __init__(self,
                 pid: int = 0,
                 tid: int = 0,
                 ppid: int = 0,
                 childPid: List[int] = None,
                 comm: str = '',
                 exe: str = '',
                 kernel: bool = False) -> None:
        self.__c_proc_topology = CtypesProcTopology(
            pid = pid,
            tid=tid,
            ppid=ppid,
            childPid=childPid,
            comm=comm,
            exe=exe,
            kernel=kernel
        )

    @property
    def c_proc_topology(self) -> CtypesProcTopology:
        return self.__c_proc_topology

    @property
    def pid(self) -> int:
        return self.c_proc_topology.pid

    @pid.setter
    def pid(self, pid: int) -> None:
        self.c_proc_topology.pid = ctypes.c_int(pid)

    @property
    def tid(self) -> int:
        return self.c_proc_topology.tid

    @tid.setter
    def tid(self, tid: int) -> None:
        self.c_proc_topology.tid = ctypes.c_int(tid)
        
        
    @property
    def ppid(self) -> int:
        return self.c_proc_topology.ppid

    @ppid.setter
    def ppid(self, ppid: int) -> None:
        self.c_proc_topology.ppid = ctypes.c_int(ppid)
    
    @property
    def numChild(self) -> int:
        return self.c_proc_topology.numChild

    @property
    def childPid(self) -> List[int]:
        return [self.c_proc_topology.childPid[i] for i in range(self.numChild)]

    @childPid.setter
    def childPid(self, childPid: List[int]) -> None:
        if childPid:
            numChildPid = len(childPid)
            self.c_proc_topology.childPid = (ctypes.c_int * numChildPid)(*childPid)
            self.c_proc_topology.numChild = ctypes.c_int(numChildPid)
        else:
            self.c_proc_topology.childPid = None
            self.c_proc_topology.numChild = ctypes.c_int(0)
    
    @property
    def comm(self) -> str:
        return self.c_proc_topology.comm.decode(UTF_8)

    @comm.setter
    def comm(self, comm: str) -> None:
        self.c_proc_topology.comm = ctypes.c_char_p(comm.encode(UTF_8))
    
    @property
    def exe(self) -> str:
        return self.c_proc_topology.exe.decode(UTF_8)

    @exe.setter
    def exe(self, exe: str) -> None:
        self.c_proc_topology.exe = ctypes.c_char_p(exe.encode(UTF_8))

    @classmethod
    def from_c_proc_topology(cls, c_proc_topology: CtypesProcTopology) -> 'ProcTopology':
        proc_topology = cls()
        proc_topology.__c_proc_topology = c_proc_topology
        return proc_topology


def SymResolverRecordKernel() -> None:
    """
    int SymResolverRecordKernel();
    """
    c_SymResolverRecordKernel = sym_so.SymResolverRecordKernel
    c_SymResolverRecordKernel.argtypes = []
    c_SymResolverRecordKernel.restype = ctypes.c_int

    c_SymResolverRecordKernel()


def SymResolverRecordModule(pid: int) -> None:
    """
    int SymResolverRecordModule(int pid);
    """
    c_SymResolverRecordModule = sym_so.SymResolverRecordModule
    c_SymResolverRecordModule.argtypes = [ctypes.c_int]
    c_SymResolverRecordModule.restype = ctypes.c_int

    c_pid = ctypes.c_int(pid)

    c_SymResolverRecordModule(c_pid)


def SymResolverRecordModuleNoDwarf(pid: int) -> None:
    """
    int SymResolverRecordModuleNoDwarf(int pid);
    """
    c_SymResolverRecordModuleNoDwarf = sym_so.SymResolverRecordModuleNoDwarf
    c_SymResolverRecordModuleNoDwarf.argtypes = [ctypes.c_int]
    c_SymResolverRecordModuleNoDwarf.restype = ctypes.c_int

    c_pid = ctypes.c_int(pid)

    c_SymResolverRecordModuleNoDwarf(c_pid)


def StackToHash(pid: int, stackList: List[int]) -> Stack:
    """
    struct Stack* StackToHash(int pid, unsigned long* stack, int nr);
    """
    c_StackToHash = sym_so.StackToHash
    c_StackToHash.argtypes = [ctypes.c_int, ctypes.POINTER(ctypes.c_ulong), ctypes.c_int]
    c_StackToHash.restype = ctypes.POINTER(CtypesStack)

    stack_len = len(stackList)
    c_pid = ctypes.c_int(pid)
    c_stack_list = (ctypes.c_ulong * stack_len)(*stackList)
    c_nr = ctypes.c_int(stack_len)

    c_stack  = c_StackToHash(c_pid, c_stack_list, c_nr)
    if not c_stack:
        return None
    return Stack.from_c_stack(c_stack.contents)


def SymResolverMapAddr(pid: int,  addr: int) -> Symbol:
    """
    struct Symbol* SymResolverMapAddr(int pid, unsigned long addr);
    """
    c_SymResolverMapAddr = sym_so.SymResolverMapAddr
    c_SymResolverMapAddr.argtypes = [ctypes.c_int, ctypes.c_ulong]
    c_SymResolverMapAddr.restype = ctypes.POINTER(CtypesSymbol)

    c_pid = ctypes.c_int(pid)
    c_addr = ctypes.c_ulong(addr)

    c_sym  = c_SymResolverMapAddr(c_pid, c_addr)
    if not c_sym:
        return None
    return Symbol.from_c_sym(c_sym.contents)


def FreeModuleData(pid: int) -> None:
    """
    void FreeModuleData(int pid);
    """
    c_FreeModuleData = sym_so.FreeModuleData
    c_FreeModuleData.argtypes = [ctypes.c_int]
    c_FreeModuleData.restype = None

    c_pid = ctypes.c_int(pid)

    c_FreeModuleData(c_pid)


def SymResolverDestroy() -> None:
    """
    void SymResolverDestroy();
    """
    c_SymResolverDestroy = sym_so.SymResolverDestroy
    c_SymResolverDestroy.argtypes = []
    c_SymResolverDestroy.restype = None

    c_SymResolverDestroy()


__all__ = [
    'CtypesSymbol',
    'Symbol',
    'CtypesStack',
    'Stack',
    'CtypesAsmCode',
    'AsmCode',
    'CtypesStackAsm',
    'StackAsm',
    'CtypesProcTopology',
    'ProcTopology',
    'SymResolverRecordKernel',
    'SymResolverRecordModule',
    'SymResolverRecordModuleNoDwarf',
    'StackToHash',
    'SymResolverMapAddr',
    'FreeModuleData',
    'SymResolverDestroy',
]

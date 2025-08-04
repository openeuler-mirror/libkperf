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
                 addr= 0,
                 module= '',
                 symbolName= '',
                 mangleName= '',
                 fileName= '',
                 lineNum= 0,
                 offset= 0,
                 codeMapEndAddr= 0,
                 codeMapAddr= 0,
                 count= 0,
                 *args, **kw):
        super(CtypesSymbol, self).__init__(*args, **kw)
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

    __slots__ = ['__c_sym','__module', '__symbolName', '__mangleName', '__fileName']
 
    def __init__(self,
                 addr= 0,
                 module= '',
                 symbolName= '',
                 mangleName= '',
                 fileName= '',
                 lineNum= 0,
                 offset= 0,
                 codeMapEndAddr= 0,
                 codeMapAddr= 0,
                 count= 0):
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
    def c_sym(self):
        return self.__c_sym

    @property
    def addr(self):
        return self.c_sym.addr

    @addr.setter
    def addr(self, addr):
        self.c_sym.addr = ctypes.c_ulong(addr)

    @property
    def module(self):
        if not self.__module:
            self.__module = self.c_sym.module.decode(UTF_8)
        return self.__module

    @module.setter
    def module(self, module):
        self.c_sym.module = ctypes.c_char_p(module.encode(UTF_8))

    @property
    def symbolName(self):
        if not self.__symbolName:
            self.__symbolName = self.c_sym.symbolName.decode(UTF_8)
        return self.__symbolName

    @symbolName.setter
    def symbolName(self, symbolName):
        self.c_sym.symbolName = ctypes.c_char_p(symbolName.encode(UTF_8))
    
    @property
    def mangleName(self):
        if not self.__mangleName:
            self.__mangleName = self.c_sym.mangleName.decode(UTF_8)
        return self.__mangleName

    @mangleName.setter
    def mangleName(self, mangleName):
        self.c_sym.mangleName = ctypes.c_char_p(mangleName.encode(UTF_8))

    @property
    def fileName(self):
        if not self.__fileName:
            self.__fileName = self.c_sym.fileName.decode(UTF_8)
        return  self.__fileName

    @fileName.setter
    def fileName(self, fileName):
        self.c_sym.fileName = ctypes.c_char_p(fileName.encode(UTF_8))

    @property
    def lineNum(self):
        return self.c_sym.lineNum

    @lineNum.setter
    def lineNum(self, lineNum):
        self.c_sym.lineNum = ctypes.c_uint(lineNum)

    @property
    def offset(self):
        return self.c_sym.offset

    @offset.setter
    def offset(self, offset):
        self.c_sym.offset = ctypes.c_ulong(offset)

    @property
    def codeMapEndAddr(self):
        return self.c_sym.codeMapEndAddr

    @codeMapEndAddr.setter
    def codeMapEndAddr(self, codeMapEndAddr):
        self.c_sym.codeMapEndAddr = ctypes.c_ulong(codeMapEndAddr)

    @property
    def codeMapAddr(self):
        return self.c_sym.codeMapAddr

    @codeMapAddr.setter
    def codeMapAddr(self, codeMapAddr):
        self.c_sym.codeMapAddr = ctypes.c_ulong(codeMapAddr)

    @property
    def count(self):
        return self.c_sym.count

    @count.setter
    def count(self, count):
        self.c_sym.count = ctypes.c_uint64(count)

    @classmethod
    def from_c_sym(cls, c_sym):
        symbol = cls()
        symbol.__c_sym = c_sym
        symbol.__module = None
        symbol.__symbolName = None
        symbol.__mangleName = None
        symbol.__fileName = None
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


class Stack(object):

    __slots__ = ['__c_stack', '__symbol', '__next', '__prev']

    def __init__(self, 
                 symbol= None,
                 next= None,
                 prev= None,
                 count= 0):
        self.__c_stack = CtypesStack(
            symbol=symbol.c_sym if symbol else None,
            next=next.c_stack if next else None,
            prev=prev.c_stack if prev else None,
            count=count
        )

    @property
    def c_stack(self):
        return self.__c_stack

    @property
    def symbol(self):
        if not self.__symbol:
            self.__symbol = Symbol.from_c_sym(self.c_stack.symbol.contents) if self.c_stack.symbol else None
        return self.__symbol

    @symbol.setter
    def symbol(self, symbol):
        self.c_stack.symbol = symbol.c_sym if symbol else None


    @property
    def next(self):
        if not self.__next:
            self.__next = self.from_c_stack(self.c_stack.next.contents) if self.c_stack.next else None
        return self.__next

    @next.setter
    def next(self, next):
        self.c_stack.next = next.c_stack if next else None


    @property
    def prev(self):
        if not self.__prev:
            self.__prev = self.from_c_stack(self.c_stack.prev.contents) if self.c_stack.prev else None
        return self.__prev

    @prev.setter
    def prev(self, prev):
        self.c_stack.prev = prev.c_stack if prev else None

    @property
    def count(self):
        return self.c_stack.count

    @count.setter
    def count(self, count):
        self.c_stack.count = ctypes.c_uint64(count)

    @classmethod
    def from_c_stack(cls, c_stack):
        stack = cls()
        stack.__c_stack = c_stack
        stack.__symbol = None
        stack.__next = None
        stack.__prev = None
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
                 addr= 0,
                 code= '',
                 fileName= '',
                 lineNum= 0,
                 *args, **kw):
        super(CtypesAsmCode, self).__init__(*args, **kw)
        self.addr =  ctypes.c_ulong(addr)
        self.code = ctypes.c_char_p(code.encode(UTF_8))
        self.fileName = ctypes.c_char_p(fileName.encode(UTF_8))
        self.lineNum =  ctypes.c_uint(lineNum)


class AsmCode:

    __slots__ = ['__c_asm_code']

    def __init__(self,
                 addr= 0,
                 code= '',
                 fileName= '',
                 lineNum= 0):
        self.__c_asm_code = CtypesAsmCode(
            addr=addr,
            code=code,
            fileName=fileName,
            lineNum=lineNum
        )

    @property
    def c_asm_code(self):
        return self.__c_asm_code

    @property
    def addr(self):
        return self.c_asm_code.addr

    @addr.setter
    def addr(self, addr):
        self.c_asm_code.addr = ctypes.c_ulong(addr)

    @property
    def code(self):
        return self.c_asm_code.code.decode(UTF_8)

    @code.setter
    def code(self, code):
        self.c_asm_code.code = ctypes.c_char_p(code.encode(UTF_8))

    @property
    def fileName(self):
        return self.c_asm_code.fileName.decode(UTF_8)

    @fileName.setter
    def fileName(self, fileName):
        self.c_asm_code.fileName = ctypes.c_char_p(fileName.encode(UTF_8))

    @property
    def lineNum(self):
        return self.c_asm_code.lineNum

    @lineNum.setter
    def lineNum(self, lineNum):
        self.c_asm_code.lineNum = ctypes.c_uint(lineNum)

    @classmethod
    def from_c_asm_code(cls, c_asm_code):
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
                 fileName= '',
                 funcStartAddr= 0,
                 functFileOffset= 0,
                 next = None,
                 asmCode= None,
                 *args, **kw):
        super(CtypesStackAsm, self).__init__(*args, **kw)
        self.fileName = ctypes.c_char_p(fileName.encode(UTF_8))
        self.funcStartAddr =  ctypes.c_ulong(funcStartAddr)
        self.functFileOffset =  ctypes.c_ulong(functFileOffset)
        self.next = next
        self.asmCode = asmCode


class StackAsm:

    __slots__ = ['__c_stack_asm']

    def __init__(self,
                 fileName= '',
                 funcStartAddr= 0,
                 functFileOffset= 0,
                 next = None,
                 asmCode= None):
        self.__c_stack_asm = CtypesStackAsm(
            fileName=fileName,
            funcStartAddr=funcStartAddr,
            functFileOffset=functFileOffset,
            next=next.c_stack_asm if next else None,
            asmCode=asmCode.c_asm_code if asmCode else None,
        )

    @property
    def c_stack_asm(self):
        return self.__c_stack_asm

    @property
    def fileName(self):
        return self.c_stack_asm.fileName.decode(UTF_8)

    @fileName.setter
    def fileName(self, fileName):
        self.c_stack_asm.fileName = ctypes.c_char_p(fileName.encode(UTF_8))

    @property
    def funcStartAddr(self):
        return self.c_stack_asm.funcStartAddr

    @funcStartAddr.setter
    def funcStartAddr(self, funcStartAddr):
        self.c_stack_asm.funcStartAddr = ctypes.c_ulong(funcStartAddr)
        
    @property
    def functFileOffset(self):
        return self.c_stack_asm.functFileOffset

    @functFileOffset.setter
    def functFileOffset(self, functFileOffset):
        self.c_stack_asm.functFileOffset = ctypes.c_ulong(functFileOffset)

    @property
    def next(self):
        return self.from_c_stack_asm(self.c_stack_asm.next.contents) if self.c_stack_asm.next else None

    @next.setter
    def next(self, next):
        self.c_stack_asm.next = next.c_stack_asm if next else None

    @property
    def asmCode(self):
        return AsmCode.from_c_asm_code(self.c_stack_asm.asmCode.contents) if self.c_stack_asm.asmCode else None

    @asmCode.setter
    def asmCode(self, asmCode):
        self.c_stack_asm.asmCode = asmCode.c_asm_code if asmCode else None

    @classmethod
    def from_c_stack_asm(cls, c_stack_asm):
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
                 pid= 0,
                 tid= 0,
                 ppid= 0,
                 childPid= None,
                 comm= '',
                 exe= '',
                 kernel= False,
                 *args, **kw):
        super(CtypesProcTopology, self).__init__(*args, **kw)
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
                 pid= 0,
                 tid= 0,
                 ppid= 0,
                 childPid= None,
                 comm= '',
                 exe= '',
                 kernel= False):
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
    def c_proc_topology(self):
        return self.__c_proc_topology

    @property
    def pid(self):
        return self.c_proc_topology.pid

    @pid.setter
    def pid(self, pid):
        self.c_proc_topology.pid = ctypes.c_int(pid)

    @property
    def tid(self):
        return self.c_proc_topology.tid

    @tid.setter
    def tid(self, tid):
        self.c_proc_topology.tid = ctypes.c_int(tid)
        
        
    @property
    def ppid(self):
        return self.c_proc_topology.ppid

    @ppid.setter
    def ppid(self, ppid):
        self.c_proc_topology.ppid = ctypes.c_int(ppid)
    
    @property
    def numChild(self):
        return self.c_proc_topology.numChild

    @property
    def childPid(self):
        return [self.c_proc_topology.childPid[i] for i in range(self.numChild)]

    @childPid.setter
    def childPid(self, childPid):
        if childPid:
            numChildPid = len(childPid)
            self.c_proc_topology.childPid = (ctypes.c_int * numChildPid)(*childPid)
            self.c_proc_topology.numChild = ctypes.c_int(numChildPid)
        else:
            self.c_proc_topology.childPid = None
            self.c_proc_topology.numChild = ctypes.c_int(0)
    
    @property
    def comm(self):
        return self.c_proc_topology.comm.decode(UTF_8)

    @comm.setter
    def comm(self, comm):
        self.c_proc_topology.comm = ctypes.c_char_p(comm.encode(UTF_8))
    
    @property
    def exe(self):
        return self.c_proc_topology.exe.decode(UTF_8)

    @exe.setter
    def exe(self, exe):
        self.c_proc_topology.exe = ctypes.c_char_p(exe.encode(UTF_8))

    @classmethod
    def from_c_proc_topology(cls, c_proc_topology):
        proc_topology = cls()
        proc_topology.__c_proc_topology = c_proc_topology
        return proc_topology


def SymResolverRecordKernel():
    """
    int SymResolverRecordKernel();
    """
    c_SymResolverRecordKernel = sym_so.SymResolverRecordKernel
    c_SymResolverRecordKernel.argtypes = []
    c_SymResolverRecordKernel.restype = ctypes.c_int

    c_SymResolverRecordKernel()


def SymResolverRecordModule(pid):
    """
    int SymResolverRecordModule(int pid);
    """
    c_SymResolverRecordModule = sym_so.SymResolverRecordModule
    c_SymResolverRecordModule.argtypes = [ctypes.c_int]
    c_SymResolverRecordModule.restype = ctypes.c_int

    c_pid = ctypes.c_int(pid)

    c_SymResolverRecordModule(c_pid)


def SymResolverRecordModuleNoDwarf(pid):
    """
    int SymResolverRecordModuleNoDwarf(int pid);
    """
    c_SymResolverRecordModuleNoDwarf = sym_so.SymResolverRecordModuleNoDwarf
    c_SymResolverRecordModuleNoDwarf.argtypes = [ctypes.c_int]
    c_SymResolverRecordModuleNoDwarf.restype = ctypes.c_int

    c_pid = ctypes.c_int(pid)

    c_SymResolverRecordModuleNoDwarf(c_pid)


def StackToHash(pid, stackList):
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


def SymResolverMapAddr(pid,  addr):
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


def FreeModuleData(pid):
    """
    void FreeModuleData(int pid);
    """
    c_FreeModuleData = sym_so.FreeModuleData
    c_FreeModuleData.argtypes = [ctypes.c_int]
    c_FreeModuleData.restype = None

    c_pid = ctypes.c_int(pid)

    c_FreeModuleData(c_pid)


def SymResolverDestroy():
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

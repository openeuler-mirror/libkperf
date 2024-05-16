"""
Copyright (c) Huawei Technologies Co., Ltd. 2024. All rights reserved.
gala-gopher licensed under the Mulan PSL v2.
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

    _fields_ = [
        ('addr',           ctypes.c_ulong),
        ('module',         ctypes.c_char_p),
        ('symbolName',     ctypes.c_char_p),
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
                 fileName: str = '',
                 lineNum: int = 0,
                 offset: int = 0,
                 codeMapEndAddr: int = 0,
                 codeMapAddr: int = 0,
                 count: int = 0,
                 *args: Any, **kw: Any):
        super().__init__(*args, **kw)
        self.addr = ctypes.c_ulong(addr)
        self.module = ctypes.c_char_p(module.encode(UTF_8))

        self.symbolName = ctypes.c_char_p(symbolName.encode(UTF_8))
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
                 fileName: str = '',
                 lineNum: int = 0,
                 offset: int = 0,
                 codeMapEndAddr: int = 0,
                 codeMapAddr: int = 0,
                 count: int = 0):
        self.__c_sym = CtypesSymbol(
            addr=addr, module=module,
            symbolName=symbolName, fileName=fileName, lineNum=lineNum, offset=offset,
            codeMapEndAddr=codeMapEndAddr, codeMapAddr=codeMapAddr,
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

    _fields_ = [
        ('symbol', ctypes.POINTER(CtypesSymbol)),
        ('next', ctypes.POINTER('CtypesStack')),
        ('prev', ctypes.POINTER('CtypesStack')),
        ('count', ctypes.c_uint64)
    ]

    def __init__(self,
                 symbol: CtypesSymbol = None,
                 next: 'CtypesStack' = None,
                 prev: 'CtypesStack' = None,
                 count: int = 0,
                 *args: Any, **kw: Any):
        super().__init__(*args, **kw)
        """
        ctypes中，一个ctypes对象赋值给一个ctypes.POINTER类型的字段时
        ctypes会自动创建一个指向该对象的指针,不需要显式地调用ctypes.byref()或ctypes.pointer()来获取对象的引用
        """
        self.symbol = symbol
        self.next = next
        self.prev = prev
        self.count = ctypes.c_uint64(count)


class Stack:

    __slots__ = ['__c_stack']

    def __init__(self,
                 symbol: Symbol = None,
                 next: 'Stack' = None,
                 prev: 'Stack' = None,
                 count: int = 0):
        self.__c_stack = CtypesStack(
            symbol=symbol.c_sym if symbol else None,
            next=next.c_stack if next else None,
            prev=prev.c_stack if next else None,
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
    def count(self, count) -> None:
        self.c_stack.count = ctypes.c_uint64(count)

    @classmethod
    def from_c_stack(cls, c_stack: CtypesStack) -> 'Stack':
        stack = cls()
        stack.__c_stack = c_stack
        return stack


class CtypesAsmCode(ctypes.Structure):

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
                 *args: Any, **kw: Any):
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
                 lineNum: int = 0):
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
    def addr(self, addr) -> None:
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
    def lineNum(self, lineNum) -> None:
        self.c_asm_code.lineNum = ctypes.c_uint(lineNum)

    @classmethod
    def from_c_asm_code(cls, c_asm_code: CtypesAsmCode) -> 'AsmCode':
        asm_code = cls()
        asm_code.__c_asm_code = c_asm_code
        return asm_code


def SymResolverRecordKernel() -> None:
    c_SymResolverRecordKernel = sym_so.SymResolverRecordKernel
    c_SymResolverRecordKernel.argtypes = []
    c_SymResolverRecordKernel.restype = ctypes.c_int

    c_SymResolverRecordKernel()


def SymResolverRecordModule(pid: int) -> None:
    c_SymResolverRecordModule = sym_so.SymResolverRecordModule
    c_SymResolverRecordModule.argtypes = [ctypes.c_int]
    c_SymResolverRecordModule.restype = ctypes.c_int

    c_pid = ctypes.c_int(pid)

    c_SymResolverRecordModule(c_pid)


def SymResolverRecordModuleNoDwarf(pid: int) -> None:
    c_SymResolverRecordModuleNoDwarf = sym_so.SymResolverRecordModuleNoDwarf
    c_SymResolverRecordModuleNoDwarf.argtypes = [ctypes.c_int]
    c_SymResolverRecordModuleNoDwarf.restype = ctypes.c_int

    c_pid = ctypes.c_int(pid)

    c_SymResolverRecordModuleNoDwarf(c_pid)


def StackToHash(pid: int, stackList: List[int]) -> Iterator[Stack]:
    c_StackToHash = sym_so.StackToHash
    c_StackToHash.argtypes = [ctypes.c_int, ctypes.POINTER(ctypes.c_ulong), ctypes.c_int]
    c_StackToHash.restype = ctypes.POINTER(CtypesStack)

    stack_len = len(stackList)
    c_pid = ctypes.c_int(pid)
    c_stack_list = (ctypes.c_ulong * stack_len)(*stackList)
    c_nr = ctypes.c_int(stack_len)

    c_stack  = c_StackToHash(c_pid, c_stack_list, c_nr)
    while c_stack:
        # 此处指针转换可能还有问题，TODO
        stack = Stack.from_c_stack(c_stack)
        yield stack
        c_stack = c_stack.contents.next


def SymResolverMapAddr(pid: int,  addr: int) -> Symbol:
    c_SymResolverMapAddr = sym_so.SymResolverMapAddr
    c_SymResolverMapAddr.argtypes = [ctypes.c_int, ctypes.c_ulong]
    c_SymResolverMapAddr.restype = ctypes.POINTER(CtypesSymbol)

    c_pid = ctypes.c_int(pid)
    c_addr = ctypes.c_ulong(addr)

    c_sym  = c_SymResolverMapAddr(c_pid, c_addr)
    # 此处指针转换可能还有问题，TODO
    return Symbol.from_c_sym(c_sym)


def FreeModuleData(pid: int) -> None:
    c_FreeModuleData = sym_so.FreeModuleData
    c_FreeModuleData.argtypes = [ctypes.c_int]
    c_FreeModuleData.restype = None

    c_pid = ctypes.c_int(pid)

    c_FreeModuleData(c_pid)


def SymResolverDestroy() -> None:
    c_SymResolverDestroy = sym_so.SymResolverDestroy
    c_SymResolverDestroy.argtypes = []
    c_SymResolverDestroy.restype = None

    c_SymResolverDestroy()

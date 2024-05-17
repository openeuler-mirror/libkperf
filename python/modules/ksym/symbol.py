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
Description: ksym symbol module
"""
from typing import List, Iterator

import _libkperf


class Symbol(_libkperf.Symbol):

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
        super().__init__(
            addr=addr,
            module=module,
            symbolName=symbolName,
            fileName=fileName,
            lineNum=lineNum,
            offset=offset,
            codeMapEndAddr=codeMapEndAddr,
            codeMapAddr=codeMapAddr,
            count=count
        )


class Stack(_libkperf.Stack):

    def __init__(self,
                 symbol: Symbol = None,
                 next: 'Stack' = None,
                 prev: 'Stack' = None,
                 count: int = 0):
        super().__init__(
            symbol=symbol.c_sym if symbol else None,
            next=next.c_stack if next else None,
            prev=prev.c_stack if next else None,
            count=count
        )


def record_kernel() -> None:
    _libkperf.SymResolverRecordKernel()


def record_module(pid: int, dwarf: bool = True) -> None:
    if dwarf:
        _libkperf.SymResolverRecordModule(pid)
    else:
        _libkperf.SymResolverRecordModuleNoDwarf(pid)


def get_stack(pid: int, stacks: List[int]) -> Iterator[Stack]:
    return _libkperf.StackToHash(pid, stacks)


def get_symbol(pid: int,  addr: int) -> Symbol:
    return _libkperf.SymResolverMapAddr(pid, addr)


def free_module(pid: int) -> None:
    _libkperf.FreeModuleData(pid)


def destroy() -> None:
    _libkperf.SymResolverDestroy()


__all__ = [
    'Stack',
    'Symbol',
    'record_kernel',
    'record_module',
    'get_stack',
    'get_symbol',
    'free_module',
    'destroy',
]

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
Description: ksym symbol module
"""
from typing import List, Iterator

import _libkperf


class Symbol(_libkperf.Symbol):

    def __init__(self,
                 addr = 0,
                 module = '',
                 symbolName = '',
                 fileName = '',
                 lineNum = 0,
                 offset = 0,
                 codeMapEndAddr = 0,
                 codeMapAddr = 0,
                 count = 0):
        super(Symbol, self).__init__(
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
                 symbol = None,
                 next = None,
                 prev = None,
                 count = 0):
        super(Stack, self).__init__(
            symbol=symbol.c_sym if symbol else None,
            next=next.c_stack if next else None,
            prev=prev.c_stack if prev else None,
            count=count
        )


def record_kernel():
    _libkperf.SymResolverRecordKernel()


def record_module(pid, dwarf = True):
    if dwarf:
        _libkperf.SymResolverRecordModule(pid)
    else:
        _libkperf.SymResolverRecordModuleNoDwarf(pid)


def get_stack(pid, stacks):
    """
    Convert a callstack to an unsigned long long hashid
    """
    return _libkperf.StackToHash(pid, stacks)


def get_symbol(pid,  addr):
    """
    Map a specific address to a symbol
    """
    return _libkperf.SymResolverMapAddr(pid, addr)


def free_module(pid):
    """
    free pid module data
    """
    _libkperf.FreeModuleData(pid)


def destroy():
    _libkperf.SymResolverDestroy()


__all__ = [
    'Symbol',
    'Stack',
    'record_kernel',
    'record_module',
    'get_stack',
    'get_symbol',
    'free_module',
    'destroy',
]
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


class Stack(_libkperf.Stack):
    pass


class Symbol(_libkperf.Symbol):
    pass


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


__all__ = [
    'Stack',
    'Symbol',
    'record_kernel',
    'record_module',
    'get_stack',
    'get_symbol',
    'free_module',
]
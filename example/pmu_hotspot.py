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
Author: Mr.Lei
Create: 2025-03-19
Description: Analyze the original data of performance monitoring unit, and compute the hotspot data.
"""
import os
import time
from collections import defaultdict
import sys

import kperf
import ksym

# Constants
UNKNOWN = "UNKNOWN"
FLOAT_PRECISION = 2
RED_TEXT = "\033[31m"
RESET_COLOR = "\033[0m"
# Initialize the global variable
g_total_period = 0

def process_symbol(symbol):
    if symbol.symbolName and symbol.symbolName != UNKNOWN:
        return symbol.symbolName
    elif symbol.codeMapAddr > 0:
        return f"0x{symbol.codeMapAddr:x}"
    else:
        return f"0x{symbol.addr:x}"


def compare_pmu_data(a, b):
    if a.evt != b.evt:
        return False

    stack_a = a.stack
    stack_b = b.stack

    while stack_a and stack_b:
        symbol_a = process_symbol(stack_a.symbol)
        symbol_b = process_symbol(stack_b.symbol)

        if not symbol_a or not symbol_b or symbol_a != symbol_b:
            return False

        stack_a = stack_a.next
        stack_b = stack_b.next

    return stack_a is None and stack_b is None


def get_pmu_data_hotspot(pmu_data, tmp_data):
    global g_total_period
    if not pmu_data:
        return

    for data in pmu_data.iter:
        if not data.stack:
            continue
        g_total_period += data.period
        is_exist = False
        for tmp in tmp_data:
            if compare_pmu_data(data, tmp):
                is_exist = True
                tmp.period += data.period
                break
        if not is_exist:
            tmp_data.append(data)

    tmp_data.sort(key=lambda x: x.period, reverse=True) # sort by period


def get_period_percent(period):
    return f"{(period * 100 / g_total_period):.{FLOAT_PRECISION}f}"


def print_stack(stack, depth=0, period=0):
    if not stack:
        return
    symbol_name = process_symbol(stack.symbol)
    module_name = stack.symbol.module if stack.symbol.module else UNKNOWN
    print("  " * depth + "|——", end="")
    out_info = f"{symbol_name} {module_name}"
    print(out_info, end="")
    if depth == 0:
        padding = max(0, 110 - len(out_info))
        print(" " * padding + f"{get_period_percent(period)}%")
    else:
        print() # newline
    print_stack(stack.next, depth + 1, period)


def print_hotspot_graph(hotspot_data):
    print("=" * 140)
    print("-" * 140)
    print(f"{'Function':<80}{'Cycles':<20}{'Module':<40}cycles(%)")
    print("-" * 140)
    for data in hotspot_data:
        module_name = getattr(data.stack.symbol, "module", UNKNOWN)
        pos = module_name.rfind("/")
        if pos != -1:
            module_name = module_name[pos + 1:]

        if data.evt == "context-switches":
            print(RED_TEXT, end="")

        func_name = process_symbol(data.stack.symbol)
        if len(func_name) > 78:
            half_len = 78 // 2 - 1
            start_pos = len(func_name) - 78 + half_len + 3
            func_name = func_name[:half_len] + "..." + func_name[start_pos:]

        print(f"  {func_name:<78}{data.period:<20}{module_name:<40}{get_period_percent(data.period)}%")
        if data.evt == "context-switches":
            print(RESET_COLOR, end="")
    print("_" * 140)



def blocked_sample(pid):
    pmu_attr = kperf.PmuAttr(
        pidList = [pid],
        sampleRate = 4000,
        useFreq = True,
        callStack = True,
        blockedSample = True,
        symbolMode = kperf.SymbolMode.RESOLVE_ELF_DWARF
    )

    pd = kperf.open(kperf.PmuTaskType.SAMPLING, pmu_attr)
    if pd == -1:
        print("open failed")
        print(f"error msg: {kperf.error()}")
        return
    
    err = kperf.enable(pd)
    if err != 0:
        print(f"enable failed, error msg: {kperf.error()}")
        return
    time.sleep(1)
    err = kperf.disable(pd)
    if err != 0:
        print(f"disable failed, error msg: {kperf.error()}")
        return
    pmu_data = kperf.read(pd)
    if pmu_data == -1:
        print(f"read failed, error msg: {kperf.error()}")
        return
    hotspot_data = []
    get_pmu_data_hotspot(pmu_data, hotspot_data)
    print_hotspot_graph(hotspot_data)
    print("=" * 50 + "Print the call stack of the hotspot function" + "=" * 50)
    print(f"{'@symbol':<40}{'@module':<40}{'@percent':>40}")
    for data in hotspot_data:
        print_stack(data.stack, 0, data.period)

def start_proc(process):
    pid = os.fork()
    if pid == 0:
        try:
            os.execlp(process, process)
        except Exception as e:
            print(f"Failed to start process: {e}", file=sys.stderr)
        os._exit(1)
    return pid

def end_proc(pid):
    if pid > 0:
        os.kill(pid, 9)

def main():
    pid = 0
    if len(sys.argv) < 2:
        print("Usage: python pmu_hotspot.py <process name>")
        sys.exit(1)

    pid = start_proc(sys.argv[1])
    blocked_sample(pid)
    end_proc(pid)

if __name__ == "__main__":
    main()
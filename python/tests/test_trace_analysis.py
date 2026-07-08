import time
import subprocess
import os
import pytest
import ctypes
import kperf
import ksym
import _libkperf

@pytest.fixture
def run_test_exe():
    """Fixture to run a test executable and return its PID."""
    def _run(exe_path, cpu_list):
        cpu_mask = ",".join(map(str, cpu_list))
        process = subprocess.Popen(["taskset", "-c", cpu_mask, exe_path])
        pid = process.pid
        yield pid
        # Ensure the process is terminated after the test
        process.terminate()
        process.wait()

    return _run
@pytest.fixture
def setup_trace():
    """Fixture to set up and tear down PMU trace resources."""
    pd_list = []

    def _setup(trace_type, pmu_trace_data):
        pd = kperf.trace_open(trace_type, pmu_trace_data)
        if pd == -1:
            pytest.fail(f"Failed to open PMU trace: {kperf.errorno()} - {kperf.error()}")
        pd_list.append(pd)
        return pd
    
    yield _setup

    # Cleanup all PMU trace resources after the test
    for pd in pd_list:
        kperf.trace_disable(pd)
        kperf.trace_close(pd)


def test_config_param_error():
    """Test case for invalid configuration parameters."""
    print("============start to test config param error ===================")
    funcList = ["testName"]
    pmu_trace_data = kperf.PmuTraceAttr(funcs=funcList)

    pd = kperf.trace_open(kperf.PmuTraceType.TRACE_SYS_CALL, pmu_trace_data)
    assert pd == -1, "Expected trace_open to fail with invalid parameters"
    print(f"Error code: {kperf.errorno()}")
    print(f"Error message: {kperf.error()}")


def test_collect_single_trace_data(run_test_exe, setup_trace):
    """Test case for collecting single syscall trace data."""
    print("============start to test collect single syscall and single cpu and single process ===================")
    current_file_path = os.path.abspath(__file__)
    main_dir = os.path.dirname(os.path.dirname(os.path.dirname(current_file_path)))
    # To ensure that the test executable is exists, you need to compile the test case and configure test=true
    sub_dir = "_build/test/test_perf/case/test_12threads"
    app_path = os.path.join(main_dir, sub_dir)
    cpuList = [1]
    # Run the test executable and get the PID
    apppid = next(run_test_exe(app_path, cpuList)) # Use next() to get the PID from the generator
    pidList = [apppid]

    funcList = ["futex"]
    pmu_trace_data = kperf.PmuTraceAttr(funcs=funcList, pidList=pidList, cpuList=cpuList)

    pd = setup_trace(kperf.PmuTraceType.TRACE_SYS_CALL, pmu_trace_data)

    kperf.trace_enable(pd)
    time.sleep(1)
    kperf.trace_disable(pd)

    pmu_trace_data = kperf.trace_read(pd)
    assert pmu_trace_data.iter, "No trace data was captured"
    for data in pmu_trace_data.iter:
        print(f"funcName: {data.funcs} startTs: {data.startTs} elapsedTime: {data.elapsedTime} pid: {data.pid} tid: {data.tid} cpu: {data.cpu} comm: {data.comm}")
    
    kperf.trace_close(pd)


def test_collect_all_syscall_trace_data(setup_trace):
    """Test case for collecting all syscall trace data."""
    print("============start to test collect all syscall and all cpu and all process ===================")
    pmu_trace_data = kperf.PmuTraceAttr()

    pd = setup_trace(kperf.PmuTraceType.TRACE_SYS_CALL, pmu_trace_data)
    
    kperf.trace_enable(pd)
    time.sleep(1)
    kperf.trace_disable(pd)

    pmu_trace_data = kperf.trace_read(pd)
    assert pmu_trace_data.iter, "No trace data was captured"
    for data in pmu_trace_data.iter:
        print(f"funcName: {data.funcs} startTs: {data.startTs} elapsedTime: {data.elapsedTime} pid: {data.pid} tid: {data.tid} cpu: {data.cpu} comm: {data.comm}")
    
    kperf.trace_close(pd)


def _validate_call_stack(stack):
    """Helper function to validate call stack data."""
    assert stack is not None, "Call stack should not be None"
    
    from _libkperf.Symbol import CtypesStack
    
    cur_stack = stack
    stack_depth = 0
    symbols = []
    
    while cur_stack:
        ctypes_stack = ctypes.cast(cur_stack, ctypes.POINTER(CtypesStack))
        if ctypes_stack.contents.symbol:
            ctypes_symbol = ctypes.cast(ctypes_stack.contents.symbol, ctypes.POINTER(ctypes.c_char_p))
            ctypes_symbol = ctypes.cast(ctypes_stack.contents.symbol, ctypes.POINTER(ctypes.py_object))
            symbol_obj = ctypes_stack.contents.symbol
            if symbol_obj:
                symbol_ptr = ctypes.cast(symbol_obj, ctypes.POINTER(_libkperf.CtypesSymbol))
                symbol = symbol_ptr.contents
                
                assert symbol.addr != 0, "Symbol address should not be 0"
                assert symbol.module is not None, "Symbol module should not be None"
                assert len(symbol.module.decode('utf-8')) > 0, "Symbol module should not be empty"
                
                symbols.append({
                    'addr': symbol.addr,
                    'module': symbol.module.decode('utf-8'),
                    'symbolName': symbol.symbolName.decode('utf-8') if symbol.symbolName else None,
                    'fileName': symbol.fileName.decode('utf-8') if symbol.fileName else None,
                    'lineNum': symbol.lineNum
                })
                stack_depth += 1
        
        cur_stack = ctypes_stack.contents.next
    
    assert stack_depth > 0, "Call stack should have at least one frame"
    return symbols


def test_collect_trace_data_with_call_stack(setup_trace):
    """Test case for collecting trace data with call stack enabled."""
    print("============start to test collect trace data with call stack ===================")
    funcList = ["clock_nanosleep"]
    pmu_trace_data = kperf.PmuTraceAttr(funcs=funcList, callStack=True, symbolMode=1)

    pd = setup_trace(kperf.PmuTraceType.TRACE_SYS_CALL, pmu_trace_data)
    
    kperf.trace_enable(pd)
    time.sleep(1)
    kperf.trace_disable(pd)

    pmu_trace_data = kperf.trace_read(pd)
    assert pmu_trace_data.iter, "No trace data was captured"
    
    has_stack = False
    for data in pmu_trace_data.iter:
        print(f"funcName: {data.funcs} startTs: {data.startTs} elapsedTime: {data.elapsedTime} pid: {data.pid} tid: {data.tid} cpu: {data.cpu} comm: {data.comm}")
        if data.stack is not None:
            has_stack = True
            symbols = _validate_call_stack(data.stack)
            print(f"Call stack depth: {len(symbols)}")
            for i, sym in enumerate(symbols):
                print(f"  [{i}] addr={hex(sym['addr'])}, module={sym['module']}, symbol={sym['symbolName']}")
    
    assert has_stack, "At least one trace data should have call stack"
    
    kperf.trace_close(pd)


def test_collect_trace_data_with_dwarf_symbol(setup_trace):
    """Test case for collecting trace data with dwarf symbol mode."""
    print("============start to test collect trace data with dwarf symbol mode ===================")
    funcList = ["clock_nanosleep"]
    pmu_trace_data = kperf.PmuTraceAttr(funcs=funcList, callStack=True, symbolMode=2)

    pd = setup_trace(kperf.PmuTraceType.TRACE_SYS_CALL, pmu_trace_data)
    
    kperf.trace_enable(pd)
    time.sleep(1)
    kperf.trace_disable(pd)

    pmu_trace_data = kperf.trace_read(pd)
    assert pmu_trace_data.iter, "No trace data was captured"
    
    has_stack = False
    has_file_info = False
    for data in pmu_trace_data.iter:
        if data.stack is not None:
            has_stack = True
            symbols = _validate_call_stack(data.stack)
            print(f"Call stack depth: {len(symbols)}")
            for i, sym in enumerate(symbols):
                print(f"  [{i}] addr={hex(sym['addr'])}, module={sym['module']}, symbol={sym['symbolName']}, file={sym['fileName']}, line={sym['lineNum']}")
                if sym['fileName']:
                    has_file_info = True
    
    assert has_stack, "At least one trace data should have call stack"
    
    kperf.trace_close(pd)


if __name__ == '__main__':
    # 提示用户使用pytest 运行测试文件
    print("This is a pytest script. Run it using the 'pytest' command.")
    print("For example: pytest test_*.py -v")
    print("if need print the run log, use pytest test_*.py -s -v")
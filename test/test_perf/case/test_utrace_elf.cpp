#include <stdio.h>

#ifdef __cplusplus
extern "C"
{
#endif

    int single_ret_func(int a)
    {
        return a + 1;
    }

    int multiple_ret_func(int condition)
    {
        if (condition > 10)
        {
            return 1;
        }
        else if (condition == 0)
        {
            return 0;
        }
        return -1;
    }

    void _start()
    {
        single_ret_func(1);
        multiple_ret_func(1);
    }

#ifdef __cplusplus
}
#endif

int cpp_trace_func(int value)
{
    return value + 2;
}

#if defined(__GNUC__)
__attribute__((noinline))
#endif
static int cpp_static_trace_func(int value)
{
    return value + 3;
}

extern "C" int call_cpp_static_trace_func(int value)
{
    return cpp_static_trace_func(value);
}

#if defined(__GNUC__)
extern "C" int cpp_compiler_clone(int value) __asm__("cpp_compiler_clone.constprop.0");
extern "C" __attribute__((noinline)) int cpp_compiler_clone(int value)
{
    return value + 4;
}
#endif

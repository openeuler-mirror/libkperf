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
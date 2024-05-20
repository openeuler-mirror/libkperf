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
Description: ctype python Perror module
"""
import ctypes

from  .Config import UTF_8, kperf_so


def Perrorno() -> int:
    c_Perrorno = kperf_so.Perrorno
    c_Perrorno.argtypes = []
    c_Perrorno.restype = ctypes.c_int
    return c_Perrorno()


def Perror() -> str:
    c_Perror = kperf_so.Perror
    c_Perror.argtypes = []
    c_Perror.restype = ctypes.c_char_p
    return c_Perror().decode(UTF_8)


__all__ = [
    'Perrorno',
    'Perror',
]

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
Description: ctype python Config module
"""
import os
import ctypes

VERSION_MAJOR = 1
VERSION_MINOR = 0
VERSION_BUGFIX = 0
VERSION_SUFFIX = ''
VERSION = '1.0'

UTF_8 = 'utf-8'

def lib_path() -> str:
    return os.path.dirname(os.path.abspath(__file__))


def libsym_path() -> str:
    libsym = 'libsym.so'
    return os.path.join(lib_path(), libsym)


def libkperf_path() -> str:
    libkperf = 'libkperf.so'
    return os.path.join(lib_path(), libkperf)


sym_so = ctypes.CDLL(libsym_path(), mode=ctypes.RTLD_GLOBAL)
kperf_so = ctypes.CDLL(libkperf_path(), mode=ctypes.RTLD_GLOBAL)


__all__ = [
    'UTF_8',
    'VERSION',
    'sym_so',
    'kperf_so',
]

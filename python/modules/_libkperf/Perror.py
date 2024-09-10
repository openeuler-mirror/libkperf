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

from  .Config import UTF_8, sym_so


def Perrorno() -> int:
    """
    int Perrorno();
    """
    c_Perrorno = sym_so.Perrorno
    c_Perrorno.argtypes = []
    c_Perrorno.restype = ctypes.c_int

    return c_Perrorno()


def Perror() -> str:
    """
    const char* Perror();
    """
    c_Perror = sym_so.Perror
    c_Perror.argtypes = []
    c_Perror.restype = ctypes.c_char_p

    return c_Perror().decode(UTF_8)


def GetWarn() -> int:
    """
    int GetWarn();
    """
    c_GetWarn = sym_so.GetWarn
    c_GetWarn.argtypes = []
    c_GetWarn.restype = ctypes.c_int

    return c_GetWarn()


def GetWarnMsg() -> str:
    """
    const char* GetWarnMsg();
    """
    c_GetWarnMsg = sym_so.GetWarnMsg
    c_GetWarnMsg.argtypes = []
    c_GetWarnMsg.restype = ctypes.c_char_p

    return c_GetWarnMsg().decode(UTF_8)


__all__ = [
    'Perrorno',
    'Perror',
    'GetWarn',
    'GetWarnMsg',
]

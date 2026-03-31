/******************************************************************************
 * Copyright (c) Huawei Technologies Co., Ltd. 2024. All rights reserved.
 * libkperf licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *     http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR
 * PURPOSE.
 * See the Mulan PSL v2 for more details.
 * Author: Mr.Li
 * Create: 2024-04-03
 * Description: Reverse the symbols in the C ++ ABI specification and convert it into a readable function name.
 ******************************************************************************/
#include <cxxabi.h>
#include <iostream>
#include <memory>
#include <name_resolve.h>

char* CppNamedDemangle(const char* abiName)
{
    int status;
    char* name = abi::__cxa_demangle(abiName, nullptr, nullptr, &status);
    if (status != 0) {
        return nullptr;
    }
    return name;
}

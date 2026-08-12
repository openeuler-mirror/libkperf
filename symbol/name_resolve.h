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
#ifndef CPP_NAME_RESOLVE_H
#define CPP_NAME_RESOLVE_H

#ifdef __cpusplus
extern "C" {
#endif
/** For further implementation such as support for python, rust or java name
 * demangel, APIs should be implemented here */
char* CppNamedDemangle(const char* abiName);
#ifdef __cpusplus
}
#endif

#endif

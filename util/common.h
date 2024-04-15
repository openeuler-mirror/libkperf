/******************************************************************************
 * Copyright (c) Huawei Technologies Co., Ltd. 2024. All rights reserved.
 * gala-gopher licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *     http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR
 * PURPOSE.
 * See the Mulan PSL v2 for more details.
 * Author: Mr.Wang
 * Create: 2024-04-03
 * Description: Provide common file operation functions and system resource management functions.
 ******************************************************************************/

#ifndef LIBKPROF_COMMON_H
#define LIBKPROF_COMMON_H
#include <string>

std::string GetRealPath(const std::string filePath);
bool IsValidPath(const std::string& filePath);
int RaiseNumFd(uint64_t numFd);

#endif  // LIBKPROF_COMMON_H

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
 * Author: Mr.Wang
 * Create: 2024-04-03
 * Description: Error code mechanism, used to return error codes and error messages.
 ******************************************************************************/
#ifndef SYMBOL_PCERR_H
#define SYMBOL_PCERR_H
#include <iostream>
#include <memory>
#include <string>
#include <sstream>
#include "pcerrc.h"

namespace pcerr {
    void [[nodiscard]] New(int code);
    void [[nodiscard]] New(int code, const std::string& msg);
    void [[nodiscard]] SetWarn(int warn);
    void [[nodiscard]] SetWarn(int warn, const std::string& msg);
    /**
     * @brief used to store custom information of the inner layer, The New interface is used to obtain the information
     */
    void [[nodiscard]] SetCustomErr(int code, const std::string& msg);
}  // namespace pcerr

#endif
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
    namespace details {
        struct BaseError;
    }

    using ProfError = std::shared_ptr<details::BaseError>;

    namespace details {
        struct BaseError : std::enable_shared_from_this<BaseError> {
            virtual ~BaseError() = 0;
            virtual const char* Msg() const
            {
                return nullptr;
            }
            virtual int Code() const
            {
                return 0;
            }
            ProfError GetThisError()
            {
                return shared_from_this();
            }
        };
        inline BaseError::~BaseError()
        {}
    }  // namespace details

    namespace details {
        class CodeStrMsgError : public BaseError {
        public:
            CodeStrMsgError(int code, const std::string& msg) : m_code{code}, m_msg{msg}
            {}
            int Code() const override
            {
                return this->m_code;
            };
            const char* Msg() const override
            {
                return this->m_msg.c_str();
            };

        private:
            int m_code;
            std::string m_msg;
        };
    }  // namespace details
    class ProfErrorObj {
    public:
        static ProfErrorObj& GetInstance();
        void SetProfError(const ProfError& profError);
        ProfError& GetProfError()
        {
            if (profError == nullptr) {
                profError = std::make_shared<details::CodeStrMsgError>(0, "success");
            }
            return profError;
        }

    private:
        ProfError profError;
        static ProfErrorObj profErrorObj;
    };

    void [[nodiscard]] New(int code);
    void [[nodiscard]] New(int code, const std::string& msg);
    void [[nodiscard]] SetWarn(int warn);
}  // namespace pcerr

#endif
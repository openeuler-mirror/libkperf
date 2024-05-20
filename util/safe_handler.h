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
 * Description: Provide the key value processing function of thread security.
 ******************************************************************************/
#ifndef SAFE_HANDLER_H
#define SAFE_HANDLER_H
#include <mutex>
#include <iostream>
#include <unistd.h>
#include <unordered_set>

template <typename Key>
class SafeHandler {
public:
    SafeHandler()
    {}
    void tryLock(const Key& key)
    {
        while (true) {
            if (find(key)) {
                continue;
            }
            {
                std::unique_lock<std::mutex> lock(_mutex);
                if (find(key)) {
                    continue;
                }
                insert(key);
            }
            break;
        }
    }

    void releaseLock(const Key& key)
    {
        std::unique_lock<std::mutex> lock(_smutex);
        _set.erase(key);
    }

private:
    std::mutex _mutex;
    std::mutex _smutex;
    std::unordered_set<Key> _set;

    void insert(const Key& key)
    {
        std::unique_lock<std::mutex> lock(_smutex);
        _set.insert(key);
    }

    bool find(const Key& key)
    {
        std::unique_lock<std::mutex> lock(_smutex);
        if (_set.find(key) != _set.end()) {
            return true;
        }
        return false;
    }
};

#endif


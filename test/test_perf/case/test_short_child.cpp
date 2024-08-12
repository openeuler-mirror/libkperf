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
 * Create: 2024-08-17
 * Description: test for short child thread.
 ******************************************************************************/
#include <iostream>
#include <unistd.h>
#include <thread>

// This time cause the child thread to end before the perf event opens, which may be inconsistent due to computer
// performance or code changes.
const int SLEEP_TIME = 2200;

void SleepSomeTime()
{
    usleep(SLEEP_TIME);
}

int main()
{
    std::thread th1;
    th1 = std::thread(SleepSomeTime);
    std::thread th2;
    th2 = std::thread(SleepSomeTime);
    th2.join();
    th1.join();
    while (true) {
        sleep(1);
    }
}
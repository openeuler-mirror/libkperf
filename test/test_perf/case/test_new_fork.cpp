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
 * Description: test for short new fork thread.
 ******************************************************************************/
#include <iostream>
#include <unistd.h>
#include <thread>

void sum()
{
    int sum  = 0;
    for (int i = 0; i < 2000; i++) {
        sleep(1000);
        sum += i;
    }
}

int main()
{
    std::thread th1;
    std::thread th2;
    std::thread th3;
    th1 = std::thread(sum);

    sleep(2);
    th2 = std::thread(sum);
    th3 = std::thread(sum);
    th3.join();
    th2.join();
    th1.join();

    std::thread th4 = std::thread(sum);
    th4.join();

    while (true) {
        sleep(1);
    }
}
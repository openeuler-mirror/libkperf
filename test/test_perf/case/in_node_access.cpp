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
 * Author: Mr.Gan
 * Create: 2024-04-26
 * Description: Two threads running on the same numa node.
 ******************************************************************************/
#include <thread>
#include <numa.h>
#include <stdlib.h>

using namespace std;

void func(int *val)
{
    while(1) {
        (*val)++;
    }
}

int main()
{
    int *a = new int();
    pthread_t t1,t2;
    pthread_create(&t1, NULL, (void* (*)(void*))func, a);
    pthread_create(&t2, NULL, (void* (*)(void*))func, a);

    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(0, &cpuset);
    pthread_setaffinity_np(t1, sizeof(cpu_set_t), &cpuset);

    cpu_set_t cpuset2;
    CPU_ZERO(&cpuset2);
    CPU_SET(1, &cpuset2);
    pthread_setaffinity_np(t2, sizeof(cpu_set_t), &cpuset2);

    pthread_join(t1, NULL);
    pthread_join(t2, NULL);
    return 0;
}
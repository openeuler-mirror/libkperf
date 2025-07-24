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
 * Description: allocate on numa node 1.
 ******************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <numa.h>
#include <signal.h>
#include <unistd.h>

int main() {
    raise(SIGSTOP);
    usleep(10000);

    int len = 1024*256;
    for (int j=0;j<64;++j) {
        int *data = (int *)numa_alloc_onnode(len * sizeof(int), 1);
        for (int i=0;i<len;++i) {
            data[i] = rand();
        }
    }
    return 0;
}

/******************************************************************************
 * Copyright (c) Huawei Technologies Co., Ltd. 2024. All rights reserved.
 * libkperf is licensed under Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *     http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR
 * PURPOSE.
 * See the Mulan PSL v2 for more details.
 * Author: Mr.Li
 * Create: 2024-04-03
 * Description: Provide a simple case for test_libsym.
 ******************************************************************************/
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/timeb.h>

static int num = 0;
static long long int count = 0xFFFFFFFFFF;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

void Perror(const char *s)
{
    perror(s);
    exit(EXIT_FAILURE);
}

long long getSystemTime() {
    struct timeb t;
    ftime(&t);
    return 1000 * t.time + t.millitm;
}

void* fun2(void *arg)
{
    pthread_t thread_id = pthread_self();
    printf("the thread2 id is %ld\n", (long)thread_id);
    int i = 1;
    for (; i<=count; ++i) {
        pthread_mutex_lock(&mutex);
        num += i*1;
        pthread_mutex_unlock(&mutex);
    }
}

int main()
{
    int err;
    pthread_t thread1;
    pthread_t thread2;

    thread1 = pthread_self();
    printf("the thread1 id is %ld\n", (long)thread1);

    long long start = getSystemTime();

    // Create thread
    err = pthread_create(&thread2, NULL, fun2, NULL);
    if (err != 0) {
        Perror("can't create thread2\n");
    }

    int i = 1;
    for (; i<=count; ++i) {
        pthread_mutex_lock(&mutex);
        num += i*1;
        pthread_mutex_unlock(&mutex);
    }

    pthread_join(thread2, NULL);
    long long end = getSystemTime();

    printf("The num is %d, pay %lld ms\n", num, (end-start));

    return 0;
}
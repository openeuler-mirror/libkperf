#include <sched.h>
#include <cstring>
#include <pthread.h>
#include <stdio.h>

#define EXE_TIME 9999999900
#define NUM_THREADS 2

int arr[32];

void *sum_a(void*)
{
    int cpu_num = 0;
    cpu_set_t mask;
    cpu_set_t get;
    CPU_ZERO(&mask);
    CPU_SET(cpu_num, &mask);
    if (sched_setaffinity(0, sizeof(mask), &mask) == -1) {
        perror("warning: could not set CPU affinity\n");
    }
    CPU_ZERO(&get);
     if (sched_getaffinity(0, sizeof(get), &get) == -1) {
        perror("warning: could not get CPU affinity\n");
    }

    if (CPU_ISSET(cpu_num, &get)) {
        printf("sum_a is running in %d cpu_id: %d\n", get, cpu_num);
    }

    int s = 0;
    for (int i = 0; i < EXE_TIME; i++) {
        s = arr[0];
        arr[0] += 1;
    }
}

void *inc_b(void*)
{
    int cpu_num = 1;
    cpu_set_t mask;
    cpu_set_t get;
    CPU_ZERO(&mask);
    CPU_SET(cpu_num, &mask);
    if (sched_setaffinity(0, sizeof(mask), &mask) == -1) {
        perror("warning: could not set CPU affinity\n");
    }
    CPU_ZERO(&get);
     if (sched_getaffinity(0, sizeof(get), &get) == -1) {
        perror("warning: could not get CPU affinity\n");
    }

    if (CPU_ISSET(cpu_num, &get)) {
        printf("sum_a is running in %d cpu_id: %d\n", get, cpu_num);
    }

    int s = 0;
    for (int i = 0; i < EXE_TIME; i++) {
        s = arr[1];
        arr[1] += 1;
    }
}

int main()
{
    int ret;
    pthread_t tids[NUM_THREADS];
    ret = pthread_create(&tids[0], NULL, sum_a, NULL);
    if (ret != 0) {
        printf("pthread_create error: error code %d\n", ret);
        return -1;
    }

    ret = pthread_create(&tids[0], NULL, inc_b, NULL);
    if (ret != 0) {
        printf("pthread_create error: error code %d\n", ret);
        return -1;
    }

    pthread_join(tids[0], NULL);
    pthread_join(tids[1], NULL);
    return 0;
}


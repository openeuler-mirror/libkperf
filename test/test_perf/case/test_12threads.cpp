#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#define MAX 1000000

int primes[MAX];
int num_primes = 0;

int func() {
    int ret = 0;
    for (int i=0;;++i) {
        ret += rand()%7;
    }
    return ret;
}

int main()
{
    pthread_t threads[12];

    for (int i=0;i<12;++i) {
        pthread_create(&threads[i], NULL, (void* (*)(void*))func, NULL);
    }

    for (int i=0;i<12;++i) {
        pthread_join(threads[i], NULL);
    }

    return 0;
}
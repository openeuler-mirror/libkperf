#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <signal.h>

int func()
{
    int ret = 0;
    for (int i=0;;++i) {
        ret += rand() % 7;
    }
    return ret;
}

void *thread_func1(void *arg)
{
    func();
    return NULL;
}

void *thread_func2(void *arg)
{
    func();
    return NULL;
}

int main()
{
    raise(SIGSTOP);
    pthread_t tid1, tid2;
    pthread_create(&tid1, NULL, thread_func1, NULL);
    pthread_create(&tid2, NULL, thread_func2, NULL);
    pthread_join(tid1, NULL);
    pthread_join(tid2, NULL);
    return 0;
}
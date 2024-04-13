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
        int *data = (int *)numa_alloc_onnode(len * sizeof(int), 2);
        for (int i=0;i<len;++i) {
            data[i] = rand();
        }
    }
    return 0;
}

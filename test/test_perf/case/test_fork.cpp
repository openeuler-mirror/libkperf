#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

struct Data {
    volatile int a;
    volatile int b;
};

int func(struct Data *data)
{
    while (1) {
        for (int i=0;i<1000000;++i) {
            data->a++;
        }
    }
    return 1;
}

int main()
{
    raise(SIGSTOP);
    pid_t pid_c;
    pid_c = fork();
    if (pid_c == 0) {
        struct Data data;
        int ret = func(&data);
    } else if (pid_c > 0) {
        wait(NULL);
    } else {
        perror("fork");
        exit(1);
    }

    return 0;
}
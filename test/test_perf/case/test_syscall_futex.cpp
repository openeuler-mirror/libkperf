#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>

int main()
{
    static int futexWord = 0;
    while (true) {
        syscall(SYS_futex, &futexWord, FUTEX_WAKE_PRIVATE, 1, nullptr, nullptr, 0);
        usleep(1000);
    }
    return 0;
}

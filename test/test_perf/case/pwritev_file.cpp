#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/uio.h>
#include <fcntl.h>
#include <string.h>

int main()
{
    char *str0 = "hello ";
    char *str1 = "world\n";

    struct iovec iov[2];
    ssize_t nwritten;

    iov[0].iov_base = str0;
    iov[0].iov_len = strlen(str0);
    iov[1].iov_base = str1;
    iov[1].iov_len = strlen(str1);

    int fd = open("/tmp/libperf_ut_write", O_RDWR | O_CREAT);
    for (int i = 0;i < 1024 * 64 * 1024; ++i) {
        pwritev(fd, iov, 2, 0);
    }
    close(fd);
    return 0;
}
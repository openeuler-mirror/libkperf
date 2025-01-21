#include <iostream>
#include <unistd.h>
#include <cstring>

int main() {
    int pipefd[2];
    char buffer;

    if (pipe(pipefd) == -1) {
        perror("pipe");
        return 1;
    }

    const char initial_data = 'A';
    if (write(pipefd[1], &initial_data, 1) != 1) {
        perror("initial write");
        close(pipefd[0]);
        close(pipefd[1]);
        return 1;
    }

    while (true) {
        ssize_t bytesRead = read(pipefd[0], &buffer, 1);

        if (bytesRead == -1) {
            perror("read");
            break;
        } else if (bytesRead == 0) {
            std::cout << "End of pipe reached." << std::endl;
            break;
        }

        ssize_t bytesWritten = write(pipefd[1], &buffer, 1);

        if (bytesWritten == -1) {
            perror("write");
            break;
        }

    }

    close(pipefd[0]);
    close(pipefd[1]);

    return 0;
}
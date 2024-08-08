
#include <iostream>
#include <unistd.h>
#include <thread>

void sum() {
    sleep(5);
}

int main() {
    std::thread th1;
    std::thread th2;

    th1 = std::thread(sum);
    sleep(2);
    th2 = std::thread(sum);
    th2.join();
    th1.join();

    while (true) {
        sleep(1);
    }
}
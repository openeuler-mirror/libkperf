#include <sys/timerfd.h>
#include <unistd.h>
#include <time.h>
#include <stdint.h>
#include <iostream>
#include <chrono>
#include <thread>

void on_cpu_task(int milliseconds)
{
    struct timespec start, now;
    clock_gettime(CLOCK_MONOTONIC, &start);
    long target_ns = start.tv_nsec + milliseconds * 1000000L;
    long target_s = start.tv_sec + target_ns / 1000000000L;
    target_ns %= 1000000000L;

    do {
        clock_gettime(CLOCK_MONOTONIC, &now);
    } while (now.tv_sec < target_s || (now.tv_sec == target_s && now.tv_nsec < target_ns));
}

void off_cpu_task(int milliseconds)
{
    int tfd = timerfd_create(CLOCK_MONOTONIC, 0);
    if (tfd == -1) {
        std::cerr << "Failed to create timerfd" << std::endl;
        return;
    }

    struct itimerspec timer = {
        .it_interval = { 
            .tv_sec = 0,
            .tv_nsec = 0
        },
        .it_value = {
            .tv_sec = milliseconds / 1000,
            .tv_nsec = (milliseconds % 1000) * 1000000L
        }        
    };

    if (timerfd_settime(tfd, 0, &timer, nullptr) == -1) {
        std::cerr << "Failed to set timerfd" << std::endl;
        close(tfd);
        return;
    }

    uint64_t expirations;
    if (read(tfd, &expirations, sizeof(expirations)) == -1) {
        std::cerr << "Failed to read timerfd" << std::endl;
    }

    close(tfd);
}

int main()
{
    std::cout << "Process ID: " << getpid() << std::endl;
    int milliseconds = 500;
    auto on_cpu_start = std::chrono::high_resolution_clock::now();
    on_cpu_task(milliseconds);
    auto on_cpu_end = std::chrono::high_resolution_clock::now();
    auto off_cpu_start = std::chrono::high_resolution_clock::now();
    off_cpu_task(milliseconds);
    auto off_cpu_end = std::chrono::high_resolution_clock::now();

    auto on_cpu_time = std::chrono::duration_cast<std::chrono::nanoseconds>(on_cpu_end - on_cpu_start);
    auto off_cpu_time = std::chrono::duration_cast<std::chrono::nanoseconds>(off_cpu_end - off_cpu_start);
    std::cout << "On CPU time: " << on_cpu_time.count() << " ns" << std::endl;
    std::cout << "Off CPU time: " << off_cpu_time.count() << " ns" << std::endl;

    return 0;
}
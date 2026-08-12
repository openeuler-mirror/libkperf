#include <iostream>
#include <cmath>
#include <unistd.h>

void function40()
{
    volatile double sum = 0;
    for (int i = 0; i < 40000000; i++) {
        sum += std::sqrt(i);
    }
}

void function30()
{
    volatile double sum = 0;
    for (int i = 0; i < 30000000; i++) {
        sum += std::sqrt(i);
    }
}

void function20()
{
    volatile double sum = 0;
    for (int i = 0; i < 20000000; i++) {
        sum += std::sqrt(i);
    }
}

void function10()
{
    volatile double sum = 0;
    for (int i = 0; i < 10000000; i++) {
        sum += std::sqrt(i);
    }
}

int main()
{
    std::cout << "Process ID: " << getpid() << std::endl;
    function10();
    function20();
    function30();
    function40();
    return 0;
}
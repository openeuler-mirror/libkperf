#include <thread>
#include <chrono>

extern "C" void my_function()
{
    static int counter = 0;
    counter++;
}

int main()
{
    while (true)
    {
        my_function();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return 0;
}
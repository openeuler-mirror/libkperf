#include <thread>
#include <cstdio>

using namespace std;

int len = 1000000;

struct Data {
    volatile int a;
    volatile int b;
};

void func(struct Data *data)
{
    while (1) {
        for (int i = 0; i< len;++i)
            for (int j = 0; j< len;++j)
                data->a++;
    }
}

int main()
{
    struct Data data;
    func(&data);
    return 0;
}
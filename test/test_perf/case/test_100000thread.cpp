#include <pthread.h>
#include <iostream>
#include <vector>
#include <unistd.h>
#include <random>

using namespace std;

void RandomCase()
{
    int len = 100000;
    int *cond = new int[len];
    for (int i = 0; i < len; ++i)
    {
        cond[i] = rand();
    }
    int sum = 0;
    for (int i = 0; i < len; ++i)
    {
        if (cond[i] % 4)
        {
            sum++;
        }
        else
        {
            sum--;
        }
    }
}

void *ThreadFunc(void *data)
{
    int *n = (int *)data;
    RandomCase();
    delete n;
    return nullptr;
}

int main()
{
    const int threadNum = 100000;
    pthread_t threads[threadNum];
    for (int i = 0; i < threadNum; i++)
    {
        int *array = new int(100);
        if (pthread_create(&threads[i], NULL, ThreadFunc, array) != 0)
        {
            cerr << "failed to create thread:" << i << endl;
            continue;
        }
    }
    for (int i = 0; i < threadNum; i++)
    {
        void *retval;
        pthread_join(threads[i], &retval);
    }
    cout << "compilte" << endl;
    return 0;
}
int main()
{
    int len = 1000000000;
    int *a = new int[len];
    int *b = new int[len];
    int sum = 0;
    for (int i = 0; i < len; ++i) {
        sum += a[i] + b[i];
    }

    return sum;
}
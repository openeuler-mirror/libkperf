#include <iostream>
#include <vector>
#include <chrono>
#include <unistd.h>

#define ARRAY_SIZE (1024 * 1024 * 512) // 512MB, ensuring it exceeds L3 cache
#define STRIDE 64 // Memory access stride (simulating cache line access)

void memory_read_test(std::vector<int> &array) {
    volatile int sum = 0; // Prevent compiler optimization
    auto start = std::chrono::high_resolution_clock::now();

    while (true) { // Infinite loop
        for (size_t i = 0; i < array.size(); i += STRIDE) {
            sum += array[i]; // Memory access operation
        }

        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> elapsed = end - start;
        double readCnt = (array.size() * sizeof(int)) / (elapsed.count() * 1024 * 1024 * 1024); // GB/s

        std::cout << "Data throughput: " << readCnt << " GB/s" << std::endl;
        start = end; // Reset timer
    }
}

int main() {
    std::vector<int> memory_array(ARRAY_SIZE, 1); // Initialize a large array
    memory_read_test(memory_array);
    return 0;
}
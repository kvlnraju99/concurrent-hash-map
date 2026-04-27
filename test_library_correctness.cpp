#include <iostream>
#include <vector>
#include <atomic>
#include <omp.h>
#include <cassert>
#include "concurrent_hash_map.h"

/**
 * Library Correctness Test
 * Verifies that the ConcurrentHashMap handles parallel operations correctly.
 */
int main() {
    const int num_buckets = 10000;
    ConcurrentHashMap<int, int> map(num_buckets);
    
    int max_threads = omp_get_max_threads();
    const int OPS_PER_THREAD = 10000;
    const int TOTAL_EXPECTED = max_threads * OPS_PER_THREAD;

    std::cout << "==========================================" << std::endl;
    std::cout << " LIBRARY CORRECTNESS TEST" << std::endl;
    std::cout << " Threads: " << max_threads << std::endl;
    std::cout << " Total Operations: " << TOTAL_EXPECTED << std::endl;
    std::cout << "==========================================" << std::endl;

    // --- TEST 1: PARALLEL WRITES ---
    std::cout << "Test 1: Running Parallel Writes..." << std::endl;
    #pragma omp parallel
    {
        int tid = omp_get_thread_num();
        for (int i = 0; i < OPS_PER_THREAD; ++i) {
            int key = tid * OPS_PER_THREAD + i;
            map.put(key, key * 10);
        }
    }

    // --- TEST 2: PARALLEL READS & VERIFICATION ---
    std::cout << "Test 2: Verifying Data Integrity..." << std::endl;
    std::atomic<int> errors{0};
    #pragma omp parallel
    {
        int tid = omp_get_thread_num();
        for (int i = 0; i < OPS_PER_THREAD; ++i) {
            int key = tid * OPS_PER_THREAD + i;
            auto val = map.get(key);
            if (!val.has_value() || val.value() != key * 10) {
                errors++;
            }
        }
    }

    // --- TEST 3: PARALLEL UPDATES ---
    std::cout << "Test 3: Testing Parallel Updates..." << std::endl;
    #pragma omp parallel
    {
        int tid = omp_get_thread_num();
        for (int i = 0; i < OPS_PER_THREAD; ++i) {
            int key = tid * OPS_PER_THREAD + i;
            map.put(key, key * 20); // Update value
        }
    }

    #pragma omp parallel
    {
        int tid = omp_get_thread_num();
        for (int i = 0; i < OPS_PER_THREAD; ++i) {
            int key = tid * OPS_PER_THREAD + i;
            auto val = map.get(key);
            if (!val.has_value() || val.value() != key * 20) {
                errors++;
            }
        }
    }

    // --- TEST 4: PARALLEL REMOVALS ---
    std::cout << "Test 4: Testing Parallel Removals..." << std::endl;
    #pragma omp parallel
    {
        int tid = omp_get_thread_num();
        // Each thread removes its own half of the keys
        for (int i = 0; i < OPS_PER_THREAD / 2; ++i) {
            int key = tid * OPS_PER_THREAD + i;
            if (!map.remove(key)) {
                errors++;
            }
        }
    }

    size_t expected_size = TOTAL_EXPECTED - (max_threads * (OPS_PER_THREAD / 2));
    
    // --- FINAL RESULTS ---
    std::cout << "==========================================" << std::endl;
    std::cout << "Final Map Size: " << map.size() << " (Expected: " << expected_size << ")" << std::endl;
    std::cout << "Verification Errors: " << errors.load() << std::endl;

    if (errors == 0 && map.size() == expected_size) {
        std::cout << "RESULT: SUCCESS (Library is Thread-Safe)" << std::endl;
    } else {
        std::cout << "RESULT: FAILURE (Data Inconsistency Found)" << std::endl;
    }
    std::cout << "==========================================" << std::endl;

    return (errors == 0) ? 0 : 1;
}

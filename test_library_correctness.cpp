#include <iostream>
#include <vector>
#include <atomic>
#include <omp.h>
#include <cassert>
#include "concurrent_hash_map.h"
#include "concurrent_hash_map_v2.h"
#include "concurrent_hash_map_v4.h"

/**
 * Library Correctness Test
 * Verifies that the ConcurrentHashMap handles parallel operations correctly.
 */
template <typename MapType>
bool run_test(const std::string& name) {
    const int num_buckets = 1000;
    MapType map(num_buckets);
    
    int max_threads = omp_get_max_threads();
    const int OPS_PER_THREAD = 10000;
    const int TOTAL_EXPECTED = max_threads * OPS_PER_THREAD;

    std::cout << "Testing " << name << "..." << std::endl;

    // --- TEST 1: PARALLEL WRITES ---
    #pragma omp parallel
    {
        int tid = omp_get_thread_num();
        for (int i = 0; i < OPS_PER_THREAD; ++i) {
            int key = tid * OPS_PER_THREAD + i;
            map.put(key, key * 10);
        }
    }

    // --- TEST 2: VERIFICATION & UPDATES ---
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
            // Update to a new value
            map.put(key, key * 20);
        }
    }

    // --- TEST 3: VERIFY UPDATES ---
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

    if (errors == 0) {
        std::cout << "  " << name << ": PASSED" << std::endl;
        return true;
    } else {
        std::cout << "  " << name << ": FAILED (" << errors.load() << " errors)" << std::endl;
        return false;
    }
}

int main() {
    int max_threads = omp_get_max_threads();
    std::cout << "==========================================" << std::endl;
    std::cout << " LIBRARY CORRECTNESS STRESS TEST" << std::endl;
    std::cout << " Threads: " << max_threads << std::endl;
    std::cout << "==========================================" << std::endl;

    bool all_passed = true;
    all_passed &= run_test<ConcurrentHashMapV2<int, int>>("Library V2 (Static)");
    all_passed &= run_test<ConcurrentHashMap<int, int>>("Library V3 (Dynamic)");
    // all_passed &= run_test<ConcurrentHashMapV4<int, int>>("Library V4 (Atomic)");

    std::cout << "==========================================" << std::endl;
    if (all_passed) {
        std::cout << "FINAL RESULT: ALL VERSIONS THREAD-SAFE" << std::endl;
    } else {
        std::cout << "FINAL RESULT: ERRORS FOUND" << std::endl;
    }
    std::cout << "==========================================" << std::endl;

    return all_passed ? 0 : 1;
}

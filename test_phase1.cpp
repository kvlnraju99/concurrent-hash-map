#include <iostream>
#include <vector>
#include <atomic>
#include <omp.h>
#include "naive_map.h"

/**
 * Phase 1 Test: Concurrency & Correctness
 * 1. Parallel Writes: Multiple threads insert unique keys.
 * 2. Parallel Reads: Multiple threads retrieve those keys and verify values.
 */
int main() {
    NaiveHashMap<int, int> map;
    
    // Determine number of threads from OMP_NUM_THREADS environment variable
    int total_threads = omp_get_max_threads();
    const int OPS_PER_THREAD = 10000;
    const int TOTAL_EXPECTED = total_threads * OPS_PER_THREAD;

    std::cout << "==========================================" << std::endl;
    std::cout << " PHASE 1: NAIVE BASELINE TEST" << std::endl;
    std::cout << " Threads: " << total_threads << std::endl;
    std::cout << " Total Operations: " << TOTAL_EXPECTED * 2 << " (Put + Get)" << std::endl;
    std::cout << "==========================================" << std::endl;

    // --- PHASE A: PARALLEL WRITES (PUT) ---
    std::cout << "Running Parallel Writes..." << std::endl;
    double start_put = omp_get_wtime();

    #pragma omp parallel
    {
        int tid = omp_get_thread_num();
        for (int i = 0; i < OPS_PER_THREAD; ++i) {
            int key = tid * OPS_PER_THREAD + i;
            map.put(key, key * 10); // Value is key * 10
        }
    }

    double end_put = omp_get_wtime();
    std::cout << "Writes completed in: " << (end_put - start_put) << "s" << std::endl;

    // --- PHASE B: PARALLEL READS (GET) ---
    std::cout << "Running Parallel Reads & Verification..." << std::endl;
    std::atomic<int> errors{0};
    double start_get = omp_get_wtime();

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

    double end_get = omp_get_wtime();
    std::cout << "Reads completed in: " << (end_get - start_get) << "s" << std::endl;

    // --- FINAL RESULTS ---
    std::cout << "==========================================" << std::endl;
    std::cout << "Final Map Size: " << map.size() << " (Expected: " << TOTAL_EXPECTED << ")" << std::endl;
    std::cout << "Verification Errors: " << errors.load() << std::endl;

    if (errors == 0 && map.size() == (size_t)TOTAL_EXPECTED) {
        std::cout << "RESULT: SUCCESS (Baseline is Thread-Safe)" << std::endl;
    } else {
        std::cout << "RESULT: FAILURE (Data Inconsistency Found)" << std::endl;
    }
    std::cout << "==========================================" << std::endl;

    return (errors == 0) ? 0 : 1;
}

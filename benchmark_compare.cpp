#include <iostream>
#include <vector>
#include <iomanip>
#include <omp.h>
#include "naive_map.h"
#include "concurrent_hash_map.h"

template <typename MapType>
void run_benchmark(const std::string& name, int num_threads, int ops_per_thread) {
    MapType map;
    
    double start = omp_get_wtime();

    #pragma omp parallel num_threads(num_threads)
    {
        int tid = omp_get_thread_num();
        // Phase 1: Writes
        for (int i = 0; i < ops_per_thread; ++i) {
            map.put(tid * ops_per_thread + i, i);
        }
        
        // Phase 2: Reads
        #pragma omp barrier
        for (int i = 0; i < ops_per_thread; ++i) {
            map.get(tid * ops_per_thread + i);
        }
    }

    double end = omp_get_wtime();
    std::cout << std::left << std::setw(20) << name 
              << " | Threads: " << std::setw(2) << num_threads 
              << " | Time: " << std::fixed << std::setprecision(4) << (end - start) << "s" << std::endl;
}

int main() {
    int max_threads = omp_get_max_threads();
    const int OPS = 100000;

    std::cout << "==========================================================" << std::endl;
    std::cout << " BENCHMARK: NAIVE GLOBAL LOCK vs. BUCKET-LEVEL LOCKING" << std::endl;
    std::cout << " Operations per thread: " << OPS << " (Put + Get)" << std::endl;
    std::cout << "==========================================================" << std::endl;

    for (int t : {1, 4, 8, max_threads}) {
        if (t > max_threads) continue;
        run_benchmark<NaiveHashMap<int, int>>("Naive (Global)", t, OPS);
        run_benchmark<ConcurrentHashMap<int, int>>("Library (Bucket)", t, OPS);
        std::cout << "----------------------------------------------------------" << std::endl;
    }

    return 0;
}

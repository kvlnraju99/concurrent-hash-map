#include <iostream>
#include <vector>
#include <iomanip>
#include <omp.h>
#include "naive_map.h"
#include "concurrent_hash_map.h"
#include "concurrent_hash_map_v2.h"

template <typename MapType>
void run_benchmark(const std::string& name, int num_threads, int ops_per_thread, size_t num_buckets) {
    MapType map(num_buckets);
    
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

int main(int argc, char* argv[]) {
    int max_threads = omp_get_max_threads();
    size_t bucket_count = 131071; // Default

    if (argc > 1) {
        bucket_count = std::stoull(argv[1]);
    }

    std::cout << "==========================================================" << std::endl;
    std::cout << " BENCHMARK SUITE: NAIVE vs. BUCKET-LEVEL LOCKING" << std::endl;
    std::cout << " Bucket count: " << bucket_count << std::endl;
    std::cout << "==========================================================" << std::endl;

    // --- WEAK SCALING ---
    const int WEAK_OPS = 100000;
    std::cout << "\n[1] WEAK SCALING (Fixed work per thread: " << WEAK_OPS << ")" << std::endl;
    std::cout << "----------------------------------------------------------" << std::endl;
    for (int t : {1, 4, 8, max_threads}) {
        if (t > max_threads) continue;
        run_benchmark<NaiveHashMap<int, int>>("Naive (Global)", t, WEAK_OPS, bucket_count);
        run_benchmark<ConcurrentHashMapV2<int, int>>("Library V2 (Static)", t, WEAK_OPS, bucket_count);
        run_benchmark<ConcurrentHashMap<int, int>>("Library V3 (Dynamic)", t, WEAK_OPS, bucket_count);
        std::cout << "----------------------------------------------------------" << std::endl;
    }

    // --- STRONG SCALING ---
    const int TOTAL_OPS = 800000;
    std::cout << "\n[2] STRONG SCALING (Fixed total work: " << TOTAL_OPS << ")" << std::endl;
    std::cout << "----------------------------------------------------------" << std::endl;
    for (int t : {1, 4, 8, max_threads}) {
        if (t > max_threads) continue;
        int ops_per_thread = TOTAL_OPS / t;
        run_benchmark<NaiveHashMap<int, int>>("Naive (Global)", t, ops_per_thread, bucket_count);
        run_benchmark<ConcurrentHashMapV2<int, int>>("Library V2 (Static)", t, ops_per_thread, bucket_count);
        run_benchmark<ConcurrentHashMap<int, int>>("Library V3 (Dynamic)", t, ops_per_thread, bucket_count);
        std::cout << "----------------------------------------------------------" << std::endl;
    }

    return 0;
}

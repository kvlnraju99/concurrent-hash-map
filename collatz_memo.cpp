#include <iostream>
#include <vector>
#include <iomanip>
#include <omp.h>
#include <functional>
#include <unordered_map>
#include "concurrent_hash_map.h"
#include "concurrent_hash_map_v2.h"
#include "concurrent_hash_map_v6.h"
#ifdef USE_TBB
#include "tbb_wrapper.h"
#endif

// Standard Collatz calculation (No Cache)
long long get_collatz_no_cache(long long n) {
    long long count = 0;
    while (n > 1) {
        if (n % 2 == 0) n /= 2;
        else n = 3 * n + 1;
        count++;
    }
    return count;
}

// Sequential Collatz with Cache
long long get_collatz_sequential(long long n, std::unordered_map<long long, long long>& cache) {
    if (n == 1) return 0;
    if (cache.count(n)) return cache[n];
    
    long long next_n = (n % 2 == 0) ? (n / 2) : (3 * n + 1);
    long long result = 1 + get_collatz_sequential(next_n, cache);
    cache[n] = result;
    return result;
}

// Parallel Collatz with Concurrent Cache
template <typename MapType>
long long get_collatz_with_cache(long long n, MapType& cache) {
    if (n == 1) return 0;
    auto cached = cache.get(n);
    if (cached) return *cached;

    long long next_n = (n % 2 == 0) ? (n / 2) : (3 * n + 1);
    long long result = 1 + get_collatz_with_cache(next_n, cache);
    cache.put(n, result);
    return result;
}

int main(int argc, char* argv[]) {
    long long upper_limit = 1000000;
    int num_threads = omp_get_max_threads();
    size_t bucket_count = 10000;

    if (argc > 1) upper_limit = std::stoll(argv[1]);
    if (argc > 2) num_threads = std::stoi(argv[2]);
    if (argc > 3) bucket_count = std::stoull(argv[3]);

    std::cout << "==========================================================" << std::endl;
    std::cout << " APPLICATION: SHARED COLLATZ MEMOIZATION" << std::endl;
    std::cout << " Range: 1 to " << upper_limit << " | Threads: " << num_threads << " | Buckets: " << bucket_count << std::endl;
    std::cout << "==========================================================" << std::endl;

    double start;

    // --- 1. NO CACHE (Brute Force) ---
    start = omp_get_wtime();
    #pragma omp parallel for num_threads(num_threads)
    for (long long i = 1; i <= upper_limit; ++i) {
        get_collatz_no_cache(i);
    }
    std::cout << std::left << std::setw(23) << "No Cache (Parallel)" << " | Time: " << std::fixed << std::setprecision(4) << (omp_get_wtime() - start) << "s" << std::endl;

    // --- 2. SEQUENTIAL CACHE (Baseline) ---
    std::unordered_map<long long, long long> seq_cache;
    start = omp_get_wtime();
    for (long long i = 1; i <= upper_limit; ++i) {
        get_collatz_sequential(i, seq_cache);
    }
    double time_seq = omp_get_wtime() - start;
    std::cout << std::left << std::setw(23) << "Sequential (1 Core)" << " | Time: " << std::fixed << std::setprecision(4) << time_seq << "s" 
              << " | Cache Size: " << seq_cache.size() << std::endl;

    // --- 3. LIBRARY V2 (Static) ---
    ConcurrentHashMapV2<long long, long long> v2_cache(bucket_count);
    start = omp_get_wtime();
    #pragma omp parallel for num_threads(num_threads)
    for (long long i = 1; i <= upper_limit; ++i) {
        get_collatz_with_cache(i, v2_cache);
    }
    double time_v2 = omp_get_wtime() - start;
    std::cout << std::left << std::setw(23) << "Library V2 (Static)" << " | Time: " << std::fixed << std::setprecision(4) << time_v2 << "s"
              << " | Cache Size: " << v2_cache.size() << std::endl;

    // --- 4. LIBRARY V3 (Dynamic) ---
    ConcurrentHashMap<long long, long long> v3_cache(bucket_count);
    start = omp_get_wtime();
    #pragma omp parallel for num_threads(num_threads)
    for (long long i = 1; i <= upper_limit; ++i) {
        get_collatz_with_cache(i, v3_cache);
    }
    double time_v3 = omp_get_wtime() - start;
    std::cout << std::left << std::setw(23) << "Library V3 (Dynamic)" << " | Time: " << std::fixed << std::setprecision(4) << time_v3 << "s"
              << " | Cache Size: " << v3_cache.size() << std::endl;

    // --- 5. LIBRARY V6 (Segmented) ---
    ConcurrentHashMapV6<long long, long long> v6_cache(bucket_count);
    start = omp_get_wtime();
    #pragma omp parallel for num_threads(num_threads)
    for (long long i = 1; i <= upper_limit; ++i) {
        get_collatz_with_cache(i, v6_cache);
    }
    double time_v6 = omp_get_wtime() - start;
    std::cout << std::left << std::setw(23) << "Library V6 (Segmented)" << " | Time: " << std::fixed << std::setprecision(4) << time_v6 << "s"
              << " | Cache Size: " << v6_cache.size() << std::endl;

#ifdef USE_TBB
    // --- 7. INTEL TBB (Industry) ---
    TBBHashMapWrapper<long long, long long> tbb_cache(bucket_count);
    start = omp_get_wtime();
    #pragma omp parallel for num_threads(num_threads)
    for (long long i = 1; i <= upper_limit; ++i) {
        get_collatz_with_cache(i, tbb_cache);
    }
    double time_tbb = omp_get_wtime() - start;
    std::cout << std::left << std::setw(23) << "Intel TBB (Industry)" << " | Time: " << std::fixed << std::setprecision(4) << time_tbb << "s"
              << " | Cache Size: " << tbb_cache.size() << std::endl;
#endif

    std::cout << "----------------------------------------------------------" << std::endl;
    return 0;
}

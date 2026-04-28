#include <iostream>
#include <vector>
#include <iomanip>
#include <omp.h>
#include "naive_map.h"
#include "concurrent_hash_map.h"
#include "concurrent_hash_map_v2.h"
#include "concurrent_hash_map_v4.h"
#include "concurrent_hash_map_v5.h"
#ifdef USE_TBB
#include "tbb_wrapper.h"
#endif

// Standard Collatz calculation (No Cache)
long long get_collatz_no_cache(long long n) {
    long long count = 0;
    while (n != 1) {
        if (n % 2 == 0) n /= 2;
        else n = 3 * n + 1;
        count++;
    }
    return count;
}

// Collatz with a shared cache
template <typename MapType>
long long get_collatz_with_cache(long long n, MapType& cache) {
    std::vector<long long> path;
    long long curr = n;
    long long total_length = -1;

    // 1. Trace the path and check for cached results
    while (curr != 1) {
        auto cached = cache.get(curr);
        if (cached) {
            total_length = *cached;
            break;
        }
        path.push_back(curr);
        if (curr % 2 == 0) curr /= 2;
        else curr = 3 * curr + 1;
    }

    if (total_length == -1) total_length = 0; // reached 1

    // 2. Backfill the cache with results for every step in the path
    for (int i = path.size() - 1; i >= 0; --i) {
        total_length++;
        cache.put(path[i], total_length);
    }

    return total_length;
}

int main(int argc, char* argv[]) {
    long long upper_limit = 500000;
    int num_threads = omp_get_max_threads();
    size_t bucket_count = 10000; // Small default to see resizing impact

    if (argc > 1) upper_limit = std::stoll(argv[1]);
    if (argc > 2) num_threads = std::stoi(argv[2]);
    if (argc > 3) bucket_count = std::stoull(argv[3]);

    std::cout << "==========================================================" << std::endl;
    std::cout << " APPLICATION: SHARED COLLATZ MEMOIZATION" << std::endl;
    std::cout << " Range: 1 to " << upper_limit << " | Threads: " << num_threads << " | Buckets: " << bucket_count << std::endl;
    std::cout << "==========================================================" << std::endl;

    // --- 1. NO CACHE ---
    double start = omp_get_wtime();
    #pragma omp parallel for num_threads(num_threads)
    for (long long i = 1; i <= upper_limit; ++i) {
        get_collatz_no_cache(i);
    }
    double time_no_cache = omp_get_wtime() - start;
    std::cout << std::left << std::setw(20) << "No Cache" << " | Time: " << std::fixed << std::setprecision(4) << time_no_cache << "s" << std::endl;

    // --- 2. NAIVE CACHE (Global Lock) ---
    NaiveHashMap<long long, long long> naive_cache(bucket_count);
    start = omp_get_wtime();
    #pragma omp parallel for num_threads(num_threads)
    for (long long i = 1; i <= upper_limit; ++i) {
        get_collatz_with_cache(i, naive_cache);
    }
    double time_naive = omp_get_wtime() - start;
    std::cout << std::left << std::setw(20) << "Naive Cache" << " | Time: " << std::fixed << std::setprecision(4) << time_naive << "s" 
              << " | Cache Size: " << naive_cache.size() << std::endl;

    // --- 3. LIBRARY V2 (Static) ---
    ConcurrentHashMapV2<long long, long long> v2_cache(bucket_count);
    start = omp_get_wtime();
    #pragma omp parallel for num_threads(num_threads)
    for (long long i = 1; i <= upper_limit; ++i) {
        get_collatz_with_cache(i, v2_cache);
    }
    double time_v2 = omp_get_wtime() - start;
    std::cout << std::left << std::setw(20) << "Library V2 (Static)" << " | Time: " << std::fixed << std::setprecision(4) << time_v2 << "s"
              << " | Cache Size: " << v2_cache.size() << std::endl;

    // --- 4. LIBRARY V3 (Dynamic) ---
    ConcurrentHashMap<long long, long long> v3_cache(bucket_count);
    start = omp_get_wtime();
    #pragma omp parallel for num_threads(num_threads)
    for (long long i = 1; i <= upper_limit; ++i) {
        get_collatz_with_cache(i, v3_cache);
    }
    double time_v3 = omp_get_wtime() - start;
    std::cout << std::left << std::setw(20) << "Library V3 (Dynamic)" << " | Time: " << std::fixed << std::setprecision(4) << time_v3 << "s"
              << " | Cache Size: " << v3_cache.size() << std::endl;

    // --- 5. LIBRARY V4 (Atomic) ---
    ConcurrentHashMapV4<long long, long long> v4_cache(bucket_count);
    start = omp_get_wtime();
    #pragma omp parallel for num_threads(num_threads)
    for (long long i = 1; i <= upper_limit; ++i) {
        get_collatz_with_cache(i, v4_cache);
    }
    double time_v4 = omp_get_wtime() - start;
    std::cout << std::left << std::setw(20) << "Library V4 (Atomic)" << " | Time: " << std::fixed << std::setprecision(4) << time_v4 << "s"
              << " | Cache Size: " << v4_cache.size() << std::endl;

    // --- 6. LIBRARY V5 (Wait-Free) ---
    ConcurrentHashMapV5<long long, long long> v5_cache(bucket_count);
#ifdef USE_TBB
    TBBHashMapWrapper<long long, long long> tbb_cache(bucket_count);
#endif
    start = omp_get_wtime();
    #pragma omp parallel for num_threads(num_threads)
    for (long long i = 1; i <= upper_limit; ++i) {
        get_collatz_with_cache(i, v5_cache);
    }
    double time_v5 = omp_get_wtime() - start;
    std::cout << std::left << std::setw(20) << "Library V5 (Wait-Free)" << " | Time: " << std::fixed << std::setprecision(4) << time_v5 << "s"
              << " | Cache Size: " << v5_cache.size() << std::endl;

#ifdef USE_TBB
    // --- 7. INTEL TBB (Industry) ---
    start = omp_get_wtime();
    #pragma omp parallel for num_threads(num_threads)
    for (long long i = 1; i <= upper_limit; ++i) {
        get_collatz_with_cache(i, tbb_cache);
    }
    double time_tbb = omp_get_wtime() - start;
    std::cout << std::left << std::setw(20) << "Intel TBB (Industry)" << " | Time: " << std::fixed << std::setprecision(4) << time_tbb << "s"
              << " | Cache Size: " << tbb_cache.size() << std::endl;
#endif

    std::cout << "----------------------------------------------------------" << std::endl;
    std::cout << "Speedup (V3 vs Naive): " << (time_naive / time_v3) << "x" << std::endl;
    std::cout << "Speedup (V3 vs V2):    " << (time_v2 / time_v3) << "x" << std::endl;

    return 0;
}

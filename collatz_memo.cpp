#include <iostream>
#include <vector>
#include <iomanip>
#include <omp.h>
#include "naive_map.h"
#include "concurrent_hash_map.h"

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
    // We go backwards to build up the lengths
    for (int i = path.size() - 1; i >= 0; --i) {
        total_length++;
        cache.put(path[i], total_length);
    }

    return total_length;
}

int main(int argc, char* argv[]) {
    long long upper_limit = 500000;
    int num_threads = omp_get_max_threads();

    if (argc > 1) upper_limit = std::stoll(argv[1]);
    if (argc > 2) num_threads = std::stoi(argv[2]);

    std::cout << "==========================================================" << std::endl;
    std::cout << " APPLICATION: SHARED COLLATZ MEMOIZATION" << std::endl;
    std::cout << " Range: 1 to " << upper_limit << " | Threads: " << num_threads << std::endl;
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
    NaiveHashMap<long long, long long> naive_cache(131071);
    start = omp_get_wtime();
    #pragma omp parallel for num_threads(num_threads)
    for (long long i = 1; i <= upper_limit; ++i) {
        get_collatz_with_cache(i, naive_cache);
    }
    double time_naive = omp_get_wtime() - start;
    std::cout << std::left << std::setw(20) << "Naive Cache" << " | Time: " << std::fixed << std::setprecision(4) << time_naive << "s" << std::endl;

    // --- 3. LIBRARY CACHE (Concurrent) ---
    ConcurrentHashMap<long long, long long> lib_cache(131071);
    start = omp_get_wtime();
    #pragma omp parallel for num_threads(num_threads)
    for (long long i = 1; i <= upper_limit; ++i) {
        get_collatz_with_cache(i, lib_cache);
    }
    double time_lib = omp_get_wtime() - start;
    std::cout << std::left << std::setw(20) << "Library Cache" << " | Time: " << std::fixed << std::setprecision(4) << time_lib << "s" << std::endl;

    std::cout << "----------------------------------------------------------" << std::endl;
    std::cout << "Speedup (Library vs No Cache): " << (time_no_cache / time_lib) << "x" << std::endl;
    std::cout << "Speedup (Library vs Naive):    " << (time_naive / time_lib) << "x" << std::endl;

    return 0;
}

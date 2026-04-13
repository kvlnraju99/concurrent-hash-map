#include <iostream>
#include <thread>
#include <vector>
#include <chrono>
#include <cstdlib>
#include "concurrent_hash_map.h"

// Total operations (constant regardless of thread count).
const int TOTAL_OPS = 800000;

// ─── Benchmark function: insert TOTAL_OPS keys into a map ───────────

double run_put_benchmark(ConcurrentHashMap<int, int>& map, int num_threads) {
    int ops_per_thread = TOTAL_OPS / num_threads;

    std::vector<std::thread> threads;

    auto start = std::chrono::high_resolution_clock::now();

    for (int t = 0; t < num_threads; t++) {
        threads.emplace_back([&map, t, ops_per_thread]() {
            for (int i = 0; i < ops_per_thread; i++) {
                int key = t * ops_per_thread + i;
                map.put(key, i);
            }
        });
    }

    for (auto& th : threads) th.join();

    auto end = std::chrono::high_resolution_clock::now();
    return std::chrono::duration<double, std::milli>(end - start).count();
}

// ─── Benchmark function: get from a pre-filled map ──────────────────

double run_get_benchmark(ConcurrentHashMap<int, int>& map, int num_threads) {
    int ops_per_thread = TOTAL_OPS / num_threads;

    std::vector<std::thread> threads;

    auto start = std::chrono::high_resolution_clock::now();

    for (int t = 0; t < num_threads; t++) {
        threads.emplace_back([&map, t, ops_per_thread]() {
            for (int i = 0; i < ops_per_thread; i++) {
                int key = t * ops_per_thread + i;
                map.get(key);
            }
        });
    }

    for (auto& th : threads) th.join();

    auto end = std::chrono::high_resolution_clock::now();
    return std::chrono::duration<double, std::milli>(end - start).count();
}

// ─── Benchmark function: mixed ops on a map ─────────────────────────

double run_mixed_benchmark(ConcurrentHashMap<int, int>& map, int num_threads) {
    int ops_per_thread = TOTAL_OPS / num_threads;

    std::vector<std::thread> threads;

    auto start = std::chrono::high_resolution_clock::now();

    for (int t = 0; t < num_threads; t++) {
        threads.emplace_back([&map, t, ops_per_thread]() {
            for (int i = 0; i < ops_per_thread; i++) {
                int key = (t * ops_per_thread + i) % 50000;
                int op = (t + i) % 3;

                if (op == 0)      map.put(key, i);
                else if (op == 1) map.get(key);
                else              map.remove(key);
            }
        });
    }

    for (auto& th : threads) th.join();

    auto end = std::chrono::high_resolution_clock::now();
    return std::chrono::duration<double, std::milli>(end - start).count();
}

// ─── Print a scaling table ──────────────────────────────────────────

void print_scaling_table(const std::string& label,
                         double (*bench_fn)(ConcurrentHashMap<int, int>&, int),
                         size_t initial_buckets, double load_factor,
                         int max_threads) {
    std::cout << "\n" << label << "\n";
    std::cout << "Threads | Time (ms) | Speedup\n";
    std::cout << "--------|-----------|--------\n";

    double time_1 = 0;
    for (int t = 1; t <= max_threads; t *= 2) {
        ConcurrentHashMap<int, int> map(initial_buckets, load_factor);

        // Pre-fill for get benchmark.
        if (bench_fn == run_get_benchmark || bench_fn == run_mixed_benchmark) {
            for (int i = 0; i < TOTAL_OPS; i++) {
                map.put(i, i);
            }
        }

        double time_t = bench_fn(map, t);
        if (t == 1) time_1 = time_t;
        double speedup = time_1 / time_t;
        printf("   %2d   | %9.1f | %5.2fx\n", t, time_t, speedup);
    }
}

// ─── Compare fixed vs dynamic sizing ────────────────────────────────

void compare_fixed_vs_dynamic(int max_threads) {
    std::cout << "\n============================================\n";
    std::cout << " Comparison: Fixed (4 buckets) vs Dynamic\n";
    std::cout << "============================================\n";

    std::cout << "\n--- PUT: Fixed 4 buckets (no resize) ---\n";
    std::cout << "Threads | Time (ms)\n";
    std::cout << "--------|---------\n";

    for (int t = 1; t <= max_threads; t *= 2) {
        // Fixed: 4 buckets, huge load factor = no resize.
        ConcurrentHashMap<int, int> map(4, 999999.0);
        double time_fixed = run_put_benchmark(map, t);
        printf("   %2d   | %9.1f\n", t, time_fixed);
    }

    std::cout << "\n--- PUT: Dynamic (starts at 4, resizes) ---\n";
    std::cout << "Threads | Time (ms) | Final buckets\n";
    std::cout << "--------|-----------|---------------\n";

    for (int t = 1; t <= max_threads; t *= 2) {
        // Dynamic: 4 buckets, load factor 0.75 = auto resize.
        ConcurrentHashMap<int, int> map(4, 0.75);
        double time_dynamic = run_put_benchmark(map, t);
        size_t final_buckets = map.get_bucket_count();
        printf("   %2d   | %9.1f | %zu\n", t, time_dynamic, final_buckets);
    }
}

// ─── Main ───────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    int max_threads = std::thread::hardware_concurrency();

    if (argc >= 2) {
        max_threads = std::atoi(argv[1]);
        if (max_threads < 1) max_threads = 1;
    }

    std::cout << "========================================\n";
    std::cout << " Concurrent Hash Map — Benchmark\n";
    std::cout << " Max threads: " << max_threads << "\n";
    std::cout << " Total ops:   " << TOTAL_OPS << "\n";
    std::cout << "========================================\n";

    // Standard benchmarks with 64 buckets.
    print_scaling_table("--- PUT Benchmark (64 buckets, dynamic) ---",
                        run_put_benchmark, 64, 0.75, max_threads);

    print_scaling_table("--- GET Benchmark (64 buckets, dynamic) ---",
                        run_get_benchmark, 64, 0.75, max_threads);

    print_scaling_table("--- MIXED Benchmark (64 buckets, dynamic) ---",
                        run_mixed_benchmark, 64, 0.75, max_threads);

    // Fixed vs Dynamic comparison.
    compare_fixed_vs_dynamic(max_threads);

    std::cout << "\n========================================\n";
    std::cout << " Done.\n";
    std::cout << "========================================\n\n";

    return 0;
}

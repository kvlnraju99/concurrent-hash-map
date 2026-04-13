#include <iostream>
#include <thread>
#include <vector>
#include <chrono>
#include <cstdlib>
#include "concurrent_hash_map.h"

// ─── Benchmark: same total work, split across N threads ─────────────

// Total operations to perform (constant regardless of thread count).
const int TOTAL_OPS = 800000;

// Run put operations split across N threads, return time in ms.
double benchmark_puts(int num_threads) {
    ConcurrentHashMap<int, int> map(64);
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

// Run get operations split across N threads, return time in ms.
double benchmark_gets(int num_threads) {
    ConcurrentHashMap<int, int> map(64);
    int ops_per_thread = TOTAL_OPS / num_threads;

    // Pre-fill the map.
    for (int i = 0; i < TOTAL_OPS; i++) {
        map.put(i, i);
    }

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

// Run mixed operations split across N threads, return time in ms.
double benchmark_mixed(int num_threads) {
    ConcurrentHashMap<int, int> map(64);
    int ops_per_thread = TOTAL_OPS / num_threads;

    // Pre-fill some data.
    for (int i = 0; i < 50000; i++) {
        map.put(i, i);
    }

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

// ─── Print a table for a given benchmark ────────────────────────────

void run_benchmark(const std::string& name,
                   double (*bench_fn)(int),
                   int max_threads) {
    std::cout << "\n--- " << name << " (" << TOTAL_OPS << " total ops) ---\n";
    std::cout << "Threads | Time (ms) | Speedup\n";
    std::cout << "--------|-----------|--------\n";

    // Baseline: 1 thread.
    double time_1 = bench_fn(1);

    for (int t = 1; t <= max_threads; t *= 2) {
        double time_t = (t == 1) ? time_1 : bench_fn(t);
        double speedup = time_1 / time_t;
        printf("   %2d   | %9.1f | %5.2fx\n", t, time_t, speedup);
    }
}

// ─── Main ───────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    // Default: use all available cores.
    int max_threads = std::thread::hardware_concurrency();

    // User can override from command line: ./benchmark 4
    if (argc >= 2) {
        max_threads = std::atoi(argv[1]);
        if (max_threads < 1) max_threads = 1;
    }

    std::cout << "========================================\n";
    std::cout << " Concurrent Hash Map — Benchmark\n";
    std::cout << " Max threads: " << max_threads << "\n";
    std::cout << " Total ops:   " << TOTAL_OPS << "\n";
    std::cout << "========================================\n";

    run_benchmark("PUT Benchmark",   benchmark_puts,  max_threads);
    run_benchmark("GET Benchmark",   benchmark_gets,  max_threads);
    run_benchmark("MIXED Benchmark", benchmark_mixed, max_threads);

    std::cout << "\n========================================\n";
    std::cout << " Done.\n";
    std::cout << "========================================\n\n";

    return 0;
}

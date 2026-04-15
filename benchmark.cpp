#include <iostream>
#include <thread>
#include <vector>
#include <chrono>
#include <cstdlib>
#include "concurrent_hash_map.h"
#include "lock_free_hash_map.h"

const int TOTAL_OPS = 800000;

size_t recommended_lockfree_buckets() {
    return 524288;
}

// ─── Generic benchmark runners ─────────────────────────────────────

// PUT benchmark for lock-based map.
double bench_put_locked(int num_threads, size_t buckets) {
    ConcurrentHashMap<int, int> map(buckets, 999999.0);  // no resize
    int ops_per_thread = TOTAL_OPS / num_threads;
    std::vector<std::thread> threads;

    auto start = std::chrono::high_resolution_clock::now();
    for (int t = 0; t < num_threads; t++) {
        threads.emplace_back([&map, t, ops_per_thread]() {
            for (int i = 0; i < ops_per_thread; i++)
                map.put(t * ops_per_thread + i, i);
        });
    }
    for (auto& th : threads) th.join();
    auto end = std::chrono::high_resolution_clock::now();
    return std::chrono::duration<double, std::milli>(end - start).count();
}

// PUT benchmark for lock-free map.
double bench_put_lockfree(int num_threads, size_t buckets) {
    LockFreeHashMap<int, int> map(buckets);
    int ops_per_thread = TOTAL_OPS / num_threads;
    std::vector<std::thread> threads;

    auto start = std::chrono::high_resolution_clock::now();
    for (int t = 0; t < num_threads; t++) {
        threads.emplace_back([&map, t, ops_per_thread]() {
            for (int i = 0; i < ops_per_thread; i++)
                map.put(t * ops_per_thread + i, i);
        });
    }
    for (auto& th : threads) th.join();
    auto end = std::chrono::high_resolution_clock::now();
    return std::chrono::duration<double, std::milli>(end - start).count();
}

// GET benchmark for lock-based map.
double bench_get_locked(int num_threads, size_t buckets) {
    ConcurrentHashMap<int, int> map(buckets, 999999.0);
    for (int i = 0; i < TOTAL_OPS; i++) map.put(i, i);

    int ops_per_thread = TOTAL_OPS / num_threads;
    std::vector<std::thread> threads;

    auto start = std::chrono::high_resolution_clock::now();
    for (int t = 0; t < num_threads; t++) {
        threads.emplace_back([&map, t, ops_per_thread]() {
            for (int i = 0; i < ops_per_thread; i++)
                map.get(t * ops_per_thread + i);
        });
    }
    for (auto& th : threads) th.join();
    auto end = std::chrono::high_resolution_clock::now();
    return std::chrono::duration<double, std::milli>(end - start).count();
}

// GET benchmark for lock-free map.
double bench_get_lockfree(int num_threads, size_t buckets) {
    LockFreeHashMap<int, int> map(buckets);
    for (int i = 0; i < TOTAL_OPS; i++) map.put(i, i);

    int ops_per_thread = TOTAL_OPS / num_threads;
    std::vector<std::thread> threads;

    auto start = std::chrono::high_resolution_clock::now();
    for (int t = 0; t < num_threads; t++) {
        threads.emplace_back([&map, t, ops_per_thread]() {
            for (int i = 0; i < ops_per_thread; i++)
                map.get(t * ops_per_thread + i);
        });
    }
    for (auto& th : threads) th.join();
    auto end = std::chrono::high_resolution_clock::now();
    return std::chrono::duration<double, std::milli>(end - start).count();
}

// MIXED benchmark for lock-based map.
double bench_mixed_locked(int num_threads, size_t buckets) {
    ConcurrentHashMap<int, int> map(buckets, 999999.0);
    for (int i = 0; i < 50000; i++) map.put(i, i);

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

// MIXED benchmark for lock-free map.
double bench_mixed_lockfree(int num_threads, size_t buckets) {
    LockFreeHashMap<int, int> map(buckets);
    for (int i = 0; i < 50000; i++) map.put(i, i);

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

// ─── Side-by-side comparison ────────────────────────────────────────

void compare(const std::string& label,
             double (*locked_fn)(int, size_t),
             double (*lockfree_fn)(int, size_t),
             size_t buckets, int max_threads) {
    std::cout << "\n--- " << label << " (" << TOTAL_OPS << " ops, "
              << buckets << " buckets) ---\n";
    std::cout << "Threads | Locked (ms) | Speedup | Lock-Free (ms) | Speedup | Winner\n";
    std::cout << "--------|-------------|---------|----------------|---------|--------\n";

    // Run 1-thread baseline first.
    double base_locked   = locked_fn(1, buckets);
    double base_lockfree = lockfree_fn(1, buckets);

    for (int t = 1; t <= max_threads; t *= 2) {
        double t_locked   = (t == 1) ? base_locked   : locked_fn(t, buckets);
        double t_lockfree = (t == 1) ? base_lockfree : lockfree_fn(t, buckets);

        double sp_locked   = base_locked / t_locked;
        double sp_lockfree = base_lockfree / t_lockfree;

        const char* winner = (t_lockfree < t_locked) ? "Lock-Free" : "Locked";
        printf("   %2d   | %11.1f | %5.2fx  | %14.1f | %5.2fx  | %s\n",
               t, t_locked, sp_locked, t_lockfree, sp_lockfree, winner);
    }
}

// ─── Main ───────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    int max_threads = std::thread::hardware_concurrency();
    if (argc >= 2) {
        max_threads = std::atoi(argv[1]);
        if (max_threads < 1) max_threads = 1;
    }

    std::cout << "========================================================\n";
    std::cout << " Benchmark: Lock-Based vs Lock-Free Hash Map\n";
    std::cout << " Max threads: " << max_threads << "\n";
    std::cout << " Total ops:   " << TOTAL_OPS << "\n";
    std::cout << "========================================================\n";

    size_t buckets = recommended_lockfree_buckets();
    compare("PUT",   bench_put_locked,   bench_put_lockfree,   buckets, max_threads);
    compare("GET",   bench_get_locked,   bench_get_lockfree,   buckets, max_threads);
    compare("MIXED", bench_mixed_locked, bench_mixed_lockfree, buckets, max_threads);

    std::cout << "\n========================================================\n";
    std::cout << " Done.\n";
    std::cout << "========================================================\n\n";

    return 0;
}

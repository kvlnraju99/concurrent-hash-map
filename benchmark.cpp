#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "concurrent_hash_map.h"
#include "lock_free_hash_map.h"

namespace {

struct Config {
    int max_threads = static_cast<int>(std::thread::hardware_concurrency());
    int total_ops = 800000;
    size_t buckets = 524288;
};

void print_usage(const char* program) {
    std::cout << "Usage: " << program
              << " [--threads N] [--ops N] [--buckets N]\n";
}

Config parse_args(int argc, char* argv[]) {
    Config config;
    if (config.max_threads <= 0) {
        config.max_threads = 4;
    }

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--threads" && i + 1 < argc) {
            config.max_threads = std::max(1, std::atoi(argv[++i]));
        } else if (arg == "--ops" && i + 1 < argc) {
            config.total_ops = std::max(1, std::atoi(argv[++i]));
        } else if (arg == "--buckets" && i + 1 < argc) {
            config.buckets = static_cast<size_t>(std::strtoull(argv[++i], nullptr, 10));
        } else {
            print_usage(argv[0]);
            std::exit(1);
        }
    }

    return config;
}

double bench_put_locked(int num_threads, size_t buckets, int total_ops) {
    ConcurrentHashMap<int, int> map(buckets, 999999.0);
    const int ops_per_thread = total_ops / num_threads;
    std::vector<std::thread> threads;
    threads.reserve(num_threads);

    const auto start = std::chrono::high_resolution_clock::now();
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&map, t, ops_per_thread]() {
            for (int i = 0; i < ops_per_thread; ++i) {
                map.put(t * ops_per_thread + i, i);
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }
    const auto end = std::chrono::high_resolution_clock::now();
    return std::chrono::duration<double, std::milli>(end - start).count();
}

double bench_put_lockfree(int num_threads, size_t buckets, int total_ops) {
    LockFreeHashMap<int, int> map(buckets);
    const int ops_per_thread = total_ops / num_threads;
    std::vector<std::thread> threads;
    threads.reserve(num_threads);

    const auto start = std::chrono::high_resolution_clock::now();
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&map, t, ops_per_thread]() {
            for (int i = 0; i < ops_per_thread; ++i) {
                map.put(t * ops_per_thread + i, i);
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }
    const auto end = std::chrono::high_resolution_clock::now();
    return std::chrono::duration<double, std::milli>(end - start).count();
}

double bench_get_locked(int num_threads, size_t buckets, int total_ops) {
    ConcurrentHashMap<int, int> map(buckets, 999999.0);
    for (int i = 0; i < total_ops; ++i) {
        map.put(i, i);
    }
    const int ops_per_thread = total_ops / num_threads;
    std::vector<std::thread> threads;
    threads.reserve(num_threads);

    const auto start = std::chrono::high_resolution_clock::now();
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&map, t, ops_per_thread]() {
            for (int i = 0; i < ops_per_thread; ++i) {
                map.get(t * ops_per_thread + i);
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }
    const auto end = std::chrono::high_resolution_clock::now();
    return std::chrono::duration<double, std::milli>(end - start).count();
}

double bench_get_lockfree(int num_threads, size_t buckets, int total_ops) {
    LockFreeHashMap<int, int> map(buckets);
    for (int i = 0; i < total_ops; ++i) {
        map.put(i, i);
    }
    const int ops_per_thread = total_ops / num_threads;
    std::vector<std::thread> threads;
    threads.reserve(num_threads);

    const auto start = std::chrono::high_resolution_clock::now();
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&map, t, ops_per_thread]() {
            for (int i = 0; i < ops_per_thread; ++i) {
                map.get(t * ops_per_thread + i);
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }
    const auto end = std::chrono::high_resolution_clock::now();
    return std::chrono::duration<double, std::milli>(end - start).count();
}

double bench_mixed_locked(int num_threads, size_t buckets, int total_ops) {
    ConcurrentHashMap<int, int> map(buckets, 999999.0);
    for (int i = 0; i < 50000; ++i) {
        map.put(i, i);
    }
    const int ops_per_thread = total_ops / num_threads;
    std::vector<std::thread> threads;
    threads.reserve(num_threads);

    const auto start = std::chrono::high_resolution_clock::now();
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&map, t, ops_per_thread]() {
            for (int i = 0; i < ops_per_thread; ++i) {
                const int key = (t * ops_per_thread + i) % 50000;
                const int op = (t + i) % 3;
                if (op == 0) {
                    map.put(key, i);
                } else if (op == 1) {
                    map.get(key);
                } else {
                    map.remove(key);
                }
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }
    const auto end = std::chrono::high_resolution_clock::now();
    return std::chrono::duration<double, std::milli>(end - start).count();
}

double bench_mixed_lockfree(int num_threads, size_t buckets, int total_ops) {
    LockFreeHashMap<int, int> map(buckets);
    for (int i = 0; i < 50000; ++i) {
        map.put(i, i);
    }
    const int ops_per_thread = total_ops / num_threads;
    std::vector<std::thread> threads;
    threads.reserve(num_threads);

    const auto start = std::chrono::high_resolution_clock::now();
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&map, t, ops_per_thread]() {
            for (int i = 0; i < ops_per_thread; ++i) {
                const int key = (t * ops_per_thread + i) % 50000;
                const int op = (t + i) % 3;
                if (op == 0) {
                    map.put(key, i);
                } else if (op == 1) {
                    map.get(key);
                } else {
                    map.remove(key);
                }
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }
    const auto end = std::chrono::high_resolution_clock::now();
    return std::chrono::duration<double, std::milli>(end - start).count();
}

using BenchFn = double (*)(int, size_t, int);

void compare(const std::string& label,
             BenchFn locked_fn,
             BenchFn lockfree_fn,
             const Config& config) {
    std::cout << "\n--- " << label << " (" << config.total_ops << " ops, "
              << config.buckets << " buckets) ---\n";
    std::cout << "Threads | Locked (ms) | Speedup | Lock-Free (ms) | Speedup | Winner\n";
    std::cout << "--------|-------------|---------|----------------|---------|--------\n";

    const double base_locked = locked_fn(1, config.buckets, config.total_ops);
    const double base_lockfree = lockfree_fn(1, config.buckets, config.total_ops);

    for (int threads = 1; threads <= config.max_threads; threads *= 2) {
        const double locked =
            (threads == 1) ? base_locked : locked_fn(threads, config.buckets, config.total_ops);
        const double lockfree =
            (threads == 1) ? base_lockfree : lockfree_fn(threads, config.buckets, config.total_ops);
        const double locked_speedup = base_locked / locked;
        const double lockfree_speedup = base_lockfree / lockfree;
        const char* winner = (lockfree < locked) ? "Lock-Free" : "Locked";

        std::printf("   %2d   | %11.1f | %5.2fx  | %14.1f | %5.2fx  | %s\n",
                    threads, locked, locked_speedup, lockfree, lockfree_speedup, winner);
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    const Config config = parse_args(argc, argv);

    std::cout << "========================================================\n";
    std::cout << " Benchmark: Lock-Based vs Lock-Free Hash Map\n";
    std::cout << " Max threads: " << config.max_threads << "\n";
    std::cout << " Total ops:   " << config.total_ops << "\n";
    std::cout << " Buckets:     " << config.buckets << "\n";
    std::cout << "========================================================\n";

    compare("PUT", bench_put_locked, bench_put_lockfree, config);
    compare("GET", bench_get_locked, bench_get_lockfree, config);
    compare("MIXED", bench_mixed_locked, bench_mixed_lockfree, config);

    std::cout << "\n========================================================\n";
    std::cout << " Done.\n";
    std::cout << "========================================================\n";
    return 0;
}

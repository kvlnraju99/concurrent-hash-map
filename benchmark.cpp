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
#include "lock_free_open_addressing_hash_map.h"

namespace {

struct Config {
    int max_threads = static_cast<int>(std::thread::hardware_concurrency());
    int total_ops = 800000;
    size_t buckets = 524288;
    bool print_open_stats = false;
};

void print_usage(const char* program) {
    std::cout << "Usage: " << program
              << " [--threads N] [--ops N] [--buckets N] [--print-open-stats]\n";
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
        } else if (arg == "--print-open-stats") {
            config.print_open_stats = true;
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

double bench_put_open(int num_threads, size_t buckets, int total_ops) {
    LockFreeOpenAddressingHashMap<int, int> map(buckets);
    const int ops_per_thread = total_ops / num_threads;
    std::vector<std::thread> threads;
    threads.reserve(num_threads);

    const auto start = std::chrono::high_resolution_clock::now();
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&map, t, ops_per_thread]() {
            for (int i = 0; i < ops_per_thread; ++i) {
                (void)map.put(t * ops_per_thread + i, i);
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

double bench_get_open(int num_threads, size_t buckets, int total_ops) {
    LockFreeOpenAddressingHashMap<int, int> map(buckets);
    const int preload_ops = std::min(total_ops, static_cast<int>(buckets / 2));
    for (int i = 0; i < preload_ops; ++i) {
        (void)map.put(i, i);
    }
    const int ops_per_thread = total_ops / num_threads;
    std::vector<std::thread> threads;
    threads.reserve(num_threads);

    const auto start = std::chrono::high_resolution_clock::now();
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&map, t, ops_per_thread, preload_ops]() {
            for (int i = 0; i < ops_per_thread; ++i) {
                map.get((t * ops_per_thread + i) % preload_ops);
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

double bench_mixed_open(int num_threads, size_t buckets, int total_ops) {
    LockFreeOpenAddressingHashMap<int, int> map(buckets);
    const int preload_keys = std::min(50000, static_cast<int>(buckets / 2));
    for (int i = 0; i < preload_keys; ++i) {
        (void)map.put(i, i);
    }
    const int ops_per_thread = total_ops / num_threads;
    std::vector<std::thread> threads;
    threads.reserve(num_threads);

    const auto start = std::chrono::high_resolution_clock::now();
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&map, t, ops_per_thread, preload_keys]() {
            for (int i = 0; i < ops_per_thread; ++i) {
                const int key = (t * ops_per_thread + i) % preload_keys;
                const int op = (t + i) % 3;
                if (op == 0) {
                    (void)map.put(key, i);
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

void print_open_mixed_stats(const Config& config) {
    LockFreeOpenAddressingHashMap<int, int> map(config.buckets);
    const int preload_keys = std::min(50000, static_cast<int>(config.buckets / 2));
    for (int i = 0; i < preload_keys; ++i) {
        (void)map.put(i, i);
    }

    const int ops_per_thread = config.total_ops / config.max_threads;
    std::vector<std::thread> threads;
    threads.reserve(config.max_threads);
    for (int t = 0; t < config.max_threads; ++t) {
        threads.emplace_back([&map, t, ops_per_thread, preload_keys]() {
            for (int i = 0; i < ops_per_thread; ++i) {
                const int key = (t * ops_per_thread + i) % preload_keys;
                const int op = (t + i) % 3;
                if (op == 0) {
                    (void)map.put(key, i);
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

    const auto stats = map.get_stats();
    const auto avg_put_probe =
        stats.put_calls == 0 ? 0.0 : static_cast<double>(stats.total_put_probes) / stats.put_calls;
    const auto avg_get_probe =
        stats.get_calls == 0 ? 0.0 : static_cast<double>(stats.total_get_probes) / stats.get_calls;
    const auto avg_remove_probe =
        stats.remove_calls == 0 ? 0.0 : static_cast<double>(stats.total_remove_probes) / stats.remove_calls;

    std::cout << "\n--- Open-LF Mixed Stats ---\n";
    std::cout << "occupied=" << stats.occupied_slots
              << ", deleted=" << stats.deleted_slots
              << ", empty=" << stats.empty_slots
              << ", failed_puts=" << stats.failed_puts << "\n";
    std::cout << "avg_put_probe=" << avg_put_probe
              << ", avg_get_probe=" << avg_get_probe
              << ", avg_remove_probe=" << avg_remove_probe << "\n";
    std::cout << "max_put_probe=" << stats.max_put_probe
              << ", max_get_probe=" << stats.max_get_probe
              << ", max_remove_probe=" << stats.max_remove_probe << "\n";
}

using BenchFn = double (*)(int, size_t, int);

void compare(const std::string& label,
             BenchFn locked_fn,
             BenchFn list_lockfree_fn,
             BenchFn open_fn,
             const Config& config) {
    std::cout << "\n--- " << label << " (" << config.total_ops << " ops, "
              << config.buckets << " buckets) ---\n";
    std::cout << "Threads | Locked (ms) | List-LF (ms) | Open-LF (ms) | Winner\n";
    std::cout << "--------|-------------|--------------|--------------|--------\n";

    const double base_locked = locked_fn(1, config.buckets, config.total_ops);
    const double base_list = list_lockfree_fn(1, config.buckets, config.total_ops);
    const double base_open = open_fn(1, config.buckets, config.total_ops);
    (void)base_locked;
    (void)base_list;
    (void)base_open;

    for (int threads = 1; threads <= config.max_threads; threads *= 2) {
        const double locked =
            (threads == 1) ? base_locked : locked_fn(threads, config.buckets, config.total_ops);
        const double list =
            (threads == 1) ? base_list : list_lockfree_fn(threads, config.buckets, config.total_ops);
        const double open =
            (threads == 1) ? base_open : open_fn(threads, config.buckets, config.total_ops);
        const char* winner = "Locked";
        double best = locked;
        if (list < best) {
            best = list;
            winner = "List-LF";
        }
        if (open < best) {
            winner = "Open-LF";
        }

        std::printf("   %2d   | %11.1f | %12.1f | %12.1f | %s\n",
                    threads, locked, list, open, winner);
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    const Config config = parse_args(argc, argv);

    std::cout << "========================================================\n";
    std::cout << " Benchmark: Locked vs List-LF vs Open-LF Hash Map\n";
    std::cout << " Max threads: " << config.max_threads << "\n";
    std::cout << " Total ops:   " << config.total_ops << "\n";
    std::cout << " Buckets:     " << config.buckets << "\n";
    std::cout << "========================================================\n";

    compare("PUT", bench_put_locked, bench_put_lockfree, bench_put_open, config);
    compare("GET", bench_get_locked, bench_get_lockfree, bench_get_open, config);
    compare("MIXED", bench_mixed_locked, bench_mixed_lockfree, bench_mixed_open, config);

    if (config.print_open_stats) {
        print_open_mixed_stats(config);
    }

    std::cout << "\n========================================================\n";
    std::cout << " Done.\n";
    std::cout << "========================================================\n";
    return 0;
}

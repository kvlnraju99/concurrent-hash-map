#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <thread>
#include <vector>

#include "concurrent_hash_map.h"
#include "lock_free_dynamic_resize_hash_map.h"
#include "lock_free_hash_map.h"
#include "lock_free_open_addressing_hash_map.h"

namespace {

struct Config {
    int max_threads = 8;
    int total_ops = 100000;
    size_t buckets = 131072;
};

struct ResultRow {
    int threads;
    double locked_ms;
    double lockfree_ms;
    double resize_ms;
    double open_ms;
};

template <typename Map>
void map_put(Map& map, int key, int value) {
    static_cast<void>(map.put(key, value));
}

void print_usage(const char* program) {
    std::cout << "Usage: " << program
              << " [--threads N] [--ops N] [--buckets N]\n";
}

Config parse_args(int argc, char* argv[]) {
    Config config;
    const int hardware_threads = static_cast<int>(std::thread::hardware_concurrency());
    if (hardware_threads > 0) {
        config.max_threads = hardware_threads;
    }

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--threads" && i + 1 < argc) {
            config.max_threads = std::max(1, std::atoi(argv[++i]));
        } else if (arg == "--ops" && i + 1 < argc) {
            config.total_ops = std::max(1, std::atoi(argv[++i]));
        } else if (arg == "--buckets" && i + 1 < argc) {
            config.buckets = std::max<size_t>(16, std::strtoull(argv[++i], nullptr, 10));
        } else {
            print_usage(argv[0]);
            std::exit(1);
        }
    }

    return config;
}

std::vector<int> thread_counts(int max_threads) {
    std::vector<int> counts;
    for (int threads = 1; threads <= max_threads; threads *= 2) {
        counts.push_back(threads);
    }
    if (counts.empty() || counts.back() != max_threads) {
        counts.push_back(max_threads);
    }
    return counts;
}

int safe_key_space(size_t buckets, int total_ops) {
    const size_t capped = std::max<size_t>(1, std::min<size_t>(static_cast<size_t>(total_ops), buckets / 4));
    return static_cast<int>(capped);
}

template <typename Map>
double time_put(int num_threads, size_t buckets, int total_ops, int key_space) {
    Map map(buckets);
    const int ops_per_thread = total_ops / num_threads;
    std::vector<std::thread> threads;
    threads.reserve(num_threads);

    const auto start = std::chrono::steady_clock::now();
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&map, t, ops_per_thread, key_space]() {
            for (int i = 0; i < ops_per_thread; ++i) {
                const int key = (t * ops_per_thread + i) % key_space;
                map_put(map, key, i);
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }

    const auto end = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(end - start).count();
}

template <typename Map>
double time_get(int num_threads, size_t buckets, int total_ops, int key_space) {
    Map map(buckets);
    for (int key = 0; key < key_space; ++key) {
        map_put(map, key, key);
    }

    const int ops_per_thread = total_ops / num_threads;
    std::vector<std::thread> threads;
    threads.reserve(num_threads);

    const auto start = std::chrono::steady_clock::now();
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&map, t, ops_per_thread, key_space]() {
            for (int i = 0; i < ops_per_thread; ++i) {
                const int key = (t * ops_per_thread + i) % key_space;
                static_cast<void>(map.get(key));
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }

    const auto end = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(end - start).count();
}

template <typename Map>
double time_mixed(int num_threads, size_t buckets, int total_ops, int key_space) {
    Map map(buckets);
    for (int key = 0; key < key_space; ++key) {
        map_put(map, key, key);
    }

    const int ops_per_thread = total_ops / num_threads;
    std::vector<std::thread> threads;
    threads.reserve(num_threads);

    const auto start = std::chrono::steady_clock::now();
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&map, t, ops_per_thread, key_space]() {
            for (int i = 0; i < ops_per_thread; ++i) {
                const int key = (t * ops_per_thread + i) % key_space;
                const int op = (t + i) % 3;
                if (op == 0) {
                    map_put(map, key, i);
                } else if (op == 1) {
                    static_cast<void>(map.get(key));
                } else {
                    map.remove(key);
                }
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }

    const auto end = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(end - start).count();
}

template <typename Runner>
std::vector<ResultRow> collect_rows(const Config& config, Runner runner) {
    std::vector<ResultRow> rows;
    const int key_space = safe_key_space(config.buckets, config.total_ops);

    for (int threads : thread_counts(config.max_threads)) {
        rows.push_back(ResultRow{
            threads,
            runner.template operator()<ConcurrentHashMap<int, int>>(threads, config.buckets, config.total_ops, key_space),
            runner.template operator()<LockFreeHashMap<int, int>>(threads, config.buckets, config.total_ops, key_space),
            runner.template operator()<LockFreeDynamicResizeHashMap<int, int>>(threads, config.buckets, config.total_ops, key_space),
            runner.template operator()<LockFreeOpenAddressingHashMap<int, int>>(threads, config.buckets, config.total_ops, key_space),
        });
    }

    return rows;
}

std::string winner_name(const ResultRow& row) {
    double best = row.locked_ms;
    std::string winner = "Locked";
    if (row.lockfree_ms < best) {
        best = row.lockfree_ms;
        winner = "LockFree";
    }
    if (row.resize_ms < best) {
        best = row.resize_ms;
        winner = "Resize-LF";
    }
    if (row.open_ms < best) {
        winner = "OpenAddr";
    }
    return winner;
}

void print_rows(const std::string& label, const std::vector<ResultRow>& rows) {
    std::cout << "\n--- " << label << " ---\n";
    std::cout << "Threads | Locked | LockFree | Resize-LF | OpenAddr | Winner\n";
    std::cout << "--------|--------|----------|-----------|----------|--------\n";
    for (const auto& row : rows) {
        std::printf("%7d | %6.1f | %8.1f | %9.1f | %8.1f | %s\n",
                    row.threads,
                    row.locked_ms,
                    row.lockfree_ms,
                    row.resize_ms,
                    row.open_ms,
                    winner_name(row).c_str());
    }
}

struct PutRunner {
    template <typename Map>
    double operator()(int threads, size_t buckets, int total_ops, int key_space) const {
        return time_put<Map>(threads, buckets, total_ops, key_space);
    }
};

struct GetRunner {
    template <typename Map>
    double operator()(int threads, size_t buckets, int total_ops, int key_space) const {
        return time_get<Map>(threads, buckets, total_ops, key_space);
    }
};

struct MixedRunner {
    template <typename Map>
    double operator()(int threads, size_t buckets, int total_ops, int key_space) const {
        return time_mixed<Map>(threads, buckets, total_ops, key_space);
    }
};

}  // namespace

int main(int argc, char* argv[]) {
    const Config config = parse_args(argc, argv);
    const int key_space = safe_key_space(config.buckets, config.total_ops);

    std::cout << "========================================================\n";
    std::cout << " Concurrent Hash Map Benchmark\n";
    std::cout << " Variants: locked, lock-free chaining, dynamic resize, open addressing\n";
    std::cout << " Threads:  up to " << config.max_threads << "\n";
    std::cout << " Ops:      " << config.total_ops << "\n";
    std::cout << " Buckets:  " << config.buckets << "\n";
    std::cout << " Key space used in each run: " << key_space << "\n";
    std::cout << "========================================================\n";

    print_rows("PUT", collect_rows(config, PutRunner{}));
    print_rows("GET", collect_rows(config, GetRunner{}));
    print_rows("MIXED", collect_rows(config, MixedRunner{}));

    std::cout << "\n========================================================\n";
    std::cout << "Done.\n";
    std::cout << "========================================================\n";
    return 0;
}

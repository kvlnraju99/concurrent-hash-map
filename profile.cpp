#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "lock_free_hash_map.h"

namespace {

struct ScenarioConfig {
    std::string label;
    size_t buckets;
    int key_space;
    bool preload_existing;
    int ops_per_thread;
};

struct Config {
    int threads = static_cast<int>(std::thread::hardware_concurrency());
    int ops_per_thread = 5000;
    int single_bucket_ops = 1000;
    int hot_key_space = 64;
    size_t wide_buckets = 131072;
};

using Clock = std::chrono::steady_clock;

void print_usage(const char* program) {
    std::cout << "Usage: " << program
              << " [--threads N] [--ops-per-thread N] [--single-bucket-ops N]"
              << " [--wide-buckets N] [--hot-key-space N]\n";
}

Config parse_args(int argc, char* argv[]) {
    Config config;
    if (config.threads <= 0) {
        config.threads = 4;
    }

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--threads" && i + 1 < argc) {
            config.threads = std::max(1, std::atoi(argv[++i]));
        } else if (arg == "--ops-per-thread" && i + 1 < argc) {
            config.ops_per_thread = std::max(1, std::atoi(argv[++i]));
        } else if (arg == "--single-bucket-ops" && i + 1 < argc) {
            config.single_bucket_ops = std::max(1, std::atoi(argv[++i]));
        } else if (arg == "--wide-buckets" && i + 1 < argc) {
            config.wide_buckets = static_cast<size_t>(std::strtoull(argv[++i], nullptr, 10));
        } else if (arg == "--hot-key-space" && i + 1 < argc) {
            config.hot_key_space = std::max(1, std::atoi(argv[++i]));
        } else {
            print_usage(argv[0]);
            std::exit(1);
        }
    }

    return config;
}

void print_metric_row(const std::string& label, uint64_t total, double avg) {
    std::cout << std::left << std::setw(24) << label
              << std::right << std::setw(16) << total
              << std::setw(18) << std::fixed << std::setprecision(2) << avg
              << "\n";
}

void run_put_profile(const ScenarioConfig& config, int num_threads) {
    LockFreeHashMap<int, int> map(config.buckets, true);

    if (config.preload_existing) {
        for (int key = 0; key < config.key_space; ++key) {
            map.put(key, -1);
        }
        map.reset_put_profile();
    }

    std::vector<std::thread> threads;
    threads.reserve(num_threads);

    const auto wall_start = Clock::now();
    for (int thread_id = 0; thread_id < num_threads; ++thread_id) {
        threads.emplace_back([&, thread_id]() {
            for (int i = 0; i < config.ops_per_thread; ++i) {
                const int logical_index = thread_id * config.ops_per_thread + i;
                const int key = logical_index % config.key_space;
                map.put(key, logical_index);
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }
    const auto wall_end = Clock::now();

    const auto stats = map.get_put_profile();
    const double puts = stats.put_calls == 0 ? 1.0 : static_cast<double>(stats.put_calls);
    const double inserts =
        stats.successful_inserts == 0 ? 1.0 : static_cast<double>(stats.successful_inserts);
    const double wall_ms =
        std::chrono::duration<double, std::milli>(wall_end - wall_start).count();

    std::cout << "\n=== " << config.label << " ===\n";
    std::cout << "threads=" << num_threads
              << ", ops/thread=" << config.ops_per_thread
              << ", total puts=" << stats.put_calls
              << ", buckets=" << config.buckets
              << ", key space=" << config.key_space
              << ", wall=" << std::fixed << std::setprecision(2) << wall_ms << " ms\n";
    std::cout << std::left << std::setw(24) << "Metric"
              << std::right << std::setw(16) << "Total"
              << std::setw(18) << "Avg / put\n";
    std::cout << std::string(58, '-') << "\n";
    print_metric_row("total_ns", stats.total_ns, stats.total_ns / puts);
    print_metric_row("hash_ns", stats.hash_ns, stats.hash_ns / puts);
    print_metric_row("traversal_ns", stats.traversal_ns, stats.traversal_ns / puts);
    print_metric_row("allocation_ns", stats.allocation_ns, stats.allocation_ns / puts);
    print_metric_row("cas_ns", stats.cas_ns, stats.cas_ns / puts);
    print_metric_row("bookkeeping_ns", stats.bookkeeping_ns, stats.bookkeeping_ns / puts);
    print_metric_row("traversal_nodes", stats.traversal_nodes, stats.traversal_nodes / puts);
    print_metric_row("cas_attempts", stats.cas_attempts, stats.cas_attempts / puts);
    print_metric_row("cas_failures", stats.cas_failures, stats.cas_failures / puts);
    std::cout << "updated_existing        " << std::setw(16) << stats.updated_existing
              << std::setw(17) << std::fixed << std::setprecision(2)
              << (100.0 * static_cast<double>(stats.updated_existing) / puts) << "%\n";
    std::cout << "successful_inserts      " << std::setw(16) << stats.successful_inserts
              << std::setw(17) << std::fixed << std::setprecision(2)
              << (100.0 * static_cast<double>(stats.successful_inserts) / puts) << "%\n";
    std::cout << "max_chain_length_seen   " << std::setw(16) << stats.max_chain_length_seen
              << std::setw(18) << "-\n";
    std::cout << "cas_failures/insert     " << std::setw(16) << "-"
              << std::setw(18) << std::fixed << std::setprecision(2)
              << (static_cast<double>(stats.cas_failures) / inserts) << "\n";
}

}  // namespace

int main(int argc, char* argv[]) {
    const Config config = parse_args(argc, argv);

    std::cout << "Lock-free PUT micro-profile\n";
    std::cout << "threads=" << config.threads
              << ", ops/thread=" << config.ops_per_thread
              << ", single-bucket ops/thread=" << config.single_bucket_ops
              << ", wide buckets=" << config.wide_buckets
              << ", hot key space=" << config.hot_key_space << "\n";

    run_put_profile({"unique keys / wide table",
                     config.wide_buckets,
                     config.threads * config.ops_per_thread,
                     false,
                     config.ops_per_thread},
                    config.threads);
    run_put_profile({"unique keys / single bucket",
                     1,
                     config.threads * config.single_bucket_ops,
                     false,
                     config.single_bucket_ops},
                    config.threads);
    run_put_profile({"hot updates / high contention",
                     static_cast<size_t>(config.hot_key_space),
                     config.hot_key_space,
                     true,
                     config.ops_per_thread},
                    config.threads);
    return 0;
}

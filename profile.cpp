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
};

using Clock = std::chrono::steady_clock;

void print_metric_row(const std::string& label, uint64_t total, double avg) {
    std::cout << std::left << std::setw(24) << label
              << std::right << std::setw(16) << total
              << std::setw(18) << std::fixed << std::setprecision(2) << avg
              << "\n";
}

void run_put_profile(const ScenarioConfig& config, int num_threads, int ops_per_thread) {
    LockFreeHashMap<int, int> map(config.buckets, true);

    if (config.preload_existing) {
        for (int key = 0; key < config.key_space; ++key) {
            map.put(key, -1);
        }
        map.reset_put_profile();
    }

    std::vector<std::thread> threads;
    threads.reserve(num_threads);

    auto wall_start = Clock::now();
    for (int thread_id = 0; thread_id < num_threads; ++thread_id) {
        threads.emplace_back([&, thread_id]() {
            for (int i = 0; i < ops_per_thread; ++i) {
                const int logical_index = thread_id * ops_per_thread + i;
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
    const double wall_ms =
        std::chrono::duration<double, std::milli>(wall_end - wall_start).count();
    const double puts = stats.put_calls == 0 ? 1.0 : static_cast<double>(stats.put_calls);
    const double inserts =
        stats.successful_inserts == 0 ? 1.0 : static_cast<double>(stats.successful_inserts);

    std::cout << "\n=== " << config.label << " ===\n";
    std::cout << "threads=" << num_threads
              << ", ops/thread=" << ops_per_thread
              << ", total puts=" << stats.put_calls
              << ", buckets=" << config.buckets
              << ", key space=" << config.key_space
              << ", wall=" << std::fixed << std::setprecision(2) << wall_ms << " ms\n";
    std::cout << std::left << std::setw(24) << "Metric"
              << std::right << std::setw(16) << "Total"
              << std::setw(18) << "Avg / put"
              << "\n";
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
              << std::setw(18) << std::fixed << std::setprecision(2)
              << (100.0 * static_cast<double>(stats.updated_existing) / puts) << "%\n";
    std::cout << "successful_inserts      " << std::setw(16) << stats.successful_inserts
              << std::setw(18) << std::fixed << std::setprecision(2)
              << (100.0 * static_cast<double>(stats.successful_inserts) / puts) << "%\n";
    std::cout << "max_chain_length_seen   " << std::setw(16) << stats.max_chain_length_seen
              << std::setw(18) << "-"
              << "\n";
    std::cout << "cas_failures/insert     " << std::setw(16) << "-"
              << std::setw(18) << std::fixed << std::setprecision(2)
              << (static_cast<double>(stats.cas_failures) / inserts) << "\n";
}

}  // namespace

int main(int argc, char* argv[]) {
    int num_threads = static_cast<int>(std::thread::hardware_concurrency());
    if (num_threads <= 0) {
        num_threads = 4;
    }
    int ops_per_thread = 50000;

    if (argc >= 2) {
        num_threads = std::max(1, std::atoi(argv[1]));
    }
    if (argc >= 3) {
        ops_per_thread = std::max(1, std::atoi(argv[2]));
    }

    std::cout << "Lock-free PUT micro-profile\n";
    std::cout << "clock=steady_clock, threads=" << num_threads
              << ", ops/thread=" << ops_per_thread << "\n";

    run_put_profile({"unique keys / wide table", 131072, num_threads * ops_per_thread, false},
                    num_threads, ops_per_thread);
    run_put_profile({"unique keys / single bucket", 1, num_threads * ops_per_thread, false},
                    num_threads, ops_per_thread);
    run_put_profile({"hot updates / high contention", 64, 64, true},
                    num_threads, ops_per_thread);

    return 0;
}

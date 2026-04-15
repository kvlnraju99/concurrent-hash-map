#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "lock_free_hash_map.h"

namespace {

using Clock = std::chrono::steady_clock;

struct Config {
    int threads = static_cast<int>(std::thread::hardware_concurrency());
    int ops_per_thread = 20000;
    bool raw_mode = false;
    std::vector<size_t> bucket_counts = {16384, 32768, 65536, 131072, 262144, 524288};
};

struct SweepResult {
    size_t buckets = 0;
    uint64_t total_keys = 0;
    double load_factor = 0.0;
    double avg_chain_non_empty = 0.0;
    uint64_t max_chain = 0;
    double put_ns = 0.0;
    double get_ns = 0.0;
    double remove_ns = 0.0;
    double miss_get_ns = 0.0;
    double put_mops = 0.0;
    double get_mops = 0.0;
    double remove_mops = 0.0;
    double miss_get_mops = 0.0;
    double put_traversal_ns = 0.0;
    double put_allocation_ns = 0.0;
    double put_cas_ns = 0.0;
    double put_cas_failures = 0.0;
    uint64_t deleted_nodes_after_remove = 0;
};

void print_usage(const char* program) {
    std::cout << "Usage: " << program
              << " [--threads N] [--ops-per-thread N] [--raw]"
              << " [--buckets N1,N2,...]\n";
}

std::vector<size_t> parse_bucket_list(const std::string& text) {
    std::vector<size_t> buckets;
    std::stringstream stream(text);
    std::string item;
    while (std::getline(stream, item, ',')) {
        if (!item.empty()) {
            buckets.push_back(static_cast<size_t>(std::strtoull(item.c_str(), nullptr, 10)));
        }
    }
    if (buckets.empty()) {
        buckets = {16384, 32768, 65536, 131072, 262144, 524288};
    }
    return buckets;
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
        } else if (arg == "--raw") {
            config.raw_mode = true;
        } else if (arg == "--buckets" && i + 1 < argc) {
            config.bucket_counts = parse_bucket_list(argv[++i]);
        } else {
            print_usage(argv[0]);
            std::exit(1);
        }
    }

    return config;
}

template <typename Fn>
double run_parallel_phase(int num_threads, Fn&& fn) {
    std::vector<std::thread> threads;
    threads.reserve(num_threads);

    const auto start = Clock::now();
    for (int thread_id = 0; thread_id < num_threads; ++thread_id) {
        threads.emplace_back([&, thread_id]() { fn(thread_id); });
    }
    for (auto& thread : threads) {
        thread.join();
    }
    const auto end = Clock::now();
    return std::chrono::duration<double, std::milli>(end - start).count();
}

template <bool EnablePutProfiling>
SweepResult run_sweep_case(size_t buckets, int num_threads, int ops_per_thread) {
    LockFreeHashMap<int, int> map(buckets, EnablePutProfiling);
    const uint64_t total_keys = static_cast<uint64_t>(num_threads) * ops_per_thread;

    const double put_ms = run_parallel_phase(num_threads, [&](int thread_id) {
        for (int i = 0; i < ops_per_thread; ++i) {
            const int key = thread_id * ops_per_thread + i;
            map.put(key, key);
        }
    });
    const auto put_profile = map.get_put_profile();
    const auto after_put = map.get_bucket_stats();

    const double get_ms = run_parallel_phase(num_threads, [&](int thread_id) {
        for (int i = 0; i < ops_per_thread; ++i) {
            const int key = thread_id * ops_per_thread + i;
            (void)map.get(key);
        }
    });
    const double remove_ms = run_parallel_phase(num_threads, [&](int thread_id) {
        for (int i = 0; i < ops_per_thread; ++i) {
            const int key = thread_id * ops_per_thread + i;
            (void)map.remove(key);
        }
    });
    const auto after_remove = map.get_bucket_stats();
    const double miss_get_ms = run_parallel_phase(num_threads, [&](int thread_id) {
        for (int i = 0; i < ops_per_thread; ++i) {
            const int key = thread_id * ops_per_thread + i;
            (void)map.get(key);
        }
    });

    const double total_ops = total_keys == 0 ? 1.0 : static_cast<double>(total_keys);

    SweepResult result;
    result.buckets = buckets;
    result.total_keys = total_keys;
    result.load_factor = static_cast<double>(after_put.live_nodes) / static_cast<double>(buckets);
    result.avg_chain_non_empty =
        after_put.non_empty_buckets == 0
            ? 0.0
            : static_cast<double>(after_put.total_nodes) /
                  static_cast<double>(after_put.non_empty_buckets);
    result.max_chain = after_put.max_chain_length;
    result.put_ns = (put_ms * 1'000'000.0) / total_ops;
    result.get_ns = (get_ms * 1'000'000.0) / total_ops;
    result.remove_ns = (remove_ms * 1'000'000.0) / total_ops;
    result.miss_get_ns = (miss_get_ms * 1'000'000.0) / total_ops;
    result.put_mops = total_ops / (put_ms * 1000.0);
    result.get_mops = total_ops / (get_ms * 1000.0);
    result.remove_mops = total_ops / (remove_ms * 1000.0);
    result.miss_get_mops = total_ops / (miss_get_ms * 1000.0);
    result.deleted_nodes_after_remove = after_remove.deleted_nodes;

    if constexpr (EnablePutProfiling) {
        result.put_traversal_ns = put_profile.traversal_ns / total_ops;
        result.put_allocation_ns = put_profile.allocation_ns / total_ops;
        result.put_cas_ns = put_profile.cas_ns / total_ops;
        result.put_cas_failures = put_profile.cas_failures / total_ops;
    }
    return result;
}

void print_results(const std::vector<SweepResult>& results, bool include_put_profile) {
    std::cout << "\nBuckets      Load    AvgChain    MaxChain      PUT ns      GET ns      DEL ns"
                 "   MISS ns    PUT Mops    GET Mops    DEL Mops   MISS Mops\n";
    std::cout << "----------------------------------------------------------------------------------------------------------------\n";

    for (const auto& result : results) {
        std::cout << std::setw(7) << result.buckets
                  << std::setw(11) << std::fixed << std::setprecision(2) << result.load_factor
                  << std::setw(12) << result.avg_chain_non_empty
                  << std::setw(12) << result.max_chain
                  << std::setw(12) << result.put_ns
                  << std::setw(12) << result.get_ns
                  << std::setw(12) << result.remove_ns
                  << std::setw(11) << result.miss_get_ns
                  << std::setw(12) << result.put_mops
                  << std::setw(12) << result.get_mops
                  << std::setw(12) << result.remove_mops
                  << std::setw(12) << result.miss_get_mops
                  << "\n";
    }

    if (include_put_profile) {
        std::cout << "\nPUT internals (avg per insert)\n";
        std::cout << "Buckets      Traverse ns   Alloc ns     CAS ns   CAS fail/op   Deleted nodes after remove\n";
        std::cout << "--------------------------------------------------------------------------------------------\n";
        for (const auto& result : results) {
            std::cout << std::setw(7) << result.buckets
                      << std::setw(16) << std::fixed << std::setprecision(2) << result.put_traversal_ns
                      << std::setw(12) << result.put_allocation_ns
                      << std::setw(11) << result.put_cas_ns
                      << std::setw(14) << result.put_cas_failures
                      << std::setw(28) << result.deleted_nodes_after_remove
                      << "\n";
        }
    } else {
        std::cout << "\nDeleted nodes after remove\n";
        std::cout << "Buckets      Deleted nodes\n";
        std::cout << "--------------------------\n";
        for (const auto& result : results) {
            std::cout << std::setw(7) << result.buckets
                      << std::setw(19) << result.deleted_nodes_after_remove
                      << "\n";
        }
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    const Config config = parse_args(argc, argv);

    std::cout << (config.raw_mode ? "Lock-free bucket sweep (raw timings)\n"
                                  : "Lock-free bucket sweep\n");
    std::cout << "threads=" << config.threads
              << ", ops/thread=" << config.ops_per_thread
              << ", buckets=";
    for (size_t i = 0; i < config.bucket_counts.size(); ++i) {
        std::cout << config.bucket_counts[i];
        if (i + 1 < config.bucket_counts.size()) {
            std::cout << ",";
        }
    }
    std::cout << "\n";

    std::vector<SweepResult> results;
    results.reserve(config.bucket_counts.size());
    for (size_t buckets : config.bucket_counts) {
        std::cout << "running buckets=" << buckets << "...\n";
        if (config.raw_mode) {
            results.push_back(run_sweep_case<false>(buckets, config.threads, config.ops_per_thread));
        } else {
            results.push_back(run_sweep_case<true>(buckets, config.threads, config.ops_per_thread));
        }
    }

    print_results(results, !config.raw_mode);
    return 0;
}

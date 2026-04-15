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

using Clock = std::chrono::steady_clock;

struct SweepResult {
    size_t buckets = 0;
    uint64_t total_keys = 0;
    double load_factor = 0.0;
    double avg_chain_non_empty = 0.0;
    uint64_t max_chain = 0;
    double put_ms = 0.0;
    double get_ms = 0.0;
    double remove_ms = 0.0;
    double miss_get_ms = 0.0;
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
    result.put_ms = put_ms;
    result.get_ms = get_ms;
    result.remove_ms = remove_ms;
    result.miss_get_ms = miss_get_ms;
    result.put_ns = (put_ms * 1'000'000.0) / total_ops;
    result.get_ns = (get_ms * 1'000'000.0) / total_ops;
    result.remove_ns = (remove_ms * 1'000'000.0) / total_ops;
    result.miss_get_ns = (miss_get_ms * 1'000'000.0) / total_ops;
    result.put_mops = total_ops / (put_ms * 1000.0);
    result.get_mops = total_ops / (get_ms * 1000.0);
    result.remove_mops = total_ops / (remove_ms * 1000.0);
    result.miss_get_mops = total_ops / (miss_get_ms * 1000.0);
    if constexpr (EnablePutProfiling) {
        result.put_traversal_ns = put_profile.traversal_ns / total_ops;
        result.put_allocation_ns = put_profile.allocation_ns / total_ops;
        result.put_cas_ns = put_profile.cas_ns / total_ops;
        result.put_cas_failures = put_profile.cas_failures / total_ops;
    }
    result.deleted_nodes_after_remove = after_remove.deleted_nodes;
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
    int num_threads = static_cast<int>(std::thread::hardware_concurrency());
    if (num_threads <= 0) {
        num_threads = 4;
    }
    int ops_per_thread = 20000;

    if (argc >= 2) {
        num_threads = std::max(1, std::atoi(argv[1]));
    }
    if (argc >= 3) {
        ops_per_thread = std::max(1, std::atoi(argv[2]));
    }
    bool raw_mode = (argc >= 4 && std::string(argv[3]) == "--raw");

    const std::vector<size_t> bucket_counts = {16384, 32768, 65536, 131072, 262144, 524288};

    std::cout << (raw_mode ? "Lock-free bucket sweep (raw timings)\n"
                           : "Lock-free bucket sweep\n");
    std::cout << "threads=" << num_threads
              << ", ops/thread=" << ops_per_thread
              << ", total unique keys=" << static_cast<uint64_t>(num_threads) * ops_per_thread
              << "\n";

    std::vector<SweepResult> results;
    results.reserve(bucket_counts.size());

    for (size_t buckets : bucket_counts) {
        std::cout << "running buckets=" << buckets << "...\n";
        if (raw_mode) {
            results.push_back(run_sweep_case<false>(buckets, num_threads, ops_per_thread));
        } else {
            results.push_back(run_sweep_case<true>(buckets, num_threads, ops_per_thread));
        }
    }

    print_results(results, !raw_mode);
    return 0;
}

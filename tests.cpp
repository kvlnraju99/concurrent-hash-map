#include <atomic>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "concurrent_hash_map.h"
#include "lock_free_dynamic_resize_hash_map.h"
#include "lock_free_hash_map.h"
#include "lock_free_open_addressing_hash_map.h"

namespace {

struct TestState {
    int passed = 0;
    int failed = 0;

    void check(bool condition, const std::string& label) {
        if (condition) {
            std::cout << "  [PASS] " << label << "\n";
            ++passed;
        } else {
            std::cout << "  [FAIL] " << label << "\n";
            ++failed;
        }
    }
};

template <typename Map>
void map_put(Map& map, int key, int value) {
    static_cast<void>(map.put(key, value));
}

template <typename Map>
void run_basic_tests(TestState& state) {
    std::cout << "\n--- Basic operations ---\n";
    Map map(64);

    map_put(map, 1, 10);
    map_put(map, 2, 20);
    map_put(map, 1, 99);

    state.check(map.get(1).has_value() && map.get(1).value() == 99, "update keeps latest value");
    state.check(map.get(2).has_value() && map.get(2).value() == 20, "second key is readable");
    state.check(!map.get(3).has_value(), "missing key returns empty");
    state.check(map.size() == 2, "size counts unique keys");
    state.check(map.remove(2), "remove returns true for existing key");
    state.check(!map.get(2).has_value(), "removed key disappears");
    state.check(map.size() == 1, "size decreases after remove");
}

template <typename Map>
void run_parallel_put_remove(TestState& state, size_t buckets) {
    std::cout << "\n--- Parallel inserts and removes ---\n";
    Map map(buckets);
    const int num_threads = 8;
    const int keys_per_thread = 1000;
    std::vector<std::thread> threads;

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&map, t]() {
            for (int i = 0; i < keys_per_thread; ++i) {
                const int key = t * keys_per_thread + i;
                map_put(map, key, key * 2);
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }

    const size_t expected = static_cast<size_t>(num_threads) * keys_per_thread;
    bool all_present = true;
    for (size_t key = 0; key < expected; ++key) {
        const auto value = map.get(static_cast<int>(key));
        if (!value.has_value() || value.value() != static_cast<int>(key * 2)) {
            all_present = false;
            break;
        }
    }

    state.check(map.size() == expected, "all disjoint inserts are counted");
    state.check(all_present, "all inserted keys are readable");

    threads.clear();
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&map, t]() {
            for (int i = 0; i < keys_per_thread; ++i) {
                const int key = t * keys_per_thread + i;
                map.remove(key);
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }

    bool all_removed = true;
    for (size_t key = 0; key < expected; ++key) {
        if (map.get(static_cast<int>(key)).has_value()) {
            all_removed = false;
            break;
        }
    }

    state.check(map.size() == 0, "size returns to zero after removals");
    state.check(all_removed, "all removed keys stay absent");
}

template <typename Map>
void run_parallel_reads(TestState& state, size_t buckets) {
    std::cout << "\n--- Parallel reads ---\n";
    Map map(buckets);
    const int total_keys = 20000;
    const int num_threads = 8;
    const int reads_per_thread = total_keys / num_threads;
    std::atomic<bool> all_ok{true};
    std::vector<std::thread> threads;

    for (int key = 0; key < total_keys; ++key) {
        map_put(map, key, key + 7);
    }

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&map, &all_ok, t]() {
            const int begin = t * reads_per_thread;
            const int end = begin + reads_per_thread;
            for (int key = begin; key < end; ++key) {
                const auto value = map.get(key);
                if (!value.has_value() || value.value() != key + 7) {
                    all_ok.store(false, std::memory_order_relaxed);
                    break;
                }
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }

    state.check(all_ok.load(std::memory_order_relaxed), "parallel reads return expected values");
    state.check(map.size() == static_cast<size_t>(total_keys), "reads do not change size");
}

template <typename Map>
void run_same_key_overwrite(TestState& state, size_t buckets) {
    std::cout << "\n--- Same-key overwrite ---\n";
    Map map(buckets);
    const int num_threads = 8;
    const int writes_per_thread = 5000;
    std::vector<std::thread> threads;

    map_put(map, 0, -1);
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&map, t]() {
            for (int i = 0; i < writes_per_thread; ++i) {
                map_put(map, 0, t * writes_per_thread + i);
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }

    const auto value = map.get(0);
    state.check(value.has_value(), "same key remains present after concurrent updates");
    state.check(value.has_value() && value.value() >= 0 && value.value() < num_threads * writes_per_thread,
                "same key ends with a valid value");
    state.check(map.size() == 1, "same-key updates do not create extra logical entries");
}

template <typename Map>
void run_resize_smoke(TestState& state, size_t initial_buckets) {
    std::cout << "\n--- Resize smoke test ---\n";
    Map map(initial_buckets);
    for (int key = 0; key < 5000; ++key) {
        map_put(map, key, key);
    }

    bool all_present = true;
    for (int key = 0; key < 5000; ++key) {
        const auto value = map.get(key);
        if (!value.has_value() || value.value() != key) {
            all_present = false;
            break;
        }
    }

    state.check(map.get_bucket_count() > initial_buckets, "bucket count grows after enough inserts");
    state.check(all_present, "all keys survive resizing");
}

template <typename Map>
void run_suite(TestState& state,
               const std::string& name,
               size_t buckets,
               bool expect_resize) {
    std::cout << "\n========================================\n";
    std::cout << name << "\n";
    std::cout << "========================================\n";

    run_basic_tests<Map>(state);
    run_parallel_put_remove<Map>(state, buckets);
    run_parallel_reads<Map>(state, buckets);
    run_same_key_overwrite<Map>(state, buckets);
    if (expect_resize) {
        run_resize_smoke<Map>(state, 4);
    }
}

void print_usage(const char* program) {
    std::cout << "Usage: " << program << " [all|locked|lockfree|resize|open]\n";
}

}  // namespace

int main(int argc, char* argv[]) {
    std::string mode = "all";
    if (argc > 2) {
        print_usage(argv[0]);
        return 1;
    }
    if (argc == 2) {
        mode = argv[1];
        if (mode != "all" && mode != "locked" && mode != "lockfree" &&
            mode != "resize" && mode != "open") {
            print_usage(argv[0]);
            return 1;
        }
    }

    TestState state;
    const size_t common_buckets = 65536;

    if (mode == "all" || mode == "locked") {
        run_suite<ConcurrentHashMap<int, int>>(state,
                                              "Locked dynamic hash map",
                                              common_buckets,
                                              true);
    }
    if (mode == "all" || mode == "lockfree") {
        run_suite<LockFreeHashMap<int, int>>(state,
                                             "Fixed-size lock-free chaining",
                                             common_buckets,
                                             false);
    }
    if (mode == "all" || mode == "resize") {
        run_suite<LockFreeDynamicResizeHashMap<int, int>>(state,
                                                          "Lock-free dynamic resize",
                                                          common_buckets,
                                                          true);
    }
    if (mode == "all" || mode == "open") {
        run_suite<LockFreeOpenAddressingHashMap<int, int>>(state,
                                                           "Open addressing experiment",
                                                           common_buckets,
                                                           false);
    }

    std::cout << "\n========================================\n";
    std::cout << "Results: " << state.passed << " passed, " << state.failed << " failed\n";
    std::cout << "========================================\n";
    return state.failed == 0 ? 0 : 1;
}

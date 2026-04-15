#include <atomic>
#include <optional>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "concurrent_hash_map.h"
#include "lock_free_hash_map.h"

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
void run_disjoint_insert_remove_round(TestState& state,
                                      const std::string& label,
                                      size_t buckets,
                                      int num_threads,
                                      int keys_per_thread) {
    std::cout << "\n--- " << label << " ---\n";
    Map map(buckets);
    std::vector<std::thread> threads;

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&map, t, keys_per_thread]() {
            for (int i = 0; i < keys_per_thread; ++i) {
                const int key = t * keys_per_thread + i;
                map.put(key, key);
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
        if (!value.has_value() || value.value() != static_cast<int>(key)) {
            all_present = false;
            break;
        }
    }

    state.check(map.size() == expected, "size matches inserted unique keys");
    state.check(all_present, "all inserted keys are readable");

    threads.clear();
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&map, t, keys_per_thread]() {
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
    state.check(all_removed, "all inserted keys are removed");
}

template <typename Map>
void run_same_key_race_rounds(TestState& state,
                              const std::string& label,
                              size_t buckets,
                              int rounds,
                              int num_threads,
                              int ops_per_thread) {
    std::cout << "\n--- " << label << " ---\n";
    bool all_rounds_ok = true;

    for (int round = 0; round < rounds; ++round) {
        Map map(buckets);
        map.put(0, -1);

        std::vector<std::thread> threads;
        for (int t = 0; t < num_threads; ++t) {
            threads.emplace_back([&map, t, ops_per_thread]() {
                for (int i = 0; i < ops_per_thread; ++i) {
                    const int op = (t + i) % 3;
                    if (op == 0) {
                        map.put(0, t * ops_per_thread + i);
                    } else if (op == 1) {
                        map.get(0);
                    } else {
                        map.remove(0);
                    }
                }
            });
        }
        for (auto& thread : threads) {
            thread.join();
        }

        const auto value = map.get(0);
        const size_t size = map.size();
        const bool valid_size = (size <= 1);
        const bool valid_value =
            !value.has_value() || (value.value() >= 0 && value.value() < num_threads * ops_per_thread);

        if (!valid_size || !valid_value) {
            all_rounds_ok = false;
            break;
        }
    }

    state.check(all_rounds_ok, "repeated same-key race keeps valid state");
}

void test_locked_single_thread(TestState& state) {
    std::cout << "\n[Locked] Single-thread\n";

    {
        std::cout << "\n--- Insert and Get ---\n";
        ConcurrentHashMap<std::string, int> map(8);
        map.put("apple", 1);
        map.put("banana", 2);
        map.put("cherry", 3);
        state.check(map.get("apple") == 1, "get apple -> 1");
        state.check(map.get("banana") == 2, "get banana -> 2");
        state.check(map.get("cherry") == 3, "get cherry -> 3");
    }

    {
        std::cout << "\n--- Missing and Update ---\n";
        ConcurrentHashMap<std::string, int> map(8);
        map.put("apple", 1);
        state.check(!map.get("grape").has_value(), "missing grape");
        map.put("apple", 10);
        map.put("apple", 99);
        state.check(map.get("apple") == 99, "update apple -> 99");
    }

    {
        std::cout << "\n--- Remove ---\n";
        ConcurrentHashMap<std::string, int> map(8);
        map.put("apple", 1);
        map.put("banana", 2);
        state.check(map.remove("apple"), "remove apple");
        state.check(!map.get("apple").has_value(), "apple removed");
        state.check(!map.remove("apple"), "remove apple again -> false");
        state.check(map.get("banana") == 2, "banana still exists");
    }

    {
        std::cout << "\n--- Size Tracking ---\n";
        ConcurrentHashMap<int, int> map(8);
        state.check(map.size() == 0, "size starts at zero");
        map.put(1, 10);
        map.put(2, 20);
        map.put(1, 99);
        state.check(map.size() == 2, "size ignores updates to existing keys");
        map.remove(2);
        state.check(map.size() == 1, "size decreases after remove");
    }
}

void test_locked_parallel(TestState& state) {
    std::cout << "\n[Locked] Parallel and resize\n";

    {
        std::cout << "\n--- Parallel Puts ---\n";
        ConcurrentHashMap<int, int> map(16);
        std::vector<std::thread> threads;
        for (int t = 0; t < 8; ++t) {
            threads.emplace_back([&map, t]() {
                for (int i = 0; i < 1000; ++i) {
                    const int key = t * 1000 + i;
                    map.put(key, key * 10);
                }
            });
        }
        for (auto& thread : threads) {
            thread.join();
        }

        bool all_ok = true;
        for (int i = 0; i < 8000; ++i) {
            const auto value = map.get(i);
            if (!value.has_value() || value.value() != i * 10) {
                all_ok = false;
                break;
            }
        }
        state.check(all_ok, "all 8000 keys inserted");
        state.check(map.size() == 8000, "size matches concurrent inserts");
    }

    {
        std::cout << "\n--- Same-key Overwrite ---\n";
        ConcurrentHashMap<int, int> map(16);
        map.put(0, -1);

        std::vector<std::thread> threads;
        for (int t = 0; t < 8; ++t) {
            threads.emplace_back([&map, t]() {
                for (int i = 0; i < 10000; ++i) {
                    map.put(0, t * 10000 + i);
                }
            });
        }
        for (auto& thread : threads) {
            thread.join();
        }

        const auto value = map.get(0);
        state.check(value.has_value(), "key exists after overwrites");
        state.check(value.value() >= 0 && value.value() < 80000, "value stays in expected range");
        state.check(map.size() == 1, "size stays at one for same-key overwrites");
    }

    {
        std::cout << "\n--- Stress ---\n";
        ConcurrentHashMap<int, int> map(16);
        std::vector<std::thread> threads;
        for (int t = 0; t < 8; ++t) {
            threads.emplace_back([&map, t]() {
                for (int i = 0; i < 50000; ++i) {
                    const int key = i % 1000;
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
        state.check(true, "400000 ops completed without crashing");
        state.check(map.size() <= 2000, "stress size stays reasonably bounded under mixed races");
    }

    {
        std::cout << "\n--- Resize ---\n";
        ConcurrentHashMap<int, int> map(4, 0.75);
        state.check(map.get_bucket_count() == 4, "starts with 4 buckets");
        map.put(1, 10);
        map.put(2, 20);
        map.put(3, 30);
        state.check(map.get_bucket_count() == 4, "still 4 after 3 inserts");
        map.put(4, 40);
        state.check(map.get_bucket_count() == 8, "resized to 8 after 4 inserts");

        ConcurrentHashMap<int, int> concurrent_map(4, 0.75);
        std::vector<std::thread> threads;
        for (int t = 0; t < 8; ++t) {
            threads.emplace_back([&concurrent_map, t]() {
                for (int i = 0; i < 1000; ++i) {
                    concurrent_map.put(t * 1000 + i, t * 1000 + i);
                }
            });
        }
        for (auto& thread : threads) {
            thread.join();
        }

        bool all_ok = true;
        for (int i = 0; i < 8000; ++i) {
            const auto value = concurrent_map.get(i);
            if (!value.has_value() || value.value() != i) {
                all_ok = false;
                break;
            }
        }
        state.check(all_ok, "concurrent resize preserves values");
        state.check(concurrent_map.get_bucket_count() > 4, "bucket count grew after concurrent inserts");
        state.check(concurrent_map.size() == 8000, "resize path keeps exact size");
    }

    run_disjoint_insert_remove_round<ConcurrentHashMap<int, int>>(
        state, "Disjoint insert/remove invariants", 64, 8, 512);
    run_same_key_race_rounds<ConcurrentHashMap<int, int>>(
        state, "Repeated same-key race", 32, 8, 8, 2000);
}

void test_lockfree_single_thread(TestState& state) {
    std::cout << "\n[Lock-free] Single-thread\n";

    {
        std::cout << "\n--- Insert and Get ---\n";
        LockFreeHashMap<std::string, int> map(8);
        map.put("apple", 1);
        map.put("banana", 2);
        map.put("cherry", 3);
        state.check(map.get("apple") == 1, "get apple -> 1");
        state.check(map.get("banana") == 2, "get banana -> 2");
        state.check(map.get("cherry") == 3, "get cherry -> 3");
    }

    {
        std::cout << "\n--- Missing and Update ---\n";
        LockFreeHashMap<std::string, int> map(8);
        map.put("apple", 1);
        state.check(!map.get("grape").has_value(), "missing grape");
        map.put("apple", 10);
        state.check(map.get("apple") == 10, "update apple -> 10");
    }

    {
        std::cout << "\n--- Remove ---\n";
        LockFreeHashMap<std::string, int> map(8);
        map.put("apple", 1);
        map.put("banana", 2);
        state.check(map.remove("apple"), "remove apple");
        state.check(!map.get("apple").has_value(), "apple removed");
        state.check(!map.remove("apple"), "remove apple again -> false");
        state.check(map.get("banana") == 2, "banana still exists");
    }

    {
        std::cout << "\n--- Size and Cleanup ---\n";
        LockFreeHashMap<int, int> map(8);
        state.check(map.size() == 0, "size starts at zero");
        map.put(1, 10);
        map.put(2, 20);
        map.put(1, 99);
        state.check(map.size() == 2, "size ignores updates to existing keys");
        map.remove(1);
        map.remove(2);
        (void)map.get(1);
        (void)map.get(2);
        const auto stats = map.get_bucket_stats();
        state.check(map.size() == 0, "size decreases after removes");
        state.check(stats.deleted_nodes == 0, "deleted nodes are unlinked in single-thread cleanup");
    }

    {
        std::cout << "\n--- Resize ---\n";
        LockFreeHashMap<int, int> map(4, false, 0.75);
        state.check(map.get_bucket_count() == 4, "starts with 4 buckets");
        map.put(1, 10);
        map.put(2, 20);
        map.put(3, 30);
        state.check(map.get_bucket_count() == 4, "still 4 after 3 inserts");
        map.put(4, 40);
        state.check(map.get_bucket_count() == 8, "resized to 8 after 4 inserts");
        state.check(map.size() == 4, "size preserved after resize");
        state.check(map.get(1) == 10, "resize keeps key 1");
        state.check(map.get(4) == 40, "resize keeps key 4");
    }
}

void test_lockfree_parallel(TestState& state) {
    std::cout << "\n[Lock-free] Parallel\n";

    {
        std::cout << "\n--- Parallel Puts ---\n";
        LockFreeHashMap<int, int> map(64);
        std::vector<std::thread> threads;
        for (int t = 0; t < 8; ++t) {
            threads.emplace_back([&map, t]() {
                for (int i = 0; i < 1000; ++i) {
                    const int key = t * 1000 + i;
                    map.put(key, key * 10);
                }
            });
        }
        for (auto& thread : threads) {
            thread.join();
        }

        bool all_ok = true;
        for (int i = 0; i < 8000; ++i) {
            const auto value = map.get(i);
            if (!value.has_value() || value.value() != i * 10) {
                all_ok = false;
                break;
            }
        }
        state.check(all_ok, "all 8000 keys inserted");
        state.check(map.size() == 8000, "size matches concurrent inserts");
    }

    {
        std::cout << "\n--- Resize Repro (Small) ---\n";
        LockFreeHashMap<int, int> map(4, false, 0.75);
        std::vector<std::thread> threads;
        for (int t = 0; t < 4; ++t) {
            threads.emplace_back([&map, t]() {
                for (int i = 0; i < 64; ++i) {
                    const int key = t * 1000 + i;
                    map.put(key, key);
                    if ((i % 8) == 0) {
                        std::this_thread::yield();
                    }
                }
            });
        }
        for (auto& thread : threads) {
            thread.join();
        }

        bool all_ok = true;
        int first_missing = -1;
        for (int t = 0; t < 4 && all_ok; ++t) {
            for (int i = 0; i < 64; ++i) {
                const int key = t * 1000 + i;
                const auto value = map.get(key);
                if (!value.has_value() || value.value() != key) {
                    all_ok = false;
                    first_missing = key;
                    break;
                }
            }
        }
        state.check(all_ok, "small resize repro preserves every inserted key");
        if (!all_ok) {
            std::cout << "    first missing key: " << first_missing << "\n";
        }
        state.check(map.size() == 256, "small resize repro keeps exact size");
    }

    {
        std::cout << "\n--- Parallel Gets ---\n";
        LockFreeHashMap<int, int> map(16);
        for (int i = 0; i < 100; ++i) {
            map.put(i, i * 5);
        }

        std::atomic<bool> all_ok(true);
        std::vector<std::thread> threads;
        for (int t = 0; t < 8; ++t) {
            threads.emplace_back([&map, &all_ok]() {
                for (int i = 0; i < 100; ++i) {
                    const auto value = map.get(i);
                    if (!value.has_value() || value.value() != i * 5) {
                        all_ok.store(false, std::memory_order_relaxed);
                    }
                }
            });
        }
        for (auto& thread : threads) {
            thread.join();
        }
        state.check(all_ok.load(std::memory_order_relaxed), "all concurrent gets returned correct values");
    }

    {
        std::cout << "\n--- Parallel Removes ---\n";
        LockFreeHashMap<int, int> map(16);
        for (int i = 0; i < 800; ++i) {
            map.put(i, i);
        }

        std::vector<std::thread> threads;
        for (int t = 0; t < 8; ++t) {
            threads.emplace_back([&map, t]() {
                for (int i = t * 100; i < (t + 1) * 100; ++i) {
                    map.remove(i);
                }
            });
        }
        for (auto& thread : threads) {
            thread.join();
        }

        bool all_removed = true;
        for (int i = 0; i < 800; ++i) {
            if (map.get(i).has_value()) {
                all_removed = false;
                break;
            }
        }
        state.check(all_removed, "all 800 keys removed");
        state.check(map.size() == 0, "size returns to zero after parallel removes");
    }

    {
        std::cout << "\n--- Same-key Overwrite ---\n";
        LockFreeHashMap<int, int> map(16);
        map.put(0, -1);
        std::vector<std::thread> threads;
        for (int t = 0; t < 8; ++t) {
            threads.emplace_back([&map, t]() {
                for (int i = 0; i < 10000; ++i) {
                    map.put(0, t * 10000 + i);
                }
            });
        }
        for (auto& thread : threads) {
            thread.join();
        }

        const auto value = map.get(0);
        state.check(value.has_value(), "key exists after overwrites");
        state.check(value.value() >= 0 && value.value() < 80000, "value stays in expected range");
        state.check(map.size() <= 1, "size stays bounded for same-key overwrites");
    }

    {
        std::cout << "\n--- Stress ---\n";
        LockFreeHashMap<int, int> map(64);
        std::vector<std::thread> threads;
        for (int t = 0; t < 8; ++t) {
            threads.emplace_back([&map, t]() {
                for (int i = 0; i < 50000; ++i) {
                    const int key = i % 1000;
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
        state.check(true, "400000 ops completed without crashing");
        state.check(map.size() <= 1000, "stress size stays within key space");
    }

    run_disjoint_insert_remove_round<LockFreeHashMap<int, int>>(
        state, "Disjoint insert/remove invariants", 256, 8, 512);
    run_same_key_race_rounds<LockFreeHashMap<int, int>>(
        state, "Repeated same-key race", 64, 8, 8, 2000);

    {
        std::cout << "\n--- Tombstone Cleanup Under Reuse ---\n";
        LockFreeHashMap<int, int> map(128);
        for (int round = 0; round < 10; ++round) {
            for (int key = 0; key < 256; ++key) {
                map.put(key, round);
            }
            for (int key = 0; key < 256; ++key) {
                map.remove(key);
            }
        }
        for (int key = 0; key < 256; ++key) {
            (void)map.get(key);
        }
        const auto stats = map.get_bucket_stats();
        state.check(map.size() == 0, "reused map returns to zero size");
        state.check(stats.deleted_nodes == 0, "deleted nodes do not accumulate after reuse");
    }

    {
        std::cout << "\n--- Concurrent Resize ---\n";
        LockFreeHashMap<int, int> map(4, false, 0.75);
        std::vector<std::thread> threads;
        for (int t = 0; t < 8; ++t) {
            threads.emplace_back([&map, t]() {
                for (int i = 0; i < 1000; ++i) {
                    const int key = t * 1000 + i;
                    map.put(key, key);
                }
            });
        }
        for (auto& thread : threads) {
            thread.join();
        }

        bool all_ok = true;
        for (int i = 0; i < 8000; ++i) {
            const auto value = map.get(i);
            if (!value.has_value() || value.value() != i) {
                all_ok = false;
                break;
            }
        }

        state.check(all_ok, "concurrent resize preserves values");
        state.check(map.get_bucket_count() > 4, "bucket count grows after concurrent inserts");
        state.check(map.size() == 8000, "concurrent resize keeps exact size");
    }
}

void print_usage(const char* program) {
    std::cout << "Usage: " << program << " [all|locked|lockfree]\n";
}

}  // namespace

int main(int argc, char* argv[]) {
    std::string suite = "all";
    if (argc >= 2) {
        suite = argv[1];
    }

    if (suite != "all" && suite != "locked" && suite != "lockfree") {
        print_usage(argv[0]);
        return 1;
    }

    TestState state;
    std::cout << "========================================\n";
    std::cout << " Concurrent Hash Map Test Suite\n";
    std::cout << " Suite: " << suite << "\n";
    std::cout << "========================================\n";

    if (suite == "all" || suite == "locked") {
        test_locked_single_thread(state);
        test_locked_parallel(state);
    }
    if (suite == "all" || suite == "lockfree") {
        test_lockfree_single_thread(state);
        test_lockfree_parallel(state);
    }

    std::cout << "\n========================================\n";
    std::cout << " Results: " << state.passed << " passed, "
              << state.failed << " failed\n";
    std::cout << "========================================\n";
    return state.failed == 0 ? 0 : 1;
}

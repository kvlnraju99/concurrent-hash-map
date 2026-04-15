#include <atomic>
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
    }
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

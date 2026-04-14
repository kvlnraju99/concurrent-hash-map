#include <iostream>
#include <thread>
#include <vector>
#include <cassert>
#include <atomic>
#include "concurrent_hash_map.h"
#include "lock_free_hash_map.h"

// ─── Helpers ────────────────────────────────────────────────────────

int pass_count = 0;
int fail_count = 0;

void check(bool condition, const std::string& label) {
    if (condition) {
        std::cout << "  [PASS] " << label << "\n";
        pass_count++;
    } else {
        std::cout << "  [FAIL] " << label << "\n";
        fail_count++;
    }
}

// =====================================================================
//  PART 1: SINGLE-THREAD TESTS (Lock-Based)
// =====================================================================

void test_insert_and_get() {
    std::cout << "\n--- Test: Insert and Get ---\n";
    ConcurrentHashMap<std::string, int> map(8);

    map.put("apple", 1);
    map.put("banana", 2);
    map.put("cherry", 3);

    check(map.get("apple") == 1,   "get apple  -> 1");
    check(map.get("banana") == 2,  "get banana -> 2");
    check(map.get("cherry") == 3,  "get cherry -> 3");
}

void test_get_missing_key() {
    std::cout << "\n--- Test: Get Missing Key ---\n";
    ConcurrentHashMap<std::string, int> map(8);
    map.put("apple", 1);

    check(!map.get("grape").has_value(),  "get grape  -> not found");
    check(!map.get("mango").has_value(),  "get mango  -> not found");
}

void test_update_existing_key() {
    std::cout << "\n--- Test: Update Existing Key ---\n";
    ConcurrentHashMap<std::string, int> map(8);

    map.put("apple", 1);
    check(map.get("apple") == 1,  "apple starts at 1");
    map.put("apple", 10);
    check(map.get("apple") == 10, "apple updated to 10");
    map.put("apple", 99);
    check(map.get("apple") == 99, "apple updated to 99");
}

void test_remove() {
    std::cout << "\n--- Test: Remove ---\n";
    ConcurrentHashMap<std::string, int> map(8);

    map.put("apple", 1);
    map.put("banana", 2);

    check(map.remove("apple") == true,          "remove apple -> true");
    check(!map.get("apple").has_value(),         "apple is gone");
    check(map.remove("apple") == false,          "remove apple again -> false");
    check(map.get("banana") == 2,                "banana still exists");
}

// =====================================================================
//  PART 2: MULTI-THREAD TESTS (Lock-Based)
// =====================================================================

void test_parallel_puts() {
    std::cout << "\n--- Test: Parallel Puts ---\n";
    ConcurrentHashMap<int, int> map(16);

    const int num_threads = 8;
    std::vector<std::thread> threads;

    for (int t = 0; t < num_threads; t++) {
        threads.emplace_back([&map, t]() {
            for (int i = 0; i < 1000; i++) {
                int key = t * 1000 + i;
                map.put(key, key * 10);
            }
        });
    }
    for (auto& th : threads) th.join();

    bool all_ok = true;
    for (int i = 0; i < 8000; i++) {
        auto val = map.get(i);
        if (!val.has_value() || val.value() != i * 10) { all_ok = false; break; }
    }
    check(all_ok, "all 8000 keys inserted correctly");
}

void test_same_key_overwrite() {
    std::cout << "\n--- Test: Same-Key Overwrite ---\n";
    ConcurrentHashMap<int, int> map(16);
    map.put(0, -1);

    std::vector<std::thread> threads;
    for (int t = 0; t < 8; t++) {
        threads.emplace_back([&map, t]() {
            for (int i = 0; i < 10000; i++) {
                map.put(0, t * 10000 + i);
            }
        });
    }
    for (auto& th : threads) th.join();

    auto val = map.get(0);
    check(val.has_value(), "key exists after 80000 overwrites");
    check(val.value() >= 0 && val.value() < 80000, "value is valid");
}

void test_stress_lock_based() {
    std::cout << "\n--- Test: Stress (lock-based) ---\n";
    ConcurrentHashMap<int, int> map(16);

    std::vector<std::thread> threads;
    for (int t = 0; t < 8; t++) {
        threads.emplace_back([&map, t]() {
            for (int i = 0; i < 50000; i++) {
                int key = i % 1000;
                int op = (t + i) % 3;
                if (op == 0)      map.put(key, i);
                else if (op == 1) map.get(key);
                else              map.remove(key);
            }
        });
    }
    for (auto& th : threads) th.join();
    check(true, "400000 ops — no crash");
}

// =====================================================================
//  PART 3: DYNAMIC RESIZING TESTS (Lock-Based)
// =====================================================================

void test_resize_triggers() {
    std::cout << "\n--- Test: Resize Triggers ---\n";
    ConcurrentHashMap<int, int> map(4, 0.75);

    check(map.get_bucket_count() == 4, "starts with 4 buckets");
    map.put(1, 10); map.put(2, 20); map.put(3, 30);
    check(map.get_bucket_count() == 4, "still 4 after 3 inserts");
    map.put(4, 40);
    check(map.get_bucket_count() == 8, "resized to 8 after 4 inserts");
    check(map.get(1) == 10 && map.get(2) == 20 && map.get(3) == 30 && map.get(4) == 40,
          "all data preserved after resize");
}

void test_concurrent_resize() {
    std::cout << "\n--- Test: Concurrent Resize ---\n";
    ConcurrentHashMap<int, int> map(4, 0.75);

    std::vector<std::thread> threads;
    for (int t = 0; t < 8; t++) {
        threads.emplace_back([&map, t]() {
            for (int i = 0; i < 1000; i++) {
                map.put(t * 1000 + i, t * 1000 + i);
            }
        });
    }
    for (auto& th : threads) th.join();

    bool all_ok = true;
    for (int i = 0; i < 8000; i++) {
        auto val = map.get(i);
        if (!val.has_value() || val.value() != i) { all_ok = false; break; }
    }
    check(all_ok, "8000 keys correct after concurrent resizes");
    check(map.get_bucket_count() > 4, "buckets grew from 4 to " + std::to_string(map.get_bucket_count()));
}

// =====================================================================
//  PART 4: LOCK-FREE SINGLE-THREAD TESTS
// =====================================================================

void test_lf_insert_and_get() {
    std::cout << "\n--- Test: [LF] Insert and Get ---\n";
    LockFreeHashMap<std::string, int> map(8);

    map.put("apple", 1);
    map.put("banana", 2);
    map.put("cherry", 3);

    check(map.get("apple") == 1,   "[LF] get apple  -> 1");
    check(map.get("banana") == 2,  "[LF] get banana -> 2");
    check(map.get("cherry") == 3,  "[LF] get cherry -> 3");
}

void test_lf_get_missing() {
    std::cout << "\n--- Test: [LF] Get Missing Key ---\n";
    LockFreeHashMap<std::string, int> map(8);
    map.put("apple", 1);

    check(!map.get("grape").has_value(), "[LF] get grape -> not found");
    check(!map.get("mango").has_value(), "[LF] get mango -> not found");
}

void test_lf_update() {
    std::cout << "\n--- Test: [LF] Update Existing Key ---\n";
    LockFreeHashMap<std::string, int> map(8);

    map.put("apple", 1);
    check(map.get("apple") == 1,  "[LF] apple starts at 1");
    map.put("apple", 10);
    check(map.get("apple") == 10, "[LF] apple updated to 10");
}

void test_lf_remove() {
    std::cout << "\n--- Test: [LF] Remove ---\n";
    LockFreeHashMap<std::string, int> map(8);

    map.put("apple", 1);
    map.put("banana", 2);

    check(map.remove("apple") == true,   "[LF] remove apple -> true");
    check(!map.get("apple").has_value(), "[LF] apple is gone");
    check(map.remove("apple") == false,  "[LF] remove apple again -> false");
    check(map.get("banana") == 2,        "[LF] banana still exists");
}

// =====================================================================
//  PART 5: LOCK-FREE MULTI-THREAD TESTS
// =====================================================================

void test_lf_parallel_puts() {
    std::cout << "\n--- Test: [LF] Parallel Puts ---\n";
    LockFreeHashMap<int, int> map(64);

    const int num_threads = 8;
    std::vector<std::thread> threads;

    for (int t = 0; t < num_threads; t++) {
        threads.emplace_back([&map, t]() {
            for (int i = 0; i < 1000; i++) {
                int key = t * 1000 + i;
                map.put(key, key * 10);
            }
        });
    }
    for (auto& th : threads) th.join();

    bool all_ok = true;
    for (int i = 0; i < 8000; i++) {
        auto val = map.get(i);
        if (!val.has_value() || val.value() != i * 10) { all_ok = false; break; }
    }
    check(all_ok, "[LF] all 8000 keys inserted correctly");
}

void test_lf_parallel_gets() {
    std::cout << "\n--- Test: [LF] Parallel Gets ---\n";
    LockFreeHashMap<int, int> map(16);

    for (int i = 0; i < 100; i++) map.put(i, i * 5);

    std::vector<std::thread> threads;
    std::atomic<bool> all_ok(true);

    for (int t = 0; t < 8; t++) {
        threads.emplace_back([&map, &all_ok]() {
            for (int i = 0; i < 100; i++) {
                auto val = map.get(i);
                if (!val.has_value() || val.value() != i * 5) all_ok = false;
            }
        });
    }
    for (auto& th : threads) th.join();
    check(all_ok, "[LF] all concurrent gets returned correct values");
}

void test_lf_parallel_removes() {
    std::cout << "\n--- Test: [LF] Parallel Removes ---\n";
    LockFreeHashMap<int, int> map(16);

    for (int i = 0; i < 800; i++) map.put(i, i);

    std::vector<std::thread> threads;
    for (int t = 0; t < 8; t++) {
        threads.emplace_back([&map, t]() {
            for (int i = t * 100; i < t * 100 + 100; i++) map.remove(i);
        });
    }
    for (auto& th : threads) th.join();

    bool all_removed = true;
    for (int i = 0; i < 800; i++) {
        if (map.get(i).has_value()) { all_removed = false; break; }
    }
    check(all_removed, "[LF] all 800 keys removed");
}

void test_lf_same_key_overwrite() {
    std::cout << "\n--- Test: [LF] Same-Key Overwrite ---\n";
    LockFreeHashMap<int, int> map(16);
    map.put(0, -1);

    std::vector<std::thread> threads;
    for (int t = 0; t < 8; t++) {
        threads.emplace_back([&map, t]() {
            for (int i = 0; i < 10000; i++) {
                map.put(0, t * 10000 + i);
            }
        });
    }
    for (auto& th : threads) th.join();

    auto val = map.get(0);
    check(val.has_value(), "[LF] key exists after 80000 overwrites");
    check(val.value() >= 0 && val.value() < 80000, "[LF] value is valid");
}

void test_lf_stress() {
    std::cout << "\n--- Test: [LF] Stress ---\n";
    LockFreeHashMap<int, int> map(64);

    std::vector<std::thread> threads;
    for (int t = 0; t < 8; t++) {
        threads.emplace_back([&map, t]() {
            for (int i = 0; i < 50000; i++) {
                int key = i % 1000;
                int op = (t + i) % 3;
                if (op == 0)      map.put(key, i);
                else if (op == 1) map.get(key);
                else              map.remove(key);
            }
        });
    }
    for (auto& th : threads) th.join();
    check(true, "[LF] 400000 ops — no crash");
}

// ─── Main ───────────────────────────────────────────────────────────

int main() {
    std::cout << "========================================\n";
    std::cout << " Concurrent Hash Map — Full Test Suite\n";
    std::cout << "========================================\n";

    // Lock-based tests.
    std::cout << "\n[PART 1] Lock-Based: Single-Thread\n";
    test_insert_and_get();
    test_get_missing_key();
    test_update_existing_key();
    test_remove();

    std::cout << "\n[PART 2] Lock-Based: Multi-Thread\n";
    test_parallel_puts();
    test_same_key_overwrite();
    test_stress_lock_based();

    std::cout << "\n[PART 3] Lock-Based: Dynamic Resizing\n";
    test_resize_triggers();
    test_concurrent_resize();

    // Lock-free tests.
    std::cout << "\n[PART 4] Lock-Free: Single-Thread\n";
    test_lf_insert_and_get();
    test_lf_get_missing();
    test_lf_update();
    test_lf_remove();

    std::cout << "\n[PART 5] Lock-Free: Multi-Thread\n";
    test_lf_parallel_puts();
    test_lf_parallel_gets();
    test_lf_parallel_removes();
    test_lf_same_key_overwrite();
    test_lf_stress();

    std::cout << "\n========================================\n";
    std::cout << " Results: " << pass_count << " passed, "
              << fail_count << " failed\n";
    std::cout << "========================================\n\n";

    return fail_count > 0 ? 1 : 0;
}

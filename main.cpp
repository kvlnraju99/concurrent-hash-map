#include <iostream>
#include <thread>
#include <vector>
#include <cassert>
#include <atomic>
#include "concurrent_hash_map.h"

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
//  PART 1: SINGLE-THREAD TESTS
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
//  PART 2: MULTI-THREAD TESTS (DIFFERENT BUCKETS)
// =====================================================================

void test_parallel_puts_different_buckets() {
    std::cout << "\n--- Test: Parallel Puts to Different Buckets ---\n";
    ConcurrentHashMap<int, int> map(16);

    const int num_threads = 8;
    const int keys_per_thread = 1000;
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
    for (int i = 0; i < num_threads * keys_per_thread; i++) {
        auto val = map.get(i);
        if (!val.has_value() || val.value() != i * 10) {
            all_ok = false;
            break;
        }
    }
    check(all_ok, "all 8000 keys inserted correctly");
}

void test_parallel_gets() {
    std::cout << "\n--- Test: Parallel Gets ---\n";
    ConcurrentHashMap<int, int> map(16);

    for (int i = 0; i < 100; i++) map.put(i, i * 5);

    const int num_threads = 8;
    std::vector<std::thread> threads;
    std::atomic<bool> all_ok(true);

    for (int t = 0; t < num_threads; t++) {
        threads.emplace_back([&map, &all_ok]() {
            for (int i = 0; i < 100; i++) {
                auto val = map.get(i);
                if (!val.has_value() || val.value() != i * 5) {
                    all_ok = false;
                }
            }
        });
    }

    for (auto& th : threads) th.join();
    check(all_ok, "all concurrent gets returned correct values");
}

void test_parallel_removes() {
    std::cout << "\n--- Test: Parallel Removes ---\n";
    ConcurrentHashMap<int, int> map(16);

    const int total_keys = 800;
    for (int i = 0; i < total_keys; i++) map.put(i, i);

    const int num_threads = 8;
    std::vector<std::thread> threads;

    for (int t = 0; t < num_threads; t++) {
        threads.emplace_back([&map, t]() {
            int start = t * 100;
            for (int i = start; i < start + 100; i++) {
                map.remove(i);
            }
        });
    }

    for (auto& th : threads) th.join();

    bool all_removed = true;
    for (int i = 0; i < total_keys; i++) {
        if (map.get(i).has_value()) {
            all_removed = false;
            break;
        }
    }
    check(all_removed, "all 800 keys removed successfully");
}

// =====================================================================
//  PART 3: SAME-BUCKET CONTENTION TESTS
// =====================================================================

void test_same_bucket_puts() {
    std::cout << "\n--- Test: Same-Bucket Puts (contention) ---\n";
    // Use large load factor to disable resizing for this test.
    ConcurrentHashMap<int, int> map(16, 999999.0);

    const int num_threads = 8;
    const int keys_per_thread = 500;
    std::vector<std::thread> threads;

    for (int t = 0; t < num_threads; t++) {
        threads.emplace_back([&map, t]() {
            int base = t * 500;
            for (int i = 0; i < 500; i++) {
                int key = (base + i) * 16;  // all keys % 16 == 0 → bucket 0
                map.put(key, key);
            }
        });
    }

    for (auto& th : threads) th.join();

    int total = num_threads * keys_per_thread;
    bool all_ok = true;
    for (int i = 0; i < total; i++) {
        int key = i * 16;
        auto val = map.get(key);
        if (!val.has_value() || val.value() != key) {
            all_ok = false;
            break;
        }
    }
    check(all_ok, "4000 keys in same bucket inserted correctly");
}

void test_same_bucket_removes() {
    std::cout << "\n--- Test: Same-Bucket Removes (contention) ---\n";
    ConcurrentHashMap<int, int> map(16, 999999.0);

    const int total_keys = 800;
    for (int i = 0; i < total_keys; i++) {
        map.put(i * 16, i);
    }

    const int num_threads = 8;
    std::vector<std::thread> threads;

    for (int t = 0; t < num_threads; t++) {
        threads.emplace_back([&map, t]() {
            int start = t * 100;
            for (int i = start; i < start + 100; i++) {
                map.remove(i * 16);
            }
        });
    }

    for (auto& th : threads) th.join();

    bool all_removed = true;
    for (int i = 0; i < total_keys; i++) {
        if (map.get(i * 16).has_value()) {
            all_removed = false;
            break;
        }
    }
    check(all_removed, "800 keys removed from same bucket correctly");
}

void test_same_key_overwrite() {
    std::cout << "\n--- Test: Same-Key Overwrite (highest contention) ---\n";
    ConcurrentHashMap<int, int> map(16);

    map.put(0, -1);

    const int num_threads = 8;
    const int writes_per_thread = 10000;
    std::vector<std::thread> threads;

    for (int t = 0; t < num_threads; t++) {
        threads.emplace_back([&map, t]() {
            for (int i = 0; i < writes_per_thread; i++) {
                map.put(0, t * writes_per_thread + i);
            }
        });
    }

    for (auto& th : threads) th.join();

    auto val = map.get(0);
    bool exists = val.has_value();
    bool in_range = exists && val.value() >= 0
                    && val.value() < num_threads * writes_per_thread;

    check(exists,   "key still exists after 80000 overwrites");
    check(in_range, "final value is valid (not corrupted)");
}

// =====================================================================
//  PART 4: MIXED OPERATIONS ON SAME BUCKET
// =====================================================================

void test_mixed_same_bucket() {
    std::cout << "\n--- Test: Mixed Ops on Same Bucket ---\n";
    // Disable resizing so all keys stay in bucket 0.
    ConcurrentHashMap<int, int> map(16, 999999.0);

    for (int i = 0; i < 200; i++) {
        map.put(i * 16, i);
    }

    std::vector<std::thread> threads;

    threads.emplace_back([&map]() {
        for (int i = 200; i < 400; i++) map.put(i * 16, i);
    });
    threads.emplace_back([&map]() {
        for (int i = 0; i < 200; i++) map.get(i * 16);
    });
    threads.emplace_back([&map]() {
        for (int i = 0; i < 100; i++) map.remove(i * 16);
    });
    threads.emplace_back([&map]() {
        for (int i = 100; i < 200; i++) map.put(i * 16, i * 100);
    });

    for (auto& th : threads) th.join();

    bool removed_ok = true;
    for (int i = 0; i < 100; i++) {
        if (map.get(i * 16).has_value()) { removed_ok = false; break; }
    }
    check(removed_ok, "keys 0-99 removed while other ops ran");

    bool updated_ok = true;
    for (int i = 100; i < 200; i++) {
        auto val = map.get(i * 16);
        if (!val.has_value() || val.value() != i * 100) { updated_ok = false; break; }
    }
    check(updated_ok, "keys 100-199 updated while other ops ran");

    bool inserted_ok = true;
    for (int i = 200; i < 400; i++) {
        auto val = map.get(i * 16);
        if (!val.has_value() || val.value() != i) { inserted_ok = false; break; }
    }
    check(inserted_ok, "keys 200-399 inserted while other ops ran");
}

// =====================================================================
//  PART 5: STRESS TEST
// =====================================================================

void test_stress() {
    std::cout << "\n--- Test: Stress (high volume) ---\n";
    ConcurrentHashMap<int, int> map(16);

    const int num_threads = 8;
    const int ops_per_thread = 50000;
    std::atomic<bool> no_crash(true);
    std::vector<std::thread> threads;

    for (int t = 0; t < num_threads; t++) {
        threads.emplace_back([&map, &no_crash, t]() {
            for (int i = 0; i < ops_per_thread; i++) {
                int key = i % 1000;
                int op = (t + i) % 3;

                if (op == 0)      map.put(key, i);
                else if (op == 1) map.get(key);
                else              map.remove(key);
            }
        });
    }

    for (auto& th : threads) th.join();

    check(no_crash, "400000 ops across 8 threads — no crash");

    bool valid = true;
    for (int i = 0; i < 1000; i++) {
        auto val = map.get(i);
        if (val.has_value() && val.value() < 0) { valid = false; break; }
    }
    check(valid, "no corrupted values after stress test");
}

// =====================================================================
//  PART 6: DYNAMIC RESIZING TESTS
// =====================================================================

void test_resize_triggers() {
    std::cout << "\n--- Test: Resize Triggers Automatically ---\n";

    // Start with 4 buckets, load factor 0.75.
    // Resize should trigger after 3 inserts (4 * 0.75 = 3).
    ConcurrentHashMap<int, int> map(4, 0.75);

    check(map.get_bucket_count() == 4, "starts with 4 buckets");

    // Insert 3 keys — should NOT trigger resize yet.
    map.put(1, 10);
    map.put(2, 20);
    map.put(3, 30);
    check(map.get_bucket_count() == 4, "still 4 buckets after 3 inserts");

    // Insert 4th key — exceeds load factor, triggers resize to 8.
    map.put(4, 40);
    check(map.get_bucket_count() == 8, "resized to 8 buckets after 4 inserts");

    // All original data should still be there.
    check(map.get(1) == 10, "key 1 preserved after resize");
    check(map.get(2) == 20, "key 2 preserved after resize");
    check(map.get(3) == 30, "key 3 preserved after resize");
    check(map.get(4) == 40, "key 4 preserved after resize");
}

void test_multiple_resizes() {
    std::cout << "\n--- Test: Multiple Resizes ---\n";

    // Start with 2 buckets. Should resize multiple times.
    ConcurrentHashMap<int, int> map(2, 0.75);

    check(map.get_bucket_count() == 2, "starts with 2 buckets");

    // Insert 100 keys — should trigger several resizes.
    for (int i = 0; i < 100; i++) {
        map.put(i, i * 10);
    }

    // Bucket count should have grown (2 → 4 → 8 → 16 → 32 → 64 → 128).
    size_t final_buckets = map.get_bucket_count();
    check(final_buckets >= 128, "bucket count grew to " + std::to_string(final_buckets));

    // Verify all data survived multiple resizes.
    bool all_ok = true;
    for (int i = 0; i < 100; i++) {
        auto val = map.get(i);
        if (!val.has_value() || val.value() != i * 10) {
            all_ok = false;
            break;
        }
    }
    check(all_ok, "all 100 keys correct after multiple resizes");
    check(map.size() == 100, "size() reports 100");
}

void test_concurrent_resize() {
    std::cout << "\n--- Test: Concurrent Inserts Triggering Resize ---\n";

    // Start with 4 buckets. 8 threads insert 1000 keys each.
    // This forces many resizes while threads are running.
    ConcurrentHashMap<int, int> map(4, 0.75);

    const int num_threads = 8;
    const int keys_per_thread = 1000;
    std::vector<std::thread> threads;

    for (int t = 0; t < num_threads; t++) {
        threads.emplace_back([&map, t]() {
            for (int i = 0; i < 1000; i++) {
                int key = t * 1000 + i;
                map.put(key, key);
            }
        });
    }

    for (auto& th : threads) th.join();

    // All 8000 keys should exist with correct values.
    int total = num_threads * keys_per_thread;
    bool all_ok = true;
    for (int i = 0; i < total; i++) {
        auto val = map.get(i);
        if (!val.has_value() || val.value() != i) {
            all_ok = false;
            break;
        }
    }
    check(all_ok, "all 8000 keys correct after concurrent resizes");
    check(map.size() == 8000, "size() reports 8000");

    size_t final_buckets = map.get_bucket_count();
    check(final_buckets > 4, "bucket count grew from 4 to " + std::to_string(final_buckets));
}

void test_remove_after_resize() {
    std::cout << "\n--- Test: Remove After Resize ---\n";

    ConcurrentHashMap<int, int> map(4, 0.75);

    // Insert enough to trigger resizes.
    for (int i = 0; i < 50; i++) {
        map.put(i, i);
    }

    // Remove all keys.
    for (int i = 0; i < 50; i++) {
        map.remove(i);
    }

    check(map.size() == 0, "size is 0 after removing all keys");

    // Verify nothing is left.
    bool all_gone = true;
    for (int i = 0; i < 50; i++) {
        if (map.get(i).has_value()) { all_gone = false; break; }
    }
    check(all_gone, "all keys gone after remove");
}

// ─── Main ───────────────────────────────────────────────────────────

int main() {
    std::cout << "========================================\n";
    std::cout << " Concurrent Hash Map — Test Suite\n";
    std::cout << "========================================\n";

    std::cout << "\n[PART 1] Single-Thread Tests\n";
    test_insert_and_get();
    test_get_missing_key();
    test_update_existing_key();
    test_remove();

    std::cout << "\n[PART 2] Multi-Thread Tests (different buckets)\n";
    test_parallel_puts_different_buckets();
    test_parallel_gets();
    test_parallel_removes();

    std::cout << "\n[PART 3] Same-Bucket Contention Tests\n";
    test_same_bucket_puts();
    test_same_bucket_removes();
    test_same_key_overwrite();

    std::cout << "\n[PART 4] Mixed Operations (same bucket)\n";
    test_mixed_same_bucket();

    std::cout << "\n[PART 5] Stress Test\n";
    test_stress();

    std::cout << "\n[PART 6] Dynamic Resizing Tests\n";
    test_resize_triggers();
    test_multiple_resizes();
    test_concurrent_resize();
    test_remove_after_resize();

    std::cout << "\n========================================\n";
    std::cout << " Results: " << pass_count << " passed, "
              << fail_count << " failed\n";
    std::cout << "========================================\n\n";

    return fail_count > 0 ? 1 : 0;
}

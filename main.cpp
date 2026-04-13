#include <iostream>
#include <thread>
#include <vector>
#include <cassert>
#include <atomic>
#include "concurrent_hash_map.h"

// ─── Helpers ────────────────────────────────────────────────────────

int pass_count = 0;
int fail_count = 0;

// Simple pass/fail check with a message.
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
//  Purpose: Verify basic correctness without any concurrency.
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
//  Purpose: Verify threads working on separate buckets don't interfere.
// =====================================================================

void test_parallel_puts_different_buckets() {
    std::cout << "\n--- Test: Parallel Puts to Different Buckets ---\n";
    ConcurrentHashMap<int, int> map(16);

    const int num_threads = 8;
    const int keys_per_thread = 1000;
    std::vector<std::thread> threads;

    // Each thread writes a unique range of keys.
    for (int t = 0; t < num_threads; t++) {
        threads.emplace_back([&map, t]() {
            for (int i = 0; i < 1000; i++) {
                int key = t * 1000 + i;
                map.put(key, key * 10);
            }
        });
    }

    for (auto& th : threads) th.join();

    // Verify every key is present and correct.
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

    // Pre-fill.
    for (int i = 0; i < 100; i++) map.put(i, i * 5);

    const int num_threads = 8;
    std::vector<std::thread> threads;
    std::atomic<bool> all_ok(true);

    // All threads read the same keys at the same time.
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

    // Each thread removes its own range of keys.
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
//  Purpose: Force multiple threads into the SAME bucket to truly test
//  the mutex. With 16 buckets, keys 0, 16, 32, 48, ... all go to
//  bucket 0 because key % 16 == 0.
// =====================================================================

void test_same_bucket_puts() {
    std::cout << "\n--- Test: Same-Bucket Puts (contention) ---\n";
    ConcurrentHashMap<int, int> map(16);

    const int num_threads = 8;
    const int keys_per_thread = 500;
    std::vector<std::thread> threads;

    // All threads insert keys that land in bucket 0.
    // Thread t inserts keys: t*500*16, (t*500+1)*16, (t*500+2)*16, ...
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

    // Verify all 4000 entries are present in bucket 0.
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
    ConcurrentHashMap<int, int> map(16);

    // Insert 800 keys all into bucket 0.
    const int total_keys = 800;
    for (int i = 0; i < total_keys; i++) {
        map.put(i * 16, i);  // key % 16 == 0
    }

    const int num_threads = 8;
    std::vector<std::thread> threads;

    // Each thread removes 100 keys from the same bucket.
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

    map.put(0, -1);  // initial value

    const int num_threads = 8;
    const int writes_per_thread = 10000;
    std::vector<std::thread> threads;

    // All threads overwrite the SAME key repeatedly.
    // This is max contention — every thread fights for the same bucket
    // and the same entry.
    for (int t = 0; t < num_threads; t++) {
        threads.emplace_back([&map, t]() {
            for (int i = 0; i < writes_per_thread; i++) {
                map.put(0, t * writes_per_thread + i);
            }
        });
    }

    for (auto& th : threads) th.join();

    // The key must still exist with SOME valid value.
    // We can't predict which thread wrote last, but the value
    // must be in the range [0, num_threads * writes_per_thread).
    auto val = map.get(0);
    bool exists = val.has_value();
    bool in_range = exists && val.value() >= 0
                    && val.value() < num_threads * writes_per_thread;

    check(exists,   "key still exists after 80000 overwrites");
    check(in_range, "final value is valid (not corrupted)");
}

// =====================================================================
//  PART 4: MIXED OPERATIONS ON SAME BUCKET
//  Purpose: Test put + get + remove happening at the same time on
//  the same bucket — the most realistic and dangerous scenario.
// =====================================================================

void test_mixed_same_bucket() {
    std::cout << "\n--- Test: Mixed Ops on Same Bucket ---\n";
    ConcurrentHashMap<int, int> map(16);

    // Pre-fill bucket 0 with some keys.
    for (int i = 0; i < 200; i++) {
        map.put(i * 16, i);
    }

    std::vector<std::thread> threads;

    // Thread 1: Insert new keys into bucket 0.
    threads.emplace_back([&map]() {
        for (int i = 200; i < 400; i++) {
            map.put(i * 16, i);
        }
    });

    // Thread 2: Read keys from bucket 0.
    threads.emplace_back([&map]() {
        for (int i = 0; i < 200; i++) {
            map.get(i * 16);  // just exercise the read path
        }
    });

    // Thread 3: Remove first 100 keys from bucket 0.
    threads.emplace_back([&map]() {
        for (int i = 0; i < 100; i++) {
            map.remove(i * 16);
        }
    });

    // Thread 4: Overwrite keys 100-199 in bucket 0.
    threads.emplace_back([&map]() {
        for (int i = 100; i < 200; i++) {
            map.put(i * 16, i * 100);
        }
    });

    for (auto& th : threads) th.join();

    // After everything settles:
    // Keys 0-99   → should be removed.
    // Keys 100-199 → should be updated to i*100.
    // Keys 200-399 → should be newly inserted.

    bool removed_ok = true;
    for (int i = 0; i < 100; i++) {
        if (map.get(i * 16).has_value()) {
            removed_ok = false;
            break;
        }
    }
    check(removed_ok, "keys 0-99 removed while other ops ran");

    bool updated_ok = true;
    for (int i = 100; i < 200; i++) {
        auto val = map.get(i * 16);
        if (!val.has_value() || val.value() != i * 100) {
            updated_ok = false;
            break;
        }
    }
    check(updated_ok, "keys 100-199 updated while other ops ran");

    bool inserted_ok = true;
    for (int i = 200; i < 400; i++) {
        auto val = map.get(i * 16);
        if (!val.has_value() || val.value() != i) {
            inserted_ok = false;
            break;
        }
    }
    check(inserted_ok, "keys 200-399 inserted while other ops ran");
}

// =====================================================================
//  PART 5: STRESS TEST
//  Purpose: High volume of operations across many threads to catch
//  any rare race conditions.
// =====================================================================

void test_stress() {
    std::cout << "\n--- Test: Stress (high volume) ---\n";
    ConcurrentHashMap<int, int> map(16);

    const int num_threads = 8;
    const int ops_per_thread = 50000;
    std::atomic<bool> no_crash(true);
    std::vector<std::thread> threads;

    // Each thread does a mix of put, get, remove on overlapping keys.
    // Keys 0-999 are shared across all threads → forces contention.
    for (int t = 0; t < num_threads; t++) {
        threads.emplace_back([&map, &no_crash, t]() {
            for (int i = 0; i < ops_per_thread; i++) {
                int key = i % 1000;  // only 1000 keys → heavy overlap
                int op = (t + i) % 3;

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

    for (auto& th : threads) th.join();

    // If we got here without a crash or hang, the mutex held up.
    check(no_crash, "400000 ops across 8 threads — no crash");

    // Spot check: all remaining keys should return valid values.
    bool valid = true;
    for (int i = 0; i < 1000; i++) {
        auto val = map.get(i);
        if (val.has_value() && val.value() < 0) {
            valid = false;  // corrupted value
            break;
        }
    }
    check(valid, "no corrupted values after stress test");
}

// ─── Main ───────────────────────────────────────────────────────────

int main() {
    std::cout << "========================================\n";
    std::cout << " Concurrent Hash Map — Test Suite\n";
    std::cout << "========================================\n";

    // Part 1: Single-thread basics.
    std::cout << "\n[PART 1] Single-Thread Tests\n";
    test_insert_and_get();
    test_get_missing_key();
    test_update_existing_key();
    test_remove();

    // Part 2: Multi-thread, different buckets.
    std::cout << "\n[PART 2] Multi-Thread Tests (different buckets)\n";
    test_parallel_puts_different_buckets();
    test_parallel_gets();
    test_parallel_removes();

    // Part 3: Same-bucket contention.
    std::cout << "\n[PART 3] Same-Bucket Contention Tests\n";
    test_same_bucket_puts();
    test_same_bucket_removes();
    test_same_key_overwrite();

    // Part 4: Mixed operations on same bucket.
    std::cout << "\n[PART 4] Mixed Operations (same bucket)\n";
    test_mixed_same_bucket();

    // Part 5: Stress test.
    std::cout << "\n[PART 5] Stress Test\n";
    test_stress();

    // Summary.
    std::cout << "\n========================================\n";
    std::cout << " Results: " << pass_count << " passed, "
              << fail_count << " failed\n";
    std::cout << "========================================\n\n";

    return fail_count > 0 ? 1 : 0;
}

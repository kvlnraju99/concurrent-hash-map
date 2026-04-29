#ifndef CONCURRENT_HASH_MAP_V3_PTR_H
#define CONCURRENT_HASH_MAP_V3_PTR_H

#include <vector>
#include <mutex>
#include <optional>
#include <functional>
#include <atomic>
#include <chrono>
#include <iostream>
#include <omp.h>

/**
 * @brief Fast Spinlock
 */
struct SpinlockPtr {
    std::atomic_flag flag = ATOMIC_FLAG_INIT;
    void lock() { while (flag.test_and_set(std::memory_order_acquire)); }
    void unlock() { flag.clear(std::memory_order_release); }
};

/**
 * @brief Diagnostic Audit
 */
struct ThreadAuditPtr {
    long long global_lock_wait_ns{0};
    long long bucket_lock_wait_ns{0};
    long long resize_time_ns{0};
    int resize_count{0};
    int retry_count{0};
};

static ThreadAuditPtr* g_ptr_audits = nullptr;
static int g_ptr_max_threads = 0;

inline void init_ptr_audit(int max_threads) {
    g_ptr_max_threads = max_threads;
    g_ptr_audits = new ThreadAuditPtr[max_threads]();
}

inline void report_ptr_audit() {
    long long total_global = 0, total_bucket = 0, total_resize_time = 0;
    int total_resize_count = 0, total_retries = 0;
    for (int i = 0; i < g_ptr_max_threads; ++i) {
        total_global += g_ptr_audits[i].global_lock_wait_ns;
        total_bucket += g_ptr_audits[i].bucket_lock_wait_ns;
        total_resize_time += g_ptr_audits[i].resize_time_ns;
        total_resize_count += g_ptr_audits[i].resize_count;
        total_retries += g_ptr_audits[i].retry_count;
    }
    std::cout << "\n--- PERFORMANCE AUDIT REPORT (V3 POINTER SWAP + LOCAL TRIGGER) ---" << std::endl;
    std::cout << "1. Global Structure Wait:  " << (total_global / 1000000.0) << " ms" << std::endl;
    std::cout << "2. Bucket Lock Contention: " << (total_bucket / 1000000.0) << " ms" << std::endl;
    std::cout << "3. Resize Operations:      " << total_resize_count << " (Total: " << (total_resize_time / 1000000.0) << " ms)" << std::endl;
    std::cout << "4. Stale-Array Retries:    " << total_retries << std::endl;
    std::cout << "-------------------------------------------------------------------" << std::endl;
}

template <typename K, typename V>
class ConcurrentHashMapV3Ptr {
private:
    struct Node {
        K key; V value; Node* next;
        Node(K k, V v, Node* n) : key(k), value(v), next(n) {}
    };

    struct alignas(64) Bucket {
        Node* head = nullptr;
        SpinlockPtr mtx;
    };

    struct BucketArray {
        std::vector<Bucket*> buckets;
        size_t count;
        BucketArray(size_t n) : count(n) {
            buckets.resize(n);
            for (size_t i = 0; i < n; ++i) buckets[i] = new Bucket();
        }
        // NOTE: Destructor intentionally does NOT free nodes.
        // Nodes are migrated to the new array during resize,
        // so only the Bucket shells are freed here.
        ~BucketArray() {
            for (auto b : buckets) {
                delete b;
            }
        }
    };

    std::atomic<BucketArray*> current_array;
    std::mutex resize_mutex; 
    const int THRESHOLD = 5; // Local chain length threshold for resizing

    /**
     * @brief Epoch-based active reader counter.
     * Resize waits until all threads that loaded the old pointer have finished
     * their critical section. Each thread increments before locking a bucket
     * and decrements after unlocking.
     */
    std::atomic<int> active_readers{0};

    size_t get_bucket_index(const K& key, size_t count) const {
        return std::hash<K>{}(key) % count;
    }

    /**
     * @brief Resize with full safety: lock all old buckets, migrate nodes, swap pointer.
     * 
     * This version:
     * 1. Acquires the resize mutex (only one resizer at a time)
     * 2. Locks ALL buckets in the old array to quiesce concurrent writers
     * 3. Copies all nodes to the new array
     * 4. Atomically swaps the pointer
     * 5. Unlocks all old buckets
     * 6. Waits for active_readers to drain, then frees the old array
     */
    void check_and_resize(size_t triggering_count) {
        // Only one thread can resize at a time
        if (!resize_mutex.try_lock()) return; 
        
        BucketArray* old_array = current_array.load(std::memory_order_acquire);
        // Only resize if nobody has resized since we triggered
        if (old_array->count > triggering_count) {
            resize_mutex.unlock(); return;
        }

        int tid = omp_get_thread_num();
        auto start = std::chrono::high_resolution_clock::now();

        // Step 1: Lock ALL buckets in the old array to prevent concurrent modifications
        for (size_t i = 0; i < old_array->count; ++i) {
            old_array->buckets[i]->mtx.lock();
        }

        // Step 2: Build the new array by migrating nodes (move, don't copy)
        size_t new_count = old_array->count * 2;
        BucketArray* new_array = new BucketArray(new_count);

        for (size_t i = 0; i < old_array->count; ++i) {
            Node* curr = old_array->buckets[i]->head;
            while (curr) {
                Node* next = curr->next; // save next before relinking
                size_t idx = get_bucket_index(curr->key, new_count);
                curr->next = new_array->buckets[idx]->head;
                new_array->buckets[idx]->head = curr;
                curr = next;
            }
            old_array->buckets[i]->head = nullptr; // detach from old array
        }

        // Step 3: Atomically swap the pointer
        current_array.store(new_array, std::memory_order_release);

        // Step 4: Unlock all old buckets — threads waiting on them will
        // see the stale array and retry with the new one
        for (size_t i = 0; i < old_array->count; ++i) {
            old_array->buckets[i]->mtx.unlock();
        }

        // Step 5: Wait for any threads that loaded the old pointer before
        // the swap to finish their operations
        while (active_readers.load(std::memory_order_acquire) > 0) {
            // Spin-wait — resize is rare, so this is acceptable
        }

        // Step 6: Safe to delete the old array shell (nodes were migrated)
        delete old_array;

        auto end = std::chrono::high_resolution_clock::now();
        if (g_ptr_audits) {
            g_ptr_audits[tid].resize_time_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
            g_ptr_audits[tid].resize_count++;
        }
        resize_mutex.unlock();
    }

public:
    explicit ConcurrentHashMapV3Ptr(size_t n = 1024) {
        current_array.store(new BucketArray(n));
    }

    ~ConcurrentHashMapV3Ptr() {
        // Final cleanup: free all remaining nodes
        BucketArray* arr = current_array.load();
        for (auto b : arr->buckets) {
            Node* curr = b->head;
            while (curr) { Node* tmp = curr; curr = curr->next; delete tmp; }
            b->head = nullptr;
        }
        delete arr;
    }

    void put(const K& key, const V& value) {
        int tid = omp_get_thread_num();

        while (true) {
            // Register as active reader before loading pointer
            active_readers.fetch_add(1, std::memory_order_acq_rel);
            BucketArray* active = current_array.load(std::memory_order_acquire);
            size_t idx = get_bucket_index(key, active->count);
            
            auto b1 = std::chrono::high_resolution_clock::now();
            active->buckets[idx]->mtx.lock();
            auto b2 = std::chrono::high_resolution_clock::now();
            if (g_ptr_audits && tid < g_ptr_max_threads)
                g_ptr_audits[tid].bucket_lock_wait_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(b2 - b1).count();

            // Validate: did the array change while we waited for the lock?
            if (current_array.load(std::memory_order_acquire) != active) {
                active->buckets[idx]->mtx.unlock();
                active_readers.fetch_sub(1, std::memory_order_acq_rel);
                if (g_ptr_audits && tid < g_ptr_max_threads)
                    g_ptr_audits[tid].retry_count++;
                continue; // Retry with the new array
            }

            // Safe to proceed — we hold the bucket lock and the array is current
            Node* curr = active->buckets[idx]->head;
            int depth = 0;
            while (curr) {
                depth++;
                if (curr->key == key) { curr->value = value; active->buckets[idx]->mtx.unlock(); active_readers.fetch_sub(1, std::memory_order_acq_rel); return; }
                curr = curr->next;
            }
            active->buckets[idx]->head = new Node(key, value, active->buckets[idx]->head);
            size_t trigger_count = active->count;
            active->buckets[idx]->mtx.unlock();
            active_readers.fetch_sub(1, std::memory_order_acq_rel);
            
            if (depth > THRESHOLD) {
                check_and_resize(trigger_count);
            }
            return;
        }
    }

    std::optional<V> get(const K& key) const {
        int tid = omp_get_thread_num();

        while (true) {
            const_cast<std::atomic<int>&>(active_readers).fetch_add(1, std::memory_order_acq_rel);
            BucketArray* active = current_array.load(std::memory_order_acquire);
            size_t idx = get_bucket_index(key, active->count);
            
            auto b1 = std::chrono::high_resolution_clock::now();
            active->buckets[idx]->mtx.lock();
            auto b2 = std::chrono::high_resolution_clock::now();
            if (g_ptr_audits && tid < g_ptr_max_threads)
                g_ptr_audits[tid].bucket_lock_wait_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(b2 - b1).count();

            // Validate array didn't change
            if (current_array.load(std::memory_order_acquire) != active) {
                active->buckets[idx]->mtx.unlock();
                const_cast<std::atomic<int>&>(active_readers).fetch_sub(1, std::memory_order_acq_rel);
                if (g_ptr_audits && tid < g_ptr_max_threads)
                    g_ptr_audits[tid].retry_count++;
                continue;
            }

            Node* curr = active->buckets[idx]->head;
            while (curr) {
                if (curr->key == key) { V val = curr->value; active->buckets[idx]->mtx.unlock(); const_cast<std::atomic<int>&>(active_readers).fetch_sub(1, std::memory_order_acq_rel); return val; }
                curr = curr->next;
            }
            active->buckets[idx]->mtx.unlock();
            const_cast<std::atomic<int>&>(active_readers).fetch_sub(1, std::memory_order_acq_rel);
            return std::nullopt;
        }
    }

    void update(const K& key, std::function<V(std::optional<V>)> updater) {
        int tid = omp_get_thread_num();

        while (true) {
            active_readers.fetch_add(1, std::memory_order_acq_rel);
            BucketArray* active = current_array.load(std::memory_order_acquire);
            size_t idx = get_bucket_index(key, active->count);
            
            auto b1 = std::chrono::high_resolution_clock::now();
            active->buckets[idx]->mtx.lock();
            auto b2 = std::chrono::high_resolution_clock::now();
            if (g_ptr_audits && tid < g_ptr_max_threads)
                g_ptr_audits[tid].bucket_lock_wait_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(b2 - b1).count();

            // Validate array didn't change
            if (current_array.load(std::memory_order_acquire) != active) {
                active->buckets[idx]->mtx.unlock();
                active_readers.fetch_sub(1, std::memory_order_acq_rel);
                if (g_ptr_audits && tid < g_ptr_max_threads)
                    g_ptr_audits[tid].retry_count++;
                continue;
            }

            Node* curr = active->buckets[idx]->head;
            int depth = 0;
            while (curr) {
                depth++;
                if (curr->key == key) { curr->value = updater(curr->value); active->buckets[idx]->mtx.unlock(); active_readers.fetch_sub(1, std::memory_order_acq_rel); return; }
                curr = curr->next;
            }
            active->buckets[idx]->head = new Node(key, updater(std::nullopt), active->buckets[idx]->head);
            size_t trigger_count = active->count;
            active->buckets[idx]->mtx.unlock();
            active_readers.fetch_sub(1, std::memory_order_acq_rel);

            if (depth > THRESHOLD) {
                check_and_resize(trigger_count);
            }
            return;
        }
    }

    size_t size() const { return 0; } // We no longer track size globally for performance
    V sum_all_values() const {
        BucketArray* active = current_array.load();
        V total = 0;
        for (auto b : active->buckets) {
            b->mtx.lock();
            Node* curr = b->head;
            while (curr) { total += curr->value; curr = curr->next; }
            b->mtx.unlock();
        }
        return total;
    }
};

#endif

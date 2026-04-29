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
    long long global_lock_wait_ns{0}; // This should drop to ~0 now!
    long long bucket_lock_wait_ns{0};
    long long resize_time_ns{0};
    int resize_count{0};
};

static ThreadAuditPtr* g_ptr_audits = nullptr;
static int g_ptr_max_threads = 0;

inline void init_ptr_audit(int max_threads) {
    g_ptr_max_threads = max_threads;
    g_ptr_audits = new ThreadAuditPtr[max_threads]();
}

inline void report_ptr_audit() {
    long long total_global = 0, total_bucket = 0, total_resize_time = 0;
    int total_resize_count = 0;
    for (int i = 0; i < g_ptr_max_threads; ++i) {
        total_global += g_ptr_audits[i].global_lock_wait_ns;
        total_bucket += g_ptr_audits[i].bucket_lock_wait_ns;
        total_resize_time += g_ptr_audits[i].resize_time_ns;
        total_resize_count += g_ptr_audits[i].resize_count;
    }
    std::cout << "\n--- PERFORMANCE AUDIT REPORT (V3 POINTER SWAP) ---" << std::endl;
    std::cout << "1. Global Structure Wait:  " << (total_global / 1000000.0) << " ms (WAIT-FREE READS)" << std::endl;
    std::cout << "2. Bucket Lock Contention: " << (total_bucket / 1000000.0) << " ms" << std::endl;
    std::cout << "3. Resize Operations:      " << total_resize_count << " (Total: " << (total_resize_time / 1000000.0) << " ms)" << std::endl;
    std::cout << "---------------------------------------------------" << std::endl;
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
        ~BucketArray() {
            for (auto b : buckets) {
                Node* curr = b->head;
                while (curr) { Node* tmp = curr; curr = curr->next; delete tmp; }
                delete b;
            }
        }
    };

    std::atomic<BucketArray*> current_array;
    std::atomic<size_t> element_count;
    std::mutex resize_mutex; // Only used by resizers

    size_t get_bucket_index(const K& key, size_t count) const {
        return std::hash<K>{}(key) % count;
    }

    void check_and_resize(BucketArray* local_array) {
        if (element_count.load(std::memory_order_relaxed) > local_array->count) {
            if (!resize_mutex.try_lock()) return; // Someone else is already resizing
            
            // Double check
            BucketArray* active = current_array.load();
            if (element_count.load() <= active->count) {
                resize_mutex.unlock(); return;
            }

            int tid = omp_get_thread_num();
            auto start = std::chrono::high_resolution_clock::now();

            size_t new_count = active->count * 2;
            BucketArray* new_array = new BucketArray(new_count);

            for (size_t i = 0; i < active->count; ++i) {
                Node* curr = active->buckets[i]->head;
                while (curr) {
                    size_t idx = get_bucket_index(curr->key, new_count);
                    new_array->buckets[idx]->head = new Node(curr->key, curr->value, new_array->buckets[idx]->head);
                    curr = curr->next;
                }
            }

            current_array.store(new_array, std::memory_order_release);
            // In a real system, we'd use RCU or Hazard Pointers to delete the old array safely.
            // For this benchmark, we'll just leak it or track it.
            
            auto end = std::chrono::high_resolution_clock::now();
            if (g_ptr_audits) {
                g_ptr_audits[tid].resize_time_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
                g_ptr_audits[tid].resize_count++;
            }
            resize_mutex.unlock();
        }
    }

public:
    explicit ConcurrentHashMapV3Ptr(size_t n = 1024) : element_count(0) {
        current_array.store(new BucketArray(n));
    }

    void put(const K& key, const V& value) {
        BucketArray* active = current_array.load(std::memory_order_acquire);
        size_t idx = get_bucket_index(key, active->count);
        
        int tid = omp_get_thread_num();
        auto b1 = std::chrono::high_resolution_clock::now();
        active->buckets[idx]->mtx.lock();
        auto b2 = std::chrono::high_resolution_clock::now();
        if (g_ptr_audits && tid < g_ptr_max_threads)
            g_ptr_audits[tid].bucket_lock_wait_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(b2 - b1).count();

        Node* curr = active->buckets[idx]->head;
        while (curr) {
            if (curr->key == key) { curr->value = value; active->buckets[idx]->mtx.unlock(); return; }
            curr = curr->next;
        }
        active->buckets[idx]->head = new Node(key, value, active->buckets[idx]->head);
        element_count.fetch_add(1, std::memory_order_relaxed);
        active->buckets[idx]->mtx.unlock();
        
        check_and_resize(active);
    }

    std::optional<V> get(const K& key) const {
        BucketArray* active = current_array.load(std::memory_order_acquire);
        size_t idx = get_bucket_index(key, active->count);
        
        int tid = omp_get_thread_num();
        auto b1 = std::chrono::high_resolution_clock::now();
        active->buckets[idx]->mtx.lock();
        auto b2 = std::chrono::high_resolution_clock::now();
        if (g_ptr_audits && tid < g_ptr_max_threads)
            g_ptr_audits[tid].bucket_lock_wait_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(b2 - b1).count();

        Node* curr = active->buckets[idx]->head;
        while (curr) {
            if (curr->key == key) { V val = curr->value; active->buckets[idx]->mtx.unlock(); return val; }
            curr = curr->next;
        }
        active->buckets[idx]->mtx.unlock();
        return std::nullopt;
    }

    void update(const K& key, std::function<V(std::optional<V>)> updater) {
        BucketArray* active = current_array.load(std::memory_order_acquire);
        size_t idx = get_bucket_index(key, active->count);
        
        int tid = omp_get_thread_num();
        auto b1 = std::chrono::high_resolution_clock::now();
        active->buckets[idx]->mtx.lock();
        auto b2 = std::chrono::high_resolution_clock::now();
        if (g_ptr_audits && tid < g_ptr_max_threads)
            g_ptr_audits[tid].bucket_lock_wait_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(b2 - b1).count();

        Node* curr = active->buckets[idx]->head;
        while (curr) {
            if (curr->key == key) { curr->value = updater(curr->value); active->buckets[idx]->mtx.unlock(); return; }
            curr = curr->next;
        }
        active->buckets[idx]->head = new Node(key, updater(std::nullopt), active->buckets[idx]->head);
        element_count.fetch_add(1, std::memory_order_relaxed);
        active->buckets[idx]->mtx.unlock();
        check_and_resize(active);
    }

    size_t size() const { return element_count.load(); }
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

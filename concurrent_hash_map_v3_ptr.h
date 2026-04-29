#ifndef CONCURRENT_HASH_MAP_V3_PTR_H
#define CONCURRENT_HASH_MAP_V3_PTR_H

#include <vector>
#include <mutex>
#include <optional>
#include <functional>
#include <atomic>
#include <omp.h>

/**
 * @brief Fast Spinlock
 */
struct SpinlockPtr {
    std::atomic_flag flag = ATOMIC_FLAG_INIT;
    void lock() { while (flag.test_and_set(std::memory_order_acquire)); }
    void unlock() { flag.clear(std::memory_order_release); }
};

#ifdef AUDIT_ENABLED
#include <chrono>
#include <iostream>

/**
 * @brief Diagnostic Audit — only compiled when AUDIT_ENABLED is defined.
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
#else
inline void init_ptr_audit(int) {}
inline void report_ptr_audit() {}
#endif

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
        // Destructor frees bucket shells only — nodes are migrated during resize
        ~BucketArray() {
            for (auto b : buckets) {
                delete b;
            }
        }
    };

    std::atomic<BucketArray*> current_array;
    std::mutex resize_mutex; 
    const int THRESHOLD = 5;
    std::atomic<int> active_readers{0};

    size_t get_bucket_index(const K& key, size_t count) const {
        return std::hash<K>{}(key) % count;
    }

    void check_and_resize(size_t triggering_count) {
        if (!resize_mutex.try_lock()) return; 
        
        BucketArray* old_array = current_array.load(std::memory_order_acquire);
        if (old_array->count > triggering_count) {
            resize_mutex.unlock(); return;
        }

#ifdef AUDIT_ENABLED
        int tid = omp_get_thread_num();
        auto start = std::chrono::high_resolution_clock::now();
#endif

        for (size_t i = 0; i < old_array->count; ++i)
            old_array->buckets[i]->mtx.lock();

        size_t new_count = old_array->count * 2;
        BucketArray* new_array = new BucketArray(new_count);

        for (size_t i = 0; i < old_array->count; ++i) {
            Node* curr = old_array->buckets[i]->head;
            while (curr) {
                Node* next = curr->next;
                size_t idx = get_bucket_index(curr->key, new_count);
                curr->next = new_array->buckets[idx]->head;
                new_array->buckets[idx]->head = curr;
                curr = next;
            }
            old_array->buckets[i]->head = nullptr;
        }

        current_array.store(new_array, std::memory_order_release);

        for (size_t i = 0; i < old_array->count; ++i)
            old_array->buckets[i]->mtx.unlock();

        while (active_readers.load(std::memory_order_acquire) > 0) {}

        delete old_array;

#ifdef AUDIT_ENABLED
        auto end = std::chrono::high_resolution_clock::now();
        if (g_ptr_audits) {
            g_ptr_audits[tid].resize_time_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
            g_ptr_audits[tid].resize_count++;
        }
#endif
        resize_mutex.unlock();
    }

public:
    explicit ConcurrentHashMapV3Ptr(size_t n = 1024) {
        current_array.store(new BucketArray(n));
    }

    ~ConcurrentHashMapV3Ptr() {
        BucketArray* arr = current_array.load();
        for (auto b : arr->buckets) {
            Node* curr = b->head;
            while (curr) { Node* tmp = curr; curr = curr->next; delete tmp; }
            b->head = nullptr;
        }
        delete arr;
    }

    void put(const K& key, const V& value) {
        while (true) {
            active_readers.fetch_add(1, std::memory_order_acq_rel);
            BucketArray* active = current_array.load(std::memory_order_acquire);
            size_t idx = get_bucket_index(key, active->count);
            
#ifdef AUDIT_ENABLED
            int tid = omp_get_thread_num();
            auto b1 = std::chrono::high_resolution_clock::now();
#endif
            active->buckets[idx]->mtx.lock();
#ifdef AUDIT_ENABLED
            auto b2 = std::chrono::high_resolution_clock::now();
            if (g_ptr_audits && tid < g_ptr_max_threads)
                g_ptr_audits[tid].bucket_lock_wait_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(b2 - b1).count();
#endif

            if (current_array.load(std::memory_order_acquire) != active) {
                active->buckets[idx]->mtx.unlock();
                active_readers.fetch_sub(1, std::memory_order_acq_rel);
#ifdef AUDIT_ENABLED
                if (g_ptr_audits && tid < g_ptr_max_threads)
                    g_ptr_audits[tid].retry_count++;
#endif
                continue;
            }

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
        while (true) {
            const_cast<std::atomic<int>&>(active_readers).fetch_add(1, std::memory_order_acq_rel);
            BucketArray* active = current_array.load(std::memory_order_acquire);
            size_t idx = get_bucket_index(key, active->count);
            
#ifdef AUDIT_ENABLED
            int tid = omp_get_thread_num();
            auto b1 = std::chrono::high_resolution_clock::now();
#endif
            active->buckets[idx]->mtx.lock();
#ifdef AUDIT_ENABLED
            auto b2 = std::chrono::high_resolution_clock::now();
            if (g_ptr_audits && tid < g_ptr_max_threads)
                g_ptr_audits[tid].bucket_lock_wait_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(b2 - b1).count();
#endif

            if (current_array.load(std::memory_order_acquire) != active) {
                active->buckets[idx]->mtx.unlock();
                const_cast<std::atomic<int>&>(active_readers).fetch_sub(1, std::memory_order_acq_rel);
#ifdef AUDIT_ENABLED
                if (g_ptr_audits && tid < g_ptr_max_threads)
                    g_ptr_audits[tid].retry_count++;
#endif
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

    template<typename F>
    void update(const K& key, F updater) {
        while (true) {
            active_readers.fetch_add(1, std::memory_order_acq_rel);
            BucketArray* active = current_array.load(std::memory_order_acquire);
            size_t idx = get_bucket_index(key, active->count);
            
#ifdef AUDIT_ENABLED
            int tid = omp_get_thread_num();
            auto b1 = std::chrono::high_resolution_clock::now();
#endif
            active->buckets[idx]->mtx.lock();
#ifdef AUDIT_ENABLED
            auto b2 = std::chrono::high_resolution_clock::now();
            if (g_ptr_audits && tid < g_ptr_max_threads)
                g_ptr_audits[tid].bucket_lock_wait_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(b2 - b1).count();
#endif

            if (current_array.load(std::memory_order_acquire) != active) {
                active->buckets[idx]->mtx.unlock();
                active_readers.fetch_sub(1, std::memory_order_acq_rel);
#ifdef AUDIT_ENABLED
                if (g_ptr_audits && tid < g_ptr_max_threads)
                    g_ptr_audits[tid].retry_count++;
#endif
                continue;
            }

            Node* curr = active->buckets[idx]->head;
            int depth = 0;
            while (curr) {
                depth++;
                if (curr->key == key) { curr->value = updater(std::optional<V>(curr->value)); active->buckets[idx]->mtx.unlock(); active_readers.fetch_sub(1, std::memory_order_acq_rel); return; }
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

    size_t size() const { return 0; }
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

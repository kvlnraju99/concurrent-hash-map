#ifndef CONCURRENT_HASH_MAP_H
#define CONCURRENT_HASH_MAP_H

#include <vector>
#include <shared_mutex>
#include <mutex>
#include <optional>
#include <functional>
#include <atomic>
#include <chrono>
#include <iostream>
#include <omp.h>

/**
 * @brief Performance Audit Structure - Thread Local version to avoid contention
 */
struct ThreadAudit {
    long long global_lock_wait_ns{0};
    long long bucket_lock_wait_ns{0};
    long long resize_time_ns{0};
    int resize_count{0};
};

// Global array of audits, one per thread
static ThreadAudit* g_thread_audits = nullptr;
static int g_audit_max_threads = 0;

inline void init_audit(int max_threads) {
    g_audit_max_threads = max_threads;
    g_thread_audits = new ThreadAudit[max_threads]();
}

inline void report_audit() {
    long long total_global = 0;
    long long total_bucket = 0;
    long long total_resize_time = 0;
    int total_resize_count = 0;

    for (int i = 0; i < g_audit_max_threads; ++i) {
        total_global += g_thread_audits[i].global_lock_wait_ns;
        total_bucket += g_thread_audits[i].bucket_lock_wait_ns;
        total_resize_time += g_thread_audits[i].resize_time_ns;
        total_resize_count += g_thread_audits[i].resize_count;
    }

    std::cout << "\n--- PERFORMANCE AUDIT REPORT ---" << std::endl;
    std::cout << "1. Global Lock Contention: " << (total_global / 1000000.0) << " ms" << std::endl;
    std::cout << "2. Bucket Lock Contention: " << (total_bucket / 1000000.0) << " ms" << std::endl;
    std::cout << "3. Resize Operations:      " << total_resize_count << " (Total: " << (total_resize_time / 1000000.0) << " ms)" << std::endl;
    std::cout << "--------------------------------" << std::endl;
}

template <typename K, typename V>
class ConcurrentHashMap {
private:
    struct Node {
        K key;
        V value;
        Node* next;
        Node(K k, V v, Node* n) : key(k), value(v), next(n) {}
    };

    struct Bucket {
        Node* head = nullptr;
        std::mutex mtx;
    };

    std::vector<Bucket*> buckets;
    size_t bucket_count;
    std::atomic<size_t> element_count;
    mutable std::shared_mutex global_structure_mtx;

    size_t get_bucket_index(const K& key, size_t count) const {
        return std::hash<K>{}(key) % count;
    }

    void clear_buckets(std::vector<Bucket*>& b_list) {
        for (auto b : b_list) {
            Node* curr = b->head;
            while (curr) {
                Node* temp = curr;
                curr = curr->next;
                delete temp;
            }
            delete b;
        }
    }

    void check_and_resize() {
        if (element_count.load() > bucket_count) {
            int tid = omp_get_thread_num();
            auto start = std::chrono::high_resolution_clock::now();
            
            std::unique_lock<std::shared_mutex> lock(global_structure_mtx);
            if (element_count.load() <= bucket_count) return;

            size_t new_count = bucket_count * 2;
            std::vector<Bucket*> new_buckets(new_count);
            for (size_t i = 0; i < new_count; ++i) new_buckets[i] = new Bucket();

            for (size_t i = 0; i < bucket_count; ++i) {
                Node* curr = buckets[i]->head;
                while (curr) {
                    size_t new_idx = get_bucket_index(curr->key, new_count);
                    new_buckets[new_idx]->head = new Node(curr->key, curr->value, new_buckets[new_idx]->head);
                    curr = curr->next;
                }
            }

            clear_buckets(buckets);
            buckets = std::move(new_buckets);
            bucket_count = new_count;
            
            auto end = std::chrono::high_resolution_clock::now();
            if (g_thread_audits) {
                g_thread_audits[tid].resize_time_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
                g_thread_audits[tid].resize_count++;
            }
        }
    }

public:
    explicit ConcurrentHashMap(size_t num_buckets = 1024)
        : bucket_count(num_buckets), element_count(0) {
        buckets.resize(bucket_count);
        for (size_t i = 0; i < bucket_count; ++i) buckets[i] = new Bucket();
    }

    ~ConcurrentHashMap() {
        clear_buckets(buckets);
    }

    void put(const K& key, const V& value) {
        int tid = omp_get_thread_num();
        
        auto s1 = std::chrono::high_resolution_clock::now();
        std::shared_lock<std::shared_mutex> lock(global_structure_mtx);
        auto s2 = std::chrono::high_resolution_clock::now();
        if (g_thread_audits && tid < g_audit_max_threads) 
            g_thread_audits[tid].global_lock_wait_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(s2 - s1).count();

        size_t idx = get_bucket_index(key, bucket_count);
        
        auto b1 = std::chrono::high_resolution_clock::now();
        std::lock_guard<std::mutex> b_lock(buckets[idx]->mtx);
        auto b2 = std::chrono::high_resolution_clock::now();
        if (g_thread_audits && tid < g_audit_max_threads)
            g_thread_audits[tid].bucket_lock_wait_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(b2 - b1).count();

        Node* curr = buckets[idx]->head;
        while (curr) {
            if (curr->key == key) {
                curr->value = value;
                return;
            }
            curr = curr->next;
        }

        buckets[idx]->head = new Node(key, value, buckets[idx]->head);
        element_count.fetch_add(1);
        
        lock.unlock(); 
        check_and_resize();
    }

    std::optional<V> get(const K& key) const {
        int tid = omp_get_thread_num();
        auto s1 = std::chrono::high_resolution_clock::now();
        std::shared_lock<std::shared_mutex> lock(global_structure_mtx);
        auto s2 = std::chrono::high_resolution_clock::now();
        if (g_thread_audits && tid < g_audit_max_threads)
            g_thread_audits[tid].global_lock_wait_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(s2 - s1).count();

        size_t idx = get_bucket_index(key, bucket_count);
        
        auto b1 = std::chrono::high_resolution_clock::now();
        std::lock_guard<std::mutex> b_lock(buckets[idx]->mtx);
        auto b2 = std::chrono::high_resolution_clock::now();
        if (g_thread_audits && tid < g_audit_max_threads)
            g_thread_audits[tid].bucket_lock_wait_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(b2 - b1).count();

        Node* curr = buckets[idx]->head;
        while (curr) {
            if (curr->key == key) return curr->value;
            curr = curr->next;
        }
        return std::nullopt;
    }

    void update(const K& key, std::function<V(std::optional<V>)> updater) {
        int tid = omp_get_thread_num();
        auto s1 = std::chrono::high_resolution_clock::now();
        std::shared_lock<std::shared_mutex> lock(global_structure_mtx);
        auto s2 = std::chrono::high_resolution_clock::now();
        if (g_thread_audits && tid < g_audit_max_threads)
            g_thread_audits[tid].global_lock_wait_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(s2 - s1).count();

        size_t idx = get_bucket_index(key, bucket_count);
        
        auto b1 = std::chrono::high_resolution_clock::now();
        std::lock_guard<std::mutex> b_lock(buckets[idx]->mtx);
        auto b2 = std::chrono::high_resolution_clock::now();
        if (g_thread_audits && tid < g_audit_max_threads)
            g_thread_audits[tid].bucket_lock_wait_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(b2 - b1).count();

        Node* curr = buckets[idx]->head;
        while (curr) {
            if (curr->key == key) {
                curr->value = updater(curr->value);
                return;
            }
            curr = curr->next;
        }

        buckets[idx]->head = new Node(key, updater(std::nullopt), buckets[idx]->head);
        element_count.fetch_add(1);
        
        lock.unlock();
        check_and_resize();
    }

    size_t size() const { return element_count.load(); }

    V sum_all_values() const {
        std::shared_lock<std::shared_mutex> lock(global_structure_mtx);
        V total = 0;
        for (auto b : buckets) {
            std::lock_guard<std::mutex> b_lock(b->mtx);
            Node* curr = b->head;
            while (curr) {
                total += curr->value;
                curr = curr->next;
            }
        }
        return total;
    }
};

#endif

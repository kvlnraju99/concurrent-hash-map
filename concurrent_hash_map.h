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

/**
 * @brief Performance Audit Structure to track bottlenecks
 */
struct PerformanceAudit {
    std::atomic<long long> global_lock_wait_ns{0};
    std::atomic<long long> bucket_lock_wait_ns{0};
    std::atomic<long long> resize_time_ns{0};
    std::atomic<int> resize_count{0};

    void report() {
        std::cout << "\n--- PERFORMANCE AUDIT REPORT ---" << std::endl;
        std::cout << "1. Global Lock Contention: " << (global_lock_wait_ns / 1000000.0) << " ms" << std::endl;
        std::cout << "2. Bucket Lock Contention: " << (bucket_lock_wait_ns / 1000000.0) << " ms" << std::endl;
        std::cout << "3. Resize Operations:      " << resize_count << " (Total: " << (resize_time_ns / 1000000.0) << " ms)" << std::endl;
        std::cout << "--------------------------------" << std::endl;
    }
};

static PerformanceAudit g_audit;

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
            auto start = std::chrono::high_resolution_clock::now();
            std::unique_lock<std::shared_mutex> lock(global_structure_mtx);
            
            // Double check
            if (element_count.load() <= bucket_count) return;

            size_t new_count = bucket_count * 2;
            std::vector<Bucket*> new_buckets(new_count);
            for (size_t i = 0; i < new_count; ++i) {
                new_buckets[i] = new Bucket();
            }

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
            g_audit.resize_time_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
            g_audit.resize_count++;
        }
    }

public:
    explicit ConcurrentHashMap(size_t num_buckets = 1024)
        : bucket_count(num_buckets), element_count(0) {
        buckets.resize(bucket_count);
        for (size_t i = 0; i < bucket_count; ++i) {
            buckets[i] = new Bucket();
        }
    }

    ~ConcurrentHashMap() {
        clear_buckets(buckets);
    }

    void put(const K& key, const V& value) {
        auto s1 = std::chrono::high_resolution_clock::now();
        std::shared_lock<std::shared_mutex> lock(global_structure_mtx);
        auto s2 = std::chrono::high_resolution_clock::now();
        g_audit.global_lock_wait_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(s2 - s1).count();

        size_t idx = get_bucket_index(key, bucket_count);
        
        auto b1 = std::chrono::high_resolution_clock::now();
        std::lock_guard<std::mutex> b_lock(buckets[idx]->mtx);
        auto b2 = std::chrono::high_resolution_clock::now();
        g_audit.bucket_lock_wait_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(b2 - b1).count();

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
        
        // Potential resize check
        lock.unlock(); 
        check_and_resize();
    }

    std::optional<V> get(const K& key) const {
        auto s1 = std::chrono::high_resolution_clock::now();
        std::shared_lock<std::shared_mutex> lock(global_structure_mtx);
        auto s2 = std::chrono::high_resolution_clock::now();
        g_audit.global_lock_wait_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(s2 - s1).count();

        size_t idx = get_bucket_index(key, bucket_count);
        
        auto b1 = std::chrono::high_resolution_clock::now();
        std::lock_guard<std::mutex> b_lock(buckets[idx]->mtx);
        auto b2 = std::chrono::high_resolution_clock::now();
        g_audit.bucket_lock_wait_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(b2 - b1).count();

        Node* curr = buckets[idx]->head;
        while (curr) {
            if (curr->key == key) {
                return curr->value;
            }
            curr = curr->next;
        }
        return std::nullopt;
    }

    void update(const K& key, std::function<V(std::optional<V>)> updater) {
        auto s1 = std::chrono::high_resolution_clock::now();
        std::shared_lock<std::shared_mutex> lock(global_structure_mtx);
        auto s2 = std::chrono::high_resolution_clock::now();
        g_audit.global_lock_wait_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(s2 - s1).count();

        size_t idx = get_bucket_index(key, bucket_count);
        
        auto b1 = std::chrono::high_resolution_clock::now();
        std::lock_guard<std::mutex> b_lock(buckets[idx]->mtx);
        auto b2 = std::chrono::high_resolution_clock::now();
        g_audit.bucket_lock_wait_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(b2 - b1).count();

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

    size_t size() const {
        return element_count.load();
    }

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

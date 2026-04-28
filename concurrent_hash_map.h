#ifndef CONCURRENT_HASH_MAP_H
#define CONCURRENT_HASH_MAP_H

#include <vector>
#include <mutex>
#include <shared_mutex>
#include <optional>
#include <functional>
#include <atomic>
#include <memory>

/**
 * @brief Phase 3 Version: Concurrent Hash Map with Dynamic Resizing.
 * Uses a Reader-Writer lock (shared_mutex) for the global structure
 * and individual mutexes for each bucket.
 */
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
        mutable std::mutex mtx; // Individual lock per bucket
    };

    // We use unique_ptr because std::mutex is not movable/copyable.
    // This allows the vector to be reallocated safely.
    std::vector<std::unique_ptr<Bucket>> buckets;
    size_t bucket_count;
    std::atomic<size_t> element_count;
    
    // Protects the structure of the map (the bucket array itself)
    mutable std::shared_mutex global_structure_mtx;

    size_t get_bucket_index(const K& key, size_t count) const {
        return std::hash<K>{}(key) % count;
    }

    // Double-checked resizing logic
    void check_and_resize() {
        // Read-only check first (Fast path)
        if (element_count.load() <= bucket_count) return;

        // Try to acquire the unique lock to perform resize
        std::unique_lock<std::shared_mutex> lock(global_structure_mtx);

        // Double check after acquiring lock (Second check)
        if (element_count.load() <= bucket_count) return;

        size_t new_count = bucket_count * 2;
        std::vector<std::unique_ptr<Bucket>> new_buckets(new_count);
        for (size_t i = 0; i < new_count; ++i) {
            new_buckets[i] = std::make_unique<Bucket>();
        }

        // Rehash all existing elements
        for (size_t i = 0; i < bucket_count; ++i) {
            Node* curr = buckets[i]->head;
            while (curr) {
                Node* next_node = curr->next;
                size_t new_idx = get_bucket_index(curr->key, new_count);
                
                curr->next = new_buckets[new_idx]->head;
                new_buckets[new_idx]->head = curr;
                
                curr = next_node;
            }
            buckets[i]->head = nullptr;
        }

        buckets = std::move(new_buckets);
        bucket_count = new_count;
    }

public:
    explicit ConcurrentHashMap(size_t num_buckets = 1024) 
        : bucket_count(num_buckets), element_count(0) {
        buckets.reserve(bucket_count);
        for (size_t i = 0; i < bucket_count; ++i) {
            buckets.push_back(std::make_unique<Bucket>());
        }
    }

    ~ConcurrentHashMap() {
        // Global unique lock during destruction
        std::unique_lock<std::shared_mutex> lock(global_structure_mtx);
        for (size_t i = 0; i < bucket_count; ++i) {
            Node* curr = buckets[i]->head;
            while (curr) {
                Node* temp = curr;
                curr = curr->next;
                delete temp;
            }
        }
    }

    void put(const K& key, const V& value) {
        bool needs_resize = false;
        {
            // Shared lock for normal operation
            std::shared_lock<std::shared_mutex> lock(global_structure_mtx);

            size_t idx = get_bucket_index(key, bucket_count);
            std::lock_guard<std::mutex> bucket_lock(buckets[idx]->mtx);

            Node* curr = buckets[idx]->head;
            while (curr) {
                if (curr->key == key) {
                    curr->value = value;
                    return;
                }
                curr = curr->next;
            }

            buckets[idx]->head = new Node(key, value, buckets[idx]->head);
            element_count.fetch_add(1, std::memory_order_relaxed);

            // Check if we need to resize
            if (element_count.load() > bucket_count) {
                needs_resize = true;
            }
        } // All locks (global shared and bucket) are released here

        if (needs_resize) {
            check_and_resize();
        }
    }

    std::optional<V> get(const K& key) const {
        std::shared_lock<std::shared_mutex> lock(global_structure_mtx);

        size_t idx = get_bucket_index(key, bucket_count);
        std::lock_guard<std::mutex> bucket_lock(buckets[idx]->mtx);

        Node* curr = buckets[idx]->head;
        while (curr) {
            if (curr->key == key) {
                return curr->value;
            }
            curr = curr->next;
        }
        return std::nullopt;
    }

    bool remove(const K& key) {
        std::shared_lock<std::shared_mutex> lock(global_structure_mtx);

        size_t idx = get_bucket_index(key, bucket_count);
        std::lock_guard<std::mutex> bucket_lock(buckets[idx]->mtx);

        Node* curr = buckets[idx]->head;
        Node* prev = nullptr;

        while (curr) {
            if (curr->key == key) {
                if (prev) {
                    prev->next = curr->next;
                } else {
                    buckets[idx]->head = curr->next;
                }
                delete curr;
                element_count.fetch_sub(1, std::memory_order_relaxed);
                return true;
            }
            prev = curr;
            curr = curr->next;
        }
        return false;
    }

    void update(const K& key, std::function<V(std::optional<V>)> updater) {
        bool needs_resize = false;
        {
            std::shared_lock<std::shared_mutex> lock(global_structure_mtx);
            size_t idx = get_bucket_index(key, bucket_count);
            std::lock_guard<std::mutex> bucket_lock(buckets[idx]->mtx);

            Node* curr = buckets[idx]->head;
            while (curr) {
                if (curr->key == key) {
                    curr->value = updater(curr->value);
                    return;
                }
                curr = curr->next;
            }

            // Key not found, create new
            V new_val = updater(std::nullopt);
            buckets[idx]->head = new Node(key, new_val, buckets[idx]->head);
            element_count.fetch_add(1, std::memory_order_relaxed);

            if (element_count.load() > bucket_count) {
                needs_resize = true;
            }
        }

        if (needs_resize) {
            check_and_resize();
        }
    }

    size_t size() const {
        return element_count.load(std::memory_order_relaxed);
    }
};

#endif

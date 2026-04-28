#ifndef CONCURRENT_HASH_MAP_V4_H
#define CONCURRENT_HASH_MAP_V4_H

#include <vector>
#include <shared_mutex>
#include <mutex>
#include <optional>
#include <functional>
#include <atomic>

/**
 * @brief Phase 5 Version: Optimistic Atomic Map (Hybrid Lock-Free)
 * This version uses a global shared_mutex for structure, but values inside
 * nodes are std::atomic<V>. This eliminates the need for bucket-level locks
 * when updating existing keys.
 */
template <typename K, typename V>
class ConcurrentHashMapV4 {
    static_assert(std::is_trivially_copyable<V>::value, "V4 requires trivially copyable types for atomic operations");

private:
    struct Node {
        K key;
        std::atomic<V> value;
        Node* next;
        Node(K k, V v, Node* n) : key(k), value(v), next(n) {}
    };

    struct Bucket {
        Node* head = nullptr;
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

public:
    explicit ConcurrentHashMapV4(size_t num_buckets = 131071)
        : bucket_count(num_buckets), element_count(0) {
        buckets.resize(bucket_count);
        for (size_t i = 0; i < bucket_count; ++i) {
            buckets[i] = new Bucket();
        }
    }

    ~ConcurrentHashMapV4() {
        clear_buckets(buckets);
    }

    void put(const K& key, const V& value) {
        // Try to find the key with a shared lock first (Optimistic)
        {
            std::shared_lock<std::shared_mutex> lock(global_structure_mtx);
            size_t idx = get_bucket_index(key, bucket_count);
            Node* curr = buckets[idx]->head;
            while (curr) {
                if (curr->key == key) {
                    curr->value.store(value, std::memory_order_relaxed);
                    return;
                }
                curr = curr->next;
            }
        }

        // If not found, we need a unique lock to insert a new node
        bool needs_resize = false;
        {
            std::unique_lock<std::shared_mutex> lock(global_structure_mtx);
            size_t idx = get_bucket_index(key, bucket_count);
            
            // Double check inside unique lock
            Node* curr = buckets[idx]->head;
            while (curr) {
                if (curr->key == key) {
                    curr->value.store(value, std::memory_order_relaxed);
                    return;
                }
                curr = curr->next;
            }

            // Truly new key
            buckets[idx]->head = new Node(key, value, buckets[idx]->head);
            element_count.fetch_add(1, std::memory_order_relaxed);
            
            if (element_count.load() > bucket_count) {
                needs_resize = true;
            }
        }

        if (needs_resize) {
            check_and_resize();
        }
    }

    std::optional<V> get(const K& key) const {
        std::shared_lock<std::shared_mutex> lock(global_structure_mtx);
        size_t idx = get_bucket_index(key, bucket_count);

        Node* curr = buckets[idx]->head;
        while (curr) {
            if (curr->key == key) {
                return curr->value.load(std::memory_order_relaxed);
            }
            curr = curr->next;
        }
        return std::nullopt;
    }

    void update(const K& key, std::function<V(std::optional<V>)> updater) {
        // Try to update optimistically with shared lock
        {
            std::shared_lock<std::shared_mutex> lock(global_structure_mtx);
            size_t idx = get_bucket_index(key, bucket_count);
            Node* curr = buckets[idx]->head;
            while (curr) {
                if (curr->key == key) {
                    // Lock-free atomic update loop
                    V old_val = curr->value.load(std::memory_order_relaxed);
                    V new_val;
                    do {
                        new_val = updater(old_val);
                    } while (!curr->value.compare_exchange_weak(old_val, new_val));
                    return;
                }
                curr = curr->next;
            }
        }

        // If key doesn't exist, we need unique lock to insert
        bool needs_resize = false;
        {
            std::unique_lock<std::shared_mutex> lock(global_structure_mtx);
            size_t idx = get_bucket_index(key, bucket_count);
            
            Node* curr = buckets[idx]->head;
            while (curr) {
                if (curr->key == key) {
                    V old_val = curr->value.load(std::memory_order_relaxed);
                    V new_val;
                    do {
                        new_val = updater(old_val);
                    } while (!curr->value.compare_exchange_weak(old_val, new_val));
                    return;
                }
                curr = curr->next;
            }

            V new_val = updater(std::nullopt);
            buckets[idx]->head = new Node(key, new_val, buckets[idx]->head);
            element_count.fetch_add(1, std::memory_order_relaxed);
            if (element_count.load() > bucket_count) needs_resize = true;
        }

        if (needs_resize) check_and_resize();
    }

    void check_and_resize() {
        std::unique_lock<std::shared_mutex> lock(global_structure_mtx);
        if (element_count.load() <= bucket_count) return;

        size_t new_count = bucket_count * 2;
        std::vector<Bucket*> new_buckets(new_count);
        for (size_t i = 0; i < new_count; ++i) new_buckets[i] = new Bucket();

        for (size_t i = 0; i < bucket_count; ++i) {
            Node* curr = buckets[i]->head;
            while (curr) {
                Node* next = curr->next;
                size_t new_idx = get_bucket_index(curr->key, new_count);
                curr->next = new_buckets[new_idx]->head;
                new_buckets[new_idx]->head = curr;
                curr = next;
            }
            delete buckets[i];
        }

        buckets = std::move(new_buckets);
        bucket_count = new_count;
    }

    size_t size() const {
        return element_count.load(std::memory_order_relaxed);
    }

    V sum_all_values() const {
        V total = 0;
        std::shared_lock<std::shared_mutex> lock(global_structure_mtx);
        for (size_t i = 0; i < bucket_count; ++i) {
            Node* curr = buckets[i]->head;
            while (curr) {
                total += curr->value.load(std::memory_order_relaxed);
                curr = curr->next;
            }
        }
        return total;
    }
};

#endif

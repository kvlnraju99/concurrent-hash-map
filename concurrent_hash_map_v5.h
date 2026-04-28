#ifndef CONCURRENT_HASH_MAP_V5_H
#define CONCURRENT_HASH_MAP_V5_H

#include <vector>
#include <mutex>
#include <optional>
#include <functional>
#include <atomic>
#include <thread>

/**
 * @brief Phase 6 Version: Wait-Free Reader Map (RCU-Style)
 * This version uses an active reader counter instead of a shared_mutex.
 * It is optimized for extremely high-throughput read/update workloads.
 */
template <typename K, typename V>
class ConcurrentHashMapV5 {
    static_assert(std::is_trivially_copyable<V>::value, "V5 requires trivially copyable types for atomic operations");

private:
    struct Node {
        K key;
        std::atomic<V> value;
        Node* next;
        Node(K k, V v, Node* n) : key(k), value(v), next(n) {}
    };

    struct Bucket {
        std::atomic<Node*> head;
        Bucket() : head(nullptr) {}
    };

    struct BucketArray {
        std::vector<Bucket*> buckets;
        size_t count;
        BucketArray(size_t n) : count(n) {
            buckets.resize(n);
            for (size_t i = 0; i < n; ++i) buckets[i] = new Bucket();
        }
        ~BucketArray() {
            for (size_t i = 0; i < count; ++i) {
                Node* curr = buckets[i]->head.load();
                while (curr) {
                    Node* temp = curr;
                    curr = curr->next;
                    delete temp;
                }
                delete buckets[i];
            }
        }
    };

    std::atomic<BucketArray*> current_array;
    std::atomic<size_t> element_count;
    std::atomic<int> active_readers;
    std::mutex structure_mtx; // Only for writers (new keys/resize)

    size_t get_bucket_index(const K& key, size_t count) const {
        return std::hash<K>{}(key) % count;
    }

public:
    explicit ConcurrentHashMapV5(size_t num_buckets = 131071)
        : element_count(0), active_readers(0) {
        current_array.store(new BucketArray(num_buckets));
    }

    ~ConcurrentHashMapV5() {
        delete current_array.load();
    }

    std::optional<V> get(const K& key) {
        active_readers.fetch_add(1, std::memory_order_acquire);
        BucketArray* arr = current_array.load(std::memory_order_relaxed);
        
        size_t idx = get_bucket_index(key, arr->count);
        Node* curr = arr->buckets[idx]->head.load(std::memory_order_relaxed);
        
        std::optional<V> result = std::nullopt;
        while (curr) {
            if (curr->key == key) {
                result = curr->value.load(std::memory_order_relaxed);
                break;
            }
            curr = curr->next;
        }
        
        active_readers.fetch_add(-1, std::memory_order_release);
        return result;
    }

    void update(const K& key, std::function<V(std::optional<V>)> updater) {
        active_readers.fetch_add(1, std::memory_order_acquire);
        BucketArray* arr = current_array.load(std::memory_order_relaxed);
        size_t idx = get_bucket_index(key, arr->count);
        
        Node* curr = arr->buckets[idx]->head.load(std::memory_order_relaxed);
        while (curr) {
            if (curr->key == key) {
                V old_val = curr->value.load(std::memory_order_relaxed);
                V new_val;
                do {
                    new_val = updater(old_val);
                } while (!curr->value.compare_exchange_weak(old_val, new_val));
                active_readers.fetch_add(-1, std::memory_order_release);
                return;
            }
            curr = curr->next;
        }
        active_readers.fetch_add(-1, std::memory_order_release);

        // Key not found, need a lock to insert
        std::unique_lock<std::mutex> lock(structure_mtx);
        arr = current_array.load(); // Refresh array pointer
        idx = get_bucket_index(key, arr->count);
        
        // Double check
        curr = arr->buckets[idx]->head.load();
        while (curr) {
            if (curr->key == key) {
                V old_val = curr->value.load();
                curr->value.store(updater(old_val));
                return;
            }
            curr = curr->next;
        }

        V new_val = updater(std::nullopt);
        Node* newNode = new Node(key, new_val, arr->buckets[idx]->head.load());
        arr->buckets[idx]->head.store(newNode);
        element_count.fetch_add(1);

        if (element_count.load() > arr->count) {
            check_and_resize(arr);
        }
    }

    void put(const K& key, const V& value) {
        update(key, [&](std::optional<V> /*old*/) { return value; });
    }

    void check_and_resize(BucketArray* old_arr) {
        // Assume structure_mtx is already held by update()
        if (element_count.load() <= old_arr->count) return;

        size_t new_count = old_arr->count * 2;
        BucketArray* new_arr = new BucketArray(new_count);

        for (size_t i = 0; i < old_arr->count; ++i) {
            Node* curr = old_arr->buckets[i]->head.load();
            while (curr) {
                Node* next = curr->next;
                size_t new_idx = get_bucket_index(curr->key, new_count);
                curr->next = new_arr->buckets[new_idx]->head.load();
                new_arr->buckets[new_idx]->head.store(curr);
                curr = next;
            }
            // Clear old bucket to prevent double deletion
            old_arr->buckets[i]->head.store(nullptr);
        }

        // SWAP the array pointer
        current_array.store(new_arr, std::memory_order_release);

        // THE WAIT: Wait for all readers to finish with the old array
        while (active_readers.load() > 0) {
            std::this_thread::yield();
        }

        // Safe to delete old array structure (nodes were moved, not deleted)
        delete old_arr;
    }

    size_t size() const {
        return element_count.load();
    }
};

#endif

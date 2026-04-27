#ifndef CONCURRENT_HASH_MAP_H
#define CONCURRENT_HASH_MAP_H

#include <vector>
#include <mutex>
#include <optional>
#include <functional>
#include <atomic>

/**
 * @brief A thread-safe Hash Map using Fine-Grained (Bucket-Level) Locking.
 * This implementation uses Separate Chaining (Linked Lists) and 
 * individual std::mutex per bucket to allow high concurrency.
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

    std::vector<Bucket> buckets;
    size_t bucket_count;
    std::atomic<size_t> element_count;

    // Standard hash-to-index mapping
    size_t get_bucket_index(const K& key) const {
        return std::hash<K>{}(key) % bucket_count;
    }

public:
    explicit ConcurrentHashMap(size_t num_buckets = 131071) // Using a prime number for better distribution
        : buckets(num_buckets), bucket_count(num_buckets), element_count(0) {}

    // Destructor to clean up all allocated nodes
    ~ConcurrentHashMap() {
        for (size_t i = 0; i < bucket_count; ++i) {
            Node* curr = buckets[i].head;
            while (curr) {
                Node* temp = curr;
                curr = curr->next;
                delete temp;
            }
        }
    }

    // Insert or update a key-value pair
    void put(const K& key, const V& value) {
        size_t idx = get_bucket_index(key);
        std::lock_guard<std::mutex> lock(buckets[idx].mtx); // Only lock the specific bucket

        Node* curr = buckets[idx].head;
        while (curr) {
            if (curr->key == key) {
                curr->value = value;
                return;
            }
            curr = curr->next;
        }

        // Key not found, insert new node at the head
        buckets[idx].head = new Node(key, value, buckets[idx].head);
        element_count.fetch_add(1, std::memory_order_relaxed);
    }

    // Retrieve a value by key
    std::optional<V> get(const K& key) const {
        size_t idx = get_bucket_index(key);
        std::lock_guard<std::mutex> lock(buckets[idx].mtx); // Only lock the specific bucket

        Node* curr = buckets[idx].head;
        while (curr) {
            if (curr->key == key) {
                return curr->value;
            }
            curr = curr->next;
        }
        return std::nullopt;
    }

    // Remove a key from the map
    bool remove(const K& key) {
        size_t idx = get_bucket_index(key);
        std::lock_guard<std::mutex> lock(buckets[idx].mtx);

        Node* curr = buckets[idx].head;
        Node* prev = nullptr;

        while (curr) {
            if (curr->key == key) {
                if (prev) {
                    prev->next = curr->next;
                } else {
                    buckets[idx].head = curr->next;
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

    // Return the total number of elements (using atomic counter)
    size_t size() const {
        return element_count.load(std::memory_order_relaxed);
    }
};

#endif

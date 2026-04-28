#ifndef CONCURRENT_HASH_MAP_V2_H
#define CONCURRENT_HASH_MAP_V2_H

#include <vector>
#include <mutex>
#include <optional>
#include <functional>
#include <atomic>

/**
 * @brief Phase 2 Version: Fine-Grained Locking (Static Bucket Count)
 */
template <typename K, typename V>
class ConcurrentHashMapV2 {
private:
    struct Node {
        K key;
        V value;
        Node* next;
        Node(K k, V v, Node* n) : key(k), value(v), next(n) {}
    };

    struct Bucket {
        Node* head = nullptr;
        mutable std::mutex mtx;
    };

    std::vector<Bucket> buckets;
    size_t bucket_count;
    std::atomic<size_t> element_count;

    size_t get_bucket_index(const K& key) const {
        return std::hash<K>{}(key) % bucket_count;
    }

public:
    explicit ConcurrentHashMapV2(size_t num_buckets = 131071)
        : buckets(num_buckets), bucket_count(num_buckets), element_count(0) {}

    ~ConcurrentHashMapV2() {
        for (size_t i = 0; i < bucket_count; ++i) {
            Node* curr = buckets[i].head;
            while (curr) {
                Node* temp = curr;
                curr = curr->next;
                delete temp;
            }
        }
    }

    void put(const K& key, const V& value) {
        size_t idx = get_bucket_index(key);
        std::lock_guard<std::mutex> lock(buckets[idx].mtx);

        Node* curr = buckets[idx].head;
        while (curr) {
            if (curr->key == key) {
                curr->value = value;
                return;
            }
            curr = curr->next;
        }

        buckets[idx].head = new Node(key, value, buckets[idx].head);
        element_count.fetch_add(1, std::memory_order_relaxed);
    }

    std::optional<V> get(const K& key) const {
        size_t idx = get_bucket_index(key);
        std::lock_guard<std::mutex> lock(buckets[idx].mtx);

        Node* curr = buckets[idx].head;
        while (curr) {
            if (curr->key == key) {
                return curr->value;
            }
            curr = curr->next;
        }
        return std::nullopt;
    }

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

    void update(const K& key, std::function<V(std::optional<V>)> updater) {
        size_t idx = get_bucket_index(key);
        std::lock_guard<std::mutex> lock(buckets[idx].mtx);

        Node* curr = buckets[idx].head;
        while (curr) {
            if (curr->key == key) {
                curr->value = updater(curr->value);
                return;
            }
            curr = curr->next;
        }

        V new_val = updater(std::nullopt);
        buckets[idx].head = new Node(key, new_val, buckets[idx].head);
        element_count.fetch_add(1, std::memory_order_relaxed);
    }

    size_t size() const {
        return element_count.load(std::memory_order_relaxed);
    }
};

#endif

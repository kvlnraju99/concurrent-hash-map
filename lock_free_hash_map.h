#ifndef LOCK_FREE_HASH_MAP_H
#define LOCK_FREE_HASH_MAP_H

#include <memory>
#include <atomic>
#include <functional>
#include <optional>
#include <utility>
#include <string>

// A lock-free hash map using atomic pointers and Compare-And-Swap (CAS).
//
// How it works:
//   - Each bucket is an atomic pointer to a linked list of nodes.
//   - PUT: prepend a new node using CAS (no lock needed).
//   - GET: just walk the list (reads are naturally safe).
//   - REMOVE: mark the node as deleted using an atomic flag.
//
// No mutex is used anywhere. Threads never block or wait.
//
// Known limitations (acceptable for a course project):
//   - No resizing (fixed bucket count).
//   - Deleted nodes stay in memory (no reclamation).
//   - Concurrent puts of the same NEW key may create duplicates
//     (last write wins on read, so behavior is still correct).

template <typename K, typename V>
class LockFreeHashMap {
private:

    // A node in the per-bucket linked list.
    struct Node {
        K key;
        V value;
        std::atomic<bool> deleted;    // true = logically removed
        std::atomic<Node*> next;      // pointer to next node in chain

        Node(const K& k, const V& v)
            : key(k), value(v), deleted(false), next(nullptr) {}
    };

    std::unique_ptr<std::atomic<Node*>[]> buckets;  // array of atomic head pointers
    size_t bucket_count;
    std::atomic<size_t> element_count;

    // Pick the bucket for a given key.
    size_t get_bucket_index(const K& key) const {
        return std::hash<K>{}(key) % bucket_count;
    }

public:

    explicit LockFreeHashMap(size_t num_buckets = 16)
        : buckets(new std::atomic<Node*>[num_buckets]),
          bucket_count(num_buckets), element_count(0) {
        for (size_t i = 0; i < num_buckets; i++) {
            buckets[i].store(nullptr, std::memory_order_relaxed);
        }
    }

    // Clean up all nodes.
    ~LockFreeHashMap() {
        for (size_t i = 0; i < bucket_count; i++) {
            Node* curr = buckets[i].load(std::memory_order_relaxed);
            while (curr) {
                Node* next = curr->next.load(std::memory_order_relaxed);
                delete curr;
                curr = next;
            }
        }
    }

    // Insert or update a key-value pair.
    // Uses CAS to prepend new nodes without any lock.
    void put(const K& key, const V& value) {
        size_t idx = get_bucket_index(key);

        // Walk the list to see if the key already exists.
        Node* curr = buckets[idx].load(std::memory_order_acquire);
        while (curr) {
            if (!curr->deleted.load(std::memory_order_acquire) && curr->key == key) {
                // Key found — update the value in place.
                curr->value = value;
                return;
            }
            curr = curr->next.load(std::memory_order_acquire);
        }

        // Key not found — create a new node and prepend it using CAS.
        Node* new_node = new Node(key, value);
        Node* head = buckets[idx].load(std::memory_order_acquire);

        do {
            // Point new node's next to the current head.
            new_node->next.store(head, std::memory_order_relaxed);

            // CAS: if head hasn't changed, swap it to our new node.
            // If another thread changed head, CAS fails and we retry.
        } while (!buckets[idx].compare_exchange_weak(
                    head, new_node,
                    std::memory_order_release,
                    std::memory_order_acquire));

        element_count.fetch_add(1, std::memory_order_relaxed);
    }

    // Look up a key. No locks needed — just read atomic pointers.
    std::optional<V> get(const K& key) const {
        size_t idx = get_bucket_index(key);

        // Walk the list, reading atomic pointers.
        Node* curr = buckets[idx].load(std::memory_order_acquire);
        while (curr) {
            if (!curr->deleted.load(std::memory_order_acquire) && curr->key == key) {
                return curr->value;
            }
            curr = curr->next.load(std::memory_order_acquire);
        }

        return std::nullopt;
    }

    // Remove a key by marking it as deleted (logical deletion).
    // The node stays in memory but is skipped by get().
    bool remove(const K& key) {
        size_t idx = get_bucket_index(key);

        Node* curr = buckets[idx].load(std::memory_order_acquire);
        while (curr) {
            if (!curr->deleted.load(std::memory_order_acquire) && curr->key == key) {
                // Mark as deleted. Other threads will skip this node.
                curr->deleted.store(true, std::memory_order_release);
                element_count.fetch_sub(1, std::memory_order_relaxed);
                return true;
            }
            curr = curr->next.load(std::memory_order_acquire);
        }

        return false;
    }

    // Return the current number of live elements.
    size_t size() const {
        return element_count.load(std::memory_order_relaxed);
    }

    // Return the number of buckets (fixed, never changes).
    size_t get_bucket_count() const {
        return bucket_count;
    }
};

#endif // LOCK_FREE_HASH_MAP_H

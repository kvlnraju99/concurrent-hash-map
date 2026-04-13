#ifndef CONCURRENT_HASH_MAP_H
#define CONCURRENT_HASH_MAP_H

#include <vector>
#include <mutex>
#include <shared_mutex>
#include <functional>
#include <optional>
#include <utility>
#include <string>
#include <atomic>
#include <memory>

// A thread-safe hash map with:
//   - One shared_mutex per bucket (readers don't block each other)
//   - A global shared_mutex to protect the bucket array during resize
//   - Dynamic resizing when load factor exceeds threshold

template <typename K, typename V>
class ConcurrentHashMap {
private:

    // Each bucket has its own vector and its own shared_mutex.
    struct Bucket {
        std::vector<std::pair<K, V>> entries;
        mutable std::shared_mutex mtx;
    };

    // Buckets are heap-allocated (unique_ptr) because shared_mutex
    // is not movable, and we need to swap the vector during resize.
    std::vector<std::unique_ptr<Bucket>> buckets;

    size_t bucket_count;                     // current number of buckets
    std::atomic<size_t> element_count;       // total key-value pairs
    double load_factor_threshold;            // resize when count/buckets > this
    mutable std::shared_mutex global_mtx;    // protects bucket array during resize

    // Pick the bucket for a given key.
    size_t get_bucket_index(const K& key) const {
        return std::hash<K>{}(key) % bucket_count;
    }

    // Double the number of buckets and rehash all entries.
    // Called only when load factor is exceeded.
    void resize() {
        // Exclusive global lock — blocks ALL operations.
        std::unique_lock<std::shared_mutex> global_lock(global_mtx);

        // Recheck — another thread might have already resized.
        if (element_count.load() <= static_cast<size_t>(bucket_count * load_factor_threshold)) {
            return;
        }

        size_t new_count = bucket_count * 2;

        // Create new empty buckets.
        std::vector<std::unique_ptr<Bucket>> new_buckets(new_count);
        for (size_t i = 0; i < new_count; i++) {
            new_buckets[i] = std::make_unique<Bucket>();
        }

        // Move all entries from old buckets to new buckets.
        for (size_t i = 0; i < bucket_count; i++) {
            for (auto& entry : buckets[i]->entries) {
                size_t new_idx = std::hash<K>{}(entry.first) % new_count;
                new_buckets[new_idx]->entries.push_back(std::move(entry));
            }
        }

        // Swap in the new bucket array.
        buckets = std::move(new_buckets);
        bucket_count = new_count;
    }

public:

    // Create the map with an initial bucket count and load factor threshold.
    // Set load_factor to a huge value (e.g. 999999) to disable resizing.
    explicit ConcurrentHashMap(size_t num_buckets = 16, double load_factor = 0.75)
        : bucket_count(num_buckets), element_count(0), load_factor_threshold(load_factor) {
        buckets.resize(num_buckets);
        for (size_t i = 0; i < num_buckets; i++) {
            buckets[i] = std::make_unique<Bucket>();
        }
    }

    // Insert or update a key-value pair.
    void put(const K& key, const V& value) {
        bool needs_resize = false;

        {
            // Shared global lock — allows other operations in parallel.
            std::shared_lock<std::shared_mutex> global_lock(global_mtx);

            size_t idx = get_bucket_index(key);
            Bucket& bucket = *buckets[idx];

            // Exclusive bucket lock — only one writer per bucket.
            std::unique_lock<std::shared_mutex> bucket_lock(bucket.mtx);

            // Check if key already exists.
            for (auto& entry : bucket.entries) {
                if (entry.first == key) {
                    entry.second = value;
                    return;  // updated existing key, no size change
                }
            }

            // New key — insert it.
            bucket.entries.emplace_back(key, value);
            size_t count = element_count.fetch_add(1) + 1;

            // Check if we need to resize.
            needs_resize = (count > static_cast<size_t>(bucket_count * load_factor_threshold));
        }

        // Resize outside of locks to avoid deadlock.
        if (needs_resize) {
            resize();
        }
    }

    // Look up a key. Returns std::nullopt if not found.
    std::optional<V> get(const K& key) const {
        std::shared_lock<std::shared_mutex> global_lock(global_mtx);

        size_t idx = get_bucket_index(key);
        const Bucket& bucket = *buckets[idx];

        // Shared bucket lock — multiple readers allowed.
        std::shared_lock<std::shared_mutex> bucket_lock(bucket.mtx);

        for (const auto& entry : bucket.entries) {
            if (entry.first == key) {
                return entry.second;
            }
        }
        return std::nullopt;
    }

    // Remove a key. Returns true if found and removed.
    bool remove(const K& key) {
        std::shared_lock<std::shared_mutex> global_lock(global_mtx);

        size_t idx = get_bucket_index(key);
        Bucket& bucket = *buckets[idx];

        // Exclusive bucket lock — only one writer per bucket.
        std::unique_lock<std::shared_mutex> bucket_lock(bucket.mtx);

        for (auto it = bucket.entries.begin(); it != bucket.entries.end(); ++it) {
            if (it->first == key) {
                bucket.entries.erase(it);
                element_count.fetch_sub(1);
                return true;
            }
        }
        return false;
    }

    // Return the current number of elements.
    size_t size() const {
        return element_count.load();
    }

    // Return the current number of buckets.
    size_t get_bucket_count() const {
        std::shared_lock<std::shared_mutex> global_lock(global_mtx);
        return bucket_count;
    }
};

#endif // CONCURRENT_HASH_MAP_H

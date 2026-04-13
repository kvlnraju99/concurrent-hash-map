#ifndef CONCURRENT_HASH_MAP_H
#define CONCURRENT_HASH_MAP_H

#include <vector>
#include <mutex>
#include <shared_mutex>
#include <functional>
#include <optional>
#include <utility>
#include <string>

// A thread-safe hash map using one shared_mutex per bucket.
// Keys are hashed to a fixed number of buckets.
// Each bucket holds a vector of key-value pairs (chaining).
// Multiple readers can access the same bucket at the same time.
// Writers get exclusive access to the bucket.

template <typename K, typename V>
class ConcurrentHashMap {
private:

    // Each bucket has its own vector and its own shared_mutex.
    struct Bucket {
        std::vector<std::pair<K, V>> entries;  // chain of key-value pairs
        mutable std::shared_mutex mtx;         // shared for reads, exclusive for writes
    };

    std::vector<Bucket> buckets;   // fixed array of buckets
    size_t bucket_count;           // total number of buckets

    // Pick the bucket for a given key.
    size_t get_bucket_index(const K& key) const {
        size_t h = std::hash<K>{}(key);
        return h % bucket_count;
    }

public:

    // Create the map with a fixed number of buckets.
    explicit ConcurrentHashMap(size_t num_buckets = 16)
        : buckets(num_buckets), bucket_count(num_buckets) {}

    // Insert or update a key-value pair.
    // If the key already exists, its value is overwritten.
    void put(const K& key, const V& value) {
        size_t idx = get_bucket_index(key);
        Bucket& bucket = buckets[idx];

        // Exclusive lock — only one writer at a time.
        std::unique_lock<std::shared_mutex> lock(bucket.mtx);

        // Check if the key already exists; if so, update it.
        for (auto& entry : bucket.entries) {
            if (entry.first == key) {
                entry.second = value;
                return;
            }
        }

        // Key not found — add a new entry.
        bucket.entries.emplace_back(key, value);
    }

    // Look up a key and return its value.
    // Returns std::nullopt if the key is not in the map.
    std::optional<V> get(const K& key) const {
        size_t idx = get_bucket_index(key);
        const Bucket& bucket = buckets[idx];

        // Shared lock — multiple readers can enter at the same time.
        std::shared_lock<std::shared_mutex> lock(bucket.mtx);

        for (const auto& entry : bucket.entries) {
            if (entry.first == key) {
                return entry.second;
            }
        }

        return std::nullopt;  // key not found
    }

    // Remove a key from the map.
    // Returns true if the key was found and removed, false otherwise.
    bool remove(const K& key) {
        size_t idx = get_bucket_index(key);
        Bucket& bucket = buckets[idx];

        // Exclusive lock — only one writer at a time.
        std::unique_lock<std::shared_mutex> lock(bucket.mtx);

        for (auto it = bucket.entries.begin(); it != bucket.entries.end(); ++it) {
            if (it->first == key) {
                bucket.entries.erase(it);
                return true;
            }
        }

        return false;  // key not found
    }
};

#endif // CONCURRENT_HASH_MAP_H

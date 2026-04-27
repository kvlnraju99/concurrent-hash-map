#ifndef NAIVE_MAP_H
#define NAIVE_MAP_H

#include <unordered_map>
#include <mutex>
#include <optional>

/**
 * @brief A thread-safe wrapper around std::unordered_map using a single global mutex.
 * This represents the "Naive" baseline for concurrency performance.
 */
template <typename K, typename V>
class NaiveHashMap {
private:
    std::unordered_map<K, V> internal_map;
    mutable std::mutex global_mtx; // Single lock for the whole map

public:
    // Insert or update a key-value pair
    void put(const K& key, const V& value) {
        std::lock_guard<std::mutex> lock(global_mtx);
        internal_map[key] = value;
    }

    // Retrieve a value by key (returns std::nullopt if not found)
    std::optional<V> get(const K& key) const {
        std::lock_guard<std::mutex> lock(global_mtx);
        auto it = internal_map.find(key);
        if (it != internal_map.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    // Remove a key from the map
    bool remove(const K& key) {
        std::lock_guard<std::mutex> lock(global_mtx);
        return internal_map.erase(key) > 0;
    }

    // Return the current number of elements
    size_t size() const {
        std::lock_guard<std::mutex> lock(global_mtx);
        return internal_map.size();
    }
};

#endif

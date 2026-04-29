#ifndef TBB_WRAPPER_H
#define TBB_WRAPPER_H

#include <tbb/concurrent_hash_map.h>
#include <optional>
#include <functional>

/**
 * @brief A wrapper around Intel TBB's concurrent_hash_map to match our library's interface.
 */
template <typename K, typename V>
class TBBHashMapWrapper {
private:
    tbb::concurrent_hash_map<K, V> map;

public:
    explicit TBBHashMapWrapper(size_t bucket_count = 131071) {
        // TBB manages its own bucket count, but we can hint it
    }

    void put(const K& key, const V& value) {
        typename tbb::concurrent_hash_map<K, V>::accessor acc;
        map.insert(acc, key);
        acc->second = value;
    }

    std::optional<V> get(const K& key) const {
        typename tbb::concurrent_hash_map<K, V>::const_accessor acc;
        if (map.find(acc, key)) {
            return acc->second;
        }
        return std::nullopt;
    }

    template<typename F>
    void update(const K& key, F updater) {
        typename tbb::concurrent_hash_map<K, V>::accessor acc;
        if (map.insert(acc, key)) {
            // New key
            acc->second = updater(std::nullopt);
        } else {
            // Existing key
            acc->second = updater(std::optional<V>(acc->second));
        }
    }

    size_t size() const {
        return map.size();
    }

    V sum_all_values() const {
        V total = 0;
        for (auto it = map.begin(); it != map.end(); ++it) {
            total += it->second;
        }
        return total;
    }
};

#endif

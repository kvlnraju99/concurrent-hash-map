#ifndef CONCURRENT_HASH_MAP_H
#define CONCURRENT_HASH_MAP_H

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <utility>
#include <vector>

template <typename K, typename V>
class ConcurrentHashMap {
private:
    struct alignas(64) Bucket {
        std::vector<std::pair<K, V>> entries;
        mutable std::shared_mutex mutex;
    };

    std::vector<std::unique_ptr<Bucket>> buckets;
    size_t bucket_count;
    std::atomic<size_t> element_count;
    double load_factor_threshold;
    mutable std::shared_mutex global_mutex;

    size_t bucket_index(const K& key) const {
        return std::hash<K>{}(key) % bucket_count;
    }

    void resize() {
        std::unique_lock<std::shared_mutex> global_lock(global_mutex);
        if (element_count.load(std::memory_order_relaxed) <=
            static_cast<size_t>(bucket_count * load_factor_threshold)) {
            return;
        }

        const size_t new_bucket_count = bucket_count * 2;
        std::vector<std::unique_ptr<Bucket>> new_buckets(new_bucket_count);
        for (size_t i = 0; i < new_bucket_count; ++i) {
            new_buckets[i] = std::make_unique<Bucket>();
        }

        for (size_t i = 0; i < bucket_count; ++i) {
            for (auto& entry : buckets[i]->entries) {
                const size_t new_index = std::hash<K>{}(entry.first) % new_bucket_count;
                new_buckets[new_index]->entries.push_back(std::move(entry));
            }
        }

        buckets = std::move(new_buckets);
        bucket_count = new_bucket_count;
    }

public:
    explicit ConcurrentHashMap(size_t num_buckets = 16, double load_factor = 0.75)
        : bucket_count(num_buckets),
          element_count(0),
          load_factor_threshold(load_factor) {
        buckets.resize(num_buckets);
        for (size_t i = 0; i < num_buckets; ++i) {
            buckets[i] = std::make_unique<Bucket>();
        }
    }

    void put(const K& key, const V& value) {
        bool needs_resize = false;
        {
            std::shared_lock<std::shared_mutex> global_lock(global_mutex);
            Bucket& bucket = *buckets[bucket_index(key)];
            std::unique_lock<std::shared_mutex> bucket_lock(bucket.mutex);

            for (auto& entry : bucket.entries) {
                if (entry.first == key) {
                    entry.second = value;
                    return;
                }
            }

            bucket.entries.emplace_back(key, value);
            const size_t count = element_count.fetch_add(1, std::memory_order_relaxed) + 1;
            needs_resize = count > static_cast<size_t>(bucket_count * load_factor_threshold);
        }

        if (needs_resize) {
            resize();
        }
    }

    std::optional<V> get(const K& key) const {
        std::shared_lock<std::shared_mutex> global_lock(global_mutex);
        const Bucket& bucket = *buckets[bucket_index(key)];
        std::shared_lock<std::shared_mutex> bucket_lock(bucket.mutex);

        for (const auto& entry : bucket.entries) {
            if (entry.first == key) {
                return entry.second;
            }
        }
        return std::nullopt;
    }

    bool remove(const K& key) {
        std::shared_lock<std::shared_mutex> global_lock(global_mutex);
        Bucket& bucket = *buckets[bucket_index(key)];
        std::unique_lock<std::shared_mutex> bucket_lock(bucket.mutex);

        for (auto it = bucket.entries.begin(); it != bucket.entries.end(); ++it) {
            if (it->first == key) {
                bucket.entries.erase(it);
                element_count.fetch_sub(1, std::memory_order_relaxed);
                return true;
            }
        }
        return false;
    }

    size_t size() const {
        return element_count.load(std::memory_order_relaxed);
    }

    size_t get_bucket_count() const {
        std::shared_lock<std::shared_mutex> global_lock(global_mutex);
        return bucket_count;
    }
};

#endif

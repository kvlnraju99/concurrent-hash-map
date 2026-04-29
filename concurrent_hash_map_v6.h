#ifndef CONCURRENT_HASH_MAP_V6_H
#define CONCURRENT_HASH_MAP_V6_H

#include <vector>
#include <mutex>
#include <optional>
#include <atomic>
#include <omp.h>

/**
 * @brief V6: Segmented Hash Map — Java ConcurrentHashMap-style design.
 * 
 * Partitioned locking eliminates the global shared_mutex contention.
 * Fixed to prevent deadlocks and use-after-free during resize.
 */
template <typename K, typename V>
class ConcurrentHashMapV6 {
private:
    struct Node {
        K key;
        V value;
        Node* next;
        Node(K k, V v, Node* n) : key(k), value(v), next(n) {}
    };

    struct alignas(64) Bucket {
        Node* head = nullptr;
        std::mutex mtx;
    };

    struct alignas(64) Segment {
        std::vector<Bucket*> buckets;
        size_t bucket_count;
        std::atomic<size_t> element_count{0};
        std::atomic<bool> resizing{false};
        std::mutex resize_mutex;
        std::atomic<size_t> resize_gen{0};
        std::vector<Bucket*> garbage;

        explicit Segment(size_t num_buckets) : bucket_count(num_buckets) {
            buckets.resize(bucket_count);
            for (size_t i = 0; i < bucket_count; ++i)
                buckets[i] = new Bucket();
        }

        ~Segment() {
            for (auto b : buckets) {
                Node* curr = b->head;
                while (curr) {
                    Node* tmp = curr;
                    curr = curr->next;
                    delete tmp;
                }
                delete b;
            }
            for (auto b : garbage) delete b;
        }

        void wait_for_resize() const {
            while (resizing.load(std::memory_order_acquire)) {}
        }

        void check_and_resize() {
            if (element_count.load(std::memory_order_relaxed) <= bucket_count) return;
            if (!resize_mutex.try_lock()) return;
            if (element_count.load() <= bucket_count) {
                resize_mutex.unlock();
                return;
            }

            resizing.store(true, std::memory_order_release);

            // Lock all buckets in this segment
            for (size_t i = 0; i < bucket_count; ++i)
                buckets[i]->mtx.lock();

            size_t new_count = bucket_count * 2;
            std::vector<Bucket*> new_buckets(new_count);
            for (size_t i = 0; i < new_count; ++i)
                new_buckets[i] = new Bucket();

            for (size_t i = 0; i < bucket_count; ++i) {
                Node* curr = buckets[i]->head;
                while (curr) {
                    Node* next = curr->next;
                    size_t new_idx = std::hash<K>{}(curr->key) % new_count;
                    curr->next = new_buckets[new_idx]->head;
                    new_buckets[new_idx]->head = curr;
                    curr = next;
                }
                buckets[i]->head = nullptr;
            }

            // Atomic update of bucket array
            std::vector<Bucket*> old_buckets = std::move(buckets);
            buckets = std::move(new_buckets);
            bucket_count = new_count;
            resize_gen.fetch_add(1, std::memory_order_release);
            resizing.store(false, std::memory_order_release);

            // Unlock and defer deletion
            for (auto b : old_buckets) b->mtx.unlock();
            for (auto b : old_buckets) garbage.push_back(b);

            resize_mutex.unlock();
        }
    };

    std::vector<Segment*> segments;
    size_t segment_count;
    size_t segment_shift;

    size_t get_segment_index(const K& key) const {
        size_t h = std::hash<K>{}(key);
        return (h >> segment_shift) % segment_count;
    }

    size_t get_bucket_index(const K& key, size_t bcount) const {
        return std::hash<K>{}(key) % bcount;
    }

public:
    explicit ConcurrentHashMapV6(size_t total_buckets = 131072, size_t num_segments = 64)
        : segment_count(num_segments), segment_shift(16) {
        size_t buckets_per_segment = std::max<size_t>(total_buckets / segment_count, 16);
        segments.resize(segment_count);
        for (size_t i = 0; i < segment_count; ++i)
            segments[i] = new Segment(buckets_per_segment);
    }

    ~ConcurrentHashMapV6() {
        for (auto seg : segments) delete seg;
    }

    void put(const K& key, const V& value) {
        size_t seg_idx = get_segment_index(key);
        Segment* seg = segments[seg_idx];

        while (true) {
            seg->wait_for_resize();
            size_t gen = seg->resize_gen.load(std::memory_order_acquire);
            size_t bkt_idx = get_bucket_index(key, seg->bucket_count);
            Bucket* bkt = seg->buckets[bkt_idx];
            bool needs_resize = false;
            {
                std::lock_guard<std::mutex> lock(bkt->mtx);
                if (seg->resize_gen.load(std::memory_order_acquire) != gen) continue;

                Node* curr = bkt->head;
                while (curr) {
                    if (curr->key == key) { curr->value = value; return; }
                    curr = curr->next;
                }
                bkt->head = new Node(key, value, bkt->head);
                seg->element_count.fetch_add(1, std::memory_order_relaxed);
                needs_resize = (seg->element_count.load(std::memory_order_relaxed) > seg->bucket_count);
            }
            if (needs_resize) seg->check_and_resize();
            return;
        }
    }

    std::optional<V> get(const K& key) const {
        size_t seg_idx = get_segment_index(key);
        Segment* seg = segments[seg_idx];

        while (true) {
            seg->wait_for_resize();
            size_t gen = seg->resize_gen.load(std::memory_order_acquire);
            size_t bkt_idx = get_bucket_index(key, seg->bucket_count);
            Bucket* bkt = seg->buckets[bkt_idx];
            std::lock_guard<std::mutex> lock(bkt->mtx);
            if (seg->resize_gen.load(std::memory_order_acquire) != gen) continue;

            Node* curr = bkt->head;
            while (curr) {
                if (curr->key == key) return curr->value;
                curr = curr->next;
            }
            return std::nullopt;
        }
    }

    template<typename F>
    void update(const K& key, F updater) {
        size_t seg_idx = get_segment_index(key);
        Segment* seg = segments[seg_idx];

        while (true) {
            seg->wait_for_resize();
            size_t gen = seg->resize_gen.load(std::memory_order_acquire);
            size_t bkt_idx = get_bucket_index(key, seg->bucket_count);
            Bucket* bkt = seg->buckets[bkt_idx];
            bool needs_resize = false;
            {
                std::lock_guard<std::mutex> lock(bkt->mtx);
                if (seg->resize_gen.load(std::memory_order_acquire) != gen) continue;

                Node* curr = bkt->head;
                while (curr) {
                    if (curr->key == key) {
                        curr->value = updater(std::optional<V>(curr->value));
                        return;
                    }
                    curr = curr->next;
                }
                bkt->head = new Node(key, updater(std::nullopt), bkt->head);
                seg->element_count.fetch_add(1, std::memory_order_relaxed);
                needs_resize = (seg->element_count.load(std::memory_order_relaxed) > seg->bucket_count);
            }
            if (needs_resize) seg->check_and_resize();
            return;
        }
    }

    size_t size() const {
        size_t total = 0;
        for (auto seg : segments) total += seg->element_count.load();
        return total;
    }

    V sum_all_values() const {
        V total = 0;
        for (auto seg : segments) {
            seg->wait_for_resize();
            for (size_t i = 0; i < seg->bucket_count; ++i) {
                std::lock_guard<std::mutex> lock(seg->buckets[i]->mtx);
                Node* curr = seg->buckets[i]->head;
                while (curr) {
                    total += curr->value;
                    curr = curr->next;
                }
            }
        }
        return total;
    }
};

#endif

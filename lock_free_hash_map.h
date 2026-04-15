#ifndef LOCK_FREE_HASH_MAP_H
#define LOCK_FREE_HASH_MAP_H

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

// A mostly lock-free hash map using linked-list buckets and CAS updates.
//
// Design notes:
//   - Regular put/get/remove operations work through atomic bucket heads.
//   - Resizing is coordinated separately: threads briefly pause while a new
//     bucket table is built and atomically published.
//   - Old tables stay alive until destruction, which avoids reclamation races.
//
// Known limitations (acceptable for a course project):
//   - Resize coordination is not lock-free; threads spin while resize runs.
//   - Old tables and copied nodes are reclaimed only in the destructor.
//   - Concurrent puts of the same NEW key may still create duplicates.

template <typename K, typename V>
class LockFreeHashMap {
private:
    using ProfileClock = std::chrono::steady_clock;

public:
    struct PutProfileSnapshot {
        uint64_t put_calls = 0;
        uint64_t successful_inserts = 0;
        uint64_t updated_existing = 0;
        uint64_t hash_ns = 0;
        uint64_t traversal_ns = 0;
        uint64_t allocation_ns = 0;
        uint64_t cas_ns = 0;
        uint64_t bookkeeping_ns = 0;
        uint64_t total_ns = 0;
        uint64_t traversal_nodes = 0;
        uint64_t max_chain_length_seen = 0;
        uint64_t cas_attempts = 0;
        uint64_t cas_failures = 0;
    };

    struct BucketStatsSnapshot {
        uint64_t total_nodes = 0;
        uint64_t live_nodes = 0;
        uint64_t deleted_nodes = 0;
        uint64_t non_empty_buckets = 0;
        uint64_t max_chain_length = 0;
    };

private:
    struct PutProfiler {
        std::atomic<uint64_t> put_calls{0};
        std::atomic<uint64_t> successful_inserts{0};
        std::atomic<uint64_t> updated_existing{0};
        std::atomic<uint64_t> hash_ns{0};
        std::atomic<uint64_t> traversal_ns{0};
        std::atomic<uint64_t> allocation_ns{0};
        std::atomic<uint64_t> cas_ns{0};
        std::atomic<uint64_t> bookkeeping_ns{0};
        std::atomic<uint64_t> total_ns{0};
        std::atomic<uint64_t> traversal_nodes{0};
        std::atomic<uint64_t> max_chain_length_seen{0};
        std::atomic<uint64_t> cas_attempts{0};
        std::atomic<uint64_t> cas_failures{0};
    };

    struct Node {
        K key;
        V value;
        std::atomic<bool> deleted;
        std::atomic<Node*> next;

        Node(const K& k, const V& v)
            : key(k), value(v), deleted(false), next(nullptr) {
        }
    };

    struct alignas(64) Bucket {
        std::atomic<Node*> head;
        Bucket() : head(nullptr) {
        }
    };

    struct Table {
        std::unique_ptr<Bucket[]> buckets;
        size_t bucket_count;
        std::atomic<size_t> active_ops;

        explicit Table(size_t count)
            : buckets(new Bucket[count]), bucket_count(count), active_ops(0) {
        }
    };

    struct TableGuard {
        Table* table = nullptr;

        TableGuard() = default;
        explicit TableGuard(Table* t) : table(t) {
        }

        TableGuard(const TableGuard&) = delete;
        TableGuard& operator=(const TableGuard&) = delete;

        TableGuard(TableGuard&& other) noexcept : table(other.table) {
            other.table = nullptr;
        }

        TableGuard& operator=(TableGuard&& other) noexcept {
            if (this != &other) {
                release();
                table = other.table;
                other.table = nullptr;
            }
            return *this;
        }

        ~TableGuard() {
            release();
        }

        void release() {
            if (table != nullptr) {
                table->active_ops.fetch_sub(1, std::memory_order_release);
                table = nullptr;
            }
        }

        explicit operator bool() const {
            return table != nullptr;
        }
    };

    std::atomic<Table*> current_table;
    std::vector<Table*> tables_for_cleanup;
    std::atomic<size_t> element_count;
    std::unique_ptr<PutProfiler> put_profiler;
    double max_load_factor;
    std::atomic<bool> resize_in_progress;
    mutable std::shared_mutex resize_mutex;

    static uint64_t elapsed_ns(const ProfileClock::time_point& start,
                               const ProfileClock::time_point& end) {
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());
    }

    static void update_max(std::atomic<uint64_t>& target, uint64_t candidate) {
        uint64_t current = target.load(std::memory_order_relaxed);
        while (current < candidate &&
               !target.compare_exchange_weak(current, candidate,
                                             std::memory_order_relaxed,
                                             std::memory_order_relaxed)) {
        }
    }

    void record_put_profile(const PutProfileSnapshot& sample) {
        if (!put_profiler) {
            return;
        }

        put_profiler->put_calls.fetch_add(sample.put_calls, std::memory_order_relaxed);
        put_profiler->successful_inserts.fetch_add(sample.successful_inserts,
                                                   std::memory_order_relaxed);
        put_profiler->updated_existing.fetch_add(sample.updated_existing,
                                                 std::memory_order_relaxed);
        put_profiler->hash_ns.fetch_add(sample.hash_ns, std::memory_order_relaxed);
        put_profiler->traversal_ns.fetch_add(sample.traversal_ns, std::memory_order_relaxed);
        put_profiler->allocation_ns.fetch_add(sample.allocation_ns, std::memory_order_relaxed);
        put_profiler->cas_ns.fetch_add(sample.cas_ns, std::memory_order_relaxed);
        put_profiler->bookkeeping_ns.fetch_add(sample.bookkeeping_ns,
                                               std::memory_order_relaxed);
        put_profiler->total_ns.fetch_add(sample.total_ns, std::memory_order_relaxed);
        put_profiler->traversal_nodes.fetch_add(sample.traversal_nodes,
                                                std::memory_order_relaxed);
        put_profiler->cas_attempts.fetch_add(sample.cas_attempts, std::memory_order_relaxed);
        put_profiler->cas_failures.fetch_add(sample.cas_failures, std::memory_order_relaxed);
        update_max(put_profiler->max_chain_length_seen, sample.max_chain_length_seen);
    }

    static bool unlink_deleted_node(std::atomic<Node*>* link, Node* curr, Node* next) {
        return link->compare_exchange_strong(
            curr, next,
            std::memory_order_release,
            std::memory_order_acquire);
    }

    static size_t get_bucket_index(const K& key, size_t bucket_count) {
        return std::hash<K>{}(key) % bucket_count;
    }

    TableGuard acquire_table() const {
        while (true) {
            while (resize_in_progress.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }

            Table* table = current_table.load(std::memory_order_acquire);
            table->active_ops.fetch_add(1, std::memory_order_acquire);

            if (table == current_table.load(std::memory_order_acquire) &&
                !resize_in_progress.load(std::memory_order_acquire)) {
                return TableGuard(table);
            }

            table->active_ops.fetch_sub(1, std::memory_order_release);
        }
    }

    void copy_live_nodes(Table* source, Table* target) {
        for (size_t i = 0; i < source->bucket_count; ++i) {
            Node* curr = source->buckets[i].head.load(std::memory_order_acquire);
            while (curr) {
                Node* next = curr->next.load(std::memory_order_acquire);
                if (!curr->deleted.load(std::memory_order_acquire)) {
                    const size_t idx = get_bucket_index(curr->key, target->bucket_count);
                    Node* copy = new Node(curr->key, curr->value);
                    Node* head = target->buckets[idx].head.load(std::memory_order_relaxed);
                    copy->next.store(head, std::memory_order_relaxed);
                    target->buckets[idx].head.store(copy, std::memory_order_relaxed);
                }
                curr = next;
            }
        }
    }

    void maybe_resize(Table* observed_table) {
        if (observed_table == nullptr) {
            return;
        }
        if (element_count.load(std::memory_order_relaxed) <=
            static_cast<size_t>(observed_table->bucket_count * max_load_factor)) {
            return;
        }

        std::unique_lock<std::shared_mutex> lock(resize_mutex);
        Table* current = current_table.load(std::memory_order_acquire);
        if (current != observed_table) {
            return;
        }
        if (element_count.load(std::memory_order_relaxed) <=
            static_cast<size_t>(current->bucket_count * max_load_factor)) {
            return;
        }

        resize_in_progress.store(true, std::memory_order_release);
        while (current->active_ops.load(std::memory_order_acquire) != 0) {
            std::this_thread::yield();
        }

        Table* grown = new Table(current->bucket_count * 2);
        copy_live_nodes(current, grown);
        current_table.store(grown, std::memory_order_release);
        tables_for_cleanup.push_back(grown);
        resize_in_progress.store(false, std::memory_order_release);
    }

public:
    explicit LockFreeHashMap(size_t num_buckets = 16,
                             bool enable_put_profiling = false,
                             double load_factor = 0.75)
        : current_table(new Table(num_buckets == 0 ? 1 : num_buckets)),
          element_count(0),
          put_profiler(enable_put_profiling ? std::make_unique<PutProfiler>() : nullptr),
          max_load_factor(load_factor),
          resize_in_progress(false) {
        tables_for_cleanup.push_back(current_table.load(std::memory_order_relaxed));
    }

    ~LockFreeHashMap() {
        for (Table* table : tables_for_cleanup) {
            for (size_t i = 0; i < table->bucket_count; ++i) {
                Node* curr = table->buckets[i].head.load(std::memory_order_relaxed);
                while (curr) {
                    Node* next = curr->next.load(std::memory_order_relaxed);
                    delete curr;
                    curr = next;
                }
            }
            delete table;
        }
    }

    void put(const K& key, const V& value) {
        const bool profiling_enabled = (put_profiler != nullptr);
        PutProfileSnapshot sample;
        const auto total_start = profiling_enabled ? ProfileClock::now() : ProfileClock::time_point{};

        auto guard = acquire_table();
        Table* table = guard.table;

        const auto hash_start = profiling_enabled ? ProfileClock::now() : ProfileClock::time_point{};
        const size_t idx = get_bucket_index(key, table->bucket_count);
        if (profiling_enabled) {
            sample.hash_ns = elapsed_ns(hash_start, ProfileClock::now());
        }

        const auto traversal_start = profiling_enabled ? ProfileClock::now() : ProfileClock::time_point{};
        std::atomic<Node*>* link = &table->buckets[idx].head;
        Node* curr = link->load(std::memory_order_acquire);
        uint64_t chain_length = 0;
        while (curr) {
            Node* next = curr->next.load(std::memory_order_acquire);
            if (curr->deleted.load(std::memory_order_acquire)) {
                if (unlink_deleted_node(link, curr, next)) {
                    curr = next;
                    continue;
                }
                curr = link->load(std::memory_order_acquire);
                continue;
            }

            ++chain_length;
            if (curr->key == key) {
                curr->value = value;
                if (profiling_enabled) {
                    sample.put_calls = 1;
                    sample.updated_existing = 1;
                    sample.traversal_nodes = chain_length;
                    sample.max_chain_length_seen = chain_length;
                    sample.traversal_ns = elapsed_ns(traversal_start, ProfileClock::now());
                    sample.total_ns = elapsed_ns(total_start, ProfileClock::now());
                    record_put_profile(sample);
                }
                return;
            }
            link = &curr->next;
            curr = next;
        }
        if (profiling_enabled) {
            sample.traversal_nodes = chain_length;
            sample.max_chain_length_seen = chain_length;
            sample.traversal_ns = elapsed_ns(traversal_start, ProfileClock::now());
        }

        const auto allocation_start = profiling_enabled ? ProfileClock::now() : ProfileClock::time_point{};
        Node* new_node = new Node(key, value);
        if (profiling_enabled) {
            sample.allocation_ns = elapsed_ns(allocation_start, ProfileClock::now());
        }
        Node* head = table->buckets[idx].head.load(std::memory_order_acquire);

        const auto cas_start = profiling_enabled ? ProfileClock::now() : ProfileClock::time_point{};
        bool inserted = false;
        while (!inserted) {
            if (profiling_enabled) {
                ++sample.cas_attempts;
            }
            new_node->next.store(head, std::memory_order_relaxed);
            inserted = table->buckets[idx].head.compare_exchange_weak(
                head, new_node,
                std::memory_order_release,
                std::memory_order_acquire);
            if (profiling_enabled && !inserted) {
                ++sample.cas_failures;
            }
        }
        if (profiling_enabled) {
            sample.cas_ns = elapsed_ns(cas_start, ProfileClock::now());
        }

        const auto bookkeeping_start = profiling_enabled ? ProfileClock::now() : ProfileClock::time_point{};
        const size_t count = element_count.fetch_add(1, std::memory_order_relaxed) + 1;
        if (profiling_enabled) {
            sample.put_calls = 1;
            sample.successful_inserts = 1;
            sample.bookkeeping_ns = elapsed_ns(bookkeeping_start, ProfileClock::now());
            sample.total_ns = elapsed_ns(total_start, ProfileClock::now());
            record_put_profile(sample);
        }

        guard.release();
        if (count > static_cast<size_t>(table->bucket_count * max_load_factor)) {
            maybe_resize(table);
        }
    }

    std::optional<V> get(const K& key) const {
        auto guard = acquire_table();
        Table* table = guard.table;
        const size_t idx = get_bucket_index(key, table->bucket_count);

        std::atomic<Node*>* link = &table->buckets[idx].head;
        Node* curr = link->load(std::memory_order_acquire);
        while (curr) {
            Node* next = curr->next.load(std::memory_order_acquire);
            if (curr->deleted.load(std::memory_order_acquire)) {
                if (unlink_deleted_node(link, curr, next)) {
                    curr = next;
                    continue;
                }
                curr = link->load(std::memory_order_acquire);
                continue;
            }

            if (curr->key == key) {
                return curr->value;
            }
            link = &curr->next;
            curr = next;
        }

        return std::nullopt;
    }

    bool remove(const K& key) {
        auto guard = acquire_table();
        Table* table = guard.table;
        const size_t idx = get_bucket_index(key, table->bucket_count);

        std::atomic<Node*>* link = &table->buckets[idx].head;
        Node* curr = link->load(std::memory_order_acquire);
        while (curr) {
            Node* next = curr->next.load(std::memory_order_acquire);
            if (curr->deleted.load(std::memory_order_acquire)) {
                if (unlink_deleted_node(link, curr, next)) {
                    curr = next;
                    continue;
                }
                curr = link->load(std::memory_order_acquire);
                continue;
            }

            if (curr->key == key) {
                bool expected = false;
                if (curr->deleted.compare_exchange_strong(
                        expected, true,
                        std::memory_order_release,
                        std::memory_order_acquire)) {
                    element_count.fetch_sub(1, std::memory_order_relaxed);
                    unlink_deleted_node(link, curr, next);
                    return true;
                }
            }
            link = &curr->next;
            curr = next;
        }

        return false;
    }

    size_t size() const {
        return element_count.load(std::memory_order_relaxed);
    }

    size_t get_bucket_count() const {
        return current_table.load(std::memory_order_acquire)->bucket_count;
    }

    bool put_profiling_enabled() const {
        return put_profiler != nullptr;
    }

    void reset_put_profile() {
        if (!put_profiler) {
            return;
        }

        put_profiler->put_calls.store(0, std::memory_order_relaxed);
        put_profiler->successful_inserts.store(0, std::memory_order_relaxed);
        put_profiler->updated_existing.store(0, std::memory_order_relaxed);
        put_profiler->hash_ns.store(0, std::memory_order_relaxed);
        put_profiler->traversal_ns.store(0, std::memory_order_relaxed);
        put_profiler->allocation_ns.store(0, std::memory_order_relaxed);
        put_profiler->cas_ns.store(0, std::memory_order_relaxed);
        put_profiler->bookkeeping_ns.store(0, std::memory_order_relaxed);
        put_profiler->total_ns.store(0, std::memory_order_relaxed);
        put_profiler->traversal_nodes.store(0, std::memory_order_relaxed);
        put_profiler->max_chain_length_seen.store(0, std::memory_order_relaxed);
        put_profiler->cas_attempts.store(0, std::memory_order_relaxed);
        put_profiler->cas_failures.store(0, std::memory_order_relaxed);
    }

    PutProfileSnapshot get_put_profile() const {
        PutProfileSnapshot snapshot;
        if (!put_profiler) {
            return snapshot;
        }

        snapshot.put_calls = put_profiler->put_calls.load(std::memory_order_relaxed);
        snapshot.successful_inserts =
            put_profiler->successful_inserts.load(std::memory_order_relaxed);
        snapshot.updated_existing =
            put_profiler->updated_existing.load(std::memory_order_relaxed);
        snapshot.hash_ns = put_profiler->hash_ns.load(std::memory_order_relaxed);
        snapshot.traversal_ns = put_profiler->traversal_ns.load(std::memory_order_relaxed);
        snapshot.allocation_ns = put_profiler->allocation_ns.load(std::memory_order_relaxed);
        snapshot.cas_ns = put_profiler->cas_ns.load(std::memory_order_relaxed);
        snapshot.bookkeeping_ns =
            put_profiler->bookkeeping_ns.load(std::memory_order_relaxed);
        snapshot.total_ns = put_profiler->total_ns.load(std::memory_order_relaxed);
        snapshot.traversal_nodes =
            put_profiler->traversal_nodes.load(std::memory_order_relaxed);
        snapshot.max_chain_length_seen =
            put_profiler->max_chain_length_seen.load(std::memory_order_relaxed);
        snapshot.cas_attempts = put_profiler->cas_attempts.load(std::memory_order_relaxed);
        snapshot.cas_failures = put_profiler->cas_failures.load(std::memory_order_relaxed);
        return snapshot;
    }

    BucketStatsSnapshot get_bucket_stats() const {
        BucketStatsSnapshot snapshot;
        auto guard = acquire_table();
        Table* table = guard.table;

        for (size_t i = 0; i < table->bucket_count; ++i) {
            uint64_t chain_length = 0;
            Node* curr = table->buckets[i].head.load(std::memory_order_acquire);
            if (curr != nullptr) {
                ++snapshot.non_empty_buckets;
            }

            while (curr) {
                ++snapshot.total_nodes;
                ++chain_length;
                if (curr->deleted.load(std::memory_order_acquire)) {
                    ++snapshot.deleted_nodes;
                } else {
                    ++snapshot.live_nodes;
                }
                curr = curr->next.load(std::memory_order_acquire);
            }

            if (chain_length > snapshot.max_chain_length) {
                snapshot.max_chain_length = chain_length;
            }
        }

        return snapshot;
    }
};

#endif // LOCK_FREE_HASH_MAP_H

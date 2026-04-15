#ifndef LOCK_FREE_HASH_MAP_H
#define LOCK_FREE_HASH_MAP_H

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

// Experimental lock-free-ish hash map with cooperative dynamic resizing.
//
// Design notes:
//   - Buckets remain linked lists updated with CAS.
//   - When load factor is exceeded, a larger table is published as next_table.
//   - Threads touching an old bucket help migrate that bucket into next_table,
//     then continue operating against the new table for that key.
//   - Old tables remain alive until destruction, so migration never races with
//     reclamation.
//
// Known limitations:
//   - Memory reclamation is deferred until destruction.
//   - Concurrent puts of the same NEW key may still create duplicates.
//   - Migration is cooperative but still experimental; performance is not tuned.

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
        std::unique_ptr<std::atomic<uint8_t>[]> bucket_state;
        size_t bucket_count;
        std::atomic<Table*> next_table;
        std::atomic<size_t> migrated_buckets;

        explicit Table(size_t count)
            : buckets(new Bucket[count]),
              bucket_state(new std::atomic<uint8_t>[count]),
              bucket_count(count),
              next_table(nullptr),
              migrated_buckets(0) {
            for (size_t i = 0; i < bucket_count; ++i) {
                bucket_state[i].store(0, std::memory_order_relaxed);
            }
        }
    };

    std::atomic<Table*> current_table;
    std::vector<Table*> tables_for_cleanup;
    std::atomic<size_t> element_count;
    std::unique_ptr<PutProfiler> put_profiler;
    double max_load_factor;

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

    static uint8_t bucket_state(Table* table, size_t idx) {
        return table->bucket_state[idx].load(std::memory_order_acquire);
    }

    static void debug_log(const char* event,
                          const void* table,
                          size_t bucket_count,
                          size_t key_hash,
                          size_t detail) {
#ifdef LF_RESIZE_DEBUG
        std::fprintf(stderr,
                     "[lf-resize] %s table=%p buckets=%zu key_hash=%zu detail=%zu\n",
                     event, table, bucket_count, key_hash, detail);
#else
        (void)event;
        (void)table;
        (void)bucket_count;
        (void)key_hash;
        (void)detail;
#endif
    }

    void promote_table_if_ready(Table* table) {
        Table* next = table->next_table.load(std::memory_order_acquire);
        if (next == nullptr) {
            return;
        }
        if (table->migrated_buckets.load(std::memory_order_acquire) != table->bucket_count) {
            return;
        }
        if (current_table.compare_exchange_strong(table, next,
                                                  std::memory_order_release,
                                                  std::memory_order_acquire)) {
            debug_log("promote", next, next->bucket_count, 0, table->bucket_count);
        }
    }

    bool insert_or_update_in_bucket(Bucket& bucket, const K& key, const V& value,
                                    PutProfileSnapshot* sample) {
        std::atomic<Node*>* link = &bucket.head;
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
                if (sample != nullptr) {
                    sample->updated_existing = 1;
                    sample->traversal_nodes = chain_length;
                    sample->max_chain_length_seen = chain_length;
                }
                return false;
            }
            link = &curr->next;
            curr = next;
        }

        if (sample != nullptr) {
            sample->traversal_nodes = chain_length;
            sample->max_chain_length_seen = chain_length;
        }

        const auto allocation_start = sample != nullptr ? ProfileClock::now() : ProfileClock::time_point{};
        Node* new_node = new Node(key, value);
        if (sample != nullptr) {
            sample->allocation_ns += elapsed_ns(allocation_start, ProfileClock::now());
        }

        Node* head = bucket.head.load(std::memory_order_acquire);
        const auto cas_start = sample != nullptr ? ProfileClock::now() : ProfileClock::time_point{};
        bool inserted = false;
        while (!inserted) {
            if (sample != nullptr) {
                ++sample->cas_attempts;
            }
            new_node->next.store(head, std::memory_order_relaxed);
            inserted = bucket.head.compare_exchange_weak(head, new_node,
                                                         std::memory_order_release,
                                                         std::memory_order_acquire);
            if (sample != nullptr && !inserted) {
                ++sample->cas_failures;
            }
        }
        if (sample != nullptr) {
            sample->cas_ns += elapsed_ns(cas_start, ProfileClock::now());
            sample->successful_inserts = 1;
        }
        return true;
    }

    bool insert_or_update(Table* table, const K& key, const V& value,
                          PutProfileSnapshot* sample) {
        const auto hash_start = sample != nullptr ? ProfileClock::now() : ProfileClock::time_point{};
        const size_t idx = get_bucket_index(key, table->bucket_count);
        if (sample != nullptr) {
            sample->hash_ns += elapsed_ns(hash_start, ProfileClock::now());
        }
        const auto traversal_start = sample != nullptr ? ProfileClock::now() : ProfileClock::time_point{};
        const bool inserted = insert_or_update_in_bucket(table->buckets[idx], key, value, sample);
        if (sample != nullptr) {
            sample->traversal_ns += elapsed_ns(traversal_start, ProfileClock::now()) - sample->allocation_ns - sample->cas_ns;
        }
        return inserted;
    }

    void copy_bucket_live_nodes(Table* source, size_t idx, Table* target) {
        Node* curr = source->buckets[idx].head.load(std::memory_order_acquire);
        while (curr) {
            Node* next = curr->next.load(std::memory_order_acquire);
            if (!curr->deleted.load(std::memory_order_acquire)) {
                const size_t target_idx = get_bucket_index(curr->key, target->bucket_count);
                (void)insert_or_update_in_bucket(target->buckets[target_idx], curr->key, curr->value, nullptr);
            }
            curr = next;
        }
    }

    void help_migrate_bucket(Table* table, size_t idx) {
        Table* next = table->next_table.load(std::memory_order_acquire);
        if (next == nullptr) {
            return;
        }

        uint8_t expected = 0;
        if (table->bucket_state[idx].compare_exchange_strong(expected, 1,
                                                             std::memory_order_acq_rel,
                                                             std::memory_order_acquire)) {
            debug_log("migrate-begin", table, table->bucket_count, idx, 0);
            copy_bucket_live_nodes(table, idx, next);
            table->bucket_state[idx].store(2, std::memory_order_release);
            const size_t migrated =
                table->migrated_buckets.fetch_add(1, std::memory_order_release) + 1;
            debug_log("migrate-end", next, next->bucket_count, idx, migrated);
            promote_table_if_ready(table);
            return;
        }

        while (bucket_state(table, idx) != 2) {
            std::this_thread::yield();
        }
        promote_table_if_ready(table);
    }

    Table* ensure_current_target(const K& key) {
        while (true) {
            Table* table = current_table.load(std::memory_order_acquire);
            while (true) {
                Table* next = table->next_table.load(std::memory_order_acquire);
                if (next == nullptr) {
                    return table;
                }

                const size_t idx = get_bucket_index(key, table->bucket_count);
                help_migrate_bucket(table, idx);
                Table* observed_current = current_table.load(std::memory_order_acquire);
                if (table == observed_current) {
                    table = next;
                    continue;
                }
                break;
            }
        }
    }

    void maybe_start_resize(Table* table) {
        if (table == nullptr) {
            return;
        }
        if (table != current_table.load(std::memory_order_acquire)) {
            return;
        }
        if (element_count.load(std::memory_order_relaxed) <=
            static_cast<size_t>(table->bucket_count * max_load_factor)) {
            return;
        }
        if (table->next_table.load(std::memory_order_acquire) != nullptr) {
            return;
        }

        Table* grown = new Table(table->bucket_count * 2);
        Table* expected = nullptr;
        if (table->next_table.compare_exchange_strong(expected, grown,
                                                      std::memory_order_release,
                                                      std::memory_order_acquire)) {
            tables_for_cleanup.push_back(grown);
            debug_log("resize-start", grown, grown->bucket_count, 0, table->bucket_count);
        } else {
            delete grown;
        }
    }

    std::optional<V> get_from_table(Table* table, const K& key) const {
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

    bool remove_from_table(Table* table, const K& key) {
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
                if (curr->deleted.compare_exchange_strong(expected, true,
                                                         std::memory_order_release,
                                                         std::memory_order_acquire)) {
                    unlink_deleted_node(link, curr, next);
                    return true;
                }
            }
            link = &curr->next;
            curr = next;
        }
        return false;
    }

public:
    explicit LockFreeHashMap(size_t num_buckets = 16,
                             bool enable_put_profiling = false,
                             double load_factor = 0.75)
        : current_table(new Table(num_buckets == 0 ? 1 : num_buckets)),
          element_count(0),
          put_profiler(enable_put_profiling ? std::make_unique<PutProfiler>() : nullptr),
          max_load_factor(load_factor) {
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
        const auto total_start = profiling_enabled ? ProfileClock::now() : ProfileClock::time_point{};

        while (true) {
            Table* table = ensure_current_target(key);
            PutProfileSnapshot sample;
            if (profiling_enabled) {
                sample.put_calls = 1;
            }

            const size_t before_alloc = sample.allocation_ns;
            const size_t before_cas = sample.cas_ns;
            const auto traversal_start = profiling_enabled ? ProfileClock::now() : ProfileClock::time_point{};
            const bool inserted = insert_or_update(table, key, value, profiling_enabled ? &sample : nullptr);
            if (profiling_enabled) {
                const uint64_t section_ns = elapsed_ns(traversal_start, ProfileClock::now());
                const uint64_t alloc_delta = sample.allocation_ns - before_alloc;
                const uint64_t cas_delta = sample.cas_ns - before_cas;
                sample.traversal_ns += (section_ns > alloc_delta + cas_delta)
                    ? (section_ns - alloc_delta - cas_delta)
                    : 0;
            }

            // If resize started while we were writing, mirror the final value
            // into the destination table before returning.
            Table* next = table->next_table.load(std::memory_order_acquire);
            if (next != nullptr) {
                const size_t idx = get_bucket_index(key, table->bucket_count);
                help_migrate_bucket(table, idx);
                (void)insert_or_update(next, key, value, nullptr);
                debug_log("mirror-insert", next, next->bucket_count, idx, 0);
            }

            if (inserted) {
                const auto bookkeeping_start = profiling_enabled ? ProfileClock::now() : ProfileClock::time_point{};
                element_count.fetch_add(1, std::memory_order_relaxed);
                if (profiling_enabled) {
                    sample.bookkeeping_ns = elapsed_ns(bookkeeping_start, ProfileClock::now());
                }
                maybe_start_resize(table);
            }

            if (profiling_enabled) {
                sample.total_ns = elapsed_ns(total_start, ProfileClock::now());
                record_put_profile(sample);
            }
            return;
        }
    }

    std::optional<V> get(const K& key) const {
        Table* table = const_cast<LockFreeHashMap*>(this)->ensure_current_target(key);
        return get_from_table(table, key);
    }

    bool remove(const K& key) {
        Table* table = ensure_current_target(key);
        const bool removed = remove_from_table(table, key);
        if (removed) {
            element_count.fetch_sub(1, std::memory_order_relaxed);
        }
        return removed;
    }

    size_t size() const {
        return element_count.load(std::memory_order_relaxed);
    }

    size_t get_bucket_count() const {
        Table* table = current_table.load(std::memory_order_acquire);
        Table* next = table->next_table.load(std::memory_order_acquire);
        return next != nullptr ? next->bucket_count : table->bucket_count;
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
        Table* table = current_table.load(std::memory_order_acquire);

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

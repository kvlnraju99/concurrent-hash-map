#ifndef LOCK_FREE_HASH_MAP_H
#define LOCK_FREE_HASH_MAP_H

#include <atomic>
#include <cstddef>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <string>
#include <utility>

// A lock-free hash map using atomic pointers and Compare-And-Swap (CAS).
//
// How it works:
//   - Each bucket is an atomic pointer to a linked list of nodes.
//   - PUT: prepend a new node using CAS (no lock needed).
//   - GET: just walk the list (reads are naturally safe).
//   - REMOVE: mark the node as deleted using an atomic flag.
//
// The fast path uses only atomics. A small mutex is used only when the
// chunk allocator needs to grow its backing storage.
//
// Known limitations (acceptable for a course project):
//   - No resizing (fixed bucket count).
//   - Deleted nodes are unlinked eagerly but memory is still reclaimed
//     only when the map is destroyed.
//   - Concurrent puts of the same NEW key may create duplicates
//     (last write wins on read, so behavior is still correct).

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

    // A node in the per-bucket linked list.
    struct Node {
        K key;
        V value;
        std::atomic<bool> deleted;    // true = logically removed
        std::atomic<Node*> next;      // pointer to next node in chain

        Node(const K& k, const V& v)
            : key(k), value(v), deleted(false), next(nullptr) {}
    };

    struct alignas(64) Bucket {
        std::atomic<Node*> head;
        Bucket() : head(nullptr) {}
    };

    struct NodeChunk {
        std::unique_ptr<std::byte[]> storage;
        std::atomic<size_t> next_index;
        size_t capacity;
        NodeChunk* next;

        explicit NodeChunk(size_t chunk_capacity)
            : storage(std::make_unique<std::byte[]>(sizeof(Node) * chunk_capacity)),
              next_index(0),
              capacity(chunk_capacity),
              next(nullptr) {
        }

        Node* slot(size_t index) const {
            return std::launder(
                reinterpret_cast<Node*>(storage.get() + index * sizeof(Node)));
        }
    };

    std::unique_ptr<Bucket[]> buckets;  // array of aligned bucket head pointers
    size_t bucket_count;
    std::atomic<size_t> element_count;
    std::unique_ptr<PutProfiler> put_profiler;
    size_t node_chunk_capacity;
    std::atomic<NodeChunk*> active_chunk;
    NodeChunk* chunk_list_head;
    mutable std::mutex chunk_mutex;

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

    NodeChunk* ensure_chunk() {
        NodeChunk* chunk = active_chunk.load(std::memory_order_acquire);
        if (chunk != nullptr) {
            return chunk;
        }

        std::lock_guard<std::mutex> lock(chunk_mutex);
        chunk = active_chunk.load(std::memory_order_acquire);
        if (chunk == nullptr) {
            chunk = new NodeChunk(node_chunk_capacity);
            chunk->next = chunk_list_head;
            chunk_list_head = chunk;
            active_chunk.store(chunk, std::memory_order_release);
        }
        return chunk;
    }

    Node* allocate_node(const K& key, const V& value) {
        while (true) {
            NodeChunk* chunk = ensure_chunk();
            size_t index = chunk->next_index.load(std::memory_order_relaxed);

            while (index < chunk->capacity) {
                if (chunk->next_index.compare_exchange_weak(
                        index, index + 1,
                        std::memory_order_relaxed,
                        std::memory_order_relaxed)) {
                    return new (chunk->slot(index)) Node(key, value);
                }
            }

            std::lock_guard<std::mutex> lock(chunk_mutex);
            NodeChunk* current = active_chunk.load(std::memory_order_acquire);
            if (current == chunk &&
                current->next_index.load(std::memory_order_relaxed) >= current->capacity) {
                NodeChunk* next_chunk = new NodeChunk(node_chunk_capacity);
                next_chunk->next = chunk_list_head;
                chunk_list_head = next_chunk;
                active_chunk.store(next_chunk, std::memory_order_release);
            }
        }
    }

    static bool unlink_deleted_node(std::atomic<Node*>* link, Node* curr, Node* next) {
        return link->compare_exchange_strong(
            curr, next,
            std::memory_order_release,
            std::memory_order_acquire);
    }

    // Pick the bucket for a given key.
    size_t get_bucket_index(const K& key) const {
        return std::hash<K>{}(key) % bucket_count;
    }

public:

    explicit LockFreeHashMap(size_t num_buckets = 16,
                             bool enable_put_profiling = false,
                             size_t node_chunk_size = 4096)
        : buckets(new Bucket[num_buckets]),
          bucket_count(num_buckets), element_count(0),
          put_profiler(enable_put_profiling ? std::make_unique<PutProfiler>() : nullptr),
          node_chunk_capacity(node_chunk_size == 0 ? 1 : node_chunk_size),
          active_chunk(nullptr),
          chunk_list_head(nullptr) {
    }

    // Clean up all nodes.
    ~LockFreeHashMap() {
        NodeChunk* chunk = chunk_list_head;
        while (chunk) {
            const size_t constructed =
                std::min(chunk->next_index.load(std::memory_order_relaxed), chunk->capacity);
            for (size_t i = 0; i < constructed; ++i) {
                chunk->slot(i)->~Node();
            }
            NodeChunk* next = chunk->next;
            delete chunk;
            chunk = next;
        }
    }

    // Insert or update a key-value pair.
    // Uses CAS to prepend new nodes without any lock.
    void put(const K& key, const V& value) {
        const bool profiling_enabled = (put_profiler != nullptr);
        PutProfileSnapshot sample;
        const auto total_start = profiling_enabled ? ProfileClock::now() : ProfileClock::time_point{};

        const auto hash_start = profiling_enabled ? ProfileClock::now() : ProfileClock::time_point{};
        size_t idx = get_bucket_index(key);
        if (profiling_enabled) {
            sample.hash_ns = elapsed_ns(hash_start, ProfileClock::now());
        }

        // Walk the list to see if the key already exists.
        const auto traversal_start =
            profiling_enabled ? ProfileClock::now() : ProfileClock::time_point{};
        std::atomic<Node*>* link = &buckets[idx].head;
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
                // Key found — update the value in place.
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

        // Key not found — create a new node and prepend it using CAS.
        const auto allocation_start =
            profiling_enabled ? ProfileClock::now() : ProfileClock::time_point{};
        Node* new_node = allocate_node(key, value);
        if (profiling_enabled) {
            sample.allocation_ns = elapsed_ns(allocation_start, ProfileClock::now());
        }
        Node* head = buckets[idx].head.load(std::memory_order_acquire);

        const auto cas_start = profiling_enabled ? ProfileClock::now() : ProfileClock::time_point{};
        bool inserted = false;
        while (!inserted) {
            if (profiling_enabled) {
                ++sample.cas_attempts;
            }
            // Point new node's next to the current head.
            new_node->next.store(head, std::memory_order_relaxed);

            // CAS: if head hasn't changed, swap it to our new node.
            // If another thread changed head, CAS fails and we retry.
            inserted = buckets[idx].head.compare_exchange_weak(
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

        const auto bookkeeping_start =
            profiling_enabled ? ProfileClock::now() : ProfileClock::time_point{};
        element_count.fetch_add(1, std::memory_order_relaxed);
        if (profiling_enabled) {
            sample.put_calls = 1;
            sample.successful_inserts = 1;
            sample.bookkeeping_ns = elapsed_ns(bookkeeping_start, ProfileClock::now());
            sample.total_ns = elapsed_ns(total_start, ProfileClock::now());
            record_put_profile(sample);
        }
    }

    // Look up a key. No locks needed — just read atomic pointers.
    std::optional<V> get(const K& key) const {
        size_t idx = get_bucket_index(key);

        // Walk the list, reading atomic pointers.
        std::atomic<Node*>* link = const_cast<std::atomic<Node*>*>(&buckets[idx].head);
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

    // Remove a key by marking it as deleted (logical deletion).
    // The node stays in memory but is skipped by get().
    bool remove(const K& key) {
        size_t idx = get_bucket_index(key);

        std::atomic<Node*>* link = &buckets[idx].head;
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
                // Mark as deleted. Other threads will skip this node.
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

    // Return the current number of live elements.
    size_t size() const {
        return element_count.load(std::memory_order_relaxed);
    }

    // Return the number of buckets (fixed, never changes).
    size_t get_bucket_count() const {
        return bucket_count;
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

        for (size_t i = 0; i < bucket_count; ++i) {
            uint64_t chain_length = 0;
            Node* curr = buckets[i].head.load(std::memory_order_acquire);
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

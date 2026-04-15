#ifndef LOCK_FREE_OPEN_ADDRESSING_HASH_MAP_H
#define LOCK_FREE_OPEN_ADDRESSING_HASH_MAP_H

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <type_traits>
#include <vector>

// Experimental fixed-size lock-free-ish hash map using open addressing.
//
// Design notes:
//   - Slots live in one contiguous table (linear probing).
//   - Slot state transitions are coordinated with atomics.
//   - This version intentionally avoids resizing while correctness is validated.
//   - To keep the implementation manageable, keys and values must be integral.
//
// Limitations:
//   - Fixed capacity (no dynamic resizing yet).
//   - Full tables may reject new inserts.
//   - Concurrent inserts of the same new key can still race in edge cases.

template <typename K, typename V>
class LockFreeOpenAddressingHashMap {
    static_assert(std::is_integral_v<K>, "experimental open-addressing map requires integral keys");
    static_assert(std::is_integral_v<V>, "experimental open-addressing map requires integral values");

public:
    struct StatsSnapshot {
        uint64_t occupied_slots = 0;
        uint64_t deleted_slots = 0;
        uint64_t empty_slots = 0;
        uint64_t put_calls = 0;
        uint64_t get_calls = 0;
        uint64_t remove_calls = 0;
        uint64_t failed_puts = 0;
        uint64_t total_put_probes = 0;
        uint64_t total_get_probes = 0;
        uint64_t total_remove_probes = 0;
        uint64_t max_put_probe = 0;
        uint64_t max_get_probe = 0;
        uint64_t max_remove_probe = 0;
    };

private:
    enum SlotState : uint8_t {
        EMPTY = 0,
        WRITING = 1,
        OCCUPIED = 2,
        DELETED = 3
    };

    struct alignas(64) Slot {
        std::atomic<uint8_t> state;
        std::atomic<K> key;
        std::atomic<V> value;

        Slot() : state(EMPTY), key(K{}), value(V{}) {
        }
    };

    std::vector<Slot> slots;
    size_t capacity;
    std::atomic<size_t> element_count;
    std::atomic<size_t> deleted_count;
    mutable std::atomic<uint64_t> put_calls;
    mutable std::atomic<uint64_t> get_calls;
    mutable std::atomic<uint64_t> remove_calls;
    mutable std::atomic<uint64_t> failed_puts;
    mutable std::atomic<uint64_t> total_put_probes;
    mutable std::atomic<uint64_t> total_get_probes;
    mutable std::atomic<uint64_t> total_remove_probes;
    mutable std::atomic<uint64_t> max_put_probe;
    mutable std::atomic<uint64_t> max_get_probe;
    mutable std::atomic<uint64_t> max_remove_probe;
    mutable std::shared_mutex table_mutex;

    size_t probe_index(const K& key, size_t step) const {
        return (std::hash<K>{}(key) + step) % capacity;
    }

    static void update_max(std::atomic<uint64_t>& target, uint64_t candidate) {
        uint64_t current = target.load(std::memory_order_relaxed);
        while (current < candidate &&
               !target.compare_exchange_weak(current, candidate,
                                             std::memory_order_relaxed,
                                             std::memory_order_relaxed)) {
        }
    }

    void record_probe(std::atomic<uint64_t>& calls,
                      std::atomic<uint64_t>& total_probes,
                      std::atomic<uint64_t>& max_probe,
                      uint64_t probes) const {
        calls.fetch_add(1, std::memory_order_relaxed);
        total_probes.fetch_add(probes, std::memory_order_relaxed);
        update_max(max_probe, probes);
    }

    bool try_claim_slot(Slot& slot, uint8_t expected_state, const K& key, const V& value) {
        uint8_t expected = expected_state;
        if (!slot.state.compare_exchange_strong(
                expected, WRITING,
                std::memory_order_acq_rel,
                std::memory_order_acquire)) {
            return false;
        }

        slot.key.store(key, std::memory_order_relaxed);
        slot.value.store(value, std::memory_order_relaxed);
        slot.state.store(OCCUPIED, std::memory_order_release);
        element_count.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    void insert_into_table(std::vector<Slot>& table, const K& key, const V& value) {
        const size_t table_capacity = table.size();
        for (size_t step = 0; step < table_capacity; ++step) {
            Slot& slot = table[(std::hash<K>{}(key) + step) % table_capacity];
            const uint8_t state = slot.state.load(std::memory_order_relaxed);
            if (state == OCCUPIED) {
                if (slot.key.load(std::memory_order_relaxed) == key) {
                    slot.value.store(value, std::memory_order_relaxed);
                    return;
                }
                continue;
            }

            slot.key.store(key, std::memory_order_relaxed);
            slot.value.store(value, std::memory_order_relaxed);
            slot.state.store(OCCUPIED, std::memory_order_relaxed);
            return;
        }
    }

    bool should_rebuild() const {
        const size_t deleted = deleted_count.load(std::memory_order_relaxed);
        const size_t live = element_count.load(std::memory_order_relaxed);
        return deleted >= capacity / 16 && deleted >= live / 2;
    }

    bool rebuild_if_needed() {
        if (!should_rebuild()) {
            return false;
        }

        std::unique_lock<std::shared_mutex> lock(table_mutex);
        if (!should_rebuild()) {
            return false;
        }

        std::vector<Slot> rebuilt(capacity);
        size_t live_entries = 0;
        for (const Slot& slot : slots) {
            if (slot.state.load(std::memory_order_acquire) == OCCUPIED) {
                insert_into_table(rebuilt,
                                  slot.key.load(std::memory_order_relaxed),
                                  slot.value.load(std::memory_order_relaxed));
                ++live_entries;
            }
        }

        slots.swap(rebuilt);
        element_count.store(live_entries, std::memory_order_relaxed);
        deleted_count.store(0, std::memory_order_relaxed);
        return true;
    }

public:
    explicit LockFreeOpenAddressingHashMap(size_t num_slots = 1024)
        : slots(num_slots == 0 ? 1 : num_slots),
          capacity(num_slots == 0 ? 1 : num_slots),
          element_count(0),
          deleted_count(0),
          put_calls(0),
          get_calls(0),
          remove_calls(0),
          failed_puts(0),
          total_put_probes(0),
          total_get_probes(0),
          total_remove_probes(0),
          max_put_probe(0),
          max_get_probe(0),
          max_remove_probe(0) {
    }

    bool put(const K& key, const V& value) {
        rebuild_if_needed();
        std::shared_lock<std::shared_mutex> lock(table_mutex);
        size_t first_reusable = capacity;
        uint8_t first_reusable_state = EMPTY;
        uint64_t probes = 0;

        for (size_t step = 0; step < capacity; ++step) {
            ++probes;
            Slot& slot = slots[probe_index(key, step)];
            uint8_t state = slot.state.load(std::memory_order_acquire);

            if (state == OCCUPIED) {
                if (slot.key.load(std::memory_order_acquire) == key) {
                    slot.value.store(value, std::memory_order_release);
                    return true;
                }
                continue;
            }

            if (state == WRITING) {
                continue;
            }

            if ((state == DELETED || state == EMPTY) && first_reusable == capacity) {
                first_reusable = probe_index(key, step);
                first_reusable_state = state;
            }

            if (state == EMPTY) {
                Slot& target = slots[first_reusable];
                while (true) {
                    if (try_claim_slot(target, first_reusable_state, key, value)) {
                        if (first_reusable_state == DELETED) {
                            deleted_count.fetch_sub(1, std::memory_order_relaxed);
                        }
                        record_probe(put_calls, total_put_probes, max_put_probe, probes);
                        return true;
                    }

                    state = target.state.load(std::memory_order_acquire);
                    if (state == OCCUPIED &&
                        target.key.load(std::memory_order_acquire) == key) {
                        target.value.store(value, std::memory_order_release);
                        record_probe(put_calls, total_put_probes, max_put_probe, probes);
                        return true;
                    }

                    if (state == EMPTY || state == DELETED) {
                        first_reusable_state = state;
                        continue;
                    }
                    break;
                }
            }
        }

        if (first_reusable != capacity) {
            Slot& target = slots[first_reusable];
            while (true) {
                if (try_claim_slot(target, first_reusable_state, key, value)) {
                    if (first_reusable_state == DELETED) {
                        deleted_count.fetch_sub(1, std::memory_order_relaxed);
                    }
                    record_probe(put_calls, total_put_probes, max_put_probe, probes);
                    return true;
                }

                const uint8_t state = target.state.load(std::memory_order_acquire);
                if (state == OCCUPIED &&
                    target.key.load(std::memory_order_acquire) == key) {
                    target.value.store(value, std::memory_order_release);
                    record_probe(put_calls, total_put_probes, max_put_probe, probes);
                    return true;
                }
                if (state == EMPTY || state == DELETED) {
                    first_reusable_state = state;
                    continue;
                }
                break;
            }
        }

        record_probe(put_calls, total_put_probes, max_put_probe, probes);
        failed_puts.fetch_add(1, std::memory_order_relaxed);
        lock.unlock();
        rebuild_if_needed();
        return false;
    }

    std::optional<V> get(const K& key) const {
        const_cast<LockFreeOpenAddressingHashMap*>(this)->rebuild_if_needed();
        std::shared_lock<std::shared_mutex> lock(table_mutex);
        for (size_t step = 0; step < capacity; ++step) {
            const Slot& slot = slots[probe_index(key, step)];
            const uint8_t state = slot.state.load(std::memory_order_acquire);

            if (state == EMPTY) {
                record_probe(get_calls, total_get_probes, max_get_probe, step + 1);
                return std::nullopt;
            }
            if (state != OCCUPIED) {
                continue;
            }

            if (slot.key.load(std::memory_order_acquire) == key) {
                record_probe(get_calls, total_get_probes, max_get_probe, step + 1);
                return slot.value.load(std::memory_order_acquire);
            }
        }

        record_probe(get_calls, total_get_probes, max_get_probe, capacity);
        return std::nullopt;
    }

    bool remove(const K& key) {
        rebuild_if_needed();
        std::shared_lock<std::shared_mutex> lock(table_mutex);
        for (size_t step = 0; step < capacity; ++step) {
            Slot& slot = slots[probe_index(key, step)];
            uint8_t state = slot.state.load(std::memory_order_acquire);

            if (state == EMPTY) {
                record_probe(remove_calls, total_remove_probes, max_remove_probe, step + 1);
                return false;
            }
            if (state != OCCUPIED) {
                continue;
            }
            if (slot.key.load(std::memory_order_acquire) != key) {
                continue;
            }

            uint8_t expected = OCCUPIED;
            if (slot.state.compare_exchange_strong(
                    expected, DELETED,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire)) {
                element_count.fetch_sub(1, std::memory_order_relaxed);
                deleted_count.fetch_add(1, std::memory_order_relaxed);
                record_probe(remove_calls, total_remove_probes, max_remove_probe, step + 1);
                lock.unlock();
                rebuild_if_needed();
                return true;
            }
        }

        record_probe(remove_calls, total_remove_probes, max_remove_probe, capacity);
        return false;
    }

    size_t size() const {
        return element_count.load(std::memory_order_relaxed);
    }

    size_t get_bucket_count() const {
        return capacity;
    }

    StatsSnapshot get_stats() const {
        StatsSnapshot stats;
        stats.put_calls = put_calls.load(std::memory_order_relaxed);
        stats.get_calls = get_calls.load(std::memory_order_relaxed);
        stats.remove_calls = remove_calls.load(std::memory_order_relaxed);
        stats.failed_puts = failed_puts.load(std::memory_order_relaxed);
        stats.total_put_probes = total_put_probes.load(std::memory_order_relaxed);
        stats.total_get_probes = total_get_probes.load(std::memory_order_relaxed);
        stats.total_remove_probes = total_remove_probes.load(std::memory_order_relaxed);
        stats.max_put_probe = max_put_probe.load(std::memory_order_relaxed);
        stats.max_get_probe = max_get_probe.load(std::memory_order_relaxed);
        stats.max_remove_probe = max_remove_probe.load(std::memory_order_relaxed);

        std::shared_lock<std::shared_mutex> lock(table_mutex);
        for (const Slot& slot : slots) {
            const uint8_t state = slot.state.load(std::memory_order_acquire);
            if (state == OCCUPIED) {
                ++stats.occupied_slots;
            } else if (state == DELETED) {
                ++stats.deleted_slots;
            } else if (state == EMPTY) {
                ++stats.empty_slots;
            }
        }

        return stats;
    }
};

#endif // LOCK_FREE_OPEN_ADDRESSING_HASH_MAP_H

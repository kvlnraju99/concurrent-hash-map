#ifndef LOCK_FREE_OPEN_ADDRESSING_HASH_MAP_H
#define LOCK_FREE_OPEN_ADDRESSING_HASH_MAP_H

#include <atomic>
#include <cstdint>
#include <functional>
#include <optional>
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

    size_t probe_index(const K& key, size_t step) const {
        return (std::hash<K>{}(key) + step) % capacity;
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

public:
    explicit LockFreeOpenAddressingHashMap(size_t num_slots = 1024)
        : slots(num_slots == 0 ? 1 : num_slots),
          capacity(num_slots == 0 ? 1 : num_slots),
          element_count(0) {
    }

    bool put(const K& key, const V& value) {
        size_t first_reusable = capacity;
        uint8_t first_reusable_state = EMPTY;

        for (size_t step = 0; step < capacity; ++step) {
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
                break;
            }
        }

        if (first_reusable == capacity) {
            return false;
        }

        Slot& target = slots[first_reusable];
        while (true) {
            if (try_claim_slot(target, first_reusable_state, key, value)) {
                return true;
            }

            uint8_t state = target.state.load(std::memory_order_acquire);
            if (state == OCCUPIED && target.key.load(std::memory_order_acquire) == key) {
                target.value.store(value, std::memory_order_release);
                return true;
            }
            if (state == EMPTY || state == DELETED) {
                first_reusable_state = state;
                continue;
            }
            break;
        }

        for (size_t step = 0; step < capacity; ++step) {
            Slot& slot = slots[probe_index(key, step)];
            const uint8_t state = slot.state.load(std::memory_order_acquire);

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

            if (try_claim_slot(slot, state, key, value)) {
                return true;
            }
        }

        return false;
    }

    std::optional<V> get(const K& key) const {
        for (size_t step = 0; step < capacity; ++step) {
            const Slot& slot = slots[probe_index(key, step)];
            const uint8_t state = slot.state.load(std::memory_order_acquire);

            if (state == EMPTY) {
                return std::nullopt;
            }
            if (state != OCCUPIED) {
                continue;
            }

            if (slot.key.load(std::memory_order_acquire) == key) {
                return slot.value.load(std::memory_order_acquire);
            }
        }

        return std::nullopt;
    }

    bool remove(const K& key) {
        for (size_t step = 0; step < capacity; ++step) {
            Slot& slot = slots[probe_index(key, step)];
            uint8_t state = slot.state.load(std::memory_order_acquire);

            if (state == EMPTY) {
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
                return true;
            }
        }

        return false;
    }

    size_t size() const {
        return element_count.load(std::memory_order_relaxed);
    }

    size_t get_bucket_count() const {
        return capacity;
    }
};

#endif // LOCK_FREE_OPEN_ADDRESSING_HASH_MAP_H

/*
 *
 * Copyright 2017 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

/**
 * @file atomic.h
 * @brief Type-safe wrapper around std::atomic with a custom MemoryOrder enum.
 */

#pragma once
#include <stdint.h>
#include <atomic>

/**
 * @brief Strongly-typed enumeration mapping to std::memory_order values.
 */
enum class MemoryOrder {
    RELAXED = std::memory_order_relaxed,   /**< No ordering constraints. */
    CONSUME = std::memory_order_consume,   /**< Consume ordering for data dependencies. */
    ACQUIRE = std::memory_order_acquire,   /**< Acquire fence (reads before this are not reordered). */
    RELEASE = std::memory_order_release,   /**< Release fence (writes after this are not reordered). */
    ACQ_REL = std::memory_order_acq_rel,   /**< Combined acquire-release fence. */
    SEQ_CST = std::memory_order_seq_cst    /**< Sequentially consistent ordering (strictest). */
};

/**
 * @brief Type-safe wrapper around std::atomic with MemoryOrder enum parameters.
 * @tparam T The underlying atomic type (e.g., bool, int32_t, uint64_t, pointer).
 */
template <typename T>
class Atomic {
public:
    /**
     * @brief Constructor. Initializes the atomic value.
     * @param val Initial value (default-constructed if omitted).
     */
    explicit Atomic(T val = T())
        : _value(val) {}

    /**
     * @brief Atomically loads and returns the current value.
     * @param order Memory ordering constraint.
     * @return The current value.
     */
    T Load(MemoryOrder order = MemoryOrder::RELAXED) const {
        return _value.load(static_cast<std::memory_order>(order));
    }

    /**
     * @brief Atomically stores a value.
     * @param val The value to store.
     * @param order Memory ordering constraint.
     */
    void Store(T val, MemoryOrder order = MemoryOrder::RELAXED) {
        _value.store(val, static_cast<std::memory_order>(order));
    }

    /**
     * @brief Atomically replaces the current value and returns the old value.
     * @param desired The new value to store.
     * @param order Memory ordering constraint.
     * @return The previous value.
     */
    T Exchange(T desired, MemoryOrder order) {
        return _value.exchange(desired, static_cast<std::memory_order>(order));
    }

    /**
     * @brief Atomically compares and exchanges (weak version, may spuriously fail).
     * @param expected Pointer to the expected value (updated on failure).
     * @param desired The new value to store if comparison succeeds.
     * @param success Memory order used when the exchange succeeds.
     * @param failure Memory order used when the exchange fails.
     * @return True if the exchange succeeded.
     */
    bool CompareExchangeWeak(T *expected, T desired, MemoryOrder success, MemoryOrder failure) {
        return _value.compare_exchange_weak(*expected, desired, static_cast<std::memory_order>(success),
                                            static_cast<std::memory_order>(failure));
    }

    /**
     * @brief Atomically compares and exchanges (strong version, never spurious failures).
     * @param expected Pointer to the expected value (updated on failure).
     * @param desired The new value to store if comparison succeeds.
     * @param success Memory order used when the exchange succeeds.
     * @param failure Memory order used when the exchange fails.
     * @return True if the exchange succeeded.
     */
    bool CompareExchangeStrong(T *expected, T desired, MemoryOrder success, MemoryOrder failure) {
        return _value.compare_exchange_strong(*expected, desired, static_cast<std::memory_order>(success),
                                              static_cast<std::memory_order>(failure));
    }

    /**
     * @brief Atomically adds arg to the current value and returns the old value.
     * @param arg The value to add.
     * @param order Memory ordering constraint.
     * @return The previous value.
     */
    template <typename Arg>
    T FetchAdd(Arg arg, MemoryOrder order = MemoryOrder::SEQ_CST) {
        return _value.fetch_add(static_cast<Arg>(arg), static_cast<std::memory_order>(order));
    }

    /**
     * @brief Atomically subtracts arg from the current value and returns the old value.
     * @param arg The value to subtract.
     * @param order Memory ordering constraint.
     * @return The previous value.
     */
    template <typename Arg>
    T FetchSub(Arg arg, MemoryOrder order = MemoryOrder::SEQ_CST) {
        return _value.fetch_sub(static_cast<Arg>(arg), static_cast<std::memory_order>(order));
    }

    /**
     * @brief Atomically increments the value only if it is currently non-zero.
     * @param load_order Memory ordering for the initial load.
     * @return True if the increment succeeded (value was non-zero), false if it was zero.
     */
    bool IncrementIfNonzero(MemoryOrder load_order = MemoryOrder::ACQUIRE) {
        T count = _value.load(static_cast<std::memory_order>(load_order));
        do {
            if (count == 0) {
                return false;
            }
        } while (!CompareExchangeWeak(&count, count + 1, MemoryOrder::ACQ_REL, load_order));
        return true;
    }

private:
    std::atomic<T> _value;
};

/** @brief Type alias for atomic boolean. */
using AtomicBool = Atomic<bool>;
/** @brief Type alias for atomic signed 32-bit integer. */
using AtomicInt32 = Atomic<int32_t>;
/** @brief Type alias for atomic signed 64-bit integer. */
using AtomicInt64 = Atomic<int64_t>;
/** @brief Type alias for atomic unsigned 32-bit integer. */
using AtomicUInt32 = Atomic<uint32_t>;
/** @brief Type alias for atomic unsigned 64-bit integer. */
using AtomicUInt64 = Atomic<uint64_t>;
/** @brief Type alias for atomic pointer-sized integer. */
using AtomicIntptr = Atomic<intptr_t>;

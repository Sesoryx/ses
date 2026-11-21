#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <type_traits>

namespace ses::engine {

// Fixed-capacity single-producer/single-consumer queue. push/pop never allocate
// or block; one thread must own each side for the lifetime of the ring.
template <typename T, std::size_t Capacity>
class SpscRing {
    static_assert(Capacity > 0);
    static_assert(std::is_trivially_copyable_v<T>, "RT queue entries must be trivially copyable");

public:
    bool push(const T& value) noexcept
    {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        const std::size_t tail = tail_.load(std::memory_order_acquire);
        if (head - tail >= Capacity)
            return false;
        entries_[head % Capacity] = value;
        head_.store(head + 1, std::memory_order_release);
        return true;
    }

    bool pop(T& value) noexcept
    {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        const std::size_t head = head_.load(std::memory_order_acquire);
        if (tail == head)
            return false;
        value = entries_[tail % Capacity];
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }

    static constexpr std::size_t capacity() noexcept { return Capacity; }

private:
    alignas(64) std::atomic<std::size_t> head_{0};
    alignas(64) std::atomic<std::size_t> tail_{0};
    std::array<T, Capacity> entries_{};
};

} // namespace ses::engine

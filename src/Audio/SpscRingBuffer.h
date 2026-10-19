#pragma once

#include <array>
#include <atomic>
#include <cstddef>

namespace anc::audio {

template <typename T, std::size_t Capacity>
class SpscRingBuffer {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");
public:
    bool push(T value) noexcept {
        const auto write = write_.load(std::memory_order_relaxed);
        const auto next = (write + 1) & (Capacity - 1);
        if (next == read_.load(std::memory_order_acquire)) return false;
        data_[write] = value;
        write_.store(next, std::memory_order_release);
        return true;
    }

    bool pop(T& value) noexcept {
        const auto read = read_.load(std::memory_order_relaxed);
        if (read == write_.load(std::memory_order_acquire)) return false;
        value = data_[read];
        read_.store((read + 1) & (Capacity - 1), std::memory_order_release);
        return true;
    }

    std::size_t size() const noexcept {
        return (write_.load(std::memory_order_acquire) - read_.load(std::memory_order_acquire)) & (Capacity - 1);
    }

private:
    std::array<T, Capacity> data_{};
    alignas(64) std::atomic<std::size_t> write_{};
    alignas(64) std::atomic<std::size_t> read_{};
};

} // namespace anc::audio

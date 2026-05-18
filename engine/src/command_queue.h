#pragma once
#include "commands.h"
#include <array>
#include <atomic>

namespace tl {

// Single-producer, single-consumer lock-free ring queue. Size must be a power of 2.
// Capacity is N-1 (one slot reserved to distinguish full from empty).
template <size_t N>
class CommandQueue {
    static_assert((N & (N - 1)) == 0, "N must be a power of two");
public:
    CommandQueue() : head_(0), tail_(0) {}

    bool push(const Command& c) {
        const size_t h = head_.load(std::memory_order_relaxed);
        const size_t next = (h + 1) & (N - 1);
        if (next == tail_.load(std::memory_order_acquire)) return false; // full
        buffer_[h] = c;
        head_.store(next, std::memory_order_release);
        return true;
    }

    bool pop(Command& out) {
        const size_t t = tail_.load(std::memory_order_relaxed);
        if (t == head_.load(std::memory_order_acquire)) return false; // empty
        out = buffer_[t];
        tail_.store((t + 1) & (N - 1), std::memory_order_release);
        return true;
    }

private:
    std::array<Command, N> buffer_;
    std::atomic<size_t> head_;
    std::atomic<size_t> tail_;
};

} // namespace tl

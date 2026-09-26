#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <memory>

namespace osci {

// One editing/publishing thread, one render thread. The render thread borrows
// immutable state at block boundaries; allocation and destruction stay on the
// publishing thread. Stop the render thread before destroying this exchange.
template <typename State, std::size_t RetiredCapacity = 64>
class PreparedState {
    static_assert(RetiredCapacity > 1);
    static_assert(std::atomic<State*>::is_always_lock_free);
    static_assert(std::atomic<std::size_t>::is_always_lock_free);
public:
    PreparedState() = default;
    PreparedState(const PreparedState&) = delete;
    PreparedState& operator=(const PreparedState&) = delete;

    ~PreparedState() {
        delete pending.exchange(nullptr);
        collect();
        delete current;
    }

    // Publisher thread only. Intermediate unpublished edits may be coalesced.
    void publish(std::unique_ptr<State> state) {
        collect();
        delete pending.exchange(state.release(), std::memory_order_acq_rel);
    }

    // Publisher thread only; call periodically even when editing has stopped.
    void collect() {
        auto readIndex = read.load(std::memory_order_relaxed);
        const auto writeIndex = write.load(std::memory_order_acquire);
        while (readIndex != writeIndex) {
            delete retired[readIndex];
            readIndex = (readIndex + 1) % RetiredCapacity;
        }
        read.store(readIndex, std::memory_order_release);
    }

    // Render thread only. No allocation, deletion, lock or unbounded retry. If
    // reclamation lags, keep rendering the current state until space is free.
    const State* acquire() noexcept {
        const auto writeIndex = write.load(std::memory_order_relaxed);
        const auto next = (writeIndex + 1) % RetiredCapacity;
        if (current != nullptr && next == read.load(std::memory_order_acquire)) {
            return current;
        }
        auto* replacement = pending.exchange(nullptr, std::memory_order_acq_rel);
        if (replacement != nullptr) {
            if (current != nullptr) {
                retired[writeIndex] = current;
                write.store(next, std::memory_order_release);
            }
            current = replacement;
        }
        return current;
    }

private:
    std::atomic<State*> pending { nullptr };
    State* current = nullptr;
    std::array<State*, RetiredCapacity> retired {};
    std::atomic<std::size_t> read { 0 };
    std::atomic<std::size_t> write { 0 };
};

}

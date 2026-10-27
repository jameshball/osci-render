#pragma once

#include <atomic>

namespace motion {
// Long preparations poll an optional flag that another thread sets to stop them.
inline bool cancelled(const std::atomic<bool>* cancel) {
    return cancel != nullptr && cancel->load(std::memory_order_relaxed);
}
}

// macOS diagnostic interposer. Counts operations, including nested allocator calls;
// these are not unique allocations and exclude unwrapped APIs and free(nullptr).
#include "allocation_probe.h"

#include <atomic>
#include <cstdlib>
#include <pthread.h>

namespace {
std::atomic<pthread_t> trackedThread { nullptr };
std::atomic<bool> enabled { false };
OsciAllocationCounts counts {};
static_assert(std::atomic<pthread_t>::is_always_lock_free);
static_assert(std::atomic<bool>::is_always_lock_free);

bool tracking() {
    return enabled.load(std::memory_order_relaxed)
        && trackedThread.load(std::memory_order_relaxed) == pthread_self();
}

void* countedMalloc(std::size_t size) {
    if (tracking()) { ++counts.mallocCalls; }
    return malloc(size);
}

void* countedCalloc(std::size_t count, std::size_t size) {
    if (tracking()) { ++counts.callocCalls; }
    return calloc(count, size);
}

void* countedRealloc(void* pointer, std::size_t size) {
    if (tracking()) { ++counts.reallocCalls; }
    return realloc(pointer, size);
}

void countedFree(void* pointer) {
    if (pointer != nullptr && tracking()) { ++counts.freeCalls; }
    free(pointer);
}
}

// Call the control API only from the thread that will execute processBlock.
extern "C" void osci_allocation_probe_reset() {
    enabled.store(false, std::memory_order_relaxed);
    trackedThread.store(pthread_self(), std::memory_order_relaxed);
    counts = {};
}

extern "C" void osci_allocation_probe_set_enabled(int value) {
    enabled.store(value != 0, std::memory_order_relaxed);
}

extern "C" void osci_allocation_probe_read(OsciAllocationCounts* output) {
    *output = counts;
}

#define OSCI_INTERPOSE(replacement, original) \
    __attribute__((used)) static const struct { const void* substitute; const void* replacee; } \
    interpose_##original __attribute__((section("__DATA,__interpose"))) = { \
        reinterpret_cast<const void*>(&replacement), reinterpret_cast<const void*>(&original) }

OSCI_INTERPOSE(countedMalloc, malloc);
OSCI_INTERPOSE(countedCalloc, calloc);
OSCI_INTERPOSE(countedRealloc, realloc);
OSCI_INTERPOSE(countedFree, free);

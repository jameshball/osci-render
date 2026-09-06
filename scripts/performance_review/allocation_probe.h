#pragma once

#include <cstdint>

struct OsciAllocationCounts {
    std::uint64_t mallocCalls, callocCalls, reallocCalls, freeCalls;
};

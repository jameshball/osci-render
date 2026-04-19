/*
    Global operator new/delete overrides for audio-thread allocation detection.
    
    This file MUST be compiled into exactly one translation unit per binary.
    It intercepts all C++ heap allocations and logs a warning
    whenever one happens on a thread that has AudioThreadGuard active.
*/

#include "AudioThreadGuard.h"

#if JUCE_DEBUG && AUDIO_THREAD_GUARD_ENABLED

#include <cstdlib>
#include <cstdio>
#include <new>
#include <execinfo.h>

// We can't use DBG() here because it may itself allocate.
// Use fprintf(stderr, ...) which is async-signal-safe on most platforms.
static void logAudioAllocation(const char* op, std::size_t size) {
    int count = AudioThreadGuard::logCount.fetch_add(1, std::memory_order_relaxed);
    if (count < AudioThreadGuard::kMaxLogs) {
        void* frames[16];
        int numFrames = backtrace(frames, 16);
        fprintf(stderr, "[AudioThreadGuard] %s of %zu bytes on audio thread! Stack:\n", op, size);
        char** symbols = backtrace_symbols(frames, numFrames);
        if (symbols) {
            for (int i = 2; i < numFrames; ++i)
                fprintf(stderr, "  [%d] %s\n", i - 2, symbols[i]);
            free(symbols);
        }
        fprintf(stderr, "\n");
    }
}

void* operator new(std::size_t size) {
    if (AudioThreadGuard::isAudioThread) {
        logAudioAllocation("ALLOC", size);
    }
    void* p = std::malloc(size);
    if (!p) throw std::bad_alloc();
    return p;
}

void* operator new[](std::size_t size) {
    if (AudioThreadGuard::isAudioThread) {
        logAudioAllocation("ALLOC[]", size);
    }
    void* p = std::malloc(size);
    if (!p) throw std::bad_alloc();
    return p;
}

void operator delete(void* ptr) noexcept {
    std::free(ptr);
}

void operator delete[](void* ptr) noexcept {
    std::free(ptr);
}

void operator delete(void* ptr, std::size_t size) noexcept {
    std::free(ptr);
}

void operator delete[](void* ptr, std::size_t size) noexcept {
    std::free(ptr);
}

// nothrow variants
void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    if (AudioThreadGuard::isAudioThread) {
        logAudioAllocation("ALLOC(nothrow)", size);
    }
    return std::malloc(size);
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
    if (AudioThreadGuard::isAudioThread) {
        logAudioAllocation("ALLOC[](nothrow)", size);
    }
    return std::malloc(size);
}

void operator delete(void* ptr, const std::nothrow_t&) noexcept {
    std::free(ptr);
}

void operator delete[](void* ptr, const std::nothrow_t&) noexcept {
    std::free(ptr);
}

#endif // JUCE_DEBUG && AUDIO_THREAD_GUARD_ENABLED

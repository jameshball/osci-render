#pragma once

/*
    Debug-only audio-thread allocation detector.

    To enable, set AUDIO_THREAD_GUARD_ENABLED=1 before including this header,
    or define it as a preprocessor macro in your build configuration.

    Usage:
      1. #define AUDIO_THREAD_GUARD_ENABLED 1  (or set in build settings)
      2. Include this header.
      3. Place `AudioThreadGuard::ScopedAudioThread guard;` at the top
         of every audio-thread callback (processBlock, renderNextBlock, etc.).
      4. In exactly ONE .cpp file, include AudioThreadGuard.cpp which
         provides the global operator new/delete overrides.

    When an allocation occurs on a thread that has the guard active,
    a warning is logged via stderr. The detector does NOT assert.
*/

#ifndef AUDIO_THREAD_GUARD_ENABLED
#define AUDIO_THREAD_GUARD_ENABLED 1
#endif

#if JUCE_DEBUG && AUDIO_THREAD_GUARD_ENABLED

#include <atomic>

namespace AudioThreadGuard {

inline thread_local bool isAudioThread = false;

inline std::atomic<int> logCount{0};
inline constexpr int kMaxLogs = 500;

struct ScopedAudioThread {
    ScopedAudioThread()  { isAudioThread = true; }
    ~ScopedAudioThread() { isAudioThread = false; }

    ScopedAudioThread(const ScopedAudioThread&) = delete;
    ScopedAudioThread& operator=(const ScopedAudioThread&) = delete;
};

} // namespace AudioThreadGuard

#else

namespace AudioThreadGuard {
    inline thread_local bool isAudioThread = false;
    struct ScopedAudioThread {};
}

#endif

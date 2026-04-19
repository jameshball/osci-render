#pragma once
#include <JuceHeader.h>
#include "../Source/lua/LuaParser.h"
#include "../Source/lua/LuaLibrary.h"
#include <lua.hpp>

// ============================================================================
// Shared benchmark infrastructure
// ============================================================================

struct BenchmarkResult {
    double totalSeconds;
    int iterations;

    double perCallNanoseconds() const { return (totalSeconds * 1e9) / iterations; }
    double perCallMicroseconds() const { return (totalSeconds * 1e6) / iterations; }
    double callsPerSecond() const { return iterations / totalSeconds; }
};

inline void logBenchmark(const juce::String& label, const BenchmarkResult& r) {
    juce::Logger::outputDebugString(juce::String::formatted(
        "  %-50s  %10.1f ns/call  %10.0f calls/sec  [%.3f ms total]",
        label.toRawUTF8(),
        r.perCallNanoseconds(),
        r.callsPerSecond(),
        r.totalSeconds * 1000.0));
}

// Iteration counts: Release builds get more iterations for stable results.
// Debug builds are 10-50x slower, so we use fewer iterations.
#ifdef NDEBUG
static constexpr int BENCH_ITERS       = 300000;
static constexpr int BENCH_ITERS_FULL  = 200000;
static constexpr int BENCH_WARMUP      = 5000;
#else
static constexpr int BENCH_ITERS       = 30000;
static constexpr int BENCH_ITERS_FULL  = 20000;
static constexpr int BENCH_WARMUP      = 500;
#endif

inline void logHeader(const juce::String& section) {
    juce::Logger::outputDebugString("");
    juce::Logger::outputDebugString("=== " + section + " ===");
}

inline void logSeparator() {
    juce::Logger::outputDebugString("  " + juce::String::charToString('-').paddedRight('-', 90));
}

// Variable names for benchmarks (matches osci-render's global variable pool)
inline const char* const* getAllVarNames() {
    static const char* names[] = {
        "step", "phase", "sample_rate", "frequency", "cycle_count",
        "slider_a", "slider_b", "slider_c", "slider_d", "slider_e",
        "slider_f", "slider_g", "slider_h", "slider_i", "slider_j",
        "slider_k", "slider_l", "slider_m", "slider_n", "slider_o",
        "slider_p", "slider_q", "slider_r", "slider_s", "slider_t",
        "slider_u", "slider_v", "slider_w", "slider_x", "slider_y",
        "slider_z",
        "ext_x", "ext_y",
        "midi_note", "velocity", "voice_index",
        "bpm", "play_time", "play_time_beats",
        "time_sig_num", "time_sig_den",
        "envelope", "envelope_stage"
    };
    return names;
}

inline int getNumAllVars() {
    return 43; // sizeof(names) / sizeof(names[0]) from getAllVarNames
}

inline void initBenchmarkCallbacks() {
    LuaParser::onPrint = [](const std::string&) {};
    LuaParser::onClear = []() {};
}

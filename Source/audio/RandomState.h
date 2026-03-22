#pragma once

#include <JuceHeader.h>
#include <vector>
#include <cmath>
#include <random>
#include <atomic>
#include "ModAssignment.h"
#include "LfoState.h"

// Number of global Random modulators available.
namespace rng {
    inline constexpr int NUM_RANDOM_SOURCES = 3;
}
using rng::NUM_RANDOM_SOURCES;

// Lock-free SPSC ring buffer for passing subsampled random values from
// the audio thread to the UI thread.  One writer (audio), one reader (UI).
struct RandomUIRingBuffer {
    struct Entry {
        float value;
        bool active;
    };

    static constexpr int kCapacity = 1024;

    // Audio thread writes here.
    void push(float value, bool active) {
        int w = writePos.load(std::memory_order_relaxed);
        values[w].store(value, std::memory_order_relaxed);
        actives[w].store(active, std::memory_order_relaxed);
        // Release-store: guarantees the data writes above are visible to the
        // reader before the updated write position.
        writePos.store((w + 1) & (kCapacity - 1), std::memory_order_release);
    }

    // UI thread drains all available entries into the supplied array.
    // Returns the number of entries read.
    int drain(Entry* out, int maxEntries) {
        int r = readPos.load(std::memory_order_relaxed);
        // Acquire-load: synchronises with the writer's release-store,
        // guaranteeing visibility of all data stores that preceded it.
        int w = writePos.load(std::memory_order_acquire);
        int count = 0;
        while (r != w && count < maxEntries) {
            out[count++] = { values[r].load(std::memory_order_relaxed),
                             actives[r].load(std::memory_order_relaxed) };
            r = (r + 1) & (kCapacity - 1);
        }
        readPos.store(r, std::memory_order_relaxed);
        return count;
    }

private:
    std::atomic<float> values[kCapacity] = {};
    std::atomic<bool> actives[kCapacity] = {};
    std::atomic<int> writePos { 0 };
    std::atomic<int> readPos { 0 };
};

// RandomAssignment is a type alias for the generic ModAssignment.
// XML serialisation uses "rng" as the index attribute name.
using RandomAssignment = ModAssignment;

// Random modulator interpolation styles (mirrors Vital).
enum class RandomStyle {
    Perlin,           // Smooth Perlin-like noise
    SampleAndHold,    // Stepped random values
    SineInterpolate,  // Cosine interpolation between random targets
};

inline juce::String randomStyleToString(RandomStyle style) {
    switch (style) {
        case RandomStyle::Perlin:           return "Perlin";
        case RandomStyle::SampleAndHold:    return "Sample & Hold";
        case RandomStyle::SineInterpolate:  return "Sine Interpolate";
    }
    return "Perlin";
}

inline RandomStyle stringToRandomStyle(const juce::String& s) {
    if (s == "Perlin")            return RandomStyle::Perlin;
    if (s == "Sample & Hold")     return RandomStyle::SampleAndHold;
    if (s == "Sine Interpolate")  return RandomStyle::SineInterpolate;
    return RandomStyle::Perlin;
}

// Audio-thread state for a single Random modulator.
// Generates random modulation values at a configurable rate.
// Responds to MIDI notes similarly to LFO (starts on note-on, stops after release).
struct RandomAudioState {
    RandomStyle style = RandomStyle::Perlin;  // Current interpolation style
    float phase = 0.0f;          // [0, 1) advances at rate Hz
    float currentValue = 0.5f;   // Current output [0, 1]
    float prevTarget = 0.5f;     // Previous random target
    float nextTarget = 0.5f;     // Current random target to interpolate towards
    bool active = false;         // Whether modulation is running
    bool noteHeld = false;
    bool released = false;
    bool finished = false;

    // Simple xorshift RNG for audio thread (no allocation, fast)
    uint32_t rngState = 0x12345678u;

    float nextRandom() {
        rngState ^= rngState << 13;
        rngState ^= rngState >> 17;
        rngState ^= rngState << 5;
        return (float)(rngState & 0x7FFFFFFF) / (float)0x7FFFFFFF;
    }

    void advanceBlock(float* output, int numSamples, float rateHz, float sampleRate) {
        if (sampleRate <= 0.0f || !active) {
            for (int s = 0; s < numSamples; ++s)
                output[s] = currentValue;
            return;
        }

        float phaseInc = rateHz / sampleRate;

        switch (style) {
            case RandomStyle::Perlin:
                for (int s = 0; s < numSamples; ++s)
                    output[s] = advancePerlin(phaseInc);
                return;
            case RandomStyle::SampleAndHold:
                for (int s = 0; s < numSamples; ++s)
                    output[s] = advanceSampleAndHold(phaseInc);
                return;
            case RandomStyle::SineInterpolate:
                for (int s = 0; s < numSamples; ++s)
                    output[s] = advanceSineInterpolate(phaseInc);
                return;
        }
    }

    void noteOn() {
        noteHeld = true;
        released = false;
        finished = false;
        active = true;

        // Generate new random target on note-on
        float newTarget = nextRandom();
        if (style == RandomStyle::SampleAndHold) {
            // SampleAndHold: immediately snap to the new value
            currentValue = newTarget;
            prevTarget = newTarget;
            nextTarget = newTarget;
        } else {
            prevTarget = currentValue;
            nextTarget = newTarget;
        }
        phase = 0.0f;
    }

    void noteOff() {
        noteHeld = false;
        released = true;
    }

    void voicesFinished() {
        if (!released) return;
        finished = true;
        active = false;
    }

    void reset() {
        phase = 0.0f;
        currentValue = 0.5f;
        prevTarget = 0.5f;
        nextTarget = 0.5f;
        active = false;
        noteHeld = false;
        released = false;
        finished = false;
    }

    void seed(uint32_t s) {
        rngState = s;
        if (rngState == 0) rngState = 0x12345678u;
    }

private:
    // Smooth interpolation using cubic Hermite (approximates Perlin smoothness)
    float advancePerlin(float phaseInc) {
        phase += phaseInc;
        if (phase >= 1.0f) {
            phase -= std::floor(phase);
            prevTarget = nextTarget;
            nextTarget = nextRandom();
        }
        // Smoothstep interpolation for Perlin-like smoothness
        float t = phase;
        float smooth = t * t * (3.0f - 2.0f * t);
        currentValue = prevTarget + smooth * (nextTarget - prevTarget);
        return currentValue;
    }

    float advanceSampleAndHold(float phaseInc) {
        phase += phaseInc;
        if (phase >= 1.0f) {
            phase -= std::floor(phase);
            currentValue = nextRandom();
        }
        return currentValue;
    }

    // Cosine interpolation between random targets
    float advanceSineInterpolate(float phaseInc) {
        phase += phaseInc;
        if (phase >= 1.0f) {
            phase -= std::floor(phase);
            prevTarget = nextTarget;
            nextTarget = nextRandom();
        }
        // Cosine interpolation: 0.5 * (1 - cos(pi * t))
        float t = 0.5f * (1.0f - std::cos(juce::MathConstants<float>::pi * phase));
        currentValue = prevTarget + t * (nextTarget - prevTarget);
        return currentValue;
    }
};

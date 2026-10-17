#pragma once

#include <JuceHeader.h>
#include <algorithm>
#include <array>
#include <cmath>

namespace motion {

// What the audio interface plays: the project's audio, or the beam itself to
// drive a real oscilloscope. X and Y alone; X, Y and the beam's brightness
// for a scope's Z input (inverted for one that blanks on a positive
// voltage); or X, Y and its red, green and blue.
enum class OutputMode { soundtrack, xy, xyrgb, xyz, xyzInverted };

// How many output channels the mode plays the beam on; none for audio.
inline int beamOutputChannels(OutputMode mode) {
    switch (mode) {
        case OutputMode::xy: return 2;
        case OutputMode::xyz:
        case OutputMode::xyzInverted: return 3;
        case OutputMode::xyrgb: return 5;
        case OutputMode::soundtrack: break;
    }
    return 0;
}

// The beam's brightness at a Z input: its brightest colour, from 0 (dark)
// to 1 (fully lit), or the other way round when inverted.
inline float zBrightness(float r, float g, float b, bool inverted) {
    const auto brightest = std::max({r, g, b});
    const auto bright = std::isfinite(brightest) ? std::clamp(brightest, 0.0f, 1.0f) : 0.0f;
    return inverted ? 1.0f - bright : bright;
}

// One sample of the beam (x, y and its colour) on the mode's channels: the
// first beamOutputChannels(mode) of these.
inline std::array<float, 5> beamSample(OutputMode mode, float x, float y, float r, float g, float b) {
    if (mode == OutputMode::xyz || mode == OutputMode::xyzInverted) {
        return {x, y, zBrightness(r, g, b, mode == OutputMode::xyzInverted), 0, 0};
    }
    return {x, y, r, g, b};
}

// Plays the beam's signal (x, y, z, r, g, b channels) on an output in the
// mode's channels. False, leaving the output as it was, for audio or when
// the output has too few channels.
inline bool routeBeam(OutputMode mode, const juce::AudioBuffer<float>& signal, juce::AudioBuffer<float>& output, int count) {
    const auto channels = beamOutputChannels(mode);
    if (channels == 0 || output.getNumChannels() < channels || signal.getNumChannels() < 6) {
        return false;
    }
    const auto* x = signal.getReadPointer(0);
    const auto* y = signal.getReadPointer(1);
    const auto* r = signal.getReadPointer(3);
    const auto* g = signal.getReadPointer(4);
    const auto* b = signal.getReadPointer(5);
    auto* const* out = output.getArrayOfWritePointers();
    for (int i = 0; i < count; ++i) {
        const auto sample = beamSample(mode, x[i], y[i], r[i], g[i], b[i]);
        for (int channel = 0; channel < channels; ++channel) {
            out[channel][i] = sample[static_cast<std::size_t>(channel)];
        }
    }
    return true;
}

}

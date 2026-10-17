#pragma once

#include <JuceHeader.h>
#include <algorithm>
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

// Plays the beam's signal (x, y, z, r, g, b channels) on an output in the
// mode's channels. False, leaving the output as it was, for audio or when
// the output has too few channels.
inline bool routeBeam(OutputMode mode, const juce::AudioBuffer<float>& signal, juce::AudioBuffer<float>& output, int count) {
    const auto channels = beamOutputChannels(mode);
    if (channels == 0 || output.getNumChannels() < channels || signal.getNumChannels() < 6) {
        return false;
    }
    output.copyFrom(0, 0, signal, 0, 0, count);
    output.copyFrom(1, 0, signal, 1, 0, count);
    if (channels == 5) {
        for (int channel = 2; channel < 5; ++channel) {
            output.copyFrom(channel, 0, signal, channel + 1, 0, count);
        }
    } else if (channels == 3) {
        const auto inverted = mode == OutputMode::xyzInverted;
        const auto* r = signal.getReadPointer(3);
        const auto* g = signal.getReadPointer(4);
        const auto* b = signal.getReadPointer(5);
        auto* z = output.getWritePointer(2);
        for (int i = 0; i < count; ++i) {
            z[i] = zBrightness(r[i], g[i], b[i], inverted);
        }
    }
    return true;
}

}

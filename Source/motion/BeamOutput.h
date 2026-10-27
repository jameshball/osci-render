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

// How a scope or laser is wired or mounted, corrected at the outputs: X
// and Y swapped first, then either output's polarity turned over (as
// osci-render's Swap X/Y and Invert X/Y do). The picture itself, the Scope
// and exports stay as designed.
struct BeamWiring {
    bool flipX = false, flipY = false, swapXY = false;
    bool any() const { return flipX || flipY || swapXY; }
    bool operator==(const BeamWiring&) const = default;
};

// Plays the beam's signal (x, y, z, r, g, b channels) on an output in the
// mode's channels, from `first` on, corrected for `wiring`. False, leaving
// the output as it was, for audio or when the output has too few channels.
inline bool routeBeam(OutputMode mode, const juce::AudioBuffer<float>& signal, juce::AudioBuffer<float>& output, int count, int first = 0, BeamWiring wiring = {}) {
    const auto channels = beamOutputChannels(mode);
    if (channels == 0 || first < 0 || output.getNumChannels() < first + channels || signal.getNumChannels() < 6) {
        return false;
    }
    const auto* x = signal.getReadPointer(wiring.swapXY ? 1 : 0);
    const auto* y = signal.getReadPointer(wiring.swapXY ? 0 : 1);
    const auto xGain = wiring.flipX ? -1.0f : 1.0f, yGain = wiring.flipY ? -1.0f : 1.0f;
    const auto* r = signal.getReadPointer(3);
    const auto* g = signal.getReadPointer(4);
    const auto* b = signal.getReadPointer(5);
    auto* const* out = output.getArrayOfWritePointers() + first;
    for (int i = 0; i < count; ++i) {
        const auto sample = beamSample(mode, xGain * x[i], yGain * y[i], r[i], g[i], b[i]);
        for (int channel = 0; channel < channels; ++channel) {
            out[channel][i] = sample[static_cast<std::size_t>(channel)];
        }
    }
    return true;
}

// Keeps the brightness on a mode's output (Z, or red, green and blue) from
// dark to full. Brought down to a slower device, a step in it rings past
// both: light that can't be, flashing at a scope's Z input or a laser.
inline void clampBrightness(OutputMode mode, juce::AudioBuffer<float>& output, int count, int first = 0) {
    const auto channels = std::min(first + beamOutputChannels(mode), output.getNumChannels());
    for (int channel = std::max(0, first) + 2; channel < channels; ++channel) {
        juce::FloatVectorOperations::clip(output.getWritePointer(channel), output.getReadPointer(channel), 0.0f, 1.0f, count);
    }
}

// Silences the beam on an output, from `first` on: still and dark. Silence
// alone isn't dark on an inverted Z input, where no voltage lights the beam
// where it rests.
inline void blankBeam(OutputMode mode, juce::AudioBuffer<float>& output, int count, int first = 0) {
    const auto channels = std::min(first + std::max(beamOutputChannels(mode), 2), output.getNumChannels());
    for (int channel = std::max(0, first); channel < channels; ++channel) { output.clear(channel, 0, count); }
    if (mode == OutputMode::xyzInverted && output.getNumChannels() >= first + 3) {
        juce::FloatVectorOperations::fill(output.getWritePointer(first + 2), zBrightness(0, 0, 0, true), count);
    }
}

// Where the project plays on the audio interface: its sound on a pair of
// outputs, the beam on as many outputs as its form needs, both, either or
// neither. Outputs count from 0 (the interface's output 1).
struct OutputRouting {
    static constexpr int off = -1;
    // The first of the sound's two outputs, or off.
    int sound = 0;
    // The beam's form (soundtrack: no beam), and its first output.
    OutputMode beam = OutputMode::soundtrack;
    int beamFirst = 0;
    // The beam outputs' corrections for this machine's scope.
    BeamWiring wiring;

    bool soundOn() const { return sound >= 0; }
    bool beamOn() const { return beamOutputChannels(beam) > 0 && beamFirst >= 0; }
    int beamChannels() const { return beamOn() ? beamOutputChannels(beam) : 0; }
    // The outputs it needs open: up to the last it uses.
    int channels() const { return std::max(soundOn() ? sound + 2 : 0, beamOn() ? beamFirst + beamChannels() : 0); }
    bool overlaps() const { return soundOn() && beamOn() && sound < beamFirst + beamChannels() && beamFirst < sound + 2; }
    bool fits(int outputs) const { return !overlaps() && channels() <= outputs; }
    bool operator==(const OutputRouting&) const = default;

    // One word for the audio thread: the wiring in its top byte (a word
    // saved before there was any reads as none).
    std::uint32_t pack() const {
        const auto flags = (wiring.flipX ? 1u : 0u) | (wiring.flipY ? 2u : 0u) | (wiring.swapXY ? 4u : 0u);
        return static_cast<std::uint32_t>(sound + 1) | (static_cast<std::uint32_t>(beam) << 8) | (static_cast<std::uint32_t>(beamFirst + 1) << 16) | (flags << 24);
    }
    static OutputRouting unpack(std::uint32_t word) {
        const auto flags = word >> 24;
        return {static_cast<int>(word & 0xff) - 1, static_cast<OutputMode>((word >> 8) & 0xff), static_cast<int>((word >> 16) & 0xff) - 1, {(flags & 1u) != 0, (flags & 2u) != 0, (flags & 4u) != 0}};
    }
};

// The nearest routing a device with `outputs` outputs can play: what fits
// stays as it was; the beam moves to the first outputs free of the sound,
// or (failing that) the sound gives way to it; a beam too wide for the
// device plays on X and Y alone.
inline OutputRouting fitRouting(OutputRouting routing, int outputs) {
    if (routing.fits(outputs)) { return routing; }
    if (routing.soundOn() && routing.sound + 2 > outputs) { routing.sound = outputs >= 2 ? 0 : OutputRouting::off; }
    if (routing.beamOn() && routing.beamChannels() > outputs) { routing.beam = outputs >= 2 ? OutputMode::xy : OutputMode::soundtrack; }
    if (!routing.beamOn()) { return routing.fits(outputs) ? routing : OutputRouting {outputs >= 2 ? 0 : OutputRouting::off, OutputMode::soundtrack, 0, routing.wiring}; }
    for (int first = 0; first + routing.beamChannels() <= outputs; ++first) {
        auto moved = routing;
        moved.beamFirst = first;
        if (moved.fits(outputs)) { return moved; }
    }
    routing.sound = OutputRouting::off;
    routing.beamFirst = 0;
    return routing;
}

}

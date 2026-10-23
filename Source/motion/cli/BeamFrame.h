#pragma once

#include "CliComposition.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace motion::cli {
// One cycle of the beam (a whole frame of what the Scope draws), sampled as
// Export samples it, and what can be measured of it.
struct BeamFrame {
    double time = 0;
    std::vector<osci::Point> samples;
    // How long drawing the cycle took.
    double milliseconds = 0;

    // The cycle under `time`, from its first sample.
    static BeamFrame at(const Composition& composition, double time) {
        BeamFrame frame;
        const auto rate = composition.sampleRate;
        const auto& prepared = *composition.prepared;
        const auto cycle = BeamRenderer::cycleOf(nearestSample(std::max(0.0, time), rate).value_or(0), rate, prepared.beamRate);
        const auto first = BeamRenderer::cycleStart(cycle, rate, prepared.beamRate);
        const auto end = BeamRenderer::cycleStart(cycle + 1, rate, prepared.beamRate);
        frame.time = static_cast<double>(first) / rate;
        frame.samples.reserve(static_cast<std::size_t>(end - first));
        BeamRenderer beam;
        const auto start = juce::Time::getMillisecondCounterHiRes();
        for (auto index = first; index < end; ++index) { frame.samples.push_back(beam.sample(prepared, static_cast<double>(index) / rate, index, rate, true, 1)); }
        frame.milliseconds = juce::Time::getMillisecondCounterHiRes() - start;
        return frame;
    }

    // A sample this bright or more is drawn; below it the beam only travels.
    static constexpr float lit = .02f;
    static float brightness(const osci::Point& point) { return std::max({point.r, point.g, point.b}); }

    // What is drawn and how: the length of beam drawn and travelled dark
    // (in the Scope's units, its width being 2), its mean brightness, the
    // box it fills, and a fingerprint that changes when any sample does.
    struct Stats {
        double time = 0, milliseconds = 0;
        int samples = 0;
        double drawn = 0, travelled = 0, brightness = 0, litShare = 0;
        juce::Rectangle<float> bounds;
        juce::String fingerprint;
    };
    Stats stats() const {
        Stats result;
        result.time = time;
        result.milliseconds = milliseconds;
        result.samples = static_cast<int>(samples.size());
        // FNV-1a over every sample, to a hundred-thousandth.
        std::uint64_t hash = 14695981039346656037ull;
        const auto mix = [&hash](float value) {
            const auto quantised = static_cast<std::int64_t>(std::llround(static_cast<double>(value) * 1.0e5));
            for (int byte = 0; byte < 8; ++byte) {
                hash ^= static_cast<std::uint64_t>((quantised >> (8 * byte)) & 0xff);
                hash *= 1099511628211ull;
            }
        };
        int litCount = 0;
        bool bounded = false;
        for (std::size_t index = 0; index < samples.size(); ++index) {
            const auto& point = samples[index];
            for (const auto value : {point.x, point.y, point.r, point.g, point.b}) { mix(value); }
            const auto bright = brightness(point);
            result.brightness += bright;
            if (bright >= lit) {
                ++litCount;
                const juce::Rectangle<float> here(point.x, point.y, 0, 0);
                result.bounds = bounded ? result.bounds.getUnion(here) : here;
                bounded = true;
            }
            if (index == 0) { continue; }
            const auto& previous = samples[index - 1];
            const auto step = std::hypot(point.x - previous.x, point.y - previous.y);
            (bright >= lit && brightness(previous) >= lit ? result.drawn : result.travelled) += step;
        }
        if (!samples.empty()) {
            result.brightness /= static_cast<double>(samples.size());
            result.litShare = static_cast<double>(litCount) / static_cast<double>(samples.size());
        }
        result.fingerprint = juce::String::toHexString(static_cast<juce::int64>(hash)).paddedLeft('0', 16);
        return result;
    }
};

// `count` frames evenly over a composition, the first at its start.
inline std::vector<double> frameTimes(double duration, int count) {
    std::vector<double> times;
    for (int index = 0; index < count; ++index) { times.push_back(duration * index / std::max(1, count)); }
    return times;
}
}

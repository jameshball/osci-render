#pragma once

#include "../render/CompositionRenderer.h"
#include "WavExporter.h"

namespace motion {
// Synchronous XYRGB worker export, separate from stereo soundtrack audio.
class SignalExporter {
public:
    static juce::Result write(Project snapshot, const juce::File& destination, double sampleRate, const std::atomic<bool>& cancel, std::atomic<double>* progress = nullptr) try {
        if (progress != nullptr) {
            progress->store(0.0, std::memory_order_relaxed);
        }
        if (cancel.load(std::memory_order_relaxed)) {
            return juce::Result::fail("Signal export cancelled.");
        }
        return write(PreparedComposition(snapshot), destination, sampleRate, cancel, progress);
    } catch (...) {
        return juce::Result::fail("Cannot prepare the composition for signal export.");
    }

    static juce::Result write(const PreparedComposition& composition, const juce::File& destination, double sampleRate, const std::atomic<bool>& cancel, std::atomic<double>* progress = nullptr) {
        return WavExporter::write<5>(composition.duration, destination, sampleRate, cancel, progress,
            [&](double index, double time) {
                const auto phase = std::fmod(index * 60.0 / sampleRate, 1.0);
                const auto point = composition.sample(time, phase);
                return std::array<float, 5> { point.x, point.y, point.r, point.g, point.b };
            }, "Signal", "The composition produced a non-finite signal sample. Check its transforms and camera animation.");
    }
};
}

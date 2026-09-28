#pragma once

#include "../render/BeamRenderer.h"
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
        return write(PreparedComposition(snapshot, sampleRate, &cancel), destination, sampleRate, cancel, progress);
    } catch (...) {
        return juce::Result::fail("Cannot prepare the composition for signal export.");
    }

    static juce::Result write(const PreparedComposition& composition, const juce::File& destination, double sampleRate, const std::atomic<bool>& cancel, std::atomic<double>* progress = nullptr) {
        for (const auto& clip : composition.clips) {
            if (clip.liveIdentity != nullptr) { return juce::Result::fail("Capture live sources before exporting the signal."); }
        }
        if (composition.preparationError.isNotEmpty()) { return juce::Result::fail(composition.preparationError); }
        if (composition.hasMidi && composition.sampleRate != sampleRate) { return juce::Result::fail("MIDI snapshot sample rate does not match the requested signal export rate."); }
        BeamRenderer beam;
        return WavExporter::write<5>(composition.duration, destination, sampleRate, cancel, progress,
            [&](double index, double time) {
                const auto point = beam.sample(composition, time, static_cast<std::int64_t>(index), sampleRate, true, 1);
                return std::array<float, 5> { point.x, point.y, point.r, point.g, point.b };
            }, "Signal", "The composition produced a non-finite signal sample. Check its transforms and camera animation.");
    }
};
}

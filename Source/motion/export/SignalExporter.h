#pragma once

#include "../render/BeamRenderer.h"
#include "../render/HiddenLineBaker.h"
#include "WavExporter.h"

namespace motion {
// Synchronous XYRGB worker export, separate from stereo soundtrack audio.
// `baked`: a bake to take frames over from (the editor's), so what is
// already baked isn't baked again.
class SignalExporter {
public:
    static juce::Result write(Project snapshot, const juce::File& destination, double sampleRate, const std::atomic<bool>& cancel, std::atomic<double>* progress = nullptr, hidden::Reach reach = squareScope(), const hidden::Bake* baked = nullptr) try {
        if (progress != nullptr) {
            progress->store(0.0, std::memory_order_relaxed);
        }
        if (cancel.load(std::memory_order_relaxed)) {
            return juce::Result::fail("Signal export cancelled.");
        }
        return write(PreparedComposition(snapshot, sampleRate, &cancel, CompositionPurpose::signal, reach), destination, sampleRate, cancel, progress, baked);
    } catch (...) {
        return juce::Result::fail("Cannot prepare the composition for signal export.");
    }

    static juce::Result write(const PreparedComposition& composition, const juce::File& destination, double sampleRate, const std::atomic<bool>& cancel, std::atomic<double>* progress = nullptr, const hidden::Bake* baked = nullptr) {
        for (const auto& clip : composition.clips) {
            if (clip.liveIdentity != nullptr) { return juce::Result::fail("Capture live sources before exporting the signal."); }
        }
        if (composition.preparationError.isNotEmpty()) { return juce::Result::fail(composition.preparationError); }
        // Every frame's hidden lines are removed before the signal is drawn.
        if (composition.hiddenLines != nullptr) {
            if (baked != nullptr) { composition.hiddenLines->inherit(*baked); }
            hidden::Baker::bakeAll(*composition.hiddenLines, sampleRate, cancel);
            if (cancel.load()) { return juce::Result::fail("Export cancelled."); }
        }
        BeamRenderer beam;
        return WavExporter::write<5>(composition.duration, destination, sampleRate, cancel, progress,
            [&](double index, double time) {
                const auto point = beam.sample(composition, time, static_cast<std::int64_t>(index), sampleRate, true, 1);
                return std::array<float, 5> { point.x, point.y, point.r, point.g, point.b };
            }, "Signal", "The composition produced a non-finite signal sample. Check its transforms and camera animation.");
    }
};
}

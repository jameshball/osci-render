#pragma once

#include "../render/CompositionRenderer.h"
#include "WavExporter.h"

namespace motion {
// Stereo project soundtrack only: no XYRGB signal and no monitor/master gain.
// Retain the project/prepared snapshot and call synchronously on a worker.
class SoundtrackExporter {
public:
    static juce::Result write(Project snapshot, const juce::File& destination, double sampleRate, const std::atomic<bool>& cancel, std::atomic<double>* progress = nullptr) try {
        if (progress != nullptr) {
            progress->store(0.0, std::memory_order_relaxed);
        }
        if (cancel.load(std::memory_order_relaxed)) {
            return juce::Result::fail("Audio export cancelled.");
        }
        return writePrepared(PreparedSoundtrack(snapshot), snapshot.duration, destination, sampleRate, cancel, progress);
    } catch (...) {
        return juce::Result::fail("Couldn't prepare the audio for export.");
    }

    static juce::Result write(const PreparedComposition& composition, const juce::File& destination, double sampleRate, const std::atomic<bool>& cancel, std::atomic<double>* progress = nullptr) {
        return writePrepared(composition.soundtrack, composition.duration, destination, sampleRate, cancel, progress);
    }

private:
    static juce::Result writePrepared(const PreparedSoundtrack& soundtrack, double duration, const juce::File& destination, double sampleRate, const std::atomic<bool>& cancel, std::atomic<double>* progress) {
        return WavExporter::write<2>(duration, destination, sampleRate, cancel, progress,
            [&](double, double time) {
                const auto value = soundtrack.sample(time);
                return std::array<float, 2> { value.left, value.right };
            }, "Audio", "The audio produced a non-finite sample. Check its gain and pan curves.");
    }
};
}

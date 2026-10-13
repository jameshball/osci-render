#pragma once

#include <JuceHeader.h>
#include <cmath>
#include <memory>

namespace motion::cli {
// How far apart two rendered signals are, sample by sample and channel by
// channel (x, y, r, g, b for an XYRGB signal): the largest difference, where
// it is, and when they first differ by more than a tolerance.
struct SignalDiff {
    int channels = 0;
    juce::int64 samples = 0;
    double sampleRate = 0;
    double largest = 0, largestAt = 0, rms = 0;
    int largestChannel = 0;
    // When they first differ by more than the tolerance; negative if never.
    double firstAt = -1;

    static juce::Result compare(const juce::File& one, const juce::File& other, double tolerance, SignalDiff& diff) {
        juce::WavAudioFormat wav;
        auto open = [&wav](const juce::File& file) { return std::unique_ptr<juce::AudioFormatReader>(wav.createReaderFor(file.createInputStream().release(), true)); };
        const auto first = open(one), second = open(other);
        if (first == nullptr || second == nullptr) { return juce::Result::fail("Couldn't read " + (first == nullptr ? one : other).getFileName() + " as WAV."); }
        if (first->numChannels != second->numChannels || first->sampleRate != second->sampleRate) {
            return juce::Result::fail("They differ in shape: " + juce::String(first->numChannels) + " channels at " + juce::String(first->sampleRate) + " Hz against " + juce::String(second->numChannels) + " at " + juce::String(second->sampleRate) + ".");
        }
        if (first->lengthInSamples != second->lengthInSamples) {
            return juce::Result::fail("They differ in length: " + juce::String(first->lengthInSamples) + " samples against " + juce::String(second->lengthInSamples) + ".");
        }
        diff.channels = static_cast<int>(first->numChannels);
        diff.samples = first->lengthInSamples;
        diff.sampleRate = first->sampleRate;
        constexpr int block = 1 << 16;
        juce::AudioBuffer<float> a(diff.channels, block), b(diff.channels, block);
        double squares = 0;
        for (juce::int64 start = 0; start < diff.samples; start += block) {
            const auto count = static_cast<int>(std::min<juce::int64>(block, diff.samples - start));
            first->read(&a, 0, count, start, true, true);
            second->read(&b, 0, count, start, true, true);
            for (int channel = 0; channel < diff.channels; ++channel) {
                const auto* x = a.getReadPointer(channel);
                const auto* y = b.getReadPointer(channel);
                for (int index = 0; index < count; ++index) {
                    const auto apart = std::abs(static_cast<double>(x[index]) - y[index]);
                    squares += apart * apart;
                    const auto at = static_cast<double>(start + index) / diff.sampleRate;
                    if (apart > diff.largest) {
                        diff.largest = apart;
                        diff.largestAt = at;
                        diff.largestChannel = channel;
                    }
                    if (apart > tolerance && (diff.firstAt < 0 || at < diff.firstAt)) { diff.firstAt = at; }
                }
            }
        }
        diff.rms = std::sqrt(squares / static_cast<double>(std::max<juce::int64>(1, diff.samples * diff.channels)));
        return juce::Result::ok();
    }
};
}

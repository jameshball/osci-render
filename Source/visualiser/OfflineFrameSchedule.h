#pragma once

#include <JuceHeader.h>
#include <cmath>

// Offline export rounds cumulative frame boundaries rather than repeatedly adding
// a rounded samples-per-frame value, which would accumulate audio/video timing drift
// when the sample rate is not an exact multiple of the frame rate.
namespace OfflineFrameSchedule {

inline int getMaxSamplesPerFrame(double sampleRate, double frameRate) {
    return juce::jmax(1, (int)std::ceil(sampleRate / frameRate));
}

inline juce::int64 getFrameBoundary(juce::int64 frameIndex, double sampleRate, double frameRate) {
    return (juce::int64)std::llround((long double)frameIndex * (long double)sampleRate / (long double)frameRate);
}

inline int getFrameSamples(juce::int64 frameIndex, double sampleRate, double frameRate) {
    const auto start = getFrameBoundary(frameIndex, sampleRate, frameRate);
    const auto end = getFrameBoundary(frameIndex + 1, sampleRate, frameRate);
    return juce::jmax(1, (int)(end - start));
}

inline juce::int64 getTotalFrames(juce::int64 totalSamples, double sampleRate, double frameRate) {
    return std::max<juce::int64>(1,
        (juce::int64)std::ceil((long double)totalSamples * (long double)frameRate / (long double)sampleRate));
}

}

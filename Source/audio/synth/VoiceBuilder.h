#pragma once

#include <JuceHeader.h>
#include "ShapeVoice.h"

class OscirenderAudioProcessor;

// Builds ShapeVoice objects on a background thread so the message/audio
// threads are never blocked by heavy allocations (e.g. DelayEffect buffers).
class VoiceBuilder : public juce::Thread {
public:
    VoiceBuilder(OscirenderAudioProcessor& p)
        : juce::Thread("Voice Builder"), processor(p) {}

    ~VoiceBuilder() override {
        signalThreadShouldExit();
        notify();
        stopThread(1000);
    }

    void setTargetVoiceCount(int count) {
        targetCount.store(count, std::memory_order_release);
        notify();
    }

    // Defined out-of-line in PluginProcessor.cpp because run() needs
    // the full OscirenderAudioProcessor definition.
    void run() override;

private:
    OscirenderAudioProcessor& processor;
    std::atomic<int> targetCount{0};
};

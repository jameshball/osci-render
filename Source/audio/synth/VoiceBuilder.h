#pragma once

#include <JuceHeader.h>
#include "ShapeVoice.h"

// Builds ShapeVoice objects on a background thread so the message/audio
// threads are never blocked by heavy allocations (e.g. DelayEffect buffers).
class VoiceBuilder : public juce::Thread {
public:
    VoiceBuilder(VoiceContext& context, VoiceManager& voices, juce::AudioBuffer<float>& externalAudio)
        : juce::Thread("Voice Builder"), context(context), voices(voices), externalAudio(externalAudio) {}

    ~VoiceBuilder() override {
        signalThreadShouldExit();
        notify();
        stopThread(1000);
    }

    void setTargetVoiceCount(int count) {
        targetCount.store(count, std::memory_order_release);
        notify();
    }

    bool hasAnyVoiceReady() const noexcept {
        return readyVoiceCount.load(std::memory_order_acquire) > 0;
    }

    bool waitForAnyVoice(int timeoutMilliseconds) {
        if (hasAnyVoiceReady()) {
            return true;
        }
        return firstVoiceReady.wait(timeoutMilliseconds) && hasAnyVoiceReady();
    }

    void run() override;

private:
    VoiceContext& context;
    VoiceManager& voices;
    juce::AudioBuffer<float>& externalAudio;
    std::atomic<int> targetCount{0};
    std::atomic<int> readyVoiceCount{0};
    juce::WaitableEvent firstVoiceReady;
};

#pragma once

#include "../CommonPluginProcessor.h"
#include "model/Document.h"
#include "render/CompositionRenderer.h"
#include "../audio/PreparedState.h"

class MotionProcessor : public CommonAudioProcessor {
public:
    MotionProcessor();
    ~MotionProcessor() override;
    void prepareToPlayInternal(double sampleRate, int samplesPerBlock) override;
    motion::Document document { getUndoManager() };
    std::atomic<bool> playing { false };
    std::atomic<bool> freezeWhenStopped { true };
    std::atomic<double> position { 0.0 };
    void seek(double seconds) { requestedPosition.store(std::max(0.0, seconds)); }
    void collectPreparedState() { composition.collect(); }
    // Message-thread-only transient editing preview; never alters saved state.
    void previewComposition(const motion::Project& project) {
        composition.publish(std::make_unique<motion::PreparedComposition>(project));
    }

private:
    osci::PreparedState<motion::PreparedComposition> composition;
    juce::AudioBuffer<float> signal;
    std::atomic<double> requestedPosition { -1.0 };
    double audioTime = 0.0;
    juce::int64 audioSample = 0;
    double phase = 0.0;
public:
    void processBlockInternal(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
    juce::AudioProcessorEditor* createEditor() override;
    void getStateInformation(juce::MemoryBlock& destination) override;
    void setStateInformation(const void* data, int size) override;
};

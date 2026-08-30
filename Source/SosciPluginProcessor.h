/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "CommonPluginProcessor.h"
#include "visualiser/VisualiserSettings.h"
#include <osci_standalone/osci_standalone.h>

//==============================================================================
/**
*/
class SosciAudioProcessor  : public CommonAudioProcessor, public juce::SystemAudioCapture::OutputMuteHandler
{
public:
    SosciAudioProcessor();
    ~SosciAudioProcessor() override;

    void processBlockInternal (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    using CommonAudioProcessor::loadAudioFile;
    void loadAudioFile(std::unique_ptr<juce::InputStream> stream) override;
    void stopAudioFile() override;
    void serviceDeferredAudioSourceChanges() override;
    void startStartupDemo();
    bool isStartupDemoActive() const { return startupDemoActive.load(std::memory_order_acquire); }
    bool setSystemAudioOutputMuted(bool muted) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorEditor* createEditor() override;

private:
    // Cached buffers to avoid reallocations in processBlock
    juce::AudioBuffer<float> wavBuffer;
    juce::AudioBuffer<float> workBuffer;
    std::atomic<bool> startupDemoActive { false };
    std::atomic<bool> startupDemoFinished { false };
    bool startupDemoStarted = false;
    std::atomic<bool> muteAfterStartupDemo { false };

    void cancelStartupDemo();

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SosciAudioProcessor)
};

/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "concurrency/ConsumerManager.h"
#include "audio/SampleRateManager.h"
#include "components/VisualiserSettings.h"
#include "audio/Effect.h"

//==============================================================================
/**
*/
class SosciAudioProcessor  : public juce::AudioProcessor, public ConsumerManager, public SampleRateManager
                            #if JucePlugin_Enable_ARA
                             , public juce::AudioProcessorARAExtension
                            #endif
{
public:
    SosciAudioProcessor();
    ~SosciAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;

    void setAudioThreadCallback(std::function<void(const juce::AudioBuffer<float>&)> callback);

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;
    double getSampleRate() override;

    std::atomic<double> currentSampleRate = 0.0;
    juce::SpinLock effectsLock;
    VisualiserParameters parameters;

    // shouldn't be accessed by audio thread, but needs to persist when GUI is closed
    // so should only be accessed by message thread
    juce::String currentProjectFile;
    juce::File lastOpenedDirectory = juce::File::getSpecialLocation(juce::File::userHomeDirectory);

private:
    
    std::vector<BooleanParameter*> booleanParameters;
    std::vector<FloatParameter*> floatParameters;
    std::vector<IntParameter*> intParameters;
    std::vector<std::shared_ptr<Effect>> allEffects;
    std::vector<std::shared_ptr<Effect>> permanentEffects;

    std::shared_ptr<Effect> getEffect(juce::String id);
    BooleanParameter* getBooleanParameter(juce::String id);
    FloatParameter* getFloatParameter(juce::String id);
    IntParameter* getIntParameter(juce::String id);

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SosciAudioProcessor)
};

/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "concurrency/AudioBackgroundThread.h"
#include "concurrency/AudioBackgroundThreadManager.h"
#include "audio/SampleRateManager.h"
#include "visualiser/VisualiserSettings.h"
#include "audio/Effect.h"

//==============================================================================
/**
*/
class CommonAudioProcessor  : public juce::AudioProcessor, public SampleRateManager
                            #if JucePlugin_Enable_ARA
                             , public juce::AudioProcessorARAExtension
                            #endif
{
public:
    CommonAudioProcessor();
    ~CommonAudioProcessor() override;

    void addAllParameters();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    virtual void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) = 0;
    virtual juce::AudioProcessorEditor* createEditor() = 0;

    bool hasEditor() const override;

    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;
    double getSampleRate() override;

    std::atomic<double> currentSampleRate = 0.0;
    juce::SpinLock effectsLock;
    VisualiserParameters visualiserParameters;
    
    AudioBackgroundThreadManager threadManager;
    std::function<void()> haltRecording;
    
    std::atomic<bool> forceDisableBrightnessInput = false;

    // shouldn't be accessed by audio thread, but needs to persist when GUI is closed
    // so should only be accessed by message thread
    juce::String currentProjectFile;
    juce::File lastOpenedDirectory = juce::File::getSpecialLocation(juce::File::userHomeDirectory);

protected:
    
    bool brightnessEnabled = false;
    
    std::vector<BooleanParameter*> booleanParameters;
    std::vector<FloatParameter*> floatParameters;
    std::vector<IntParameter*> intParameters;
    std::vector<std::shared_ptr<Effect>> effects;
    std::vector<std::shared_ptr<Effect>> permanentEffects;

    std::shared_ptr<Effect> getEffect(juce::String id);
    BooleanParameter* getBooleanParameter(juce::String id);
    FloatParameter* getFloatParameter(juce::String id);
    IntParameter* getIntParameter(juce::String id);

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CommonAudioProcessor)
};

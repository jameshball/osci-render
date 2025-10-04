#pragma once

#include <JuceHeader.h>
#include <any>
#include "visualiser/VisualiserParameters.h"
#include "audio/BitCrushEffect.h"
#include "audio/AutoGainControlEffect.h"
#include "txt/TextParser.h"
#include "audio/ShapeVectorRenderer.h"

class EffectAudioProcessor  : public juce::AudioProcessor
                            #if JucePlugin_Enable_ARA
                             , public juce::AudioProcessorARAExtension
                            #endif
{
public:
    EffectAudioProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

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
    void changeProgramName(int index, const juce::String& newName) override;    std::shared_ptr<osci::Effect> bitCrush = BitCrushEffect().build();

    std::shared_ptr<osci::Effect> autoGain = AutoGainControlEffect().build();
    
    VisualiserParameters visualiserParameters;
    
    osci::AudioBackgroundThreadManager threadManager;
    
    juce::SpinLock sliderLock;
    ShapeVectorRenderer sliderRenderer;

protected:
    
    std::vector<std::shared_ptr<osci::Effect>> effects;

private:
    double currentSampleRate = 44100.0;
    
    ShapeVectorRenderer titleRenderer;

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EffectAudioProcessor)
};

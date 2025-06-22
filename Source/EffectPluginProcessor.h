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
    void changeProgramName(int index, const juce::String& newName) override;    std::shared_ptr<osci::Effect> bitCrush = std::make_shared<osci::Effect>(
        std::make_shared<BitCrushEffect>(),
        new osci::EffectParameter("Bit Crush", "Limits the resolution of points drawn to the screen, making the object look pixelated, and making the audio sound more 'digital' and distorted.", "bitCrush", VERSION_HINT, 0.0, 0.0, 1.0)
    );

    std::shared_ptr<osci::Effect> autoGain = std::make_shared<osci::Effect>(
        std::make_shared<AutoGainControlEffect>(),
        std::vector<osci::EffectParameter*>{
            new osci::EffectParameter("Intensity", "Controls how aggressively the gain adjustment is applied", "agcIntensity", VERSION_HINT, 1.0, 0.0, 1.0),
            new osci::EffectParameter("Target Level", "Target output level for the automatic gain control", "agcTarget", VERSION_HINT, 0.6, 0.0, 1.0),
            new osci::EffectParameter("Response", "How quickly the effect responds to level changes (lower is slower)", "agcResponse", VERSION_HINT, 0.0001, 0.0, 1.0)
        }
    );
    
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

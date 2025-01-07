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
#include "visualiser/RecordingSettings.h"
#include "audio/Effect.h"
#include "wav/WavParser.h"


class AudioPlayerListener;
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
    void loadAudioFile(const juce::File& file);
    void stopAudioFile();
    void addAudioPlayerListener(AudioPlayerListener* listener);
    void removeAudioPlayerListener(AudioPlayerListener* listener);

    juce::SpinLock audioPlayerListenersLock;
    std::vector<AudioPlayerListener*> audioPlayerListeners;

    std::atomic<double> volume = 1.0;
    std::atomic<double> threshold = 1.0;

    std::shared_ptr<Effect> volumeEffect = std::make_shared<Effect>(
        [this](int index, OsciPoint input, const std::vector<std::atomic<double>>& values, double sampleRate) {
            volume = values[0].load();
            return input;
        }, new EffectParameter(
            "Volume",
            "Controls the volume of the output audio.",
            "volume",
            VERSION_HINT, 1.0, 0.0, 3.0
        )
    );

    std::shared_ptr<Effect> thresholdEffect = std::make_shared<Effect>(
        [this](int index, OsciPoint input, const std::vector<std::atomic<double>>& values, double sampleRate) {
            threshold = values[0].load();
            return input;
        }, new EffectParameter(
            "Threshold",
            "Clips the audio to a maximum value. Applying a harsher threshold results in a more distorted sound.",
            "threshold",
            VERSION_HINT, 1.0, 0.0, 1.0
        )
    );

    juce::SpinLock wavParserLock;
    WavParser wavParser{ *this };

    std::atomic<double> currentSampleRate = 0.0;
    juce::SpinLock effectsLock;
    VisualiserParameters visualiserParameters;
    RecordingParameters recordingParameters;
    
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

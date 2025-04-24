/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================

*/

#pragma once

#include <JuceHeader.h>
#include <any>
#include "audio/SampleRateManager.h"
#include "visualiser/VisualiserSettings.h"
#include "visualiser/RecordingSettings.h"
#include "wav/WavParser.h"


class AudioPlayerListener;
class CommonAudioProcessor  : public juce::AudioProcessor, public SampleRateManager
                            #if JucePlugin_Enable_ARA
                             , public juce::AudioProcessorARAExtension
                            #endif
{
public:
    CommonAudioProcessor(const BusesProperties& busesProperties);
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
    void loadAudioFile(std::unique_ptr<juce::InputStream> stream);
    void stopAudioFile();
    void addAudioPlayerListener(AudioPlayerListener* listener);
    void removeAudioPlayerListener(AudioPlayerListener* listener);
    std::any getProperty(const std::string& key);
    std::any getProperty(const std::string& key, std::any defaultValue);
    void setProperty(const std::string& key, std::any value);
    
    // Get the ffmpeg binary file
    juce::File getFFmpegFile() const { return applicationFolder.getChildFile(ffmpegFileName); }
    
    // Check if ffmpeg exists, if not download it
    bool ensureFFmpegExists(std::function<void()> onStart = nullptr, std::function<void()> onSuccess = nullptr);
    
    // A static method to get the appropriate ffmpeg URL based on platform
    static juce::String getFFmpegURL();
    
    // Global settings methods
    bool getGlobalBoolValue(const juce::String& keyName, bool defaultValue = false) const;
    int getGlobalIntValue(const juce::String& keyName, int defaultValue = 0) const;
    double getGlobalDoubleValue(const juce::String& keyName, double defaultValue = 0.0) const;
    juce::String getGlobalStringValue(const juce::String& keyName, const juce::String& defaultValue = "") const;
    void setGlobalValue(const juce::String& keyName, const juce::var& value);
    void removeGlobalValue(const juce::String& keyName);
    void saveGlobalSettings();
    void reloadGlobalSettings();
    
    bool hasSetSessionStartTime = false;
    bool programCrashedAndUserWantsToReset();

    std::atomic<bool> licenseVerified = true;
    
    juce::SpinLock audioPlayerListenersLock;
    std::vector<AudioPlayerListener*> audioPlayerListeners;

    std::atomic<double> volume = 1.0;
    std::atomic<double> threshold = 1.0;
    osci::BooleanParameter* muteParameter = nullptr;

    std::shared_ptr<osci::Effect> volumeEffect = std::make_shared<osci::Effect>(
        [this](int index, osci::Point input, const std::vector<std::atomic<double>>& values, double sampleRate) {
            volume = values[0].load();
            return input;
        }, new osci::EffectParameter(
            "Volume",
            "Controls the volume of the output audio.",
            "volume",
            VERSION_HINT, 1.0, 0.0, 3.0
        )
    );

    std::shared_ptr<osci::Effect> thresholdEffect = std::make_shared<osci::Effect>(
        [this](int index, osci::Point input, const std::vector<std::atomic<double>>& values, double sampleRate) {
            threshold = values[0].load();
            return input;
        }, new osci::EffectParameter(
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
    
    osci::AudioBackgroundThreadManager threadManager;
    std::function<void()> haltRecording;
    
    std::atomic<bool> forceDisableBrightnessInput = false;

    // shouldn't be accessed by audio thread, but needs to persist when GUI is closed
    // so should only be accessed by message thread
    juce::String currentProjectFile;
    
    // Methods to get/set the last opened directory as a global setting
    juce::File getLastOpenedDirectory();
    void setLastOpenedDirectory(const juce::File& directory);

    juce::File applicationFolder = juce::File::getSpecialLocation(juce::File::SpecialLocationType::userApplicationDataDirectory)
#if JUCE_MAC
        .getChildFile("Application Support")
#endif
        .getChildFile("osci-render");
    
    juce::String ffmpegFileName =
#if JUCE_WINDOWS
        "ffmpeg.exe";
#else
        "ffmpeg";
#endif

protected:
    
    bool brightnessEnabled = false;
    
    std::vector<osci::BooleanParameter*> booleanParameters;
    std::vector<osci::FloatParameter*> floatParameters;
    std::vector<osci::IntParameter*> intParameters;
    std::vector<std::shared_ptr<osci::Effect>> effects;
    std::vector<std::shared_ptr<osci::Effect>> permanentEffects;

    std::shared_ptr<osci::Effect> getEffect(juce::String id);
    osci::BooleanParameter* getBooleanParameter(juce::String id);
    osci::FloatParameter* getFloatParameter(juce::String id);
    osci::IntParameter* getIntParameter(juce::String id);
    
    void saveProperties(juce::XmlElement& xml);
    void loadProperties(juce::XmlElement& xml);
    
    juce::SpinLock propertiesLock;
    std::unordered_map<std::string, std::any> properties;
    
    // Global settings that persist across plugin instances
    std::unique_ptr<juce::PropertiesFile> globalSettings;

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CommonAudioProcessor)
};

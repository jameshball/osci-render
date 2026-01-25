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

class AudioPlayerListener {
public:
    virtual void parserChanged() = 0;
};

class CommonAudioProcessor  : public juce::AudioProcessor, public SampleRateManager, public juce::Timer
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
    
#if OSCI_PREMIUM
    // Get the ffmpeg binary file
    juce::File getFFmpegFile() const { return applicationFolder.getChildFile(ffmpegFileName); }
    
    // Check if ffmpeg exists, if not download it
    bool ensureFFmpegExists(std::function<void()> onStart = nullptr, std::function<void()> onSuccess = nullptr);
    
    // A static method to get the appropriate ffmpeg URL based on platform
    static juce::String getFFmpegURL();
#endif
    
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
    
    juce::SpinLock audioPlayerListenersLock;
    std::vector<AudioPlayerListener*> audioPlayerListeners;

    std::atomic<double> volume = 1.0;
    std::atomic<double> threshold = 1.0;
    osci::BooleanParameter* muteParameter = nullptr;

    std::shared_ptr<osci::Effect> volumeEffect = std::make_shared<osci::SimpleEffect>(
        [this](int index, osci::Point input, const std::vector<std::atomic<float>>& values, float sampleRate, float frequency) {
            volume = values[0].load();
            return input;
        }, new osci::EffectParameter(
            "Volume",
            "Controls the volume of the output audio.",
            "volume",
            VERSION_HINT, 1.0, 0.0, 3.0
        )
    );

    std::shared_ptr<osci::Effect> thresholdEffect = std::make_shared<osci::SimpleEffect>(
        [this](int index, osci::Point input, const std::vector<std::atomic<float>>& values, float sampleRate, float frequency) {
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

    // When true, processBlock should do minimal work and output silence.
    // Used during offline video rendering so the UI renderer can use CPU/GPU without contention.
    std::atomic<bool> offlineRenderActive { false };
    
    std::atomic<bool> forceDisableBrightnessInput = false;
    std::atomic<bool> forceDisableRgbInput = false;

    // shouldn't be accessed by audio thread, but needs to persist when GUI is closed
    // so should only be accessed by message thread
    juce::String currentProjectFile;
    
    // Methods to get/set the last opened directory as a global setting
    juce::File getLastOpenedDirectory();
    void setLastOpenedDirectory(const juce::File& directory);

    // Standalone-only: persist and restore currentProjectFile in the project state XML.
    // This allows standalone to "relink" saves to the same project file after restart.
    void saveStandaloneProjectFilePathToXml(juce::XmlElement& xml) const;
    void restoreStandaloneProjectFilePathFromXml(const juce::XmlElement& xml);

    // Recently opened project files (persisted in global settings).
    // This is used for the File > Open Recent menu in both standalone and DAW plugin builds.
    int getNumRecentProjectFiles() const;
    juce::File getRecentProjectFile(int index) const;
    void addRecentProjectFile(const juce::File& file);
    int createRecentProjectsPopupMenuItems(juce::PopupMenu& menuToAddItemsTo,
                                          int baseItemId,
                                          bool showFullPaths,
                                          bool dontAddNonExistentFiles);

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
    void setAcceptsKeys(bool shouldAcceptKeys) {
        setGlobalValue("acceptsAllKeys", shouldAcceptKeys);
    }
    bool getAcceptsKeys() {
        return getGlobalBoolValue("acceptsAllKeys", juce::JUCEApplicationBase::isStandaloneApp());
    }
    
protected:
    
    bool brightnessEnabled = false;
    bool rgbEnabled = false;

    // Expose flags to GUI thread safely
public:
    bool isBrightnessEnabled() const { return brightnessEnabled; }
    bool isRgbEnabled() const { return rgbEnabled; }
    bool getForceDisableBrightnessInput() const { return forceDisableBrightnessInput.load(); }
    bool getForceDisableRgbInput() const { return forceDisableRgbInput.load(); }

    void setOfflineRenderActive(bool active) { offlineRenderActive.store(active); }
    bool isOfflineRenderActive() const { return offlineRenderActive.load(); }
protected:
    
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

    juce::RecentlyOpenedFilesList recentProjectFiles;

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CommonAudioProcessor)

private:
    void startHeartbeat();
    void stopHeartbeat();
    void timerCallback() override;

    bool heartbeatActive = false;
};

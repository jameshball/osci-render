/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================

*/

#pragma once

#include <JuceHeader.h>
#include <any>
#include "audio/platform/SampleRateManager.h"
#include "visualiser/VisualiserSettings.h"
#include "visualiser/RecordingSettings.h"
#include "audio/wav/WavParser.h"

class AudioPlayerListener {
public:
    virtual void parserChanged() = 0;
};

class CommonAudioProcessor  : public juce::AudioProcessor, public SampleRateManager, public juce::Timer,
                              public juce::ValueTree::Listener
                            #if JucePlugin_Enable_ARA
                             , public juce::AudioProcessorARAExtension
                            #endif
{
public:
    CommonAudioProcessor(const BusesProperties& busesProperties);
    ~CommonAudioProcessor() override;

    void addAllParameters();

    juce::UndoManager& getUndoManager() { return undoManager; }

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
    juce::File getGlobalSettingsFile() const { return globalSettings != nullptr ? globalSettings->getFile() : juce::File(); }
    // Path to the standalone app's settings file (written by CustomStandaloneFilterApp).
    // The file only exists when running as a standalone; when hosted in a DAW
    // this returns the would-be path (which will not exist on disk).
    static juce::File getAppSettingsFile();
    
    bool hasSetSessionStartTime = false;
    bool programCrashedAndUserWantsToReset();
    
    juce::SpinLock audioPlayerListenersLock;
    std::vector<AudioPlayerListener*> audioPlayerListeners;

    osci::BooleanParameter* muteParameter = nullptr;

    osci::MidiCCManager midiCCManager;

    std::shared_ptr<osci::Effect> volumeEffect = std::make_shared<osci::SimpleEffect>(
        new osci::EffectParameter(
            "Volume",
            "Controls the volume of the output audio.",
            "volume",
            VERSION_HINT, 1.0, 0.0, 3.0
        )
    );

    std::shared_ptr<osci::Effect> thresholdEffect = std::make_shared<osci::SimpleEffect>(
        new osci::EffectParameter(
            "Threshold",
            "Clips the audio to a maximum value. Applying a harsher threshold results in a more distorted sound.",
            "threshold",
            VERSION_HINT, 1.0, 0.0, 1.0
        )
    );

    juce::SpinLock wavParserLock;
    WavParser wavParser{ *this };

    // Apply per-sample volume scaling and threshold clipping to a stereo buffer.
    // Uses SIMD (FloatVectorOperations) when the animated buffer is not populated.
    void applyVolumeAndThreshold(float* const* channels, int numSamples);

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

    // ValueTree undo support
    juce::UndoManager undoManager;
    juce::ValueTree stateTree { "State" };

    // Tracks which parameter last changed, so we start a new undo transaction
    // only when a different parameter is modified (slider drags coalesce).
    juce::String lastUndoParamId;

    // When true, parameter changes and modulation assignment changes bypass the
    // UndoManager.  Used during preview hover operations.
    bool undoSuppressed = false;

    // When true, parameter setters and modulation methods skip calling
    // beginNewTransaction() so multiple changes coalesce into one transaction.
    bool undoGrouping = false;

    // RAII guard that sets a bool flag to true on construction and false on destruction.
    struct ScopedFlag {
        bool& flag;
        explicit ScopedFlag(bool& f) : flag(f) { flag = true; }
        ~ScopedFlag() { flag = false; }
        ScopedFlag(const ScopedFlag&) = delete;
        ScopedFlag& operator=(const ScopedFlag&) = delete;
    };

    // ValueTree::Listener override — dispatches undo/redo changes to parameters
    void valueTreePropertyChanged(juce::ValueTree& treeWhosePropertyHasChanged, const juce::Identifier& property) override;

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

    void loadMidiCCState(const juce::XmlElement* xml);
    
    void saveProperties(juce::XmlElement& xml);
    void loadProperties(juce::XmlElement& xml);

    // Loads effects from an <effects> XML element, logging counts of loaded/skipped.
    // Caller must hold effectsLock.
    void loadEffectsFromXml(const juce::XmlElement* effectsXml);
    
    juce::SpinLock propertiesLock;
    std::unordered_map<std::string, std::any> properties;
    
    // Global settings that persist across plugin instances
    std::unique_ptr<juce::PropertiesFile> globalSettings;

    // File logger for writing diagnostics to the application data folder
    std::unique_ptr<juce::FileLogger> fileLogger;

    juce::RecentlyOpenedFilesList recentProjectFiles;

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CommonAudioProcessor)

private:
    void startHeartbeat();
    void stopHeartbeat();
    void timerCallback() override;

    bool heartbeatActive = false;

#if OSCI_PREMIUM
    // Verify the downloaded ffmpeg binary actually runs on this system.
    // Returns true if ffmpeg -version exits successfully. Result is cached.
    bool validateFFmpegBinary();
    bool ffmpegValidated = false;
#endif

    // Maps paramID → AudioProcessorParameter* for O(1) lookup in valueTreePropertyChanged
    std::unordered_map<juce::String, juce::AudioProcessorParameter*> paramIdMap;
};

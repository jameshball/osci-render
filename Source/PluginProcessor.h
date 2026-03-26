/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

#include <limits>
#include <numbers>
#include <unordered_map>

#include "CommonPluginProcessor.h"
#include "audio/CustomEffect.h"
#include "audio/DelayEffect.h"
#include "audio/LuaEffectState.h"
#include "audio/PerspectiveEffect.h"
#include "audio/PublicSynthesiser.h"
#include "audio/SampleRateManager.h"
#include "audio/ShapeSound.h"
#include "audio/ShapeVoice.h"
#include "audio/DahdsrEnvelope.h"
#include "audio/LfoState.h"
#include "audio/EnvState.h"
#include "audio/RandomState.h"
#include "audio/SidechainState.h"
#include "audio/ModulationTypes.h"
#include "obj/ObjectServer.h"

class FileParser;

#if (JUCE_MAC || JUCE_WINDOWS) && OSCI_PREMIUM
#include "img/ImageParser.h"
#include "../modules/juce_sharedtexture/SharedTexture.h"
#include "video/SyphonFrameGrabber.h"
#endif

//==============================================================================

// Central 60 Hz timer that drives modulation display updates (LFO overlays,
// envelope overlays, drag-highlight repaints, etc.) for all registered UI
// components.  Replaces the per-EffectComponent 30 Hz timers.
class ModulationUpdateBroadcaster : private juce::Timer {
public:
    using Callback = std::function<void()>;

    void addListener(void* key, Callback cb) {
        entries.push_back({key, std::move(cb)});
        if (!isTimerRunning()) startTimerHz(60);
    }

    void removeListener(void* key) {
        entries.erase(
            std::remove_if(entries.begin(), entries.end(),
                [key](const Entry& e) { return e.key == key; }),
            entries.end());
        if (entries.empty()) stopTimer();
    }

private:
    void timerCallback() override {
        for (auto& e : entries) e.callback();
    }

    struct Entry { void* key; Callback callback; };
    std::vector<Entry> entries;
};

/**
 */
class OscirenderAudioProcessor : public CommonAudioProcessor, juce::AudioProcessorParameter::Listener
#if JucePlugin_Enable_ARA
    ,
                                 public juce::AudioProcessorARAExtension
#endif
{
public:
    OscirenderAudioProcessor();
    ~OscirenderAudioProcessor() override;

    // Beginner mode: per-parameter LFO dropdowns, mic icon, single amplitude envelope, simplified layout.
    // Advanced mode: drag-and-drop LFO/ENV module panels, full layout.
    // Set once at construction from OSCI_PREMIUM; persisted in state XML.
    // Changing mode requires a plugin reload (parameter tree is fixed at construction).
    bool isBeginnerMode() const { return beginnerMode; }

    // Central 60 Hz broadcaster for modulation display updates.
    // EffectComponents register/unregister via wireModulation / destructor.
    ModulationUpdateBroadcaster modulationUpdateBroadcaster;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;

    void setAudioThreadCallback(std::function<void(const juce::AudioBuffer<float>&)> callback);

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;
    void parameterValueChanged(int parameterIndex, float newValue) override;
    void parameterGestureChanged(int parameterIndex, bool gestureIsStarting) override;
    DahdsrParams getCurrentDahdsrParams() const;
    DahdsrParams getCurrentDahdsrParams(int envIndex) const;

    // UI telemetry for per-voice envelope visualization (written on audio thread, read on message thread)
    static constexpr int kMaxUiVoices = 16;
    std::atomic<double> uiVoiceEnvelopeTimeSeconds[kMaxUiVoices]{};
    std::atomic<bool> uiVoiceActive[kMaxUiVoices]{};
    // Per-envelope UI telemetry for flow markers (same as envelope 0 for backward compat)
    std::atomic<double> uiVoiceEnvTimeSeconds[NUM_ENVELOPES][kMaxUiVoices]{};
    std::atomic<bool> uiVoiceEnvActive[NUM_ENVELOPES][kMaxUiVoices]{};
    // Per-voice per-envelope current values for modulation (written by audio-thread voices)
    std::atomic<float> uiVoiceEnvValue[NUM_ENVELOPES][kMaxUiVoices]{};

    std::vector<std::shared_ptr<osci::Effect>> toggleableEffects;
    std::vector<std::shared_ptr<osci::Effect>> luaEffects;
    // Temporary preview effect applied while hovering effects in the grid (guarded by effectsLock)
    std::shared_ptr<osci::Effect> previewEffect;
    std::atomic<double> luaValues[26] = {0.0};

    std::shared_ptr<osci::Effect> frequencyEffect = std::make_shared<osci::SimpleEffect>(
        [this](int index, osci::Point input, const std::vector<std::atomic<float>>& values, float sampleRate, float freq) {
            // Update the global frequency from the slider value
            frequency = values[0].load() + 0.000001; // epsilon prevents a weird bug on mac
            return input;
        },
        new osci::EffectParameter(
            "Frequency",
            "Controls how many times per second the image is drawn, thereby controlling the pitch of the sound. Lower frequencies result in more-accurately drawn images, but more flickering, and vice versa.",
            "frequency",
            VERSION_HINT, 220.0, 0.0, 4200.0
        )
    );

    std::shared_ptr<DelayEffect> delayEffect = std::make_shared<DelayEffect>();

    std::function<void(int, juce::String, juce::String)> errorCallback = [this](int lineNum, juce::String fileName, juce::String error) { notifyErrorListeners(lineNum, fileName, error); };
    std::unique_ptr<LuaEffectState> luaEffectState = std::make_unique<LuaEffectState>(LuaEffectState::UNIQUE_ID, "return { x, y, z }", errorCallback);
    std::shared_ptr<osci::Effect> custom = std::make_shared<osci::SimpleEffect>(
        std::make_shared<CustomEffect>(*luaEffectState, luaValues),
        new osci::EffectParameter("Lua Effect", "Controls the strength of the custom Lua effect applied. You can write your own custom effect using Lua by pressing the edit button on the right.", "customEffectStrength", VERSION_HINT, 1.0, 0.0, 1.0));

    std::shared_ptr<osci::Effect> perspective = PerspectiveEffect().build();

    osci::BooleanParameter* midiEnabled = new osci::BooleanParameter("MIDI Enabled", "midiEnabled", VERSION_HINT, false, "Enable MIDI input for the synth. If disabled, the synth will play a constant tone, as controlled by the frequency slider.");
    osci::BooleanParameter* inputEnabled = new osci::BooleanParameter("Audio Input Enabled", "inputEnabled", VERSION_HINT, false, "Enable to use input audio, instead of the generated audio.");
    std::atomic<double> frequency = 220.0;

    // DAW transport state (updated in processBlock, read by voices for Lua)
    std::atomic<double> luaBpm = 120.0;
    std::atomic<double> luaPlayTime = 0.0;
    std::atomic<double> luaPlayTimeBeats = 0.0;
    std::atomic<bool> luaIsPlaying = false;
    std::atomic<int> luaTimeSigNum = 4;
    std::atomic<int> luaTimeSigDen = 4;

    juce::SpinLock parsersLock;
    std::vector<std::shared_ptr<FileParser>> parsers;
    std::vector<ShapeSound::Ptr> sounds;
    std::vector<std::shared_ptr<juce::MemoryBlock>> fileBlocks;
    std::vector<juce::String> fileNames;
    int currentFileId = 0;
    std::vector<int> fileIds;
    std::atomic<int> currentFile = -1;

    juce::ChangeBroadcaster broadcaster;
    std::atomic<bool> objectServerRendering = false;
    juce::ChangeBroadcaster fileChangeBroadcaster;

    osci::FloatParameter* delayTime = new osci::FloatParameter("Delay Time", "delayTime", VERSION_HINT, 0.0f, osci_audio::kDahdsrTimeMinSeconds, osci_audio::kDahdsrTimeMaxSeconds, osci_audio::kDahdsrTimeStepSeconds);
    osci::FloatParameter* attackTime = new osci::FloatParameter("Attack Time", "attackTime", VERSION_HINT, 0.005f, osci_audio::kDahdsrTimeMinSeconds, osci_audio::kDahdsrTimeMaxSeconds, osci_audio::kDahdsrTimeStepSeconds);
    osci::FloatParameter* holdTime = new osci::FloatParameter("Hold Time", "holdTime", VERSION_HINT, 0.0f, osci_audio::kDahdsrTimeMinSeconds, osci_audio::kDahdsrTimeMaxSeconds, osci_audio::kDahdsrTimeStepSeconds);
    osci::FloatParameter* decayTime = new osci::FloatParameter("Decay Time", "decayTime", VERSION_HINT, 0.095f, osci_audio::kDahdsrTimeMinSeconds, osci_audio::kDahdsrTimeMaxSeconds, osci_audio::kDahdsrTimeStepSeconds);
    osci::FloatParameter* sustainLevel = new osci::FloatParameter("Sustain Level", "sustainLevel", VERSION_HINT, 0.6f, 0.0f, 1.0f, 0.00001f);
    osci::FloatParameter* releaseTime = new osci::FloatParameter("Release Time", "releaseTime", VERSION_HINT, 0.4f, osci_audio::kDahdsrTimeMinSeconds, osci_audio::kDahdsrTimeMaxSeconds, osci_audio::kDahdsrTimeStepSeconds);
    osci::FloatParameter* attackShape = new osci::FloatParameter("Attack Shape", "attackShape", VERSION_HINT, 5.0f, -50.0f, 50.0f, 0.00001f);
    osci::FloatParameter* decayShape = new osci::FloatParameter("Decay Shape", "decayShape", VERSION_HINT, -20.0f, -50.0f, 50.0f, 0.00001f);
    osci::FloatParameter* releaseShape = new osci::FloatParameter("Release Shape", "releaseShape", VERSION_HINT, -5.0f, -50.0f, 50.0f, 0.00001f);

    // === Envelopes 1–4 (additional modulation envelopes) ===
    // Envelope 0 uses the legacy params above. Envelopes 1–4 have indexed IDs.
    struct EnvelopeParamSet {
        osci::FloatParameter* delayTime = nullptr;
        osci::FloatParameter* attackTime = nullptr;
        osci::FloatParameter* holdTime = nullptr;
        osci::FloatParameter* decayTime = nullptr;
        osci::FloatParameter* sustainLevel = nullptr;
        osci::FloatParameter* releaseTime = nullptr;
        osci::FloatParameter* attackShape = nullptr;
        osci::FloatParameter* decayShape = nullptr;
        osci::FloatParameter* releaseShape = nullptr;
    };
    EnvelopeParamSet envParams[NUM_ENVELOPES]; // index 0 points to legacy params

    // === Global Envelope assignment system ===
    int activeEnvTab = 0;

    void addEnvAssignment(const EnvAssignment& assignment);
    void removeEnvAssignment(int envIndex, const juce::String& paramId);
    std::vector<EnvAssignment> getEnvAssignments() const;

    // Get the current envelope output value (0..1) for visualization
    float getEnvCurrentValue(int envIndex) const;

    // Look up human-readable name for any parameter by ID (searches all effects).
    juce::String getParamDisplayName(const juce::String& paramId) const;

    // === Global LFO system ===
    osci::FloatParameter* lfoRate[NUM_LFOS] = {};

    // DAW or standalone BPM – updated every processBlock
    std::atomic<double> currentBpm{120.0};

    // Standalone-only BPM parameter (automatable)
    osci::FloatParameter* standaloneBpm = new osci::FloatParameter("Tempo", "standaloneBpm", VERSION_HINT, 120.0f, 20.0f, 300.0f, 0.1f);

    // UI-persisted LFO state (preset type + active tab)
    LfoPreset lfoPresets[NUM_LFOS] = { LfoPreset::Triangle, LfoPreset::Triangle, LfoPreset::Triangle, LfoPreset::Triangle, LfoPreset::Triangle, LfoPreset::Triangle, LfoPreset::Triangle, LfoPreset::Triangle };
    LfoRateMode lfoRateModes[NUM_LFOS] = { LfoRateMode::Seconds, LfoRateMode::Seconds, LfoRateMode::Seconds, LfoRateMode::Seconds, LfoRateMode::Seconds, LfoRateMode::Seconds, LfoRateMode::Seconds, LfoRateMode::Seconds };
    LfoMode lfoModes[NUM_LFOS] = { LfoMode::Free, LfoMode::Free, LfoMode::Free, LfoMode::Free, LfoMode::Free, LfoMode::Free, LfoMode::Free, LfoMode::Free };
    std::atomic<float> lfoPhaseOffsets[NUM_LFOS] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    std::atomic<float> lfoSmoothAmounts[NUM_LFOS] = { 0.005f, 0.005f, 0.005f, 0.005f, 0.005f, 0.005f, 0.005f, 0.005f }; // seconds
    std::atomic<float> lfoDelayAmounts[NUM_LFOS] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }; // seconds
    int lfoTempoDivisions[NUM_LFOS] = { 8, 8, 8, 8, 8, 8, 8, 8 };  // index into getTempoDivisions(), default 1/4
    int activeLfoTab = 0;

    void lfoWaveformChanged(int index, const LfoWaveform& waveform);
    void addLfoAssignment(const LfoAssignment& assignment);
    void removeLfoAssignment(int lfoIndex, const juce::String& paramId);
    void removeAllAssignmentsForEffect(const osci::Effect& effect);
    void autoAssignLfosForEffect(osci::Effect& effect);
    std::vector<LfoAssignment> getLfoAssignments() const;

    // Returns all modulation source bindings for generic wiring in EffectComponent.
    std::vector<ModulationSourceBinding> getModulationSourceBindings();

    // Preview LFO assignment: temporarily assign LFOs while hovering an effect
    void autoAssignLfosForPreview(const juce::String& effectId);
    void clearPreviewLfoAssignments();
    void promotePreviewLfoAssignments();
    LfoWaveform getLfoWaveform(int index) const;

    // Get the current LFO output value (0..1) for visualization
    float getLfoCurrentValue(int lfoIndex) const;
    // Get the current LFO phase [0,1) for flow marker visualization
    float getLfoCurrentPhase(int lfoIndex) const;
    // Returns true if the LFO is actively modulating (not idle/finished)
    bool isLfoActive(int lfoIndex) const;
    // Consumes the retrigger flag (returns true once, then resets). Call from UI thread.
    bool consumeLfoRetriggered(int lfoIndex) { return lfoRetriggered[lfoIndex].exchange(false, std::memory_order_relaxed); }

    // Thread-safe setters for LFO rate mode/division (acquires lfoWaveformLock)
    void setLfoRateMode(int lfoIndex, LfoRateMode mode);
    void setLfoTempoDivision(int lfoIndex, int divisionIndex);
    void setLfoMode(int lfoIndex, LfoMode mode);
    void setLfoPhaseOffset(int lfoIndex, float phase);
    void setLfoSmoothAmount(int lfoIndex, float seconds);
    void setLfoDelayAmount(int lfoIndex, float seconds);

    // Thread-safe getters for LFO rate mode/division
    LfoRateMode getLfoRateMode(int lfoIndex) const;
    int getLfoTempoDivision(int lfoIndex) const;
    LfoMode getLfoMode(int lfoIndex) const;
    float getLfoPhaseOffset(int lfoIndex) const;
    float getLfoSmoothAmount(int lfoIndex) const;
    float getLfoDelayAmount(int lfoIndex) const;

    // === Global Random modulation system ===
    osci::FloatParameter* randomRate[NUM_RANDOM_SOURCES] = {};

    // UI-persisted Random state
    RandomStyle randomStyles[NUM_RANDOM_SOURCES] = { RandomStyle::Perlin, RandomStyle::Perlin, RandomStyle::Perlin };
    LfoRateMode randomRateModes[NUM_RANDOM_SOURCES] = { LfoRateMode::Seconds, LfoRateMode::Seconds, LfoRateMode::Seconds };
    int randomTempoDivisions[NUM_RANDOM_SOURCES] = { 8, 8, 8 };
    int activeRandomTab = 0;

    void addRandomAssignment(const RandomAssignment& assignment);
    void removeRandomAssignment(int randomIndex, const juce::String& paramId);
    std::vector<RandomAssignment> getRandomAssignments() const;

    // === Global Sidechain modulation system ===
    int activeSidechainTab = 0;

    void addSidechainAssignment(const SidechainAssignment& assignment);
    void removeSidechainAssignment(int scIndex, const juce::String& paramId);
    std::vector<SidechainAssignment> getSidechainAssignments() const;

    float getSidechainCurrentValue(int scIndex) const;
    bool isSidechainActive(int scIndex) const;
    float getSidechainInputLevel(int scIndex) const;

    // Attack/release time (seconds)
    std::atomic<float> sidechainAttack{0.3f};
    std::atomic<float> sidechainRelease{0.3f};

    void setSidechainAttack(int scIndex, float seconds);
    void setSidechainRelease(int scIndex, float seconds);
    float getSidechainAttack(int scIndex) const;
    float getSidechainRelease(int scIndex) const;

    // Transfer curve (2 nodes: start input min -> end input max)
    void setSidechainTransferCurve(int scIndex, const std::vector<GraphNode>& nodes);
    std::vector<GraphNode> getSidechainTransferCurve(int scIndex) const;

    float getRandomCurrentValue(int randomIndex) const;
    bool isRandomActive(int randomIndex) const;
    bool consumeRandomRetriggered(int randomIndex) { return randomRetriggered[randomIndex].exchange(false, std::memory_order_relaxed); }

    // UI thread drains subsampled values from the audio thread ring buffer.
    int drainRandomUIBuffer(int randomIndex, RandomUIRingBuffer::Entry* out, int maxEntries);

    void setRandomRateMode(int randomIndex, LfoRateMode mode);
    void setRandomTempoDivision(int randomIndex, int divisionIndex);
    void setRandomStyle(int randomIndex, RandomStyle style);

    LfoRateMode getRandomRateMode(int randomIndex) const;
    int getRandomTempoDivision(int randomIndex) const;
    RandomStyle getRandomStyle(int randomIndex) const;

    juce::MidiKeyboardState keyboardState;

    osci::IntParameter* voices = new osci::IntParameter("Voices", "voices", VERSION_HINT, 4, 1, 16);

    // 1..100 maps to file index with a 1-based offset (1 = first file, 2 = second, ...). Intended for DAW automation.
    osci::IntParameter* fileSelect = new osci::IntParameter("File Select", "fileSelect", VERSION_HINT, 1, 1, 100);

    // Audio-thread readable pointer to the currently selected sound.
    // Lifetime is owned by defaultSound/objectServerSound/sounds[].
    ShapeSound* getActiveShapeSound() const { return activeShapeSound.load(std::memory_order_acquire); }

    osci::BooleanParameter* animateFrames = new osci::BooleanParameter("Animate", "animateFrames", VERSION_HINT, true, "Enables animation for files that have multiple frames, such as GIFs or Line Art.");
    osci::BooleanParameter* loopAnimation = new osci::BooleanParameter("Loop Animation", "loopAnimation", VERSION_HINT, true, "Loops the animation. If disabled, the animation will stop at the last frame.");
    osci::BooleanParameter* animationSyncBPM = new osci::BooleanParameter("Sync To BPM", "animationSyncBPM", VERSION_HINT, false, "Synchronises the animation's framerate with the BPM of your DAW.");
    osci::FloatParameter* animationRate = new osci::FloatParameter("Animation Rate", "animationRate", VERSION_HINT, 30, -1000, 1000);
    osci::FloatParameter* animationOffset = new osci::FloatParameter("Animation Offset", "animationOffset", VERSION_HINT, 0, -10000, 10000);

    osci::BooleanParameter* invertImage = new osci::BooleanParameter("Invert Image", "invertImage", VERSION_HINT, false, "Inverts the image so that dark pixels become light, and vice versa.");
    std::shared_ptr<osci::Effect> imageThreshold = std::make_shared<osci::SimpleEffect>(
        [this](int index, osci::Point input, const std::vector<std::atomic<float>>& values, float sampleRate, float frequency) {
            return input;
        },
        new osci::EffectParameter(
            "Image Threshold",
            "Controls the probability of visiting a dark pixel versus a light pixel. Darker pixels are less likely to be visited, so turning the threshold to a lower value makes it more likely to visit dark pixels.",
            "imageThreshold",
            VERSION_HINT, 0.5, 0, 1));
    std::shared_ptr<osci::Effect> imageStride = std::make_shared<osci::SimpleEffect>(
        [this](int index, osci::Point input, const std::vector<std::atomic<float>>& values, float sampleRate, float frequency) {
            return input;
        },
        new osci::EffectParameter(
            "Image Stride",
            "Controls the spacing between pixels when drawing an image. Larger values mean more of the image can be drawn, but at a lower fidelity.",
            "imageStride",
            VERSION_HINT, 4, 1, 50, 1));

    // Fractal L-system parameters
    std::atomic<int> fractalIterations{3};

    std::shared_ptr<osci::Effect> fractalIterationsEffect = std::make_shared<osci::SimpleEffect>(
        [this](int index, osci::Point input, const std::vector<std::atomic<float>>& values, float sampleRate, float frequency) {
            fractalIterations.store(juce::roundToInt(values[0].load()), std::memory_order_relaxed);
            return input;
        },
        new osci::EffectParameter(
            "Fractal Depth",
            "Controls the recursion depth of the L-system fractal. Higher values produce more detail but require more processing.",
            "fractalIterations",
            VERSION_HINT, 3.0, 0.0, 15.0, 1.0));

    std::atomic<double> animationFrame = 0.f;

    const double FONT_SIZE = 1.0f;
    juce::Font font = juce::Font(juce::Font::getDefaultMonospacedFontName(), FONT_SIZE, juce::Font::plain);

    ShapeSound::Ptr objectServerSound = new ShapeSound();

    std::function<void()> haltRecording;

    // Add a callback to notify the editor when a file is removed
    std::function<void(int)> fileRemovedCallback;

    void addLuaSlider();
    void updateEffectPrecedence();
    void updateFileBlock(int index, std::shared_ptr<juce::MemoryBlock> block);
    void addFile(juce::File file);
    void addFile(juce::String fileName, const char* data, const int size);
    void addFile(juce::String fileName, std::shared_ptr<juce::MemoryBlock> data);
    void removeFile(int index);
    int numFiles();
    void changeCurrentFile(int index);
    void openFile(int index);
    int getCurrentFileIndex();
    std::shared_ptr<FileParser> getCurrentFileParser();
    juce::String getCurrentFileName();
    juce::String getFileName(int index);
    juce::String getFileId(int index);
    std::shared_ptr<juce::MemoryBlock> getFileBlock(int index);
    void setObjectServerRendering(bool enabled);
    void setObjectServerPort(int port);
    void addErrorListener(ErrorListener* listener);
    void removeErrorListener(ErrorListener* listener);
    void notifyErrorListeners(int lineNumber, juce::String id, juce::String error);

    // Preview API: set/clear a temporary effect by ID for hover auditioning
    void setPreviewEffectId(const juce::String& effectId);
    void clearPreviewEffect();
    std::shared_ptr<osci::SimpleEffect> getCachedPreviewEffect() { 
        return std::dynamic_pointer_cast<osci::SimpleEffect>(previewEffect); 
    }

    // Get the external input buffer for effects that need it
    juce::AudioBuffer<float>* getInputBuffer() { return &inputBuffer; }

    // Centralized toggleable effect application (used by both synth voices and audio-input mode)
    // effectsLock should be held when calling this from the audio thread.
    void applyToggleableEffectsToBuffer(
        juce::AudioBuffer<float>& buffer,
        juce::AudioBuffer<float>* externalInput,
        juce::AudioBuffer<float>* volumeBuffer,
        juce::AudioBuffer<float>* frequencyBuffer,
        juce::AudioBuffer<float>* frameSyncBuffer,
        const std::unordered_map<juce::String, std::shared_ptr<osci::SimpleEffect>>* perVoiceEffects,
        const std::shared_ptr<osci::Effect>& previewEffectInstance);

    // Setter for the callback
    void setFileRemovedCallback(std::function<void(int)> callback);

    // Added declaration for the new `removeParser` method.
    void removeParser(FileParser* parser);

    const std::vector<juce::String> FILE_EXTENSIONS = {
        "obj",
        "svg",
        "lua",
        "txt",
        "gpla",
        "gif",
        "png",
        "jpg",
        "jpeg",
        "wav",
        "aiff",
        "ogg",
        "flac",
        "mp3",
        "lsystem",
#if OSCI_PREMIUM
        "mp4",
        "mov",
#endif
    };

private:
    juce::AudioBuffer<float> inputBuffer;
    juce::AudioBuffer<float> inputFrequencyBuffer;

    std::atomic<bool> prevMidiEnabled = !midiEnabled->getBoolValue();

    juce::SpinLock audioThreadCallbackLock;
    std::function<void(const juce::AudioBuffer<float>&)> audioThreadCallback;

    juce::SpinLock errorListenersLock;
    std::vector<ErrorListener*> errorListeners;

    ShapeSound::Ptr defaultSound;
    PublicSynthesiser synth;
    bool retriggerMidi = true;

    ObjectServer objectServer{*this};

    // Peak-rectified input audio: per-sample max(|L|, |R|), no smoothing.
    // Fed into envelope followers (sidechain, beginner-mode per-parameter sidechain).
    juce::AudioBuffer<float> rectifiedInputBuffer;

    // Default envelope follower for beginner-mode per-parameter sidechain.
    // Uses fixed attack/release so beginner-mode effects respond to input level.
    static constexpr float kDefaultEnvelopeAttack  = 0.1f;
    static constexpr float kDefaultEnvelopeRelease = 0.1f;
    // Linear identity curve: input [0,1] → output [0,1] unchanged.
    // Used by the default envelope follower for beginner-mode sidechain.
    const std::vector<GraphNode> kIdentityCurve = { { 0.0, 0.0, 0.0f }, { 1.0, 1.0, 0.0f } };
    SidechainAudioState defaultEnvelopeState;
    juce::AudioBuffer<float> currentVolumeBuffer;

    std::pair<std::shared_ptr<osci::Effect>, osci::EffectParameter*> effectFromLegacyId(const juce::String& id, bool updatePrecedence = false);
    osci::LfoType lfoTypeFromLegacyAnimationType(const juce::String& type);
    double valueFromLegacy(double value, const juce::String& id);
    void changeSound(ShapeSound::Ptr sound);

    // parsersLock AND effectsLock must be held when calling this
    void applyFileSelectLocked();

    std::atomic<ShapeSound*> activeShapeSound { nullptr };

    struct FileSelectionAsyncNotifier : public juce::AsyncUpdater {
        explicit FileSelectionAsyncNotifier(OscirenderAudioProcessor& p) : processor(p) {}
        void handleAsyncUpdate() override { processor.fileChangeBroadcaster.sendChangeMessage(); }
        OscirenderAudioProcessor& processor;
    };

    std::unique_ptr<FileSelectionAsyncNotifier> fileSelectionNotifier;

    void parseVersion(int result[3], const juce::String& input) {
        std::istringstream parser(input.toStdString());
        parser >> result[0];
        for (int idx = 1; idx < 3; idx++) {
            parser.get(); // Skip period
            parser >> result[idx];
        }
    }

    bool lessThanVersion(const juce::String& a, const juce::String& b) {
        int parsedA[3], parsedB[3];
        parseVersion(parsedA, a);
        parseVersion(parsedB, b);
        return std::lexicographical_compare(parsedA, parsedA + 3, parsedB, parsedB + 3);
    }

    juce::AudioPlayHead* playHead;

    // Global LFO audio-thread state
    LfoWaveform lfoWaveforms[NUM_LFOS];
    mutable juce::SpinLock lfoWaveformLock;
    std::vector<LfoAssignment> lfoAssignments;
    mutable juce::SpinLock lfoAssignmentLock;
    LfoAudioState lfoAudioStates[NUM_LFOS];
    float lfoSmoothedOutput[NUM_LFOS] = {}; // One-pole filter state for LFO output smoothing
    float lfoDelayElapsed[NUM_LFOS] = {}; // Seconds elapsed since LFO trigger (for startup delay)
    int lfoActiveNoteCount = 0;  // Tracks how many MIDI notes are currently held for LFO triggering
    bool lfoPrevAnyVoiceActive = false;  // Previous block's effective voice-active state for transition detection
    std::array<std::vector<float>, NUM_LFOS> lfoBlockBuffer;

    // Thread-safe snapshot of most recent LFO output for UI visualization
    std::atomic<float> lfoCurrentValues[NUM_LFOS] = {};
    // Thread-safe snapshot of most recent LFO phase [0,1) for UI flow markers
    std::atomic<float> lfoCurrentPhases[NUM_LFOS] = {};
    // Whether each LFO is actively modulating (not idle/finished) — for UI marker visibility
    std::atomic<bool> lfoActive[NUM_LFOS] = {};
    // Pulsed true on a noteOn while the LFO was already running — consumed by UI to reset flow trail
    std::atomic<bool> lfoRetriggered[NUM_LFOS] = {};

    // Preview LFO assignment state (message-thread only — no lock needed)
    juce::String previewLfoEffectId;
    std::vector<std::pair<juce::String, float>> previewSavedParamValues;
    void clearPreviewLfoAssignmentsInternal();

    void applyGlobalLfoModulation(int numSamples, double sampleRate, const juce::MidiBuffer& midi);

    // Global Envelope assignment audio-thread state
    std::vector<EnvAssignment> envAssignments;
    mutable juce::SpinLock envAssignmentLock;
    // Per-envelope global DAHDSR state (triggered on note-on, tracks highest active voice)
    DahdsrState envGlobalStates[NUM_ENVELOPES];
    std::atomic<float> envCurrentValues[NUM_ENVELOPES] = {};
    std::atomic<bool> envNoteOnPending{false}; // Set by MIDI note-on, consumed by audio thread
    std::atomic<bool> envNoteOffPending{false}; // Set by MIDI note-off
    std::array<std::vector<float>, NUM_ENVELOPES> envBlockBuffer;
    std::array<float, NUM_ENVELOPES> envPrevBlockValues = {};

    void applyGlobalEnvModulation(int numSamples, double sampleRate);

    // Global Random modulation audio-thread state
    std::vector<RandomAssignment> randomAssignments;
    mutable juce::SpinLock randomAssignmentLock;
    RandomAudioState randomAudioStates[NUM_RANDOM_SOURCES];
    int randomActiveNoteCount = 0;
    bool randomPrevAnyVoiceActive = false;
    std::array<std::vector<float>, NUM_RANDOM_SOURCES> randomBlockBuffer;

    std::atomic<float> randomCurrentValues[NUM_RANDOM_SOURCES] = {};
    std::atomic<bool> randomActive[NUM_RANDOM_SOURCES] = {};
    std::atomic<bool> randomRetriggered[NUM_RANDOM_SOURCES] = {};

    RandomUIRingBuffer randomUIBuffers[NUM_RANDOM_SOURCES];
    static constexpr int kRandomUISubsampleInterval = 64;

    void applyGlobalRandomModulation(int numSamples, double sampleRate, const juce::MidiBuffer& midi);

    // Global Sidechain modulation audio-thread state
    std::vector<SidechainAssignment> sidechainAssignments;
    mutable juce::SpinLock sidechainAssignmentLock;
    SidechainAudioState sidechainAudioStates[NUM_SIDECHAINS];
    std::array<std::vector<float>, NUM_SIDECHAINS> sidechainBlockBuffer;
    std::atomic<float> sidechainCurrentValues[NUM_SIDECHAINS] = {};
    std::atomic<float> sidechainInputLevels[NUM_SIDECHAINS] = {};
    std::vector<GraphNode> sidechainTransferCurve;
    std::vector<GraphNode> sidechainFullCurve; // cached: corner + transfer + corner
    mutable juce::SpinLock sidechainCurveLock;

    void applyGlobalSidechainModulation(int numSamples, double sampleRate, const float* rectifiedIn);

    // Rebuild sidechainFullCurve from sidechainTransferCurve. Call while holding sidechainCurveLock.
    void rebuildSidechainFullCurve();

    // Generic helper: applies per-sample modulation from precomputed source buffers
    // to effect parameters via assignments. Works for any modulation source type.
    void applyModulationBuffers(int numSamples,
                                const std::vector<ModAssignment>& assignments,
                                const std::vector<float>* sourceBuffers,
                                int maxSourceIndex);

    // Precomputed paramId → (effect*, paramIndex) lookup for O(1) modulation target resolution.
    // Built once after all effects are populated; the effect lists are stable after construction.
    struct ParamLocation {
        osci::Effect* effect;
        int paramIndex;
    };
    std::unordered_map<juce::String, ParamLocation> paramLocationMap;
    void buildParamLocationMap();

    // Beginner/advanced mode flag — read from global settings at construction, immutable after.
    // Free builds are always beginner; premium reads the persisted preference.
    bool beginnerMode = true;

#if (JUCE_MAC || JUCE_WINDOWS) && OSCI_PREMIUM
public:
    std::atomic<bool> syphonInputActive = false;

    ImageParser syphonImageParser = ImageParser(*this);
#endif


    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OscirenderAudioProcessor)
};

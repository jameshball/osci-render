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
#include "audio/effects/CustomEffect.h"
#include "audio/effects/DelayEffect.h"
#include "audio/modulation/LuaEffectState.h"
#include "audio/effects/PerspectiveEffect.h"
#include "audio/synthesis/PublicSynthesiser.h"
#include "audio/platform/SampleRateManager.h"
#include "audio/synthesis/ShapeSound.h"
#include "audio/synthesis/ShapeVoice.h"
#include "audio/modulation/DahdsrEnvelope.h"
#include "audio/modulation/LfoState.h"
#include "audio/modulation/LfoParameters.h"
#include "audio/modulation/EnvState.h"
#include "audio/modulation/EnvelopeParameters.h"
#include "audio/modulation/RandomState.h"
#include "audio/modulation/RandomParameters.h"
#include "audio/modulation/SidechainState.h"
#include "audio/modulation/SidechainParameters.h"
#include "audio/modulation/ModulationEngine.h"
#include "audio/modulation/ModulationTypes.h"
#include "obj/ObjectServer.h"

class FileParser;

#if (JUCE_MAC || JUCE_WINDOWS) && OSCI_PREMIUM
#include "parser/img/ImageParser.h"
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

    // Beginner/advanced mode flag — read from global settings at construction, immutable after.
    // Free builds are always beginner; premium reads the persisted preference.
    bool beginnerMode = true;

    // === Envelope modulation state ===
    EnvelopeParameters envelopeParameters;

    // Look up human-readable name for any parameter by ID (searches all effects).
    juce::String getParamDisplayName(const juce::String& paramId) const;

    // DAW or standalone BPM – updated every processBlock
    std::atomic<double> currentBpm{120.0};

    // Standalone-only BPM parameter (automatable)
    osci::FloatParameter* standaloneBpm = new osci::FloatParameter("Tempo", "standaloneBpm", VERSION_HINT, 120.0f, 20.0f, 300.0f, 0.1f);

    // === Global modulation state classes ===
    LfoParameters lfoParameters;
    RandomParameters randomParameters;
    SidechainParameters sidechainParameters;

    // Cross-modulation-source operations (delegated to modulationEngine declared below)
    void removeAllAssignmentsForEffect(const osci::Effect& effect);

    // LFO auto-assignment (delegates to lfoParameters)
    void autoAssignLfosForEffect(osci::Effect& effect);
    void autoAssignLfosForPreview(const juce::String& effectId);
    void clearPreviewLfoAssignments();
    void promotePreviewLfoAssignments();

    // Returns all modulation source bindings for generic wiring in EffectComponent.
    std::vector<ModulationSourceBinding> getModulationSourceBindings();

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

    // Precomputed paramId → (effect*, paramIndex) lookup for O(1) modulation target resolution.
    // Built once after all effects are populated; the effect lists are stable after construction.
    std::unordered_map<juce::String, ParamLocation> paramLocationMap;
    void buildParamLocationMap();

    // Modulation engine: operates on all sources via common ModulationSource interface.
    // Must be declared after paramLocationMap (it holds a reference to it).
    ModulationEngine modulationEngine{paramLocationMap};

#if (JUCE_MAC || JUCE_WINDOWS) && OSCI_PREMIUM
public:
    std::atomic<bool> syphonInputActive = false;

    ImageParser syphonImageParser = ImageParser(*this);
#endif


    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OscirenderAudioProcessor)
};

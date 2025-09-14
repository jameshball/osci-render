/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

#include <numbers>

#include "CommonPluginProcessor.h"
#include "UGen/Env.h"
#include "UGen/ugen_JuceEnvelopeComponent.h"
#include "audio/CustomEffect.h"
#include "audio/DelayEffect.h"
#include "audio/PerspectiveEffect.h"
#include "audio/PublicSynthesiser.h"
#include "audio/SampleRateManager.h"
#include "audio/ShapeSound.h"
#include "audio/ShapeVoice.h"
#include "obj/ObjectServer.h"

#if (JUCE_MAC || JUCE_WINDOWS) && OSCI_PREMIUM
#include "../modules/juce_sharedtexture/SharedTexture.h"
#include "video/SyphonFrameGrabber.h"
#endif

//==============================================================================
/**
 */
class OscirenderAudioProcessor : public CommonAudioProcessor, juce::AudioProcessorParameter::Listener, public EnvelopeComponentListener
#if JucePlugin_Enable_ARA
    ,
                                 public juce::AudioProcessorARAExtension
#endif
{
public:
    OscirenderAudioProcessor();
    ~OscirenderAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;

    void setAudioThreadCallback(std::function<void(const juce::AudioBuffer<float>&)> callback);

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;
    void parameterValueChanged(int parameterIndex, float newValue) override;
    void parameterGestureChanged(int parameterIndex, bool gestureIsStarting) override;
    void envelopeChanged(EnvelopeComponent* changedEnvelope) override;

    std::vector<std::shared_ptr<osci::Effect>> toggleableEffects;
    std::vector<std::shared_ptr<osci::Effect>> luaEffects;
    // Temporary preview effect applied while hovering effects in the grid (guarded by effectsLock)
    std::shared_ptr<osci::Effect> previewEffect;
    std::atomic<double> luaValues[26] = {0.0};

    std::shared_ptr<osci::Effect> frequencyEffect = std::make_shared<osci::SimpleEffect>(
        [this](int index, osci::Point input, const std::vector<std::atomic<double>>& values, double sampleRate) {
            // TODO: Root cause why the epsilon is needed. This prevents a weird bug on mac.
            frequency = values[0].load() + 0.00001;
            return input;
        },
        new osci::EffectParameter(
            "Frequency",
            "Controls how many times per second the image is drawn, thereby controlling the pitch of the sound. Lower frequencies result in more-accurately drawn images, but more flickering, and vice versa.",
            "frequency",
            VERSION_HINT, 220.0, 0.0, 4200.0));

    std::shared_ptr<DelayEffect> delayEffect = std::make_shared<DelayEffect>();

    std::function<void(int, juce::String, juce::String)> errorCallback = [this](int lineNum, juce::String fileName, juce::String error) { notifyErrorListeners(lineNum, fileName, error); };
    std::shared_ptr<CustomEffect> customEffect = std::make_shared<CustomEffect>(errorCallback, luaValues);
    std::shared_ptr<osci::Effect> custom = std::make_shared<osci::SimpleEffect>(
        customEffect,
        new osci::EffectParameter("Lua Effect", "Controls the strength of the custom Lua effect applied. You can write your own custom effect using Lua by pressing the edit button on the right.", "customEffectStrength", VERSION_HINT, 1.0, 0.0, 1.0));

    std::shared_ptr<osci::Effect> perspective = PerspectiveEffect().build();

    osci::BooleanParameter* midiEnabled = new osci::BooleanParameter("MIDI Enabled", "midiEnabled", VERSION_HINT, false, "Enable MIDI input for the synth. If disabled, the synth will play a constant tone, as controlled by the frequency slider.");
    osci::BooleanParameter* inputEnabled = new osci::BooleanParameter("Audio Input Enabled", "inputEnabled", VERSION_HINT, false, "Enable to use input audio, instead of the generated audio.");
    std::atomic<double> frequency = 220.0;

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

    osci::FloatParameter* attackTime = new osci::FloatParameter("Attack Time", "attackTime", VERSION_HINT, 0.005, 0.0, 1.0);
    osci::FloatParameter* attackLevel = new osci::FloatParameter("Attack Level", "attackLevel", VERSION_HINT, 1.0, 0.0, 1.0);
    osci::FloatParameter* decayTime = new osci::FloatParameter("Decay Time", "decayTime", VERSION_HINT, 0.095, 0.0, 1.0);
    osci::FloatParameter* sustainLevel = new osci::FloatParameter("Sustain Level", "sustainLevel", VERSION_HINT, 0.6, 0.0, 1.0);
    osci::FloatParameter* releaseTime = new osci::FloatParameter("Release Time", "releaseTime", VERSION_HINT, 0.4, 0.0, 1.0);
    osci::FloatParameter* attackShape = new osci::FloatParameter("Attack Shape", "attackShape", VERSION_HINT, 5, -50, 50);
    osci::FloatParameter* decayShape = new osci::FloatParameter("Decay osci::Shape", "decayShape", VERSION_HINT, -20, -50, 50);
    osci::FloatParameter* releaseShape = new osci::FloatParameter("Release Shape", "releaseShape", VERSION_HINT, -5, -50, 50);

    Env adsrEnv = Env::adsr(
        attackTime->getValueUnnormalised(),
        decayTime->getValueUnnormalised(),
        sustainLevel->getValueUnnormalised(),
        releaseTime->getValueUnnormalised(),
        1.0,
        std::vector<EnvCurve>{attackShape->getValueUnnormalised(), decayShape->getValueUnnormalised(), releaseShape->getValueUnnormalised()});

    juce::MidiKeyboardState keyboardState;

    osci::IntParameter* voices = new osci::IntParameter("Voices", "voices", VERSION_HINT, 4, 1, 16);

    osci::BooleanParameter* animateFrames = new osci::BooleanParameter("Animate", "animateFrames", VERSION_HINT, true, "Enables animation for files that have multiple frames, such as GIFs or Line Art.");
    osci::BooleanParameter* loopAnimation = new osci::BooleanParameter("Loop Animation", "loopAnimation", VERSION_HINT, true, "Loops the animation. If disabled, the animation will stop at the last frame.");
    osci::BooleanParameter* animationSyncBPM = new osci::BooleanParameter("Sync To BPM", "animationSyncBPM", VERSION_HINT, false, "Synchronises the animation's framerate with the BPM of your DAW.");
    osci::FloatParameter* animationRate = new osci::FloatParameter("Animation Rate", "animationRate", VERSION_HINT, 30, -1000, 1000);
    osci::FloatParameter* animationOffset = new osci::FloatParameter("Animation Offset", "animationOffset", VERSION_HINT, 0, -10000, 10000);

    osci::BooleanParameter* invertImage = new osci::BooleanParameter("Invert Image", "invertImage", VERSION_HINT, false, "Inverts the image so that dark pixels become light, and vice versa.");
    std::shared_ptr<osci::Effect> imageThreshold = std::make_shared<osci::SimpleEffect>(
        [this](int index, osci::Point input, const std::vector<std::atomic<double>>& values, double sampleRate) {
            return input;
        },
        new osci::EffectParameter(
            "Image Threshold",
            "Controls the probability of visiting a dark pixel versus a light pixel. Darker pixels are less likely to be visited, so turning the threshold to a lower value makes it more likely to visit dark pixels.",
            "imageThreshold",
            VERSION_HINT, 0.5, 0, 1));
    std::shared_ptr<osci::Effect> imageStride = std::make_shared<osci::SimpleEffect>(
        [this](int index, osci::Point input, const std::vector<std::atomic<double>>& values, double sampleRate) {
            return input;
        },
        new osci::EffectParameter(
            "Image Stride",
            "Controls the spacing between pixels when drawing an image. Larger values mean more of the image can be drawn, but at a lower fidelity.",
            "imageStride",
            VERSION_HINT, 4, 1, 50, 1));

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
#if OSCI_PREMIUM
        "mp4",
        "mov",
#endif
    };

private:
    juce::AudioBuffer<float> inputBuffer;

    std::atomic<bool> prevMidiEnabled = !midiEnabled->getBoolValue();

    juce::SpinLock audioThreadCallbackLock;
    std::function<void(const juce::AudioBuffer<float>&)> audioThreadCallback;

    juce::SpinLock errorListenersLock;
    std::vector<ErrorListener*> errorListeners;

    ShapeSound::Ptr defaultSound = new ShapeSound(*this, std::make_shared<FileParser>(*this));
    PublicSynthesiser synth;
    bool retriggerMidi = true;

    ObjectServer objectServer{*this};

    const double VOLUME_BUFFER_SECONDS = 0.1;

    std::vector<double> volumeBuffer;
    int volumeBufferIndex = 0;
    double squaredVolume = 0;
    double currentVolume = 0;

    std::pair<std::shared_ptr<osci::Effect>, osci::EffectParameter*> effectFromLegacyId(const juce::String& id, bool updatePrecedence = false);
    osci::LfoType lfoTypeFromLegacyAnimationType(const juce::String& type);
    double valueFromLegacy(double value, const juce::String& id);
    void changeSound(ShapeSound::Ptr sound);

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

    const double MIN_LENGTH_INCREMENT = 0.000001;

    juce::AudioPlayHead* playHead;

#if (JUCE_MAC || JUCE_WINDOWS) && OSCI_PREMIUM
public:
    std::atomic<bool> syphonInputActive = false;

    ImageParser syphonImageParser = ImageParser(*this);
#endif


    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OscirenderAudioProcessor)
};

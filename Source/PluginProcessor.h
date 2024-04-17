/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "shape/Shape.h"
#include "concurrency/BufferConsumer.h"
#include "audio/Effect.h"
#include "audio/ShapeSound.h"
#include "audio/ShapeVoice.h"
#include "audio/PublicSynthesiser.h"
#include <numbers>
#include "audio/AudioWebSocketServer.h"
#include "audio/DelayEffect.h"
#include "audio/PitchDetector.h"
#include "audio/WobbleEffect.h"
#include "audio/PerspectiveEffect.h"
#include "obj/ObjectServer.h"
#include "UGen/Env.h"
#include "UGen/ugen_JuceEnvelopeComponent.h"
#include "audio/CustomEffect.h"
#include "audio/DashedLineEffect.h"

//==============================================================================
/**
*/
class OscirenderAudioProcessor  : public juce::AudioProcessor, juce::AudioProcessorParameter::Listener, public EnvelopeComponentListener
                            #if JucePlugin_Enable_ARA
                             , public juce::AudioProcessorARAExtension
                            #endif
{
public:
    OscirenderAudioProcessor();
    ~OscirenderAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;

    void setAudioThreadCallback(std::function<void(const juce::AudioBuffer<float>&)> callback);

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;
    std::shared_ptr<BufferConsumer> consumerRegister(std::vector<float>& buffer);
    void consumerStop(std::shared_ptr<BufferConsumer> consumer);
    void consumerRead(std::shared_ptr<BufferConsumer> consumer);
    void parameterValueChanged(int parameterIndex, float newValue) override;
    void parameterGestureChanged(int parameterIndex, bool gestureIsStarting) override;
    void envelopeChanged(EnvelopeComponent* changedEnvelope) override;
    
    int VERSION_HINT = 2;

    std::atomic<double> currentSampleRate = 0.0;

    juce::SpinLock effectsLock;
	std::vector<std::shared_ptr<Effect>> toggleableEffects;
    std::vector<std::shared_ptr<Effect>> luaEffects;
    double luaValues[26] = { 0.0 };

    std::shared_ptr<Effect> frequencyEffect = std::make_shared<Effect>(
        [this](int index, Point input, const std::vector<double>& values, double sampleRate) {
            frequency = values[0];
            return input;
        }, new EffectParameter(
            "Frequency",
            "Controls how many times per second the image is drawn, thereby controlling the pitch of the sound. Lower frequencies result in more-accurately drawn images, but more flickering, and vice versa.",
            "frequency",
            VERSION_HINT, 220.0, 0.0, 12000.0, 0.1
        )
    );

    std::shared_ptr<Effect> volumeEffect = std::make_shared<Effect>(
        [this](int index, Point input, const std::vector<double>& values, double sampleRate) {
            volume = values[0];
            return input;
        }, new EffectParameter(
            "Volume",
            "Controls the volume of the sound. Works by scaling the image and sound by a factor.",
            "volume",
            VERSION_HINT, 1.0, 0.0, 3.0
        )
    );

    std::shared_ptr<Effect> thresholdEffect = std::make_shared<Effect>(
        [this](int index, Point input, const std::vector<double>& values, double sampleRate) {
            threshold = values[0];
            return input;
        }, new EffectParameter(
            "Threshold",
            "Clips the sound and image to a maximum value. Applying a harsher threshold results in a more distorted sound.",
            "threshold",
            VERSION_HINT, 1.0, 0.0, 1.0
        )
    );
    
    std::shared_ptr<Effect> traceMax = std::make_shared<Effect>(
        [this](int index, Point input, const std::vector<double>& values, double sampleRate) {
            return input;
        }, new EffectParameter(
            "Trace max",
            "Defines the maximum proportion of the image that is drawn before skipping to the next frame. This has the effect of 'tracing' out the image from a single dot when animated. By default, we draw until the end of the frame, so this value is 1.0.",
            "traceMax",
            VERSION_HINT, 0.75, 0.0, 1.0
        )
    );
    std::shared_ptr<Effect> traceMin = std::make_shared<Effect>(
        [this](int index, Point input, const std::vector<double>& values, double sampleRate) {
            return input;
        }, new EffectParameter(
            "Trace min",
            "Defines the proportion of the image that drawing starts from. This has the effect of 'tracing' out the image from a single dot when animated. By default, we start drawing from the beginning of the frame, so this value is 0.0.",
            "traceMin",
            VERSION_HINT, 0.25, 0.0, 1.0
        )
    );

    std::shared_ptr<DelayEffect> delayEffect = std::make_shared<DelayEffect>();

    std::shared_ptr<DashedLineEffect> dashedLineEffect = std::make_shared<DashedLineEffect>();

    std::function<void(int, juce::String, juce::String)> errorCallback = [this](int lineNum, juce::String fileName, juce::String error) { notifyErrorListeners(lineNum, fileName, error); };
    std::shared_ptr<CustomEffect> customEffect = std::make_shared<CustomEffect>(errorCallback, luaValues);
    std::shared_ptr<Effect> custom = std::make_shared<Effect>(
        customEffect,
        new EffectParameter("Lua Effect", "Controls the strength of the custom Lua effect applied. You can write your own custom effect using Lua by pressing the edit button on the right.", "customEffectStrength", VERSION_HINT, 1.0, 0.0, 1.0)
    );

    std::shared_ptr<PerspectiveEffect> perspectiveEffect = std::make_shared<PerspectiveEffect>();
    std::shared_ptr<Effect> perspective = std::make_shared<Effect>(
        perspectiveEffect,
        std::vector<EffectParameter*>{
            new EffectParameter("3D Perspective", "Controls the strength of the 3D perspective projection.", "perspectiveStrength", VERSION_HINT, 1.0, 0.0, 1.0),
            new EffectParameter("Focal Length", "Controls the focal length of the 3D perspective effect. A higher focal length makes the image look more flat, and a lower focal length makes the image look more 3D.", "perspectiveFocalLength", VERSION_HINT, 2.0, 0.0, 10.0),
        }
    );
    
    BooleanParameter* midiEnabled = new BooleanParameter("MIDI Enabled", "midiEnabled", VERSION_HINT, false);
    BooleanParameter* inputEnabled = new BooleanParameter("Audio Input Enabled", "inputEnabled", VERSION_HINT, false);
    std::atomic<float> frequency = 220.0f;
    
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

    FloatParameter* attackTime = new FloatParameter("Attack Time", "attackTime", VERSION_HINT, 0.005, 0.0, 1.0);
    FloatParameter* attackLevel = new FloatParameter("Attack Level", "attackLevel", VERSION_HINT, 1.0, 0.0, 1.0);
    FloatParameter* decayTime = new FloatParameter("Decay Time", "decayTime", VERSION_HINT, 0.095, 0.0, 1.0);
    FloatParameter* sustainLevel = new FloatParameter("Sustain Level", "sustainLevel", VERSION_HINT, 0.6, 0.0, 1.0);
    FloatParameter* releaseTime = new FloatParameter("Release Time", "releaseTime", VERSION_HINT, 0.4, 0.0, 1.0);
    FloatParameter* attackShape = new FloatParameter("Attack Shape", "attackShape", VERSION_HINT, 5, -50, 50);
    FloatParameter* decayShape = new FloatParameter("Decay Shape", "decayShape", VERSION_HINT, -20, -50, 50);
    FloatParameter* releaseShape = new FloatParameter("Release Shape", "releaseShape", VERSION_HINT, -5,-50, 50);

    Env adsrEnv = Env::adsr(
        attackTime->getValueUnnormalised(),
        decayTime->getValueUnnormalised(),
        sustainLevel->getValueUnnormalised(),
        releaseTime->getValueUnnormalised(),
        1.0,
        std::vector<EnvCurve>{ attackShape->getValueUnnormalised(), decayShape->getValueUnnormalised(), releaseShape->getValueUnnormalised() }
    );

    juce::MidiKeyboardState keyboardState;

    IntParameter* voices = new IntParameter("Voices", "voices", VERSION_HINT, 4, 1, 16);

    bool animateLineArt = false;
    bool syncMIDIAnimation = false;
    float animationRate = 8.f;
    int animationOffset = 0;
    double animationTime = 0.f;

private:
    juce::SpinLock consumerLock;
    std::vector<std::shared_ptr<BufferConsumer>> consumers;
public:
    
    PitchDetector pitchDetector{*this};
    std::shared_ptr<WobbleEffect> wobbleEffect = std::make_shared<WobbleEffect>(pitchDetector);

    // shouldn't be accessed by audio thread, but needs to persist when GUI is closed
    // so should only be accessed by message thread
    juce::String currentProjectFile;

    juce::SpinLock fontLock;
    juce::Font font = juce::Font(juce::Font::getDefaultSansSerifFontName(), 1.0f, juce::Font::plain);

    ShapeSound::Ptr objectServerSound = new ShapeSound();

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
    void addErrorListener(ErrorListener* listener);
    void removeErrorListener(ErrorListener* listener);
    void notifyErrorListeners(int lineNumber, juce::String id, juce::String error);
private:
    std::atomic<double> volume = 1.0;
    std::atomic<double> threshold = 1.0;
    
    bool prevMidiEnabled = !midiEnabled->getBoolValue();

    juce::SpinLock audioThreadCallbackLock;
    std::function<void(const juce::AudioBuffer<float>&)> audioThreadCallback;

    std::vector<BooleanParameter*> booleanParameters;
    std::vector<FloatParameter*> floatParameters;
    std::vector<IntParameter*> intParameters;
    std::vector<std::shared_ptr<Effect>> allEffects;
    std::vector<std::shared_ptr<Effect>> permanentEffects;

    juce::SpinLock errorListenersLock;
    std::vector<ErrorListener*> errorListeners;

    ShapeSound::Ptr defaultSound = new ShapeSound(std::make_shared<FileParser>());
    PublicSynthesiser synth;
    bool retriggerMidi = true;

    AudioWebSocketServer softwareOscilloscopeServer{*this};
    ObjectServer objectServer{*this};

    const double VOLUME_BUFFER_SECONDS = 0.1;

    std::vector<double> volumeBuffer;
    int volumeBufferIndex = 0;
    double squaredVolume = 0;
    double currentVolume = 0;

    std::shared_ptr<Effect> getEffect(juce::String id);
    BooleanParameter* getBooleanParameter(juce::String id);
    FloatParameter* getFloatParameter(juce::String id);
    IntParameter* getIntParameter(juce::String id);
    void openLegacyProject(const juce::XmlElement* xml);
    std::pair<std::shared_ptr<Effect>, EffectParameter*> effectFromLegacyId(const juce::String& id, bool updatePrecedence = false);
    LfoType lfoTypeFromLegacyAnimationType(const juce::String& type);
    double valueFromLegacy(double value, const juce::String& id);
    void changeSound(ShapeSound::Ptr sound);

    const double MIN_LENGTH_INCREMENT = 0.000001;

    juce::AudioPlayHead* playHead;
    juce::AudioPlayHead::PositionInfo currentPositionInfo;

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OscirenderAudioProcessor)
};

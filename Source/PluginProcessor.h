/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "shape/Shape.h"
#include "parser/FileParser.h"
#include "parser/FrameProducer.h"
#include "parser/FrameConsumer.h"
#include "audio/Effect.h"
#include <numbers>
#include "concurrency/BufferProducer.h"
#include "audio/AudioWebSocketServer.h"
#include "audio/DelayEffect.h"
#include "audio/PitchDetector.h"
#include "audio/WobbleEffect.h"

//==============================================================================
/**
*/
class OscirenderAudioProcessor  : public juce::AudioProcessor
                            #if JucePlugin_Enable_ARA
                             , public juce::AudioProcessorARAExtension
                            #endif
    , public FrameConsumer
{
public:
    //==============================================================================
    OscirenderAudioProcessor();
    ~OscirenderAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

	std::atomic<float> frequency = 440.0f;
    std::atomic<double> volume = 1.0;
    std::atomic<double> threshold = 1.0;

    std::atomic<double> currentSampleRate = 0.0;

    juce::SpinLock effectsLock;
	std::vector<std::shared_ptr<Effect>> allEffects;
	std::vector<std::shared_ptr<Effect>> enabledEffects;
    std::vector<std::shared_ptr<Effect>> luaEffects;

    // TODO see if there is a way to move this code to .cpp
    std::function<Vector2(int, Vector2, std::vector<EffectDetails>, double)> onRotationChange = [this](int index, Vector2 input, std::vector<EffectDetails> details, double sampleRate) {
        if (getCurrentFileIndex() != -1) {
            auto obj = getCurrentFileParser()->getObject();
            if (obj == nullptr) return input;
            obj->setBaseRotation(
                rotateX.getValue() * std::numbers::pi,
                rotateY.getValue() * std::numbers::pi,
                rotateZ.getValue() * std::numbers::pi
            );
        }
        return input;
    };
    
    Effect focalLength{
        [this](int index, Vector2 input, std::vector<EffectDetails> details, double sampleRate) {
            if (getCurrentFileIndex() != -1) {
                auto camera = getCurrentFileParser()->getCamera();
                if (camera == nullptr) return input;
                camera->setFocalLength(details[0].value);
            }
            return input;
		},
        "Focal length",
        "focalLength",
        1
    };
    Effect rotateX{onRotationChange, "Rotate x", "rotateX", 1};
    Effect rotateY{onRotationChange, "Rotate y", "rotateY", 1};
    Effect rotateZ{onRotationChange, "Rotate z", "rotateZ", 0};
    Effect currentRotateX{
        [this](int index, Vector2 input, std::vector<EffectDetails> details, double sampleRate) {
            if (getCurrentFileIndex() != -1) {
                auto obj = getCurrentFileParser()->getObject();
                if (obj == nullptr) return input;
                obj->setCurrentRotationX(details[0].value * std::numbers::pi);
            }
            return input;
        },
        "Current Rotate x",
        "currentRotateX",
        0
    };
    Effect currentRotateY{
        [this](int index, Vector2 input, std::vector<EffectDetails> details, double sampleRate) {
            if (getCurrentFileIndex() != -1) {
                auto obj = getCurrentFileParser()->getObject();
                if (obj == nullptr) return input;
                obj->setCurrentRotationY(details[0].value * std::numbers::pi);
            }
            return input;
        },
        "Current Rotate y",
        "currentRotateY",
        0
    };
    Effect currentRotateZ{
        [this](int index, Vector2 input, std::vector<EffectDetails> details, double sampleRate) {
            if (getCurrentFileIndex() != -1) {
                auto obj = getCurrentFileParser()->getObject();
                if (obj == nullptr) return input;
                obj->setCurrentRotationZ(details[0].value * std::numbers::pi);
            }
            return input;
        },
        "Current Rotate z",
        "currentRotateZ",
        0
    };
    Effect rotateSpeed{
        [this](int index, Vector2 input, std::vector<EffectDetails> details, double sampleRate) {
            if (getCurrentFileIndex() != -1) {
                auto obj = getCurrentFileParser()->getObject();
                if (obj == nullptr) return input;
                obj->setRotationSpeed(details[0].value);
            }
            return input;
		},
        "Rotate speed",
        "rotateSpeed",
        0
    };
    std::atomic<bool> fixedRotateX = false;
    std::atomic<bool> fixedRotateY = false;
    std::atomic<bool> fixedRotateZ = false;

    std::shared_ptr<DelayEffect> delayEffect = std::make_shared<DelayEffect>();
    
    juce::SpinLock parsersLock;
    std::vector<std::shared_ptr<FileParser>> parsers;
    std::vector<std::shared_ptr<juce::MemoryBlock>> fileBlocks;
    std::vector<juce::String> fileNames;
    std::atomic<int> currentFile = -1;
    
    FrameProducer producer = FrameProducer(*this, std::make_shared<FileParser>());

    BufferProducer audioProducer;

    PitchDetector pitchDetector{audioProducer};
    std::shared_ptr<WobbleEffect> wobbleEffect = std::make_shared<WobbleEffect>(pitchDetector);

    void addLuaSlider();
    void updateAngleDelta();
    void addFrame(std::vector<std::unique_ptr<Shape>> frame, int fileIndex) override;
	void enableEffect(std::shared_ptr<Effect> effect);
    void disableEffect(std::shared_ptr<Effect> effect);
    void updateEffectPrecedence();
    void updateFileBlock(int index, std::shared_ptr<juce::MemoryBlock> block);
    void addFile(juce::File file);
    void addFile(juce::String fileName, const char* data, const int size);
    void removeFile(int index);
    int numFiles();
    void changeCurrentFile(int index);
    int getCurrentFileIndex();
    std::shared_ptr<FileParser> getCurrentFileParser();
	juce::String getCurrentFileName();
    juce::String getFileName(int index);
	std::shared_ptr<juce::MemoryBlock> getFileBlock(int index);
private:
    double theta = 0.0;
    double thetaDelta = 0.0;

	juce::AbstractFifo frameFifo{ 10 };
	std::vector<std::unique_ptr<Shape>> frameBuffer[10];
    int frameBufferIndices[10];

	int currentShape = 0;
    std::vector<std::unique_ptr<Shape>> frame;
    int currentBufferIndex = -1;
    double frameLength;
	double shapeDrawn = 0.0;
	double frameDrawn = 0.0;
	double lengthIncrement = 0.0;
    bool invalidateFrameBuffer = false;

    std::shared_ptr<Effect> traceMax = std::make_shared<Effect>(
        [this](int index, Vector2 input, std::vector<EffectDetails> details, double sampleRate) {
            traceMaxEnabled = true;
            return input;
        },
        "Trace max",
        "traceMax",
        1
    );
    std::shared_ptr<Effect> traceMin = std::make_shared<Effect>(
        [this](int index, Vector2 input, std::vector<EffectDetails> details, double sampleRate) {
            traceMinEnabled = true;
            return input;
        },
        "Trace min",
        "traceMin",
        0
    );
    const double MIN_TRACE = 0.005;
    double actualTraceMax = traceMax->getValue();
    double actualTraceMin = traceMin->getValue();
    bool traceMaxEnabled = false;
    bool traceMinEnabled = false;

    AudioWebSocketServer softwareOscilloscopeServer{audioProducer};

	void updateFrame();
    void updateLengthIncrement();
    void incrementShapeDrawing();
    void openFile(int index);
    void updateLuaValues();
    void updateObjValues();

    const double MIN_LENGTH_INCREMENT = 0.000001;

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OscirenderAudioProcessor)
};

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
#include "audio/PerspectiveEffect.h"

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

    std::atomic<double> currentSampleRate = 0.0;

    juce::SpinLock effectsLock;
	std::vector<std::shared_ptr<Effect>> toggleableEffects;
    std::vector<std::shared_ptr<Effect>> luaEffects;

    std::shared_ptr<Effect> frequencyEffect = std::make_shared<Effect>(
        [this](int index, Vector2 input, const std::vector<double>& values, double sampleRate) {
            frequency = values[0];
            return input;
        }, new EffectParameter("Frequency", "frequency", 440.0, 0.0, 12000.0, 0.1)
    );

    std::shared_ptr<Effect> volumeEffect = std::make_shared<Effect>(
        [this](int index, Vector2 input, const std::vector<double>& values, double sampleRate) {
            volume = values[0];
            return input;
        }, new EffectParameter("Volume", "volume", 1.0, 0.0, 3.0)
    );

    std::shared_ptr<Effect> thresholdEffect = std::make_shared<Effect>(
        [this](int index, Vector2 input, const std::vector<double>& values, double sampleRate) {
            threshold = values[0];
            return input;
        }, new EffectParameter("Threshold", "threshold", 1.0, 0.0, 1.0)
    );
    
    std::shared_ptr<Effect> focalLength = std::make_shared<Effect>(
        [this](int index, Vector2 input, const std::vector<double>& values, double sampleRate) {
            if (getCurrentFileIndex() != -1) {
                auto camera = getCurrentFileParser()->getCamera();
                if (camera == nullptr) return input;
                camera->setFocalLength(values[0]);
            }
            return input;
		}, new EffectParameter("Focal length", "focalLength", 1.0, 0.0, 2.0)
    );

    BooleanParameter* fixedRotateX = new BooleanParameter("Object Fixed Rotate X", "objFixedRotateX", false);
    BooleanParameter* fixedRotateY = new BooleanParameter("Object Fixed Rotate Y", "objFixedRotateY", false);
    BooleanParameter* fixedRotateZ = new BooleanParameter("Object Fixed Rotate Z", "objFixedRotateZ", false);
    std::shared_ptr<Effect> rotateX = std::make_shared<Effect>(
        [this](int index, Vector2 input, const std::vector<double>& values, double sampleRate) {
            if (getCurrentFileIndex() != -1) {
                auto obj = getCurrentFileParser()->getObject();
                if (obj == nullptr) return input;
                auto rotation = values[0] * std::numbers::pi;
                if (fixedRotateX->getBoolValue()) {
                    obj->setCurrentRotationX(rotation);
                } else {
                    obj->setBaseRotationX(rotation);
                }
            }
            return input;
        }, new EffectParameter("Rotate X", "objRotateX", 1.0, -1.0, 1.0)
    );
    std::shared_ptr<Effect> rotateY = std::make_shared<Effect>(
        [this](int index, Vector2 input, const std::vector<double>& values, double sampleRate) {
            if (getCurrentFileIndex() != -1) {
                auto obj = getCurrentFileParser()->getObject();
                if (obj == nullptr) return input;
                auto rotation = values[0] * std::numbers::pi;
                if (fixedRotateY->getBoolValue()) {
                    obj->setCurrentRotationY(rotation);
                } else {
                    obj->setBaseRotationY(rotation);
                }
            }
            return input;
        }, new EffectParameter("Rotate Y", "objRotateY", 1.0, -1.0, 1.0)
    );
    std::shared_ptr<Effect> rotateZ = std::make_shared<Effect>(
        [this](int index, Vector2 input, const std::vector<double>& values, double sampleRate) {
            if (getCurrentFileIndex() != -1) {
                auto obj = getCurrentFileParser()->getObject();
                if (obj == nullptr) return input;
                auto rotation = values[0] * std::numbers::pi;
                if (fixedRotateZ->getBoolValue()) {
                    obj->setCurrentRotationZ(rotation);
                } else {
                    obj->setBaseRotationZ(rotation);
                }
            }
            return input;
        }, new EffectParameter("Rotate Z", "objRotateZ", 0.0, -1.0, 1.0)
    );
    std::shared_ptr<Effect> rotateSpeed = std::make_shared<Effect>(
        [this](int index, Vector2 input, const std::vector<double>& values, double sampleRate) {
            if (getCurrentFileIndex() != -1) {
                auto obj = getCurrentFileParser()->getObject();
                if (obj == nullptr) return input;
                obj->setRotationSpeed(values[0]);
            }
            return input;
		}, new EffectParameter("Rotate Speed", "objRotateSpeed", 0.0, -1.0, 1.0)
    );

    std::shared_ptr<DelayEffect> delayEffect = std::make_shared<DelayEffect>();
    std::shared_ptr<PerspectiveEffect> perspectiveEffect = std::make_shared<PerspectiveEffect>();
    
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
    void addFrame(std::vector<std::unique_ptr<Shape>> frame, int fileIndex) override;
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
    std::atomic<float> frequency = 440.0f;
    std::atomic<double> volume = 1.0;
    std::atomic<double> threshold = 1.0;

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

    std::vector<std::shared_ptr<Effect>> permanentEffects;

    std::shared_ptr<Effect> traceMax = std::make_shared<Effect>(
        [this](int index, Vector2 input, const std::vector<double>& values, double sampleRate) {
            traceMaxValue = values[0];
            traceMaxEnabled = true;
            return input;
        }, new EffectParameter("Trace max", "traceMax", 1.0, 0.0, 1.0)
    );
    std::shared_ptr<Effect> traceMin = std::make_shared<Effect>(
        [this](int index, Vector2 input, const std::vector<double>& values, double sampleRate) {
            traceMinValue = values[0];
            traceMinEnabled = true;
            return input;
        }, new EffectParameter("Trace min", "traceMin", 0.0, 0.0, 1.0)
    );
    const double MIN_TRACE = 0.005;
    double traceMaxValue = traceMax->getValue();
    double traceMinValue = traceMin->getValue();
    double actualTraceMax = traceMaxValue;
    double actualTraceMin = traceMinValue;
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

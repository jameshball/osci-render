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

    float noteOnVel;
	float frequency = 440.0f;

    double currentSampleRate = 0.0;

	std::vector<std::shared_ptr<Effect>> allEffects;
	std::shared_ptr<std::vector<std::shared_ptr<Effect>>> enabledEffects = std::make_shared<std::vector<std::shared_ptr<Effect>>>();

    std::vector<std::shared_ptr<Effect>> luaEffects;
    
    std::vector<std::shared_ptr<FileParser>> parsers;
    std::vector<std::shared_ptr<juce::MemoryBlock>> fileBlocks;
    std::vector<juce::File> files;
    int currentFile = -1;
    
    std::unique_ptr<FrameProducer> producer;

    void addLuaSlider();
    void updateLuaValues();
    void updateAngleDelta();
    void addFrame(std::vector<std::unique_ptr<Shape>> frame, int fileIndex) override;
	void enableEffect(std::shared_ptr<Effect> effect);
    void disableEffect(std::shared_ptr<Effect> effect);
    void updateEffectPrecedence();
    void updateFileBlock(int index, std::shared_ptr<juce::MemoryBlock> block);
    void addFile(juce::File file);
    void removeFile(int index);
    int numFiles();
    void openFile(int index);
    void changeCurrentFile(int index);
    int getCurrentFileIndex();
    std::shared_ptr<FileParser> getCurrentFileParser();
	juce::File getCurrentFile();
    juce::File getFile(int index);
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

	void updateFrame();
    void updateLengthIncrement();
    void syncLuaSliders();

    const double MIN_LENGTH_INCREMENT = 0.000001;

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OscirenderAudioProcessor)
};

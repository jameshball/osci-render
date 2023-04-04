/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "parser/FileParser.h"
#include "parser/FrameProducer.h"
#include "audio/RotateEffect.h"
#include "audio/VectorCancellingEffect.h"
#include "audio/DistortEffect.h"
#include "audio/SmoothEffect.h"

//==============================================================================
OscirenderAudioProcessor::OscirenderAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       )
#endif
    {
    producer = std::make_unique<FrameProducer>(*this, std::make_shared<FileParser>());
    producer->startThread();

	allEffects.push_back(std::make_shared<Effect>(std::make_unique<BitCrushEffect>(), "Bit Crush", "bitCrush"));
	allEffects.push_back(std::make_shared<Effect>(std::make_unique<BulgeEffect>(), "Bulge", "bulge"));
    allEffects.push_back(std::make_shared<Effect>(std::make_unique<RotateEffect>(), "2D Rotate Speed", "rotateSpeed"));
    allEffects.push_back(std::make_shared<Effect>(std::make_unique<VectorCancellingEffect>(), "Vector cancelling", "vectorCancelling"));
    allEffects.push_back(std::make_shared<Effect>(std::make_unique<DistortEffect>(true), "Vertical shift", "verticalDistort"));
    allEffects.push_back(std::make_shared<Effect>(std::make_unique<DistortEffect>(false), "Horizontal shift", "horizontalDistort"));
    allEffects.push_back(std::make_shared<Effect>(std::make_unique<SmoothEffect>(), "Smoothing", "smoothing"));
}

OscirenderAudioProcessor::~OscirenderAudioProcessor()
{
}

//==============================================================================
const juce::String OscirenderAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool OscirenderAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool OscirenderAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool OscirenderAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double OscirenderAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int OscirenderAudioProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int OscirenderAudioProcessor::getCurrentProgram()
{
    return 0;
}

void OscirenderAudioProcessor::setCurrentProgram (int index)
{
}

const juce::String OscirenderAudioProcessor::getProgramName (int index)
{
    return {};
}

void OscirenderAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
}

//==============================================================================
void OscirenderAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
	currentSampleRate = sampleRate;
    updateAngleDelta();
}

void OscirenderAudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool OscirenderAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    // This is the place where you check if the layout is supported.
    // In this template code we only support mono or stereo.
    // Some plugin hosts, such as certain GarageBand versions, will only
    // load plugins that support stereo bus layouts.
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // This checks if the input layout matches the output layout
   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}
#endif

void OscirenderAudioProcessor::updateAngleDelta() {
	auto cyclesPerSample = frequency / currentSampleRate;
	thetaDelta = cyclesPerSample * 2.0 * juce::MathConstants<double>::pi;
}

void OscirenderAudioProcessor::enableEffect(std::shared_ptr<Effect> effect) {
	// need to make a new vector because the old one is being iterated over in another thread
	std::shared_ptr<std::vector<std::shared_ptr<Effect>>> newEffects = std::make_shared<std::vector<std::shared_ptr<Effect>>>();
	for (auto& e : *enabledEffects) {
		newEffects->push_back(e);
	}
	// remove any existing effects with the same id
	for (auto it = newEffects->begin(); it != newEffects->end();) {
		if ((*it)->getId() == effect->getId()) {
			it = newEffects->erase(it);
		} else {
			it++;
		}
	}
	// insert according to precedence (sorts from lowest to highest precedence)
	auto it = newEffects->begin();
	while (it != newEffects->end() && (*it)->getPrecedence() <= effect->getPrecedence()) {
		it++;
	}
	newEffects->insert(it, effect);
	enabledEffects = newEffects;
}

void OscirenderAudioProcessor::disableEffect(std::shared_ptr<Effect> effect) {
	// need to make a new vector because the old one is being iterated over in another thread
	std::shared_ptr<std::vector<std::shared_ptr<Effect>>> newEffects = std::make_shared<std::vector<std::shared_ptr<Effect>>>();
	for (auto& e : *enabledEffects) {
		newEffects->push_back(e);
	}
	// remove any existing effects with the same id
	for (auto it = newEffects->begin(); it != newEffects->end();) {
		if ((*it)->getId() == effect->getId()) {
			it = newEffects->erase(it);
		} else {
			it++;
		}
	}
	enabledEffects = newEffects;
}

void OscirenderAudioProcessor::updateEffectPrecedence() {
	// need to make a new vector because the old one is being iterated over in another thread
	std::shared_ptr<std::vector<std::shared_ptr<Effect>>> newEffects = std::make_shared<std::vector<std::shared_ptr<Effect>>>();
	for (auto& e : *enabledEffects) {
		newEffects->push_back(e);
	}
	std::sort(newEffects->begin(), newEffects->end(), [](std::shared_ptr<Effect> a, std::shared_ptr<Effect> b) {
		return a->getPrecedence() < b->getPrecedence();
    });
	enabledEffects = newEffects;
}

void OscirenderAudioProcessor::updateFileBlock(int index, std::shared_ptr<juce::MemoryBlock> block) {
	fileBlocks[index] = block;
	openFile(index);
}

void OscirenderAudioProcessor::addFile(juce::File file) {
    fileBlocks.push_back(std::make_shared<juce::MemoryBlock>());
    files.push_back(file);
	parsers.push_back(std::make_unique<FileParser>());
    file.createInputStream()->readIntoMemoryBlock(*fileBlocks.back());

    openFile(fileBlocks.size() - 1);
}

void OscirenderAudioProcessor::removeFile(int index) {
	if (index < 0 || index >= fileBlocks.size()) {
		return;
	}
    changeCurrentFile(index - 1);
    fileBlocks.erase(fileBlocks.begin() + index);
    files.erase(files.begin() + index);
	parsers.erase(parsers.begin() + index);
}

int OscirenderAudioProcessor::numFiles() {
    return fileBlocks.size();
}

void OscirenderAudioProcessor::openFile(int index) {
	if (index < 0 || index >= fileBlocks.size()) {
		return;
	}
    parsers[index]->parse(files[index].getFileExtension(), std::make_unique<juce::MemoryInputStream>(*fileBlocks[index], false));
    producer->setSource(parsers[index], index);
    currentFile = index;
	invalidateFrameBuffer = true;
}

void OscirenderAudioProcessor::changeCurrentFile(int index) {
	if (index < 0 || index >= fileBlocks.size()) {
		return;
	}
	producer->setSource(parsers[index], index);
    currentFile = index;
	invalidateFrameBuffer = true;
}

int OscirenderAudioProcessor::getCurrentFileIndex() {
    return currentFile;
}

juce::File OscirenderAudioProcessor::getCurrentFile() {
    return files[currentFile];
}

std::shared_ptr<juce::MemoryBlock> OscirenderAudioProcessor::getFileBlock(int index) {
    return fileBlocks[index];
}

void OscirenderAudioProcessor::addFrame(std::vector<std::unique_ptr<Shape>> frame, int fileIndex) {
    const auto scope = frameFifo.write(1);

    if (scope.blockSize1 > 0) {
        frameBuffer[scope.startIndex1].clear();
        for (auto& shape : frame) {
            frameBuffer[scope.startIndex1].push_back(std::move(shape));
        }
        frameBufferIndices[scope.startIndex1] = fileIndex;
    }

    if (scope.blockSize2 > 0) {
        frameBuffer[scope.startIndex2].clear();
        for (auto& shape : frame) {
            frameBuffer[scope.startIndex2].push_back(std::move(shape));
        }
        frameBufferIndices[scope.startIndex2] = fileIndex;
    }
}

void OscirenderAudioProcessor::updateFrame() {
    currentShape = 0;
    shapeDrawn = 0.0;
    frameDrawn = 0.0;
    
	if (frameFifo.getNumReady() > 0) {
        {
            const auto scope = frameFifo.read(1);

            if (scope.blockSize1 > 0) {
				frame.swap(frameBuffer[scope.startIndex1]);
                currentBufferIndex = frameBufferIndices[scope.startIndex1];
            } else if (scope.blockSize2 > 0) {
                frame.swap(frameBuffer[scope.startIndex2]);
                currentBufferIndex = frameBufferIndices[scope.startIndex2];
            }

            frameLength = Shape::totalLength(frame);
        }
	}
}

void OscirenderAudioProcessor::updateLengthIncrement() {
    lengthIncrement = std::max(frameLength / (currentSampleRate / frequency), MIN_LENGTH_INCREMENT);
}

void OscirenderAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    // In case we have more outputs than inputs, this code clears any output
    // channels that didn't contain input data, (because these aren't
    // guaranteed to be empty - they may contain garbage).
    // This is here to avoid people getting screaming feedback
    // when they first compile a plugin, but obviously you don't need to keep
    // this code if your algorithm always overwrites all the output channels.
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    
    auto* channelData = buffer.getArrayOfWritePointers();
	auto numSamples = buffer.getNumSamples();

    if (invalidateFrameBuffer) {
        frameFifo.reset();
        // keeps getting the next frame until the frame comes from the file that we want to render.
        // this MIGHT be hacky and cause issues later down the line, but for now it works as a
        // solution to get instant changing of current file when pressing j and k.
        while (currentBufferIndex != currentFile) {
            updateFrame();
        }
        invalidateFrameBuffer = false;
    }
    
	for (auto sample = 0; sample < numSamples; ++sample) {
        updateLengthIncrement();
        
        Vector2 channels;
        double x = 0.0;
        double y = 0.0;
        double length = 0.0;

        if (currentShape < frame.size()) {
            auto& shape = frame[currentShape];
            length = shape->length();
            double drawingProgress = length == 0.0 ? 1 : shapeDrawn / length;
            channels = shape->nextVector(drawingProgress);
        }
        
		auto enabledEffectsCopy = enabledEffects;

		for (auto effect : *enabledEffectsCopy) {
			channels = effect->apply(sample, channels);
		}

		x = channels.x;
		y = channels.y;

        if (totalNumOutputChannels >= 2) {
			channelData[0][sample] = x;
			channelData[1][sample] = y;
		} else if (totalNumOutputChannels == 1) {
            channelData[0][sample] = x;
        }
        
        // hard cap on how many times it can be over the length to
        // prevent audio stuttering
        frameDrawn += std::min(lengthIncrement, 20 * length);
		shapeDrawn += std::min(lengthIncrement, 20 * length);

        // Need to skip all shapes that the lengthIncrement draws over.
        // This is especially an issue when there are lots of small lines being
        // drawn.
		while (shapeDrawn > length) {
			shapeDrawn -= length;
			currentShape++;
			if (currentShape >= frame.size()) {
				currentShape = 0;
                break;
			}
            // POTENTIAL TODO: Think of a way to make this more efficient when iterating
            // this loop many times
            length = frame[currentShape]->len;
		}

        if (frameDrawn >= frameLength) {
			updateFrame();
		}
	}

	juce::MidiBuffer processedMidi;
    
    for (const auto metadata : midiMessages) {
        auto message = metadata.getMessage();
        const auto time = metadata.samplePosition;
        
        if (message.isNoteOn()) {
            message = juce::MidiMessage::noteOn(message.getChannel(), message.getNoteNumber(), (juce::uint8)noteOnVel);
        }
        processedMidi.addEvent(message, time);
    }
	midiMessages.swapWith(processedMidi);
}

//==============================================================================
bool OscirenderAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* OscirenderAudioProcessor::createEditor()
{
    return new OscirenderAudioProcessorEditor (*this);
}

//==============================================================================
void OscirenderAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // You should use this method to store your parameters in the memory block.
    // You could do that either as raw data, or use the XML or ValueTree classes
    // as intermediaries to make it easy to save and load complex data.
}

void OscirenderAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // You should use this method to restore your parameters from this memory block,
    // whose contents will have been created by the getStateInformation() call.
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new OscirenderAudioProcessor();
}

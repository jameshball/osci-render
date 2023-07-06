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
#include "audio/BitCrushEffect.h"
#include "audio/BulgeEffect.h"
#include "audio/LuaEffect.h"

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
    
    juce::SpinLock::ScopedLockType lock(effectsLock);

    allEffects.push_back(std::make_shared<Effect>(std::make_unique<BitCrushEffect>(), "Bit Crush", "bitCrush"));
    allEffects.push_back(std::make_shared<Effect>(std::make_unique<BulgeEffect>(), "Bulge", "bulge"));
    allEffects.push_back(std::make_shared<Effect>(std::make_unique<RotateEffect>(), "2D Rotate Speed", "rotateSpeed"));
    allEffects.push_back(std::make_shared<Effect>(std::make_unique<VectorCancellingEffect>(), "Vector cancelling", "vectorCancelling"));
    allEffects.push_back(std::make_shared<Effect>(std::make_unique<DistortEffect>(true), "Vertical shift", "verticalDistort"));
    allEffects.push_back(std::make_shared<Effect>(std::make_unique<DistortEffect>(false), "Horizontal shift", "horizontalDistort"));
    allEffects.push_back(std::make_shared<Effect>(std::make_unique<SmoothEffect>(), "Smoothing", "smoothing"));
    allEffects.push_back(traceMax);
    allEffects.push_back(traceMin);

    for (int i = 0; i < 5; i++) {
        addLuaSlider();
    }
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

// effectsLock should be held when calling this
void OscirenderAudioProcessor::addLuaSlider() {
    juce::String sliderName = "";

    int sliderNum = luaEffects.size() + 1;
    while (sliderNum > 0) {
        int mod = (sliderNum - 1) % 26;
        sliderName = (char)(mod + 'A') + sliderName;
        sliderNum = (sliderNum - mod) / 26;
    }

    luaEffects.push_back(std::make_shared<Effect>(std::make_unique<LuaEffect>(sliderName, *this), "Lua " + sliderName, "lua" + sliderName));
}

// effectsLock should be held when calling this
void OscirenderAudioProcessor::updateLuaValues() {
    for (auto& effect : luaEffects) {
        effect->apply();
	}
}

// parsersLock should be held when calling this
void OscirenderAudioProcessor::updateObjValues() {
    focalLength.apply();
    rotateX.apply();
    rotateSpeed.apply();
}

void OscirenderAudioProcessor::updateAngleDelta() {
	auto cyclesPerSample = frequency / currentSampleRate;
	thetaDelta = cyclesPerSample * 2.0 * juce::MathConstants<double>::pi;
}

// effectsLock MUST be held when calling this
void OscirenderAudioProcessor::enableEffect(std::shared_ptr<Effect> effect) {
	// remove any existing effects with the same id
	for (auto it = enabledEffects.begin(); it != enabledEffects.end();) {
		if ((*it)->getId() == effect->getId()) {
			it = enabledEffects.erase(it);
		} else {
			it++;
		}
	}
	// insert according to precedence (sorts from lowest to highest precedence)
	auto it = enabledEffects.begin();
	while (it != enabledEffects.end() && (*it)->getPrecedence() <= effect->getPrecedence()) {
		it++;
	}
    enabledEffects.insert(it, effect);
}

// effectsLock MUST be held when calling this
void OscirenderAudioProcessor::disableEffect(std::shared_ptr<Effect> effect) {
	// remove any existing effects with the same id
	for (auto it = enabledEffects.begin(); it != enabledEffects.end();) {
		if ((*it)->getId() == effect->getId()) {
			it = enabledEffects.erase(it);
		} else {
			it++;
		}
	}
}

// effectsLock MUST be held when calling this
void OscirenderAudioProcessor::updateEffectPrecedence() {
    auto sortFunc = [](std::shared_ptr<Effect> a, std::shared_ptr<Effect> b) {
        return a->getPrecedence() < b->getPrecedence();
    };
	std::sort(enabledEffects.begin(), enabledEffects.end(), sortFunc);
    std::sort(allEffects.begin(), allEffects.end(), sortFunc);
}

// parsersLock AND effectsLock must be locked before calling this function
void OscirenderAudioProcessor::updateFileBlock(int index, std::shared_ptr<juce::MemoryBlock> block) {
    if (index < 0 || index >= fileBlocks.size()) {
		return;
	}
	fileBlocks[index] = block;
	openFile(index);
}

// parsersLock AND effectsLock must be locked before calling this function
void OscirenderAudioProcessor::addFile(juce::File file) {
    fileBlocks.push_back(std::make_shared<juce::MemoryBlock>());
    fileNames.push_back(file.getFileName());
	parsers.push_back(std::make_unique<FileParser>());
    file.createInputStream()->readIntoMemoryBlock(*fileBlocks.back());

    openFile(fileBlocks.size() - 1);
}

// parsersLock AND effectsLock must be locked before calling this function
void OscirenderAudioProcessor::addFile(juce::String fileName, const char* data, const int size) {
    fileBlocks.push_back(std::make_shared<juce::MemoryBlock>());
    fileNames.push_back(fileName);
    parsers.push_back(std::make_unique<FileParser>());
    fileBlocks.back()->append(data, size);

    openFile(fileBlocks.size() - 1);
}

// parsersLock AND effectsLock must be locked before calling this function
void OscirenderAudioProcessor::removeFile(int index) {
	if (index < 0 || index >= fileBlocks.size()) {
		return;
	}
    changeCurrentFile(index - 1);
    fileBlocks.erase(fileBlocks.begin() + index);
    fileNames.erase(fileNames.begin() + index);
	parsers.erase(parsers.begin() + index);
}

int OscirenderAudioProcessor::numFiles() {
    return fileBlocks.size();
}

// used for opening NEW files. Should be the default way of opening files as
// it will reparse any existing files, so it is safer.
// parsersLock AND effectsLock must be locked before calling this function
void OscirenderAudioProcessor::openFile(int index) {
	if (index < 0 || index >= fileBlocks.size()) {
		return;
	}
    parsers[index]->parse(fileNames[index].fromLastOccurrenceOf(".", true, false), std::make_unique<juce::MemoryInputStream>(*fileBlocks[index], false));
    changeCurrentFile(index);
}

// used ONLY for changing the current file to an EXISTING file.
// much faster than openFile(int index) because it doesn't reparse any files.
// parsersLock AND effectsLock must be locked before calling this function
void OscirenderAudioProcessor::changeCurrentFile(int index) {
    if (index == -1) {
        currentFile = -1;
        producer->setSource(std::make_shared<FileParser>(), -1);
    }
	if (index < 0 || index >= fileBlocks.size()) {
		return;
	}
	producer->setSource(parsers[index], index);
    currentFile = index;
	invalidateFrameBuffer = true;
    updateLuaValues();
    updateObjValues();
}

int OscirenderAudioProcessor::getCurrentFileIndex() {
    return currentFile;
}

std::shared_ptr<FileParser> OscirenderAudioProcessor::getCurrentFileParser() {
    return parsers[currentFile];
}

juce::String OscirenderAudioProcessor::getCurrentFileName() {
    if (currentFile == -1) {
		return "";
    } else {
        return fileNames[currentFile];
    }
}

juce::String OscirenderAudioProcessor::getFileName(int index) {
    return fileNames[index];
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
    } else {
        DBG("frame not ready!");
    }
}

void OscirenderAudioProcessor::updateLengthIncrement() {
    double traceMaxValue = traceMaxEnabled ? actualTraceMax : 1.0;
    double traceMinValue = traceMinEnabled ? actualTraceMin : 0.0;
    double proportionalLength = (traceMaxValue - traceMinValue) * frameLength;
    lengthIncrement = std::max(proportionalLength / (currentSampleRate / frequency), MIN_LENGTH_INCREMENT);
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

        traceMaxEnabled = false;
        traceMinEnabled = false;
        
        Vector2 channels;
        double x = 0.0;
        double y = 0.0;


        std::shared_ptr<FileParser> sampleParser;
        {
            juce::SpinLock::ScopedLockType lock(parsersLock);
            if (currentFile >= 0 && parsers[currentFile]->isSample()) {
                sampleParser = parsers[currentFile];
            }
        }
        bool renderingSample = sampleParser != nullptr;

        if (renderingSample) {
            channels = sampleParser->nextSample();
        } else if (currentShape < frame.size()) {
            auto& shape = frame[currentShape];
            double length = shape->length();
            double drawingProgress = length == 0.0 ? 1 : shapeDrawn / length;
            channels = shape->nextVector(drawingProgress);
        }

        {
            juce::SpinLock::ScopedLockType lock(effectsLock);
            for (auto& effect : enabledEffects) {
                channels = effect->apply(sample, channels);
            }
        }

		x = channels.x;
		y = channels.y;

        // clip to -1.0 to 1.0
        x = std::max(-1.0, std::min(1.0, x));
        y = std::max(-1.0, std::min(1.0, y));
        

        if (totalNumOutputChannels >= 2) {
			channelData[0][sample] = x;
			channelData[1][sample] = y;
		} else if (totalNumOutputChannels == 1) {
            channelData[0][sample] = x;
        }

        actualTraceMax = std::max(actualTraceMin + MIN_TRACE, std::min(traceMax->getValue(), 1.0));
        actualTraceMin = std::max(MIN_TRACE, std::min(traceMin->getValue(), actualTraceMax - MIN_TRACE));
        
        if (!renderingSample) {
            incrementShapeDrawing();
        }
        
        double drawnFrameLength = traceMaxEnabled ? actualTraceMax * frameLength : frameLength;

        if (!renderingSample && frameDrawn >= drawnFrameLength) {
            updateFrame();
            if (traceMinEnabled) {
                while (frameDrawn < actualTraceMin * frameLength) {
                    incrementShapeDrawing();
                }
            }
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

void OscirenderAudioProcessor::incrementShapeDrawing() {
    double length = currentShape < frame.size() ? frame[currentShape]->len : 0.0;
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

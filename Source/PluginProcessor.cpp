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
#include "audio/EffectParameter.h"

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
    producer.startThread();

    // locking isn't necessary here because we are in the constructor

    toggleableEffects.push_back(std::make_shared<Effect>(
        std::make_shared<BitCrushEffect>(),
        new EffectParameter("Bit Crush", "bitCrush", 0.0, 0.0, 1.0)
    ));
    toggleableEffects.push_back(std::make_shared<Effect>(
        std::make_shared<BulgeEffect>(),
        new EffectParameter("Bulge", "bulge", 0.0, 0.0, 1.0)
    ));
    toggleableEffects.push_back(std::make_shared<Effect>(
        std::make_shared<RotateEffect>(),
        new EffectParameter("2D Rotate", "2DRotateSpeed", 0.0, 0.0, 1.0)
    ));
    toggleableEffects.push_back(std::make_shared<Effect>(
        std::make_shared<VectorCancellingEffect>(),
        new EffectParameter("Vector Cancelling", "vectorCancelling", 0.0, 0.0, 1.0)
    ));
    toggleableEffects.push_back(std::make_shared<Effect>(
        std::make_shared<DistortEffect>(false),
        new EffectParameter("Distort X", "distortX", 0.0, 0.0, 1.0)
    ));
    toggleableEffects.push_back(std::make_shared<Effect>(
        std::make_shared<DistortEffect>(true),
        new EffectParameter("Distort Y", "distortY", 0.0, 0.0, 1.0)
    ));
    toggleableEffects.push_back(std::make_shared<Effect>(
        [this](int index, Vector2 input, const std::vector<double>& values, double sampleRate) {
            input.x += values[0];
            input.y += values[1];
            return input;
        }, std::vector<EffectParameter*>{new EffectParameter("Translate X", "translateX", 0.0, -1.0, 1.0), new EffectParameter("Translate Y", "translateY", 0.0, -1.0, 1.0)}
    ));
    toggleableEffects.push_back(std::make_shared<Effect>(
        std::make_shared<SmoothEffect>(),
        new EffectParameter("Smoothing", "smoothing", 0.0, 0.0, 1.0)
    ));
    toggleableEffects.push_back(std::make_shared<Effect>(
        wobbleEffect,
        new EffectParameter("Wobble", "wobble", 0.0, 0.0, 1.0)
    ));
    toggleableEffects.push_back(std::make_shared<Effect>(
        delayEffect,
        std::vector<EffectParameter*>{new EffectParameter("Delay Decay", "delayDecay", 0.0, 0.0, 1.0), new EffectParameter("Delay Length", "delayLength", 0.5, 0.0, 1.0)}
    ));
    toggleableEffects.push_back(std::make_shared<Effect>(
        perspectiveEffect,
        std::vector<EffectParameter*>{
            new EffectParameter("3D Perspective", "perspectiveStrength", 0.0, 0.0, 1.0),
            new EffectParameter("Depth (z)", "perspectiveZPos", 0.1, 0.0, 1.0),
            new EffectParameter("Rotate Speed", "perspectiveRotateSpeed", 0.0, -1.0, 1.0),
            new EffectParameter("Rotate X", "perspectiveRotateX", 1.0, -1.0, 1.0),
            new EffectParameter("Rotate Y", "perspectiveRotateY", 1.0, -1.0, 1.0),
            new EffectParameter("Rotate Z", "perspectiveRotateZ", 0.0, -1.0, 1.0),
        }
    ));
    toggleableEffects.push_back(traceMax);
    toggleableEffects.push_back(traceMin);

    for (auto& effect : toggleableEffects) {
        effect->markEnableable(false);
        addParameter(effect->enabled);
        effect->enabled->setValueNotifyingHost(false);
    }

    permanentEffects.push_back(frequencyEffect);
    permanentEffects.push_back(volumeEffect);
    permanentEffects.push_back(thresholdEffect);
    permanentEffects.push_back(rotateSpeed);
    permanentEffects.push_back(rotateX);
    permanentEffects.push_back(rotateY);
    permanentEffects.push_back(rotateZ);
    permanentEffects.push_back(focalLength);

    for (int i = 0; i < 26; i++) {
        addLuaSlider();
    }

    allEffects = toggleableEffects;
    allEffects.insert(allEffects.end(), permanentEffects.begin(), permanentEffects.end());
    allEffects.insert(allEffects.end(), luaEffects.begin(), luaEffects.end());

    for (auto effect : allEffects) {
        for (auto effectParameter : effect->parameters) {
            auto parameters = effectParameter->getParameters();
            for (auto parameter : parameters) {
                addParameter(parameter);
            }
        }
    }

    booleanParameters.push_back(fixedRotateX);
    booleanParameters.push_back(fixedRotateY);
    booleanParameters.push_back(fixedRotateZ);
    booleanParameters.push_back(perspectiveEffect->fixedRotateX);
    booleanParameters.push_back(perspectiveEffect->fixedRotateY);
    booleanParameters.push_back(perspectiveEffect->fixedRotateZ);

    for (auto parameter : booleanParameters) {
        addParameter(parameter);
    }
}

OscirenderAudioProcessor::~OscirenderAudioProcessor() {}

const juce::String OscirenderAudioProcessor::getName() const {
    return JucePlugin_Name;
}

bool OscirenderAudioProcessor::acceptsMidi() const {
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool OscirenderAudioProcessor::producesMidi() const {
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool OscirenderAudioProcessor::isMidiEffect() const {
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double OscirenderAudioProcessor::getTailLengthSeconds() const {
    return 0.0;
}

int OscirenderAudioProcessor::getNumPrograms() {
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int OscirenderAudioProcessor::getCurrentProgram() {
    return 0;
}

void OscirenderAudioProcessor::setCurrentProgram(int index) {
}

const juce::String OscirenderAudioProcessor::getProgramName(int index) {
    return {};
}

void OscirenderAudioProcessor::changeProgramName(int index, const juce::String& newName) {}

void OscirenderAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
	currentSampleRate = sampleRate;
    pitchDetector.setSampleRate(sampleRate);
}

void OscirenderAudioProcessor::releaseResources() {
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

    luaEffects.push_back(std::make_shared<Effect>(
        std::make_shared<LuaEffect>(sliderName, *this),
        new EffectParameter("Lua " + sliderName, "lua" + sliderName, 0.0, 0.0, 1.0, 0.001, false)
    ));

    auto& effect = luaEffects.back();
    effect->parameters[0]->disableLfo();
}

// effectsLock should be held when calling this
void OscirenderAudioProcessor::updateLuaValues() {
    for (auto& effect : luaEffects) {
        effect->apply();
	}
}

// parsersLock should be held when calling this
void OscirenderAudioProcessor::updateObjValues() {
    focalLength->apply();
    rotateX->apply();
    rotateY->apply();
    rotateZ->apply();
    rotateSpeed->apply();
}

// effectsLock should be held when calling this
std::shared_ptr<Effect> OscirenderAudioProcessor::getEffect(juce::String id) {
    for (auto& effect : allEffects) {
        if (effect->getId() == id) {
            return effect;
        }
    }
    return nullptr;
}

// effectsLock should be held when calling this
BooleanParameter* OscirenderAudioProcessor::getBooleanParameter(juce::String id) {
    for (auto& parameter : booleanParameters) {
        if (parameter->paramID == id) {
            return parameter;
        }
    }
    return nullptr;
}

// effectsLock MUST be held when calling this
void OscirenderAudioProcessor::updateEffectPrecedence() {
    auto sortFunc = [](std::shared_ptr<Effect> a, std::shared_ptr<Effect> b) {
        return a->getPrecedence() < b->getPrecedence();
    };
    std::sort(toggleableEffects.begin(), toggleableEffects.end(), sortFunc);
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
void OscirenderAudioProcessor::addFile(juce::String fileName, std::shared_ptr<juce::MemoryBlock> data) {
    fileBlocks.push_back(data);
    fileNames.push_back(fileName);
    parsers.push_back(std::make_unique<FileParser>());

    openFile(fileBlocks.size() - 1);
}

// parsersLock AND effectsLock must be locked before calling this function
void OscirenderAudioProcessor::removeFile(int index) {
	if (index < 0 || index >= fileBlocks.size()) {
		return;
	}
    fileBlocks.erase(fileBlocks.begin() + index);
    fileNames.erase(fileNames.begin() + index);
    parsers.erase(parsers.begin() + index);
    auto newFileIndex = index;
    if (newFileIndex >= fileBlocks.size()) {
        newFileIndex = fileBlocks.size() - 1;
    }
    changeCurrentFile(newFileIndex);
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
        producer.setSource(std::make_shared<FileParser>(), -1);
    }
	if (index < 0 || index >= fileBlocks.size()) {
		return;
	}
	producer.setSource(parsers[index], index);
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
    }
}

void OscirenderAudioProcessor::updateLengthIncrement() {
    double traceMaxValue = traceMaxEnabled ? actualTraceMax : 1.0;
    double traceMinValue = traceMinEnabled ? actualTraceMin : 0.0;
    double proportionalLength = (traceMaxValue - traceMinValue) * frameLength;
    lengthIncrement = juce::jmax(proportionalLength / (currentSampleRate / frequency), MIN_LENGTH_INCREMENT);
}

void OscirenderAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
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
            juce::SpinLock::ScopedLockType lock1(parsersLock);
            juce::SpinLock::ScopedLockType lock2(effectsLock);
            for (auto& effect : toggleableEffects) {
                if (effect->enabled->getValue()) {
                    channels = effect->apply(sample, channels);
                }
            }
            for (auto& effect : permanentEffects) {
                channels = effect->apply(sample, channels);
            }
        }

		x = channels.x;
		y = channels.y;

        x *= volume;
        y *= volume;

        // clip
        x = juce::jmax(-threshold, juce::jmin(threshold.load(), x));
        y = juce::jmax(-threshold, juce::jmin(threshold.load(), y));
        
        if (totalNumOutputChannels >= 2) {
			channelData[0][sample] = x;
			channelData[1][sample] = y;
		} else if (totalNumOutputChannels == 1) {
            channelData[0][sample] = x;
        }

        audioProducer.write(x, y);

        actualTraceMax = juce::jmax(actualTraceMin + MIN_TRACE, juce::jmin(traceMaxValue, 1.0));
        actualTraceMin = juce::jmax(MIN_TRACE, juce::jmin(traceMinValue, actualTraceMax - MIN_TRACE));
        
        if (!renderingSample) {
            incrementShapeDrawing();
        }
        
        double drawnFrameLength = traceMaxEnabled ? actualTraceMax * frameLength : frameLength;

        if (!renderingSample && frameDrawn >= drawnFrameLength) {
            updateFrame();
            // TODO: updateFrame already iterates over all the shapes,
            // so we can improve performance by calculating frameDrawn
            // and shapeDrawn directly. frameDrawn is simply actualTraceMin * frameLength
            // but shapeDrawn is the amount of the current shape that has been drawn so
            // we need to iterate over all the shapes to calculate it.
            if (traceMinEnabled) {
                while (frameDrawn < actualTraceMin * frameLength) {
                    incrementShapeDrawing();
                }
            }
        }
	}
}

// TODO this is the slowest part of the program - any way to improve this would help!
void OscirenderAudioProcessor::incrementShapeDrawing() {
    double length = currentShape < frame.size() ? frame[currentShape]->len : 0.0;
    // hard cap on how many times it can be over the length to
    // prevent audio stuttering
    auto increment = juce::jmin(lengthIncrement, 20 * length);
    frameDrawn += increment;
    shapeDrawn += increment;

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
bool OscirenderAudioProcessor::hasEditor() const {
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* OscirenderAudioProcessor::createEditor() {
    return new OscirenderAudioProcessorEditor (*this);
}

//==============================================================================
void OscirenderAudioProcessor::getStateInformation(juce::MemoryBlock& destData) {
    juce::SpinLock::ScopedLockType lock1(parsersLock);
    juce::SpinLock::ScopedLockType lock2(effectsLock);

    std::unique_ptr<juce::XmlElement> xml = std::make_unique<juce::XmlElement>("project");
    xml->setAttribute("version", ProjectInfo::versionString);
    auto effectsXml = xml->createNewChildElement("effects");
    for (auto effect : allEffects) {
        effect->save(effectsXml->createNewChildElement("effect"));
    }

    auto booleanParametersXml = xml->createNewChildElement("booleanParameters");
    for (auto parameter : booleanParameters) {
        auto parameterXml = booleanParametersXml->createNewChildElement("parameter");
        parameter->save(parameterXml);
    }

    auto perspectiveFunction = xml->createNewChildElement("perspectiveFunction");
    perspectiveFunction->addTextElement(juce::Base64::toBase64(perspectiveEffect->getCode()));
    auto filesXml = xml->createNewChildElement("files");
    
    for (int i = 0; i < fileBlocks.size(); i++) {
        auto fileXml = filesXml->createNewChildElement("file");
        fileXml->setAttribute("name", fileNames[i]);
        auto fileString = juce::MemoryInputStream(*fileBlocks[i], false).readEntireStreamAsString();
        fileXml->addTextElement(juce::Base64::toBase64(fileString));
    }
    xml->setAttribute("currentFile", currentFile);
    copyXmlToBinary(*xml, destData);
}

void OscirenderAudioProcessor::setStateInformation(const void* data, int sizeInBytes) {
    juce::SpinLock::ScopedLockType lock1(parsersLock);
    juce::SpinLock::ScopedLockType lock2(effectsLock);

    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));

    if (xml.get() != nullptr && xml->hasTagName("project")) {
        auto effectsXml = xml->getChildByName("effects");
        if (effectsXml != nullptr) {
            for (auto effectXml : effectsXml->getChildIterator()) {
                auto effect = getEffect(effectXml->getStringAttribute("id"));
                if (effect != nullptr) {
                    effect->load(effectXml);
                }
            }
        }
        updateEffectPrecedence();

        auto booleanParametersXml = xml->getChildByName("booleanParameters");
        if (booleanParametersXml != nullptr) {
            for (auto parameterXml : booleanParametersXml->getChildIterator()) {
                auto parameter = getBooleanParameter(parameterXml->getStringAttribute("id"));
                if (parameter != nullptr) {
                    parameter->load(parameterXml);
                }
            }
        }

        auto perspectiveFunction = xml->getChildByName("perspectiveFunction");
        if (perspectiveFunction != nullptr) {
            auto stream = juce::MemoryOutputStream();
            juce::Base64::convertFromBase64(stream, perspectiveFunction->getAllSubText());
            perspectiveEffect->updateCode(stream.toString());
        }
        // close all files
        auto numFiles = fileBlocks.size();
        for (int i = 0; i < numFiles; i++) {
            removeFile(0);
        }

        auto filesXml = xml->getChildByName("files");
        if (filesXml != nullptr) {
            for (auto fileXml : filesXml->getChildIterator()) {
                auto fileName = fileXml->getStringAttribute("name");
                auto stream = juce::MemoryOutputStream();
                juce::Base64::convertFromBase64(stream, fileXml->getAllSubText());
                auto fileBlock = std::make_shared<juce::MemoryBlock>(stream.getData(), stream.getDataSize());
                addFile(fileName, fileBlock);
            }
        }
        changeCurrentFile(xml->getIntAttribute("currentFile", -1));
        broadcaster.sendChangeMessage();
    }
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new OscirenderAudioProcessor();
}

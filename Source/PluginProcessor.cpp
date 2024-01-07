/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "parser/FileParser.h"
#include "parser/FrameProducer.h"
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
    // locking isn't necessary here because we are in the constructor

    toggleableEffects.push_back(std::make_shared<Effect>(
        std::make_shared<BitCrushEffect>(),
        new EffectParameter("Bit Crush", "Limits the resolution of points drawn to the screen, making the image look pixelated, and making the audio sound more 'digital' and distorted.", "bitCrush", VERSION_HINT, 0.0, 0.0, 1.0)
    ));
    toggleableEffects.push_back(std::make_shared<Effect>(
        std::make_shared<BulgeEffect>(),
        new EffectParameter("Bulge", "Applies a bulge that makes the centre of the image larger, and squishes the edges of the image. This applies a distortion to the audio.", "bulge", VERSION_HINT, 0.0, 0.0, 1.0)
    ));
    toggleableEffects.push_back(std::make_shared<Effect>(
        std::make_shared<VectorCancellingEffect>(),
        new EffectParameter("Vector Cancelling", "Inverts the audio and image every few samples to 'cancel out' the audio, making the audio quiet, and distorting the image.", "vectorCancelling", VERSION_HINT, 0.0, 0.0, 1.0)
    ));
    toggleableEffects.push_back(std::make_shared<Effect>(
        std::make_shared<DistortEffect>(false),
        new EffectParameter("Distort X", "Distorts the image in the horizontal direction by jittering the audio sample being drawn.", "distortX", VERSION_HINT, 0.0, 0.0, 1.0)
    ));
    toggleableEffects.push_back(std::make_shared<Effect>(
        std::make_shared<DistortEffect>(true),
        new EffectParameter("Distort Y", "Distorts the image in the vertical direction by jittering the audio sample being drawn.", "distortY", VERSION_HINT, 0.0, 0.0, 1.0)
    ));
    toggleableEffects.push_back(std::make_shared<Effect>(
        [this](int index, Point input, const std::vector<double>& values, double sampleRate) {
            input.x += values[0];
            input.y += values[1];
            return input;
        }, std::vector<EffectParameter*>{
            new EffectParameter("Translate X", "Moves the image horizontally.", "translateX", VERSION_HINT, 0.0, -1.0, 1.0),
            new EffectParameter("Translate Y", "Moves the image vertically.", "translateY", VERSION_HINT, 0.0, -1.0, 1.0)
        }
    ));
    toggleableEffects.push_back(std::make_shared<Effect>(
        std::make_shared<SmoothEffect>(),
        new EffectParameter("Smoothing", "This works as a low-pass frequency filter that removes high frequencies, making the image look smoother, and audio sound less harsh.", "smoothing", VERSION_HINT, 0.0, 0.0, 1.0)
    ));
    toggleableEffects.push_back(std::make_shared<Effect>(
        wobbleEffect,
        new EffectParameter("Wobble", "Adds a sine wave of the prominent frequency in the audio currently playing. The sine wave's frequency is slightly offset to create a subtle 'wobble' in the image. Increasing the slider increases the strength of the wobble.", "wobble", VERSION_HINT, 0.0, 0.0, 1.0)
    ));
    toggleableEffects.push_back(std::make_shared<Effect>(
        delayEffect,
        std::vector<EffectParameter*>{
            new EffectParameter("Delay Decay", "Adds repetitions, delays, or echos to the audio. This slider controls the volume of the echo.", "delayDecay", VERSION_HINT, 0.0, 0.0, 1.0),
            new EffectParameter("Delay Length", "Controls the time in seconds between echos.", "delayLength", VERSION_HINT, 0.5, 0.0, 1.0)
        }
    ));
    toggleableEffects.push_back(std::make_shared<Effect>(
        customEffect,
        new EffectParameter("Effect Strength", "Controls the strength of the custom effect applied.", "customEffectStrength", VERSION_HINT, 1.0, 0.0, 1.0)
    ));
    toggleableEffects.push_back(traceMax);
    toggleableEffects.push_back(traceMin);

    for (int i = 0; i < toggleableEffects.size(); i++) {
        auto effect = toggleableEffects[i];
        effect->markEnableable(false);
        addParameter(effect->enabled);
        effect->enabled->setValueNotifyingHost(false);
        effect->setPrecedence(i);
    }

    permanentEffects.push_back(perspective);
    permanentEffects.push_back(frequencyEffect);
    permanentEffects.push_back(volumeEffect);
    permanentEffects.push_back(thresholdEffect);

    for (int i = 0; i < 26; i++) {
        addLuaSlider();
    }

    for (auto& effect : luaEffects) {
        effect->addListener(0, this);
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

    booleanParameters.push_back(perspectiveEffect->fixedRotateX);
    booleanParameters.push_back(perspectiveEffect->fixedRotateY);
    booleanParameters.push_back(perspectiveEffect->fixedRotateZ);
    booleanParameters.push_back(midiEnabled);
    booleanParameters.push_back(inputEnabled);

    for (auto parameter : booleanParameters) {
        addParameter(parameter);
    }

    floatParameters.push_back(attackTime);
    floatParameters.push_back(attackLevel);
    floatParameters.push_back(attackShape);
    floatParameters.push_back(decayTime);
    floatParameters.push_back(decayShape);
    floatParameters.push_back(sustainLevel);
    floatParameters.push_back(releaseTime);
    floatParameters.push_back(releaseShape);

    for (auto parameter : floatParameters) {
        addParameter(parameter);
    }

    for (int i = 0; i < voices->getValueUnnormalised(); i++) {
        synth.addVoice(new ShapeVoice(*this));
    }

    intParameters.push_back(voices);

    for (auto parameter : intParameters) {
        addParameter(parameter);
    }

    voices->addListener(this);
        
    synth.addSound(defaultSound);
}

OscirenderAudioProcessor::~OscirenderAudioProcessor() {
    for (auto& effect : luaEffects) {
        effect->removeListener(0, this);
    }
    voices->removeListener(this);
}

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
    volumeBuffer = std::vector<double>(VOLUME_BUFFER_SECONDS * sampleRate, 0);
    pitchDetector.setSampleRate(sampleRate);
    synth.setCurrentPlaybackSampleRate(sampleRate);
    retriggerMidi = true;
    
    for (auto& effect : allEffects) {
        effect->updateSampleRate(currentSampleRate);
    }
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
        new EffectParameter(
            "Lua Slider " + sliderName,
            "Controls the value of the Lua variable called slider_" + sliderName.toLowerCase() + ".",
            "lua" + sliderName,
            VERSION_HINT, 0.0, 0.0, 1.0, 0.001, false
        )
    ));

    auto& effect = luaEffects.back();
    effect->parameters[0]->disableLfo();
    effect->parameters[0]->disableSidechain();
}

// effectsLock should be held when calling this
void OscirenderAudioProcessor::updateLuaValues() {
    for (auto& effect : luaEffects) {
        effect->apply();
	}
}

void OscirenderAudioProcessor::addErrorListener(ErrorListener* listener) {
    juce::SpinLock::ScopedLockType lock(errorListenersLock);
    errorListeners.push_back(listener);
}

void OscirenderAudioProcessor::removeErrorListener(ErrorListener* listener) {
    juce::SpinLock::ScopedLockType lock(errorListenersLock);
    errorListeners.erase(std::remove(errorListeners.begin(), errorListeners.end(), listener), errorListeners.end());
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

// effectsLock should be held when calling this
FloatParameter* OscirenderAudioProcessor::getFloatParameter(juce::String id) {
    for (auto& parameter : floatParameters) {
        if (parameter->paramID == id) {
            return parameter;
        }
    }
    return nullptr;
}

// effectsLock should be held when calling this
IntParameter* OscirenderAudioProcessor::getIntParameter(juce::String id) {
    for (auto& parameter : intParameters) {
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
    fileIds.push_back(currentFileId++);
	parsers.push_back(std::make_shared<FileParser>(errorCallback));
    sounds.push_back(new ShapeSound(parsers.back()));
    file.createInputStream()->readIntoMemoryBlock(*fileBlocks.back());

    openFile(fileBlocks.size() - 1);
}

// parsersLock AND effectsLock must be locked before calling this function
void OscirenderAudioProcessor::addFile(juce::String fileName, const char* data, const int size) {
    fileBlocks.push_back(std::make_shared<juce::MemoryBlock>());
    fileNames.push_back(fileName);
    fileIds.push_back(currentFileId++);
    parsers.push_back(std::make_shared<FileParser>(errorCallback));
    sounds.push_back(new ShapeSound(parsers.back()));
    fileBlocks.back()->append(data, size);

    openFile(fileBlocks.size() - 1);
}

// parsersLock AND effectsLock must be locked before calling this function
void OscirenderAudioProcessor::addFile(juce::String fileName, std::shared_ptr<juce::MemoryBlock> data) {
    fileBlocks.push_back(data);
    fileNames.push_back(fileName);
    fileIds.push_back(currentFileId++);
    parsers.push_back(std::make_shared<FileParser>(errorCallback));
    sounds.push_back(new ShapeSound(parsers.back()));

    openFile(fileBlocks.size() - 1);
}

// parsersLock AND effectsLock must be locked before calling this function
void OscirenderAudioProcessor::removeFile(int index) {
	if (index < 0 || index >= fileBlocks.size()) {
		return;
	}
    fileBlocks.erase(fileBlocks.begin() + index);
    fileNames.erase(fileNames.begin() + index);
    fileIds.erase(fileIds.begin() + index);
    parsers.erase(parsers.begin() + index);
    sounds.erase(sounds.begin() + index);
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
    juce::SpinLock::ScopedLockType lock(fontLock);
    parsers[index]->parse(juce::String(fileIds[index]), fileNames[index].fromLastOccurrenceOf(".", true, false), std::make_unique<juce::MemoryInputStream>(*fileBlocks[index], false), font);
    changeCurrentFile(index);
}

// used ONLY for changing the current file to an EXISTING file.
// much faster than openFile(int index) because it doesn't reparse any files.
// parsersLock AND effectsLock must be locked before calling this function
void OscirenderAudioProcessor::changeCurrentFile(int index) {
    if (index == -1) {
        currentFile = -1;
        changeSound(defaultSound);
    }
	if (index < 0 || index >= fileBlocks.size()) {
		return;
	}
    currentFile = index;
    updateLuaValues();
    changeSound(sounds[index]);
}

void OscirenderAudioProcessor::changeSound(ShapeSound::Ptr sound) {
    if (!objectServerRendering || sound == objectServerSound) {
        synth.clearSounds();
        synth.addSound(sound);
        for (int i = 0; i < synth.getNumVoices(); i++) {
            auto voice = dynamic_cast<ShapeVoice*>(synth.getVoice(i));
            voice->updateSound(sound.get());
        }
    }
}

void OscirenderAudioProcessor::notifyErrorListeners(int lineNumber, juce::String fileName, juce::String error) {
    juce::SpinLock::ScopedLockType lock(errorListenersLock);
    for (auto listener : errorListeners) {
        if (listener->getFileName() == fileName) {
            listener->onError(lineNumber, error);
        }
    }
}

int OscirenderAudioProcessor::getCurrentFileIndex() {
    return currentFile;
}

std::shared_ptr<FileParser> OscirenderAudioProcessor::getCurrentFileParser() {
    return parsers[currentFile];
}

juce::String OscirenderAudioProcessor::getCurrentFileName() {
    if (objectServerRendering || currentFile == -1) {
		return "";
    } else {
        return fileNames[currentFile];
    }
}

juce::String OscirenderAudioProcessor::getFileName(int index) {
    return fileNames[index];
}

juce::String OscirenderAudioProcessor::getFileId(int index) {
    return juce::String(fileIds[index]);
}

std::shared_ptr<juce::MemoryBlock> OscirenderAudioProcessor::getFileBlock(int index) {
    return fileBlocks[index];
}

void OscirenderAudioProcessor::setObjectServerRendering(bool enabled) {
    {
        juce::SpinLock::ScopedLockType lock1(parsersLock);

        objectServerRendering = enabled;
        if (enabled) {
            changeSound(objectServerSound);
        } else {
            changeCurrentFile(currentFile);
        }
    }

    {
        juce::MessageManagerLock lock;
        fileChangeBroadcaster.sendChangeMessage();
    }
}

void OscirenderAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) {
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    // merge keyboard state and midi messages
    keyboardState.processNextMidiBuffer(midiMessages, 0, buffer.getNumSamples(), true);

    bool usingInput = inputEnabled->getBoolValue();

    bool usingMidi = midiEnabled->getBoolValue();
    if (!usingMidi) {
        midiMessages.clear();
    }
    
    // if midi enabled has changed state
    if (prevMidiEnabled != usingMidi) {
        for (int i = 1; i <= 16; i++) {
            midiMessages.addEvent(juce::MidiMessage::allNotesOff(i), i);
        }
    }
    
    // if midi has just been disabled or we need to retrigger
    if (!usingMidi && (retriggerMidi || prevMidiEnabled)) {
        midiMessages.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 17);
        retriggerMidi = false;
    }
    
    prevMidiEnabled = usingMidi;

    const double EPSILON = 0.00001;

    juce::AudioBuffer<float> inputBuffer = juce::AudioBuffer<float>(totalNumInputChannels, buffer.getNumSamples());
    for (auto channel = 0; channel < totalNumInputChannels; channel++) {
        inputBuffer.copyFrom(channel, 0, buffer, channel, 0, buffer.getNumSamples());
    }

    juce::AudioBuffer<float> outputBuffer3d = juce::AudioBuffer<float>(3, buffer.getNumSamples());
    outputBuffer3d.clear();

    if (usingInput && totalNumInputChannels >= 2) {
        for (auto channel = 0; channel < juce::jmin(2, totalNumInputChannels); channel++) {
            outputBuffer3d.copyFrom(channel, 0, inputBuffer, channel, 0, buffer.getNumSamples());
        }

        // handle all midi messages
        auto midiIterator = midiMessages.cbegin();
        std::for_each(midiIterator,
            midiMessages.cend(),
            [&] (const juce::MidiMessageMetadata& meta) { synth.publicHandleMidiEvent(meta.getMessage()); }
        );
    } else {
        juce::SpinLock::ScopedLockType lock1(parsersLock);
        juce::SpinLock::ScopedLockType lock2(effectsLock);
        synth.renderNextBlock(outputBuffer3d, midiMessages, 0, buffer.getNumSamples());
    }
    
    midiMessages.clear();
    
    auto* channelData = buffer.getArrayOfWritePointers();
    
	for (auto sample = 0; sample < buffer.getNumSamples(); ++sample) {
        auto left = 0.0;
        auto right = 0.0;
        if (totalNumInputChannels >= 2) {
            left = inputBuffer.getSample(0, sample);
            right = inputBuffer.getSample(1, sample);
        } else if (totalNumInputChannels == 1) {
            left = inputBuffer.getSample(0, sample);
            right = inputBuffer.getSample(0, sample);
        }

        // update volume using a moving average
        int oldestBufferIndex = (volumeBufferIndex + 1) % volumeBuffer.size();
        squaredVolume -= volumeBuffer[oldestBufferIndex] / volumeBuffer.size();
        volumeBufferIndex = oldestBufferIndex;
        volumeBuffer[volumeBufferIndex] = (left * left + right * right) / 2;
        squaredVolume += volumeBuffer[volumeBufferIndex] / volumeBuffer.size();
        currentVolume = std::sqrt(squaredVolume);
        currentVolume = juce::jlimit(0.0, 1.0, currentVolume);

        Point channels = { outputBuffer3d.getSample(0, sample), outputBuffer3d.getSample(1, sample), outputBuffer3d.getSample(2, sample) };

        {
            juce::SpinLock::ScopedLockType lock1(parsersLock);
            juce::SpinLock::ScopedLockType lock2(effectsLock);
            if (volume > EPSILON) {
                for (auto& effect : toggleableEffects) {
                    if (effect->enabled->getValue()) {
                        channels = effect->apply(sample, channels, currentVolume);
                    }
                }
            }
            for (auto& effect : permanentEffects) {
                channels = effect->apply(sample, channels, currentVolume);
            }
        }

		double x = channels.x;
		double y = channels.y;

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

        {
            juce::SpinLock::ScopedLockType scope(consumerLock);
            for (auto consumer : consumers) {
                consumer->write(x);
                consumer->write(y);
                consumer->notifyIfFull();
            }
        }
	}
}

//==============================================================================
bool OscirenderAudioProcessor::hasEditor() const {
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* OscirenderAudioProcessor::createEditor() {
    auto editor = new OscirenderAudioProcessorEditor(*this);
    return editor;
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

    auto floatParametersXml = xml->createNewChildElement("floatParameters");
    for (auto parameter : floatParameters) {
        auto parameterXml = floatParametersXml->createNewChildElement("parameter");
        parameter->save(parameterXml);
    }

    auto intParametersXml = xml->createNewChildElement("intParameters");
    for (auto parameter : intParameters) {
        auto parameterXml = intParametersXml->createNewChildElement("parameter");
        parameter->save(parameterXml);
    }

    auto customFunction = xml->createNewChildElement("customFunction");
    customFunction->addTextElement(juce::Base64::toBase64(customEffect->getCode()));

    auto fontXml = xml->createNewChildElement("font");
    fontXml->setAttribute("family", font.getTypefaceName());
    fontXml->setAttribute("bold", font.isBold());
    fontXml->setAttribute("italic", font.isItalic());

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
    std::unique_ptr<juce::XmlElement> xml;

    const uint32_t magicXmlNumber = 0x21324356;
    if (sizeInBytes > 8 && juce::ByteOrder::littleEndianInt(data) == magicXmlNumber) {
        // this is a binary xml format
        xml = getXmlFromBinary(data, sizeInBytes);
    } else {
        // this is a text xml format
        xml = juce::XmlDocument::parse(juce::String((const char*)data, sizeInBytes));
    }

    if (xml.get() != nullptr && xml->hasTagName("project")) {
        auto versionXml = xml->getChildByName("version");
        if (versionXml != nullptr && versionXml->getAllSubText().startsWith("v1.")) {
            openLegacyProject(xml.get());
            return;
        }

        juce::SpinLock::ScopedLockType lock1(parsersLock);
        juce::SpinLock::ScopedLockType lock2(effectsLock);

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

        auto floatParametersXml = xml->getChildByName("floatParameters");
        if (floatParametersXml != nullptr) {
            for (auto parameterXml : floatParametersXml->getChildIterator()) {
                auto parameter = getFloatParameter(parameterXml->getStringAttribute("id"));
                if (parameter != nullptr) {
                    parameter->load(parameterXml);
                }
            }
        }

        auto intParametersXml = xml->getChildByName("intParameters");
        if (intParametersXml != nullptr) {
            for (auto parameterXml : intParametersXml->getChildIterator()) {
                auto parameter = getIntParameter(parameterXml->getStringAttribute("id"));
                if (parameter != nullptr) {
                    parameter->load(parameterXml);
                }
            }
        }

        auto customFunction = xml->getChildByName("customFunction");
        if (customFunction == nullptr) {
            customFunction = xml->getChildByName("perspectiveFunction");
        }
        if (customFunction != nullptr) {
            auto stream = juce::MemoryOutputStream();
            juce::Base64::convertFromBase64(stream, customFunction->getAllSubText());
            customEffect->updateCode(stream.toString());
        }

        auto fontXml = xml->getChildByName("font");
        if (fontXml != nullptr) {
            auto family = fontXml->getStringAttribute("family");
            auto bold = fontXml->getBoolAttribute("bold");
            auto italic = fontXml->getBoolAttribute("italic");
            juce::SpinLock::ScopedLockType lock(fontLock);
            font = juce::Font(family, 1.0, (bold ? juce::Font::bold : 0) | (italic ? juce::Font::italic : 0));
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
        prevMidiEnabled = !midiEnabled->getBoolValue();
    }
}

std::shared_ptr<BufferConsumer> OscirenderAudioProcessor::consumerRegister(std::vector<float>& buffer) {
    std::shared_ptr<BufferConsumer> consumer = std::make_shared<BufferConsumer>(buffer);
    juce::SpinLock::ScopedLockType scope(consumerLock);
    consumers.push_back(consumer);
    
    return consumer;
}

void OscirenderAudioProcessor::consumerRead(std::shared_ptr<BufferConsumer> consumer) {
    consumer->waitUntilFull();
    juce::SpinLock::ScopedLockType scope(consumerLock);
    consumers.erase(std::remove(consumers.begin(), consumers.end(), consumer), consumers.end());
}

void OscirenderAudioProcessor::consumerStop(std::shared_ptr<BufferConsumer> consumer) {
    if (consumer != nullptr) {
        juce::SpinLock::ScopedLockType scope(consumerLock);
        consumer->forceNotify();
    }
}

void OscirenderAudioProcessor::parameterValueChanged(int parameterIndex, float newValue) {
    // call apply on lua effects
    for (auto& effect : luaEffects) {
        if (parameterIndex == effect->parameters[0]->getParameterIndex()) {
            effect->apply();
            return;
        }
    }

    if (parameterIndex == voices->getParameterIndex()) {
        int numVoices = voices->getValueUnnormalised();
        // if the number of voices has changed, update the synth without clearing all the voices
        if (numVoices != synth.getNumVoices()) {
            if (numVoices > synth.getNumVoices()) {
                for (int i = synth.getNumVoices(); i < numVoices; i++) {
                    synth.addVoice(new ShapeVoice(*this));
                }
            } else {
                for (int i = synth.getNumVoices() - 1; i >= numVoices; i--) {
                    synth.removeVoice(i);
                }
            }
        }
    }
}

void OscirenderAudioProcessor::parameterGestureChanged(int parameterIndex, bool gestureIsStarting) {}

void updateIfApproxEqual(FloatParameter* parameter, float newValue) {
    if (std::abs(parameter->getValueUnnormalised() - newValue) > 0.0001) {
        parameter->setUnnormalisedValueNotifyingHost(newValue);
    }
}

void OscirenderAudioProcessor::envelopeChanged(EnvelopeComponent* changedEnvelope) {
    Env env = changedEnvelope->getEnv();
    std::vector<double> levels = env.getLevels();
    std::vector<double> times = env.getTimes();
    EnvCurveList curves = env.getCurves();

    if (levels.size() == 4 && times.size() == 3 && curves.size() == 3) {
        {
            juce::SpinLock::ScopedLockType lock(effectsLock);
            this->adsrEnv = env;
        }
        updateIfApproxEqual(attackTime, times[0]);
        updateIfApproxEqual(attackLevel, levels[1]);
        updateIfApproxEqual(attackShape, curves[0].getCurve());
        updateIfApproxEqual(decayTime, times[1]);
        updateIfApproxEqual(sustainLevel, levels[2]);
        updateIfApproxEqual(decayShape, curves[1].getCurve());
        updateIfApproxEqual(releaseTime, times[2]);
        updateIfApproxEqual(releaseShape, curves[2].getCurve());
    }
}


//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new OscirenderAudioProcessor();
}

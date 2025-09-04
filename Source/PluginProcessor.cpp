/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"

#include "PluginEditor.h"
#include "audio/BitCrushEffect.h"
#include "audio/BulgeEffect.h"
#include "audio/TwistEffect.h"
#include "audio/DistortEffect.h"
#include "audio/KaleidoscopeEffect.h"
#include "audio/MultiplexEffect.h"
#include "audio/SmoothEffect.h"
#include "audio/WobbleEffect.h"
#include "audio/DashedLineEffect.h"
#include "audio/VectorCancellingEffect.h"
#include "audio/ScaleEffect.h"
#include "audio/RotateEffect.h"
#include "audio/TranslateEffect.h"
#include "audio/RippleEffect.h"
#include "audio/SwirlEffect.h"
#include "audio/BounceEffect.h"
#include "audio/SkewEffect.h"
#include "audio/RadialWrapEffect.h"
#include "parser/FileParser.h"
#include "parser/FrameProducer.h"

#if (JUCE_MAC || JUCE_WINDOWS) && OSCI_PREMIUM
#include "img/ImageParser.h"
#endif

//==============================================================================
OscirenderAudioProcessor::OscirenderAudioProcessor() : CommonAudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::namedChannelSet(2), true).withOutput("Output", juce::AudioChannelSet::stereo(), true)) {
    // locking isn't necessary here because we are in the constructor

    toggleableEffects.push_back(BitCrushEffect().build());
    toggleableEffects.push_back(BulgeEffect().build());
    toggleableEffects.push_back(VectorCancellingEffect().build());
    toggleableEffects.push_back(RippleEffectApp().build());
    toggleableEffects.push_back(RotateEffectApp().build());
    toggleableEffects.push_back(TranslateEffectApp().build());
    toggleableEffects.push_back(SwirlEffectApp().build());
    toggleableEffects.push_back(SmoothEffect().build());
    toggleableEffects.push_back(DelayEffect().build());
    toggleableEffects.push_back(DashedLineEffect(*this).build());
    toggleableEffects.push_back(TraceEffect(*this).build());
    toggleableEffects.push_back(WobbleEffect(*this).build());

#if OSCI_PREMIUM
    toggleableEffects.push_back(MultiplexEffect(*this).build());
    toggleableEffects.push_back(KaleidoscopeEffect(*this).build());
    toggleableEffects.push_back(BounceEffect().build());
    toggleableEffects.push_back(TwistEffect().build());
    toggleableEffects.push_back(SkewEffect().build());
    toggleableEffects.push_back(RadialWrapEffect().build());
#endif

    auto scaleEffect = ScaleEffectApp().build();
    booleanParameters.push_back(scaleEffect->linked);
    toggleableEffects.push_back(scaleEffect);

    auto distortEffect = DistortEffect().build();
    booleanParameters.push_back(distortEffect->linked);
    toggleableEffects.push_back(distortEffect);

    custom->setIcon(BinaryData::lua_svg);
    toggleableEffects.push_back(custom);

    for (int i = 0; i < toggleableEffects.size(); i++) {
        auto effect = toggleableEffects[i];
        effect->markSelectable(false);
        booleanParameters.push_back(effect->selected);
        effect->selected->setValueNotifyingHost(false);
        effect->markEnableable(false);
        booleanParameters.push_back(effect->enabled);
        effect->enabled->setValueNotifyingHost(false);
        effect->setPrecedence(i);
    }

    std::vector<std::shared_ptr<osci::Effect>> osciPermanentEffects;
    osciPermanentEffects.push_back(perspective);
    osciPermanentEffects.push_back(frequencyEffect);
    osciPermanentEffects.push_back(imageThreshold);
    osciPermanentEffects.push_back(imageStride);

    for (int i = 0; i < 26; i++) {
        addLuaSlider();
        if (i < luaEffects.size()) {
            luaEffects[i]->setIcon(BinaryData::lua_svg);
        }
    }

    effects.insert(effects.end(), toggleableEffects.begin(), toggleableEffects.end());
    permanentEffects.insert(permanentEffects.end(), osciPermanentEffects.begin(), osciPermanentEffects.end());
    effects.insert(effects.end(), osciPermanentEffects.begin(), osciPermanentEffects.end());
    effects.insert(effects.end(), luaEffects.begin(), luaEffects.end());

    booleanParameters.push_back(midiEnabled);
    booleanParameters.push_back(inputEnabled);
    booleanParameters.push_back(animateFrames);
    booleanParameters.push_back(loopAnimation);
    booleanParameters.push_back(animationSyncBPM);
    booleanParameters.push_back(invertImage);

    floatParameters.push_back(attackTime);
    floatParameters.push_back(attackLevel);
    floatParameters.push_back(attackShape);
    floatParameters.push_back(decayTime);
    floatParameters.push_back(decayShape);
    floatParameters.push_back(sustainLevel);
    floatParameters.push_back(releaseTime);
    floatParameters.push_back(releaseShape);
    floatParameters.push_back(animationRate);
    floatParameters.push_back(animationOffset);

    for (int i = 0; i < voices->getValueUnnormalised(); i++) {
        synth.addVoice(new ShapeVoice(*this, inputBuffer));
    }

    intParameters.push_back(voices);

    voices->addListener(this);

    for (int i = 0; i < luaEffects.size(); i++) {
        luaEffects[i]->parameters[0]->addListener(this);
    }

    synth.addSound(defaultSound);

    addAllParameters();
}

OscirenderAudioProcessor::~OscirenderAudioProcessor() {
    for (int i = luaEffects.size() - 1; i >= 0; i--) {
        luaEffects[i]->parameters[0]->removeListener(this);
    }
    voices->removeListener(this);
}

void OscirenderAudioProcessor::setAudioThreadCallback(std::function<void(const juce::AudioBuffer<float>&)> callback) {
    juce::SpinLock::ScopedLockType lock(audioThreadCallbackLock);
    audioThreadCallback = callback;
}

void OscirenderAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    CommonAudioProcessor::prepareToPlay(sampleRate, samplesPerBlock);

    volumeBuffer = std::vector<double>(VOLUME_BUFFER_SECONDS * sampleRate, 0);
    synth.setCurrentPlaybackSampleRate(sampleRate);
    retriggerMidi = true;
}

// effectsLock should be held when calling this
void OscirenderAudioProcessor::addLuaSlider() {
    juce::String sliderName = "";

    int sliderIndex = luaEffects.size();
    int sliderNum = sliderIndex + 1;
    while (sliderNum > 0) {
        int mod = (sliderNum - 1) % 26;
        sliderName = (char)(mod + 'A') + sliderName;
        sliderNum = (sliderNum - mod) / 26;
    }

    luaEffects.push_back(std::make_shared<osci::Effect>(
        [this, sliderIndex](int index, osci::Point input, const std::vector<std::atomic<double>>& values, double sampleRate) {
            luaValues[sliderIndex].store(values[0]);
            return input;
        },
        new osci::EffectParameter(
            "Lua Slider " + sliderName,
            "Controls the value of the Lua variable called slider_" + sliderName.toLowerCase() + ".",
            "lua" + sliderName,
            VERSION_HINT, 0.0, 0.0, 1.0)));
}

void OscirenderAudioProcessor::addErrorListener(ErrorListener* listener) {
    juce::SpinLock::ScopedLockType lock(errorListenersLock);
    errorListeners.push_back(listener);
}

void OscirenderAudioProcessor::removeErrorListener(ErrorListener* listener) {
    juce::SpinLock::ScopedLockType lock(errorListenersLock);
    errorListeners.erase(std::remove(errorListeners.begin(), errorListeners.end(), listener), errorListeners.end());
}

// effectsLock MUST be held when calling this
void OscirenderAudioProcessor::updateEffectPrecedence() {
    auto sortFunc = [](std::shared_ptr<osci::Effect> a, std::shared_ptr<osci::Effect> b) {
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
    parsers.push_back(std::make_shared<FileParser>(*this, errorCallback));
    sounds.push_back(new ShapeSound(*this, parsers.back()));
    file.createInputStream()->readIntoMemoryBlock(*fileBlocks.back());

    openFile(fileBlocks.size() - 1);
}

// parsersLock AND effectsLock must be locked before calling this function
void OscirenderAudioProcessor::addFile(juce::String fileName, const char* data, const int size) {
    fileBlocks.push_back(std::make_shared<juce::MemoryBlock>());
    fileNames.push_back(fileName);
    fileIds.push_back(currentFileId++);
    parsers.push_back(std::make_shared<FileParser>(*this, errorCallback));
    sounds.push_back(new ShapeSound(*this, parsers.back()));
    fileBlocks.back()->append(data, size);

    openFile(fileBlocks.size() - 1);
}

// parsersLock AND effectsLock must be locked before calling this function
void OscirenderAudioProcessor::addFile(juce::String fileName, std::shared_ptr<juce::MemoryBlock> data) {
    fileBlocks.push_back(data);
    fileNames.push_back(fileName);
    fileIds.push_back(currentFileId++);
    parsers.push_back(std::make_shared<FileParser>(*this, errorCallback));
    sounds.push_back(new ShapeSound(*this, parsers.back()));

    openFile(fileBlocks.size() - 1);
}

// Setter for the callback
void OscirenderAudioProcessor::setFileRemovedCallback(std::function<void(int)> callback) {
    fileRemovedCallback = std::move(callback);
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

    // Notify the editor about the file removal
    if (fileRemovedCallback) {
        fileRemovedCallback(index);
    }
}

// parsersLock AND effectsLock must be locked before calling this function
void OscirenderAudioProcessor::removeParser(FileParser* parser) {
    int parserIndex = -1;
    for (int i = 0; i < parsers.size(); i++) {
        if (parsers[i].get() == parser) {
            parserIndex = i;
            break;
        }
    }

    if (parserIndex >= 0) {
        removeFile(parserIndex);
    }
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
    parsers[index]->parse(juce::String(fileIds[index]), fileNames[index], fileNames[index].fromLastOccurrenceOf(".", true, false).toLowerCase(), std::make_unique<juce::MemoryInputStream>(*fileBlocks[index], false), font);
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

void OscirenderAudioProcessor::notifyErrorListeners(int lineNumber, juce::String id, juce::String error) {
    juce::SpinLock::ScopedLockType lock(errorListenersLock);
    for (auto listener : errorListeners) {
        if (listener->getId() == id) {
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

void OscirenderAudioProcessor::setObjectServerPort(int port) {
    setProperty("objectServerPort", port);
    objectServer.reload();
}

void OscirenderAudioProcessor::setPreviewEffectId(const juce::String& effectId) {
    previewEffect.reset();
    for (auto& eff : toggleableEffects) {
        if (eff->getId() == effectId) { previewEffect = eff; break; }
    }
}

void OscirenderAudioProcessor::clearPreviewEffect() {
    previewEffect.reset();
}

void OscirenderAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) {
    juce::ScopedNoDenormals noDenormals;
    // Audio info variables
    int totalNumInputChannels = getTotalNumInputChannels();
    int totalNumOutputChannels = getTotalNumOutputChannels();
    double sampleRate = getSampleRate();

    // MIDI transport info variables (defaults to 60bpm, 4/4 time signature at zero seconds and not playing)
    double bpm = 60;
    double playTimeSeconds = 0;
    bool isPlaying = false;
    juce::AudioPlayHead::TimeSignature timeSig;

    // Get MIDI transport info
    playHead = this->getPlayHead();
    if (playHead != nullptr) {
        auto pos = playHead->getPosition();
        if (pos.hasValue()) {
            juce::AudioPlayHead::PositionInfo pi = *pos;
            bpm = pi.getBpm().orFallback(bpm);
            playTimeSeconds = pi.getTimeInSeconds().orFallback(playTimeSeconds);
            isPlaying = pi.getIsPlaying();
            timeSig = pi.getTimeSignature().orFallback(timeSig);
        }
    }

    // Calculated number of beats
    // TODO: To make this more resilient to changing BPMs, we should change how this is calculated
    // or use another property of the AudioPlayHead::PositionInfo
    double playTimeBeats = bpm * playTimeSeconds / 60;

    // Calculated time per sample in seconds and beats
    double sTimeSec = 1.f / sampleRate;
    double sTimeBeats = bpm * sTimeSec / 60;

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

    inputBuffer = juce::AudioBuffer<float>(totalNumInputChannels, buffer.getNumSamples());
    for (auto channel = 0; channel < totalNumInputChannels; channel++) {
        inputBuffer.copyFrom(channel, 0, buffer, channel, 0, buffer.getNumSamples());
    }

    juce::AudioBuffer<float> outputBuffer3d = juce::AudioBuffer<float>(3, buffer.getNumSamples());
    outputBuffer3d.clear();

#if (JUCE_MAC || JUCE_WINDOWS) && OSCI_PREMIUM
    if (syphonInputActive) {
        for (int sample = 0; sample < outputBuffer3d.getNumSamples(); sample++) {
            osci::Point point = syphonImageParser.getSample();
            outputBuffer3d.setSample(0, sample, point.x);
            outputBuffer3d.setSample(1, sample, point.y);
        }
    } else
#endif
    if (usingInput && totalNumInputChannels >= 1) {
        if (totalNumInputChannels >= 2) {
            for (auto channel = 0; channel < juce::jmin(2, totalNumInputChannels); channel++) {
                outputBuffer3d.copyFrom(channel, 0, inputBuffer, channel, 0, buffer.getNumSamples());
            }
        } else {
            // For mono input, copy the single channel to both left and right
            outputBuffer3d.copyFrom(0, 0, inputBuffer, 0, 0, buffer.getNumSamples());
            outputBuffer3d.copyFrom(1, 0, inputBuffer, 0, 0, buffer.getNumSamples());
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
        for (int i = 0; i < synth.getNumVoices(); i++) {
            auto voice = dynamic_cast<ShapeVoice*>(synth.getVoice(i));
            if (voice->isVoiceActive()) {
                customEffect->frequency = voice->getFrequency();
                break;
            }
        }
    }

    midiMessages.clear();

    auto* channelData = buffer.getArrayOfWritePointers();

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample) {
        if (animateFrames->getBoolValue()) {
            if (juce::JUCEApplicationBase::isStandaloneApp()) {
                animationFrame = animationFrame + sTimeSec * animationRate->getValueUnnormalised();
            } else if (animationSyncBPM->getValue()) {
                animationFrame = playTimeBeats * animationRate->getValueUnnormalised() + animationOffset->getValueUnnormalised();
            } else {
                animationFrame = playTimeSeconds * animationRate->getValueUnnormalised() + animationOffset->getValueUnnormalised();
            }

            juce::SpinLock::ScopedLockType lock1(parsersLock);
            juce::SpinLock::ScopedLockType lock2(effectsLock);
            if (currentFile >= 0 && sounds[currentFile]->parser->isAnimatable) {
                int totalFrames = sounds[currentFile]->parser->getNumFrames();
                if (loopAnimation->getBoolValue()) {
                    animationFrame = std::fmod(animationFrame, totalFrames);
                } else {
                    animationFrame = juce::jlimit(0.0, (double)totalFrames - 1, animationFrame.load());
                }
                sounds[currentFile]->parser->setFrame(animationFrame);
            }
        }

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

        osci::Point channels = {outputBuffer3d.getSample(0, sample), outputBuffer3d.getSample(1, sample), outputBuffer3d.getSample(2, sample)};

        {
            juce::SpinLock::ScopedLockType lock1(parsersLock);
            juce::SpinLock::ScopedLockType lock2(effectsLock);
            if (volume > EPSILON) {
                for (auto& effect : toggleableEffects) {
                    bool isEnabled = effect->enabled != nullptr && effect->enabled->getValue();
                    bool isSelected = effect->selected == nullptr ? true : effect->selected->getBoolValue();
                    if (isEnabled && isSelected) {
                        if (effect->getId() == custom->getId()) {
                            effect->setExternalInput(osci::Point{ left, right });
                        }
                        channels = effect->apply(sample, channels, currentVolume);
                    }
                }
                // Apply preview effect if present and not already active in the main chain
                if (previewEffect) {
                    const bool prevEnabled = (previewEffect->enabled != nullptr) && previewEffect->enabled->getValue();
                    const bool prevSelected = (previewEffect->selected == nullptr) ? true : previewEffect->selected->getBoolValue();
                    if (!(prevEnabled && prevSelected)) {
                        if (previewEffect->getId() == custom->getId())
                            previewEffect->setExternalInput(osci::Point{ left, right });
                        channels = previewEffect->apply(sample, channels, currentVolume);
                    }
                }
            }
            for (auto& effect : permanentEffects) {
                channels = effect->apply(sample, channels, currentVolume);
            }
            auto lua = currentFile >= 0 ? sounds[currentFile]->parser->getLua() : nullptr;
            if (lua != nullptr || custom->enabled->getBoolValue()) {
                for (auto& effect : luaEffects) {
                    effect->apply(sample, channels, currentVolume);
                }
            }
        }

        double x = channels.x;
        double y = channels.y;

        x *= volume;
        y *= volume;

        // clip
        x = juce::jmax(-threshold, juce::jmin(threshold.load(), x));
        y = juce::jmax(-threshold, juce::jmin(threshold.load(), y));

        threadManager.write(osci::Point(x, y, 1));

        // Apply mute if active
        if (muteParameter->getBoolValue()) {
            x = 0.0;
            y = 0.0;
        }

        if (totalNumOutputChannels >= 2) {
            channelData[0][sample] = x;
            channelData[1][sample] = y;
        } else if (totalNumOutputChannels == 1) {
            channelData[0][sample] = x;
        }

        if (isPlaying) {
            playTimeSeconds += sTimeSec;
            playTimeBeats += sTimeBeats;
        }
    }

    // used for any callback that must guarantee all audio is recieved (e.g. when recording to a file)
    juce::SpinLock::ScopedLockType lock(audioThreadCallbackLock);
    if (audioThreadCallback != nullptr) {
        audioThreadCallback(buffer);
    }
}

juce::AudioProcessorEditor* OscirenderAudioProcessor::createEditor() {
    auto editor = new OscirenderAudioProcessorEditor(*this);
    return editor;
}

//==============================================================================
void OscirenderAudioProcessor::getStateInformation(juce::MemoryBlock& destData) {
    // we need to stop recording the visualiser when saving the state, otherwise
    // there are issues. This is the only place we can do this because there is
    // no callback when closing the standalone app except for this.

    if (haltRecording != nullptr && juce::JUCEApplicationBase::isStandaloneApp()) {
        haltRecording();
    }

    juce::SpinLock::ScopedLockType lock1(parsersLock);
    juce::SpinLock::ScopedLockType lock2(effectsLock);

    std::unique_ptr<juce::XmlElement> xml = std::make_unique<juce::XmlElement>("project");
    xml->setAttribute("version", ProjectInfo::versionString);
    auto effectsXml = xml->createNewChildElement("effects");
    for (auto effect : effects) {
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
        auto base64 = fileBlocks[i]->toBase64Encoding();
        fileXml->addTextElement(base64);
    }
    xml->setAttribute("currentFile", currentFile);

    recordingParameters.save(xml.get());

    saveProperties(*xml);

    copyXmlToBinary(*xml, destData);
}

void OscirenderAudioProcessor::setStateInformation(const void* data, int sizeInBytes) {
    if (juce::JUCEApplicationBase::isStandaloneApp() && programCrashedAndUserWantsToReset()) {
        return;
    }

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
            // this is an old version of osci-render, ignore it
            return;
        }
        auto version = xml->hasAttribute("version") ? xml->getStringAttribute("version") : "2.0.0";

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
            font = juce::Font(family, FONT_SIZE, (bold ? juce::Font::bold : 0) | (italic ? juce::Font::italic : 0));
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
                auto text = fileXml->getAllSubText();
                std::shared_ptr<juce::MemoryBlock> fileBlock;

                if (lessThanVersion(version, "2.2.0")) {
                    // Older versions of osci-render opened files in a silly way
                    auto stream = juce::MemoryOutputStream();
                    juce::Base64::convertFromBase64(stream, text);
                    fileBlock = std::make_shared<juce::MemoryBlock>(stream.getData(), stream.getDataSize());
                } else {
                    fileBlock = std::make_shared<juce::MemoryBlock>();
                    fileBlock->fromBase64Encoding(text);
                }

                addFile(fileName, fileBlock);
            }
        }
        changeCurrentFile(xml->getIntAttribute("currentFile", -1));

        recordingParameters.load(xml.get());

        loadProperties(*xml);
        objectServer.reload();

        broadcaster.sendChangeMessage();
        prevMidiEnabled = !midiEnabled->getBoolValue();
    }
}

void OscirenderAudioProcessor::parameterValueChanged(int parameterIndex, float newValue) {
    for (auto effect : luaEffects) {
        if (parameterIndex == effect->parameters[0]->getParameterIndex()) {
            effect->apply();
        }
    }
    if (parameterIndex == voices->getParameterIndex()) {
        int numVoices = voices->getValueUnnormalised();
        // if the number of voices has changed, update the synth without clearing all the voices
        if (numVoices != synth.getNumVoices()) {
            if (numVoices > synth.getNumVoices()) {
                for (int i = synth.getNumVoices(); i < numVoices; i++) {
                    synth.addVoice(new ShapeVoice(*this, inputBuffer));
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

void updateIfApproxEqual(osci::FloatParameter* parameter, float newValue) {
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

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new OscirenderAudioProcessor();
}

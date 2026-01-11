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
#include "audio/PolygonizerEffect.h"
#include "audio/SpiralBitCrushEffect.h"
#include "audio/DistortEffect.h"
#include "audio/UnfoldEffect.h"
#include "audio/MultiplexEffect.h"
#include "audio/SmoothEffect.h"
#include "audio/WobbleEffect.h"
#include "audio/DuplicatorEffect.h"
#include "audio/DashedLineEffect.h"
#include "audio/VectorCancellingEffect.h"
#include "audio/ScaleEffect.h"
#include "audio/RotateEffect.h"
#include "audio/TranslateEffect.h"
#include "audio/RippleEffect.h"
#include "audio/SwirlEffect.h"
#include "audio/BounceEffect.h"
#include "audio/SkewEffect.h"
#include "audio/KaleidoscopeEffect.h"
#include "audio/VortexEffect.h"
#include "audio/GodRayEffect.h"
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
    toggleableEffects.push_back(DashedLineEffect().build());
    toggleableEffects.push_back(TraceEffect().build());
    toggleableEffects.push_back(WobbleEffect().build());
    toggleableEffects.push_back(DuplicatorEffect().build());

    std::vector<std::shared_ptr<osci::Effect>> premiumEffects;

    premiumEffects.push_back(MultiplexEffect().build());
    premiumEffects.push_back(UnfoldEffect().build());
    premiumEffects.push_back(BounceEffect().build());
    premiumEffects.push_back(TwistEffect().build());
    premiumEffects.push_back(SkewEffect().build());
    premiumEffects.push_back(PolygonizerEffect().build());
    premiumEffects.push_back(KaleidoscopeEffect().build());
    premiumEffects.push_back(VortexEffect().build());
    premiumEffects.push_back(GodRayEffect().build());
    premiumEffects.push_back(SpiralBitCrushEffect().build());

    for (auto& premiumEffect : premiumEffects) {
        premiumEffect->setPremiumOnly(true);
        toggleableEffects.push_back(premiumEffect);
    }

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
    intParameters.push_back(fileSelect);

    voices->addListener(this);

    for (int i = 0; i < luaEffects.size(); i++) {
        luaEffects[i]->parameters[0]->addListener(this);
    }

    synth.addSound(defaultSound);

    activeShapeSound.store(defaultSound.get(), std::memory_order_release);

    fileSelectionNotifier = std::make_unique<FileSelectionAsyncNotifier>(*this);

    addAllParameters();
}

OscirenderAudioProcessor::~OscirenderAudioProcessor() {
    for (int i = luaEffects.size() - 1; i >= 0; i--) {
        luaEffects[i]->parameters[0]->removeListener(this);
    }
    voices->removeListener(this);
}

// parsersLock AND effectsLock must be held when calling this
void OscirenderAudioProcessor::applyFileSelectLocked() {
    const int previousFileIndex = currentFile.load();
    auto* previousSound = activeShapeSound.load(std::memory_order_acquire);

    ShapeSound* selectedSound = nullptr;

    if (objectServerRendering.load()) {
        selectedSound = objectServerSound.get();
        activeShapeSound.store(selectedSound, std::memory_order_release);
        currentFile.store(-1);
    } else {
        const int fileCount = (int)fileBlocks.size();

        // 1-based mapping: 1 -> first file (index 0), 2 -> second file (index 1), ...
        const int requestedParamValue = juce::jlimit(1, 100, (int)fileSelect->getValueUnnormalised());
        const int requestedIndex = requestedParamValue - 1;

        int targetFileIndex = -1;
        if (fileCount <= 0) {
            targetFileIndex = -1;
        } else {
            const int maxIndex = fileCount - 1;
            targetFileIndex = juce::jlimit(0, maxIndex, requestedIndex);
        }

        currentFile.store(targetFileIndex);
        selectedSound = targetFileIndex >= 0 ? sounds[(size_t)targetFileIndex].get() : defaultSound.get();
        activeShapeSound.store(selectedSound, std::memory_order_release);
    }

    assert(selectedSound != nullptr);

    const int newFileIndex = currentFile.load();
    const bool selectionChanged = (newFileIndex != previousFileIndex) || (selectedSound != previousSound);
    if (!selectionChanged || selectedSound == nullptr) {
        return;
    }

    for (int i = 0; i < synth.getNumVoices(); i++) {
        auto voice = dynamic_cast<ShapeVoice*>(synth.getVoice(i));
        if (voice != nullptr) {
            voice->updateSound(selectedSound);
        }
    }

    // Safe to call from the audio thread; AsyncUpdater will deliver on message thread.
    if (fileSelectionNotifier != nullptr) {
        fileSelectionNotifier->triggerAsyncUpdate();
    }
}

void OscirenderAudioProcessor::setAudioThreadCallback(std::function<void(const juce::AudioBuffer<float>&)> callback) {
    juce::SpinLock::ScopedLockType lock(audioThreadCallbackLock);
    audioThreadCallback = callback;
}

void OscirenderAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    CommonAudioProcessor::prepareToPlay(sampleRate, samplesPerBlock);

    currentVolume = 0.0;
    synth.setCurrentPlaybackSampleRate(sampleRate);
    retriggerMidi = true;
    
    // Update sample rate for all effects so they have correct timing
    {
        juce::SpinLock::ScopedLockType lock(effectsLock);
        
        // Update sample rate for all voice effects
        for (int i = 0; i < synth.getNumVoices(); i++) {
            auto voice = dynamic_cast<ShapeVoice*>(synth.getVoice(i));
            if (voice) {
                voice->prepareToPlay(sampleRate, samplesPerBlock);
            }
        }
    }
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

    luaEffects.push_back(std::make_shared<osci::SimpleEffect>(
        [this, sliderIndex](int index, osci::Point input, const std::vector<std::atomic<float>>& values, float sampleRate, float frequency) {
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

    // Keep fileSelect parameter in sync with UI-driven file selection.
    const int value = juce::jlimit(1, 100, index + 1);
    fileSelect->setUnnormalisedValueNotifyingHost((float)value);
}

void OscirenderAudioProcessor::changeSound(ShapeSound::Ptr sound) {
    if (objectServerRendering && sound != objectServerSound) {
        return;
    }

    activeShapeSound.store(sound.get(), std::memory_order_release);
    for (int i = 0; i < synth.getNumVoices(); i++) {
        auto voice = dynamic_cast<ShapeVoice*>(synth.getVoice(i));
        if (voice != nullptr) {
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
        juce::SpinLock::ScopedLockType lock2(effectsLock);

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
        if (eff->getId() == effectId) {
            previewEffect = eff;
            break;
        }
    }
    // Only set preview effect on active voices to reduce overhead
    // Inactive voices will get it lazily when they become active
    auto simplePreview = std::dynamic_pointer_cast<osci::SimpleEffect>(previewEffect);
    for (int i = 0; i < synth.getNumVoices(); i++) {
        auto voice = dynamic_cast<ShapeVoice*>(synth.getVoice(i));
        if (voice && voice->isVoiceActive()) {
            voice->setPreviewEffect(simplePreview);
        }
    }
}

void OscirenderAudioProcessor::clearPreviewEffect() {
    previewEffect.reset();
    // Clear preview effect from active voices
    for (int i = 0; i < synth.getNumVoices(); i++) {
        auto voice = dynamic_cast<ShapeVoice*>(synth.getVoice(i));
        if (voice && voice->isVoiceActive()) {
            voice->clearPreviewEffect();
        }
    }
}

// effectsLock should be held when calling this
void OscirenderAudioProcessor::applyToggleableEffectsToBuffer(
    juce::AudioBuffer<float>& buffer,
    juce::AudioBuffer<float>* externalInput,
    juce::AudioBuffer<float>* volumeBuffer,
    juce::AudioBuffer<float>* frequencyBuffer,
    juce::AudioBuffer<float>* frameSyncBuffer,
    const std::unordered_map<juce::String, std::shared_ptr<osci::SimpleEffect>>* perVoiceEffects,
    const std::shared_ptr<osci::Effect>& previewEffectInstance) {
    juce::MidiBuffer emptyMidi;

    for (auto& globalEffect : toggleableEffects) {
        std::shared_ptr<osci::Effect> effectInstance = globalEffect;
        if (perVoiceEffects != nullptr) {
            auto it = perVoiceEffects->find(globalEffect->getId());
            if (it == perVoiceEffects->end()) {
                continue;
            }
            effectInstance = it->second;
        }

#if !OSCI_PREMIUM
        if (effectInstance->isPremiumOnly()) {
            continue;
        }
#endif

        const bool isEnabled = effectInstance->enabled != nullptr && effectInstance->enabled->getValue();
        const bool isSelected = effectInstance->selected == nullptr ? true : effectInstance->selected->getBoolValue();
        if (!(isEnabled && isSelected)) {
            continue;
        }

        juce::AudioBuffer<float>* extInput = nullptr;
        if (externalInput != nullptr && globalEffect->getId() == custom->getId()) {
            extInput = externalInput;
        }
        effectInstance->processBlockWithInputs(buffer, emptyMidi, extInput, volumeBuffer, frequencyBuffer, frameSyncBuffer);
    }

    if (previewEffectInstance != nullptr) {
        const bool prevEnabled = (previewEffectInstance->enabled != nullptr) && previewEffectInstance->enabled->getValue();
        const bool prevSelected = (previewEffectInstance->selected == nullptr) ? true : previewEffectInstance->selected->getBoolValue();
        
        // We only apply the preview effect if it wasn't already applied as a regular effect
        if (!(prevEnabled && prevSelected)) {
            juce::AudioBuffer<float>* extInput = nullptr;
            if (externalInput != nullptr && previewEffectInstance->getId() == custom->getId()) {
                extInput = externalInput;
            }
            previewEffectInstance->processBlockWithInputs(buffer, emptyMidi, extInput, volumeBuffer, frequencyBuffer, frameSyncBuffer);
        }
    }
}

void OscirenderAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) {
    juce::ScopedNoDenormals noDenormals;
    // Audio info variables
    int totalNumInputChannels = getTotalNumInputChannels();
    int totalNumOutputChannels = getTotalNumOutputChannels();
    double sampleRate = getSampleRate();
    int numSamples = buffer.getNumSamples();

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

    // Calculate per-sample volume buffer BEFORE synth rendering (needed for effect pre-animation)
    currentVolumeBuffer.setSize(1, numSamples, false, false, false);
    auto* volumeData = currentVolumeBuffer.getWritePointer(0);
    
    // Calculate instantaneous squared magnitude using vectorized operations
    if (totalNumInputChannels >= 2) {
        const float* leftData = inputBuffer.getReadPointer(0);
        const float* rightData = inputBuffer.getReadPointer(1);
        
        // L² + R² using SIMD operations
        juce::FloatVectorOperations::multiply(volumeData, leftData, leftData, numSamples);
        juce::FloatVectorOperations::addWithMultiply(volumeData, rightData, rightData, numSamples);
        // Divide by 2: (L² + R²) / 2
        juce::FloatVectorOperations::multiply(volumeData, 0.5f, numSamples);
    } else if (totalNumInputChannels == 1) {
        const float* monoData = inputBuffer.getReadPointer(0);
        // mono²
        juce::FloatVectorOperations::multiply(volumeData, monoData, monoData, numSamples);
    } else {
        juce::FloatVectorOperations::clear(volumeData, numSamples);
    }
    
    // Apply exponential moving average for smoothing
    const float smoothingCoeff = 1.0f - std::exp(-1.0f / (float)(VOLUME_BUFFER_SECONDS * sampleRate));
    
    for (int i = 0; i < numSamples; ++i) {
        // EMA: smoothed = smoothed + coeff * (new - smoothed)
        currentVolume += smoothingCoeff * (volumeData[i] - currentVolume);
        // Take square root and clamp to get final volume
        volumeData[i] = juce::jlimit(0.0f, 1.0f, std::sqrt((float)currentVolume));
    }

    // Pre-animate all effects for this block BEFORE rendering
    // This ensures LFO phases advance exactly once per sample regardless of voice count or processing location
    // Only animate effects that are enabled or being previewed (animation is expensive!)
    {
        juce::SpinLock::ScopedLockType lock(effectsLock);
        for (auto& effect : toggleableEffects) {
            const bool isEnabled = effect->enabled != nullptr && effect->enabled->getBoolValue();
            const bool isPreviewed = (effect == previewEffect);
            if (isEnabled || isPreviewed) {
                effect->animateValues(numSamples, &currentVolumeBuffer);
            }
        }
        for (auto& effect : permanentEffects) {
            effect->animateValues(numSamples, &currentVolumeBuffer);
        }
        for (auto& effect : luaEffects) {
            effect->animateValues(numSamples, &currentVolumeBuffer);
        }
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

		// Apply toggleable effects directly in input mode (no synth voices involved)
		{
			juce::SpinLock::ScopedLockType lock(effectsLock);

            inputFrequencyBuffer.setSize(1, numSamples, false, false, true);
            juce::FloatVectorOperations::fill(inputFrequencyBuffer.getWritePointer(0), (float)frequency.load(), numSamples);

                        applyToggleableEffectsToBuffer(outputBuffer3d, &inputBuffer, &currentVolumeBuffer, &inputFrequencyBuffer, nullptr, nullptr, previewEffect);
		}
    } else {
        juce::SpinLock::ScopedLockType lock1(parsersLock);
        juce::SpinLock::ScopedLockType lock2(effectsLock);

        // Apply file selection on the audio thread (among already-loaded files)
        applyFileSelectLocked();

        synth.renderNextBlock(outputBuffer3d, midiMessages, 0, buffer.getNumSamples());
    }

    midiMessages.clear();

    auto* channelData = buffer.getArrayOfWritePointers();

    // Handle animation frame updates
    if (animateFrames->getBoolValue()) {
        double frameIncrement;
        if (juce::JUCEApplicationBase::isStandaloneApp()) {
            frameIncrement = sTimeSec * animationRate->getValueUnnormalised() * numSamples;
        } else if (animationSyncBPM->getValue()) {
            animationFrame = playTimeBeats * animationRate->getValueUnnormalised() + animationOffset->getValueUnnormalised();
            frameIncrement = 0.0; // Already calculated absolute position
        } else {
            animationFrame = playTimeSeconds * animationRate->getValueUnnormalised() + animationOffset->getValueUnnormalised();
            frameIncrement = 0.0; // Already calculated absolute position
        }

        if (juce::JUCEApplicationBase::isStandaloneApp()) {
            animationFrame = animationFrame + frameIncrement;
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


    {
        juce::SpinLock::ScopedLockType lock1(parsersLock);
        juce::SpinLock::ScopedLockType lock2(effectsLock);

        // If we're in audio-input mode, the synth path above didn't run, but we still
        // want file selection to affect Lua/custom processing on the audio thread.
        if (usingInput) {
            applyFileSelectLocked();
        }

        // Note: toggleableEffects/previewEffect are applied via shared helper:
        // - per-voice when using the synth path
        // - directly to the buffer when using input mode
        // Only permanentEffects and luaEffects are always global

        for (auto& effect : permanentEffects) {
            effect->processBlockWithInputs(outputBuffer3d, midiMessages, nullptr, &currentVolumeBuffer, nullptr);
        }
        auto lua = currentFile >= 0 ? sounds[currentFile]->parser->getLua() : nullptr;
        if (lua != nullptr || custom->enabled->getBoolValue()) {
            for (auto& effect : luaEffects) {
                effect->processBlockWithInputs(outputBuffer3d, midiMessages, nullptr, &currentVolumeBuffer, nullptr);
            }
        }
    }

    // Process in batches using buffer-wide operations
    auto* outputArray = outputBuffer3d.getArrayOfWritePointers();
    
    // Scale by volume
    juce::FloatVectorOperations::multiply(outputArray[0], outputArray[0], (float)volume.load(), numSamples);
    juce::FloatVectorOperations::multiply(outputArray[1], outputArray[1], (float)volume.load(), numSamples);
    
    // Hard clip to threshold
    float thresholdVal = (float)threshold.load();
    juce::FloatVectorOperations::clip(outputArray[0], outputArray[0], -thresholdVal, thresholdVal, numSamples);
    juce::FloatVectorOperations::clip(outputArray[1], outputArray[1], -thresholdVal, thresholdVal, numSamples);
    
    // Write to thread manager (for visualizers, etc.)
    threadManager.write(outputBuffer3d);
    
    // Apply mute if active
    if (muteParameter->getBoolValue()) {
        juce::FloatVectorOperations::clear(outputArray[0], numSamples);
        juce::FloatVectorOperations::clear(outputArray[1], numSamples);
    }
    
    // Copy to output channels
    if (totalNumOutputChannels >= 2) {
        juce::FloatVectorOperations::copy(channelData[0], outputArray[0], numSamples);
        juce::FloatVectorOperations::copy(channelData[1], outputArray[1], numSamples);
    } else if (totalNumOutputChannels == 1) {
        juce::FloatVectorOperations::copy(channelData[0], outputArray[0], numSamples);
    }
    
    // Update playback time
    if (isPlaying) {
        playTimeSeconds += sTimeSec * numSamples;
        playTimeBeats += sTimeBeats * numSamples;
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

    saveStandaloneProjectFilePathToXml(*xml);
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
    customFunction->addTextElement(juce::Base64::toBase64(luaEffectState->getCode()));

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
        restoreStandaloneProjectFilePathFromXml(*xml);

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
            luaEffectState->updateCode(stream.toString());
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
    // TODO: Figure out why below code was needed and if it is still needed
    // for (auto effect : luaEffects) {
    //     if (parameterIndex == effect->parameters[0]->getParameterIndex()) {
    //         effect->apply();
    //     }
    // }
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

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

    // Resolve beginner/advanced mode from persisted global settings.
    // Free builds are always beginner; premium defaults to advanced.
#if OSCI_PREMIUM
    beginnerMode = getGlobalBoolValue("beginnerMode", false);
#else
    beginnerMode = true;
#endif

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
    osciPermanentEffects.push_back(fractalIterationsEffect);

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

    // --- Advanced mode: strip per-parameter LFO dropdowns and sidechain from all effects ---
    // These are only used in beginner mode. In advanced mode, modulation is done via
    // the global LFO/ENV module panels with drag-and-drop assignments.
    if (!beginnerMode) {
        for (auto& effect : effects) {
            for (auto* param : effect->parameters) {
                param->disableLfo();
                param->disableSidechain();
            }
        }
    }

    booleanParameters.push_back(midiEnabled);
    booleanParameters.push_back(inputEnabled);
    booleanParameters.push_back(animateFrames);
    booleanParameters.push_back(loopAnimation);
    booleanParameters.push_back(animationSyncBPM);
    booleanParameters.push_back(invertImage);

    floatParameters.push_back(delayTime);
    floatParameters.push_back(attackTime);
    floatParameters.push_back(holdTime);
    floatParameters.push_back(attackShape);
    floatParameters.push_back(decayTime);
    floatParameters.push_back(decayShape);
    floatParameters.push_back(sustainLevel);
    floatParameters.push_back(releaseTime);
    floatParameters.push_back(releaseShape);
    floatParameters.push_back(animationRate);
    floatParameters.push_back(animationOffset);
    floatParameters.push_back(standaloneBpm);

    // Initialize global LFO rate parameters and default waveforms
    // Only in advanced mode — beginner mode uses per-parameter LFO dropdowns instead.
    if (!beginnerMode) {
        for (int i = 0; i < NUM_LFOS; ++i) {
            lfoRate[i] = new osci::FloatParameter(
                "LFO " + juce::String(i + 1) + " Rate",
                "lfo" + juce::String(i + 1) + "Rate",
                VERSION_HINT, 1.0f, 0.01f, 100.0f, 0.01f, "Hz");
            floatParameters.push_back(lfoRate[i]);
            lfoWaveforms[i] = createLfoPreset(LfoPreset::Triangle);
        }
    }

    // Initialize envelope parameter sets
    // Envelope 0 reuses the legacy DAHDSR params
    envParams[0] = { delayTime, attackTime, holdTime, decayTime, sustainLevel, releaseTime, attackShape, decayShape, releaseShape };
    // Envelopes 1–4 get fresh indexed parameters (advanced mode only)
    if (!beginnerMode) {
        for (int i = 1; i < NUM_ENVELOPES; ++i) {
            juce::String idx = juce::String(i + 1);
            auto* dt = new osci::FloatParameter("Env " + idx + " Delay",   "env" + idx + "Delay",   VERSION_HINT, 0.0f,   osci_audio::kDahdsrTimeMinSeconds, osci_audio::kDahdsrTimeMaxSeconds, osci_audio::kDahdsrTimeStepSeconds);
            auto* at = new osci::FloatParameter("Env " + idx + " Attack",  "env" + idx + "Attack",  VERSION_HINT, 0.005f, osci_audio::kDahdsrTimeMinSeconds, osci_audio::kDahdsrTimeMaxSeconds, osci_audio::kDahdsrTimeStepSeconds);
            auto* ht = new osci::FloatParameter("Env " + idx + " Hold",    "env" + idx + "Hold",    VERSION_HINT, 0.0f,   osci_audio::kDahdsrTimeMinSeconds, osci_audio::kDahdsrTimeMaxSeconds, osci_audio::kDahdsrTimeStepSeconds);
            auto* dc = new osci::FloatParameter("Env " + idx + " Decay",   "env" + idx + "Decay",   VERSION_HINT, 0.095f, osci_audio::kDahdsrTimeMinSeconds, osci_audio::kDahdsrTimeMaxSeconds, osci_audio::kDahdsrTimeStepSeconds);
            auto* sl = new osci::FloatParameter("Env " + idx + " Sustain", "env" + idx + "Sustain", VERSION_HINT, 0.6f, 0.0f, 1.0f, 0.00001f);
            auto* rt = new osci::FloatParameter("Env " + idx + " Release", "env" + idx + "Release", VERSION_HINT, 0.4f,   osci_audio::kDahdsrTimeMinSeconds, osci_audio::kDahdsrTimeMaxSeconds, osci_audio::kDahdsrTimeStepSeconds);
            auto* as = new osci::FloatParameter("Env " + idx + " Atk Shape", "env" + idx + "AtkShape", VERSION_HINT, 5.0f, -50.0f, 50.0f, 0.00001f);
            auto* ds = new osci::FloatParameter("Env " + idx + " Dec Shape", "env" + idx + "DecShape", VERSION_HINT, -20.0f, -50.0f, 50.0f, 0.00001f);
            auto* rs = new osci::FloatParameter("Env " + idx + " Rel Shape", "env" + idx + "RelShape", VERSION_HINT, -5.0f, -50.0f, 50.0f, 0.00001f);
            envParams[i] = { dt, at, ht, dc, sl, rt, as, ds, rs };
            floatParameters.push_back(dt);
            floatParameters.push_back(at);
            floatParameters.push_back(ht);
            floatParameters.push_back(dc);
            floatParameters.push_back(sl);
            floatParameters.push_back(rt);
            floatParameters.push_back(as);
            floatParameters.push_back(ds);
            floatParameters.push_back(rs);
        }
    }

    // Initialize global Random rate parameters (advanced mode only)
    if (!beginnerMode) {
        for (int i = 0; i < NUM_RANDOM_SOURCES; ++i) {
            randomRate[i] = new osci::FloatParameter(
                "Random " + juce::String(i + 1) + " Rate",
                "random" + juce::String(i + 1) + "Rate",
                VERSION_HINT, 1.0f, 0.01f, 1000.0f, 0.01f, "Hz");
            floatParameters.push_back(randomRate[i]);
            randomAudioStates[i].seed(0x12345678u + (uint32_t)i * 0x9E3779B9u);
        }
    }

    for (int i = 0; i < voices->getValueUnnormalised(); i++) {
        uiVoiceActive[i].store(false, std::memory_order_relaxed);
        uiVoiceEnvelopeTimeSeconds[i].store(0.0, std::memory_order_relaxed);
        synth.addVoice(new ShapeVoice(*this, inputBuffer, i));
    }

    intParameters.push_back(voices);
    intParameters.push_back(fileSelect);

    voices->addListener(this);
    attackTime->addListener(this);
    attackShape->addListener(this);
    delayTime->addListener(this);
    holdTime->addListener(this);
    decayTime->addListener(this);
    decayShape->addListener(this);
    sustainLevel->addListener(this);
    releaseTime->addListener(this);
    releaseShape->addListener(this);

    for (int i = 0; i < luaEffects.size(); i++) {
        luaEffects[i]->parameters[0]->addListener(this);
    }

    synth.addSound(defaultSound);

    activeShapeSound.store(defaultSound.get(), std::memory_order_release);

    fileSelectionNotifier = std::make_unique<FileSelectionAsyncNotifier>(*this);

    addAllParameters();

    buildParamLocationMap();

    // Wire up renderer-side modulation for visualiser effects.
    // The renderer calls this after animateValues() to apply LFO/ENV modulation.
    visualiserParameters.applyExternalModulation = [this](int numSamples) {
        auto applyAssignments = [&](const auto& assignments, auto getValueFn, int maxIndex) {
            for (const auto& assignment : assignments) {
                if (assignment.sourceIndex < 0 || assignment.sourceIndex >= maxIndex) continue;

                auto findAndApply = [&](std::vector<std::shared_ptr<osci::Effect>>& effectList) {
                    for (auto& effect : effectList) {
                        for (int p = 0; p < (int)effect->parameters.size(); ++p) {
                            if (effect->parameters[p]->paramID != assignment.paramId) continue;

                            float* buf = effect->getAnimatedValuesWritePointer(p, numSamples);
                            if (buf == nullptr) continue;

                            float val = getValueFn(assignment.sourceIndex);
                            float paramMin = effect->parameters[p]->min;
                            float paramMax = effect->parameters[p]->max;
                            float range = paramMax - paramMin;
                            float depth = assignment.depth;

                            if (assignment.bipolar) {
                                float offset = (val * 2.0f - 1.0f) * depth * range * 0.5f;
                                for (int s = 0; s < numSamples; ++s)
                                    buf[s] = juce::jlimit(paramMin, paramMax, buf[s] + offset);
                            } else {
                                float offset = val * depth * range;
                                for (int s = 0; s < numSamples; ++s)
                                    buf[s] = juce::jlimit(paramMin, paramMax, buf[s] + offset);
                            }
                        }
                    }
                };

                findAndApply(visualiserParameters.effects);
                findAndApply(visualiserParameters.audioEffects);
            }
        };

        // Apply LFO modulation
        {
            juce::SpinLock::ScopedLockType assnLock(lfoAssignmentLock);
            applyAssignments(lfoAssignments,
                [this](int i) { return lfoCurrentValues[i].load(std::memory_order_relaxed); },
                NUM_LFOS);
        }
        // Apply ENV modulation
        {
            juce::SpinLock::ScopedLockType assnLock(envAssignmentLock);
            applyAssignments(envAssignments,
                [this](int i) { return envCurrentValues[i].load(std::memory_order_relaxed); },
                NUM_ENVELOPES);
        }
    };
}

OscirenderAudioProcessor::~OscirenderAudioProcessor() {
    for (int i = luaEffects.size() - 1; i >= 0; i--) {
        luaEffects[i]->parameters[0]->removeListener(this);
    }
    releaseShape->removeListener(this);
    releaseTime->removeListener(this);
    sustainLevel->removeListener(this);
    decayShape->removeListener(this);
    decayTime->removeListener(this);
    holdTime->removeListener(this);
    delayTime->removeListener(this);
    attackShape->removeListener(this);
    attackTime->removeListener(this);
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

    if (isOfflineRenderActive()) {
        midiMessages.clear();
        buffer.clear();
        return;
    }

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

    // In standalone mode, use the standaloneBpm parameter as the tempo source
    if (juce::JUCEApplicationBase::isStandaloneApp()) {
        bpm = (double)standaloneBpm->getValueUnnormalised();
    }

    // Publish BPM for UI components (LFO rate display, etc.)
    currentBpm.store(bpm, std::memory_order_relaxed);

    // Calculated number of beats
    // TODO: To make this more resilient to changing BPMs, we should change how this is calculated
    // or use another property of the AudioPlayHead::PositionInfo
    double playTimeBeats = bpm * playTimeSeconds / 60;

    // Calculated time per sample in seconds and beats
    double sTimeSec = 1.f / sampleRate;
    double sTimeBeats = bpm * sTimeSec / 60;

    // Store DAW transport for Lua access from voices
    luaBpm.store(bpm, std::memory_order_relaxed);
    luaPlayTime.store(playTimeSeconds, std::memory_order_relaxed);
    luaPlayTimeBeats.store(playTimeBeats, std::memory_order_relaxed);
    luaIsPlaying.store(isPlaying, std::memory_order_relaxed);
    luaTimeSigNum.store(timeSig.numerator, std::memory_order_relaxed);
    luaTimeSigDen.store(timeSig.denominator, std::memory_order_relaxed);

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

        if (!beginnerMode) {
            // Apply global LFO modulation on top of animated values
            applyGlobalLfoModulation(numSamples, sampleRate, midiMessages);

            // Apply global envelope modulation on top of LFO modulation
            applyGlobalEnvModulation(numSamples, sampleRate);

            // Apply global random modulation
            applyGlobalRandomModulation(numSamples, sampleRate, midiMessages);
        }
    }

    juce::AudioBuffer<float> outputBuffer3d = juce::AudioBuffer<float>(6, buffer.getNumSamples());
    outputBuffer3d.clear();

    // Initialise colour channels to the "no colour" sentinel (-1) so that
    // samples untouched by any voice fall back to the default line colour
    // rather than being interpreted as explicit black (0,0,0).
    juce::FloatVectorOperations::fill(outputBuffer3d.getWritePointer(3), -1.0f, buffer.getNumSamples());
    juce::FloatVectorOperations::fill(outputBuffer3d.getWritePointer(4), -1.0f, buffer.getNumSamples());
    juce::FloatVectorOperations::fill(outputBuffer3d.getWritePointer(5), -1.0f, buffer.getNumSamples());

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
    juce::Logger::writeToLog("getStateInformation: saving state (version " + juce::String(ProjectInfo::versionString) + ")");

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
    xml->setAttribute("beginnerMode", beginnerMode);

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

    // Save global LFO waveforms & assignments (advanced mode only)
    if (!beginnerMode) {
        auto lfosXml = xml->createNewChildElement("lfos");
        lfosXml->setAttribute("activeTab", activeLfoTab);
        {
            juce::SpinLock::ScopedLockType wfLock(lfoWaveformLock);
            for (int i = 0; i < NUM_LFOS; ++i) {
                auto lfoXml = lfosXml->createNewChildElement("lfo");
                lfoXml->setAttribute("index", i);
                lfoXml->setAttribute("preset", lfoPresetToString(lfoPresets[i]));
                lfoXml->setAttribute("rateMode", lfoRateModeToString(lfoRateModes[i]));
                lfoXml->setAttribute("tempoDivision", lfoTempoDivisions[i]);
                lfoXml->setAttribute("mode", lfoModeToString(lfoModes[i]));
                lfoXml->setAttribute("phaseOffset", (double)lfoPhaseOffsets[i]);
                lfoXml->setAttribute("smoothAmount", (double)lfoSmoothAmounts[i]);
                lfoXml->setAttribute("delayAmount", (double)lfoDelayAmounts[i]);
                lfoWaveforms[i].saveToXml(lfoXml);
            }
        }

        auto lfoAssignmentsXml = xml->createNewChildElement("lfoAssignments");
        {
            juce::SpinLock::ScopedLockType lock(lfoAssignmentLock);
            for (const auto& assignment : lfoAssignments) {
                auto assignXml = lfoAssignmentsXml->createNewChildElement("assignment");
                assignment.saveToXml(assignXml, "lfo");
            }
        }

        auto envXml = xml->createNewChildElement("envelopes");
        envXml->setAttribute("activeTab", activeEnvTab);
        auto envAssignmentsXml = envXml->createNewChildElement("envAssignments");
        {
            juce::SpinLock::ScopedLockType lock(envAssignmentLock);
            for (const auto& assignment : envAssignments) {
                auto assignXml = envAssignmentsXml->createNewChildElement("assignment");
                assignment.saveToXml(assignXml, "env");
            }
        }

        // Save global Random modulators & assignments
        auto randomsXml = xml->createNewChildElement("randoms");
        randomsXml->setAttribute("activeTab", activeRandomTab);
        for (int i = 0; i < NUM_RANDOM_SOURCES; ++i) {
            auto rndXml = randomsXml->createNewChildElement("random");
            rndXml->setAttribute("index", i);
            rndXml->setAttribute("style", randomStyleToString(randomStyles[i]));
            rndXml->setAttribute("rateMode", lfoRateModeToString(randomRateModes[i]));
            rndXml->setAttribute("tempoDivision", randomTempoDivisions[i]);
        }
        auto randomAssignmentsXml = randomsXml->createNewChildElement("randomAssignments");
        {
            juce::SpinLock::ScopedLockType lock(randomAssignmentLock);
            for (const auto& assignment : randomAssignments) {
                auto assignXml = randomAssignmentsXml->createNewChildElement("assignment");
                assignment.saveToXml(assignXml, "rng");
            }
        }
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
    juce::Logger::writeToLog("getStateInformation: saved " + juce::String(effects.size()) + " effects, "
        + juce::String(fileBlocks.size()) + " files, " + juce::String((int)destData.getSize()) + " bytes");
}

void OscirenderAudioProcessor::setStateInformation(const void* data, int sizeInBytes) {
    juce::Logger::writeToLog("setStateInformation: loading state (" + juce::String(sizeInBytes) + " bytes)");

    if (juce::JUCEApplicationBase::isStandaloneApp() && programCrashedAndUserWantsToReset()) {
        juce::Logger::writeToLog("setStateInformation: user chose to reset after crash, skipping restore");
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

    if (xml.get() == nullptr || !xml->hasTagName("project")) {
        juce::Logger::writeToLog("setStateInformation: failed to parse XML from state data");
        return;
    }

    restoreStandaloneProjectFilePathFromXml(*xml);

        auto versionXml = xml->getChildByName("version");
        if (versionXml != nullptr && versionXml->getAllSubText().startsWith("v1.")) {
            juce::Logger::writeToLog("setStateInformation: ignoring v1.x state format");
            return;
        }
        auto version = xml->hasAttribute("version") ? xml->getStringAttribute("version") : "2.0.0";
        juce::Logger::writeToLog("setStateInformation: restoring state version " + version);

        juce::SpinLock::ScopedLockType lock1(parsersLock);
        juce::SpinLock::ScopedLockType lock2(effectsLock);

        loadEffectsFromXml(xml->getChildByName("effects"));
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
            juce::Logger::writeToLog("setStateInformation: restored custom Lua function (" + juce::String((int)stream.getDataSize()) + " bytes)");
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
            int fileCount = 0;
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
                fileCount++;
            }
            juce::Logger::writeToLog("setStateInformation: restored " + juce::String(fileCount) + " files");
        } else {
            juce::Logger::writeToLog("setStateInformation: no files section found");
        }
        changeCurrentFile(xml->getIntAttribute("currentFile", -1));

        // Load global LFO waveforms & assignments (only relevant in advanced mode)
        if (!beginnerMode) {
            auto lfosXml = xml->getChildByName("lfos");
            if (lfosXml != nullptr) {
                activeLfoTab = lfosXml->getIntAttribute("activeTab", 0);
                juce::SpinLock::ScopedLockType wfLock(lfoWaveformLock);
                for (auto* lfoXml : lfosXml->getChildWithTagNameIterator("lfo")) {
                    int idx = lfoXml->getIntAttribute("index", -1);
                    if (idx >= 0 && idx < NUM_LFOS) {
                        lfoWaveforms[idx].loadFromXml(lfoXml);
                        lfoPresets[idx] = stringToLfoPreset(lfoXml->getStringAttribute("preset", "Custom"));
                        lfoRateModes[idx] = stringToLfoRateMode(lfoXml->getStringAttribute("rateMode", "Seconds"));
                        lfoTempoDivisions[idx] = lfoXml->getIntAttribute("tempoDivision", 8);
                        lfoModes[idx] = stringToLfoMode(lfoXml->getStringAttribute("mode", "Free"));
                        lfoPhaseOffsets[idx] = (float)lfoXml->getDoubleAttribute("phaseOffset", 0.0);
                        lfoSmoothAmounts[idx] = (float)lfoXml->getDoubleAttribute("smoothAmount", 0.005);
                        lfoDelayAmounts[idx] = (float)lfoXml->getDoubleAttribute("delayAmount", 0.0);
                    }
                }
            }
        }

        // Reset LFO audio-thread state so stale note counts don't carry over
        lfoActiveNoteCount = 0;
        lfoPrevAnyVoiceActive = false;
        for (int i = 0; i < NUM_LFOS; ++i)
            lfoAudioStates[i].reset();

        // Load LFO assignments (advanced mode only)
        if (!beginnerMode) {
            auto lfoAssignmentsXml = xml->getChildByName("lfoAssignments");
            if (lfoAssignmentsXml != nullptr) {
                juce::SpinLock::ScopedLockType assnLock(lfoAssignmentLock);
                lfoAssignments.clear();
                for (auto* assignXml : lfoAssignmentsXml->getChildWithTagNameIterator("assignment")) {
                    lfoAssignments.push_back(LfoAssignment::loadFromXml(assignXml, "lfo"));
                }
            } else {
                juce::SpinLock::ScopedLockType assnLock(lfoAssignmentLock);
                lfoAssignments.clear();
            }
        }

        // Load envelope assignments (advanced mode only)
        if (!beginnerMode) {
            auto envelopesXml = xml->getChildByName("envelopes");
            if (envelopesXml != nullptr) {
                activeEnvTab = envelopesXml->getIntAttribute("activeTab", 0);
                auto envAssignmentsXml = envelopesXml->getChildByName("envAssignments");
                if (envAssignmentsXml != nullptr) {
                    juce::SpinLock::ScopedLockType assnLock(envAssignmentLock);
                    envAssignments.clear();
                    for (auto* assignXml : envAssignmentsXml->getChildWithTagNameIterator("assignment")) {
                        envAssignments.push_back(EnvAssignment::loadFromXml(assignXml, "env"));
                    }
                } else {
                    juce::SpinLock::ScopedLockType assnLock(envAssignmentLock);
                    envAssignments.clear();
                }
            } else {
                activeEnvTab = 0;
                juce::SpinLock::ScopedLockType assnLock(envAssignmentLock);
                envAssignments.clear();
            }
        }

        // Load random modulator state (advanced mode only)
        if (!beginnerMode) {
            auto randomsXml = xml->getChildByName("randoms");
            if (randomsXml != nullptr) {
                activeRandomTab = randomsXml->getIntAttribute("activeTab", 0);
                for (auto* rndXml : randomsXml->getChildWithTagNameIterator("random")) {
                    int idx = rndXml->getIntAttribute("index", -1);
                    if (idx >= 0 && idx < NUM_RANDOM_SOURCES) {
                        randomStyles[idx] = stringToRandomStyle(rndXml->getStringAttribute("style", "Perlin"));
                        randomRateModes[idx] = stringToLfoRateMode(rndXml->getStringAttribute("rateMode", "Seconds"));
                        randomTempoDivisions[idx] = rndXml->getIntAttribute("tempoDivision", 8);
                    }
                }
                auto randomAssignmentsXml = randomsXml->getChildByName("randomAssignments");
                if (randomAssignmentsXml != nullptr) {
                    juce::SpinLock::ScopedLockType assnLock(randomAssignmentLock);
                    randomAssignments.clear();
                    for (auto* assignXml : randomAssignmentsXml->getChildWithTagNameIterator("assignment")) {
                        randomAssignments.push_back(RandomAssignment::loadFromXml(assignXml, "rng"));
                    }
                } else {
                    juce::SpinLock::ScopedLockType assnLock(randomAssignmentLock);
                    randomAssignments.clear();
                }
            } else {
                activeRandomTab = 0;
                juce::SpinLock::ScopedLockType assnLock(randomAssignmentLock);
                randomAssignments.clear();
            }
        }

        recordingParameters.load(xml.get());

        loadProperties(*xml);
        objectServer.reload();

        broadcaster.sendChangeMessage();
        prevMidiEnabled = !midiEnabled->getBoolValue();
        juce::Logger::writeToLog("setStateInformation: state restore complete");
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
                    uiVoiceActive[i].store(false, std::memory_order_relaxed);
                    uiVoiceEnvelopeTimeSeconds[i].store(0.0, std::memory_order_relaxed);
                    synth.addVoice(new ShapeVoice(*this, inputBuffer, i));
                }
            } else {
                for (int i = synth.getNumVoices() - 1; i >= numVoices; i--) {
                    uiVoiceActive[i].store(false, std::memory_order_relaxed);
                    uiVoiceEnvelopeTimeSeconds[i].store(0.0, std::memory_order_relaxed);
                    synth.removeVoice(i);
                }
            }
        }
    }

    // Envelope UI listens to these parameters.
}

void OscirenderAudioProcessor::parameterGestureChanged(int parameterIndex, bool gestureIsStarting) {}

// === Global LFO system ===

void OscirenderAudioProcessor::lfoWaveformChanged(int index, const LfoWaveform& waveform) {
    if (index < 0 || index >= NUM_LFOS) return;
    juce::SpinLock::ScopedLockType lock(lfoWaveformLock);
    lfoWaveforms[index] = waveform;
}

void OscirenderAudioProcessor::addLfoAssignment(const LfoAssignment& assignment) {
    juce::SpinLock::ScopedLockType lock(lfoAssignmentLock);
    // Update existing assignment in-place to preserve ordering
    for (auto& a : lfoAssignments) {
        if (a.sourceIndex == assignment.sourceIndex && a.paramId == assignment.paramId) {
            a.depth = assignment.depth;
            a.bipolar = assignment.bipolar;
            return;
        }
    }
    lfoAssignments.push_back(assignment);
}

void OscirenderAudioProcessor::removeLfoAssignment(int lfoIndex, const juce::String& paramId) {
    juce::SpinLock::ScopedLockType lock(lfoAssignmentLock);
    lfoAssignments.erase(
        std::remove_if(lfoAssignments.begin(), lfoAssignments.end(),
            [&](const LfoAssignment& a) { return a.sourceIndex == lfoIndex && a.paramId == paramId; }),
        lfoAssignments.end());
}

std::vector<LfoAssignment> OscirenderAudioProcessor::getLfoAssignments() const {
    juce::SpinLock::ScopedLockType lock(lfoAssignmentLock);
    return lfoAssignments;
}

LfoWaveform OscirenderAudioProcessor::getLfoWaveform(int index) const {
    if (index < 0 || index >= NUM_LFOS) return {};
    juce::SpinLock::ScopedLockType lock(lfoWaveformLock);
    return lfoWaveforms[index];
}

float OscirenderAudioProcessor::getLfoCurrentValue(int lfoIndex) const {
    if (lfoIndex < 0 || lfoIndex >= NUM_LFOS) return 0.0f;
    return lfoCurrentValues[lfoIndex].load(std::memory_order_relaxed);
}

float OscirenderAudioProcessor::getLfoCurrentPhase(int lfoIndex) const {
    if (lfoIndex < 0 || lfoIndex >= NUM_LFOS) return 0.0f;
    return lfoCurrentPhases[lfoIndex].load(std::memory_order_relaxed);
}

bool OscirenderAudioProcessor::isLfoActive(int lfoIndex) const {
    if (lfoIndex < 0 || lfoIndex >= NUM_LFOS) return false;
    return lfoActive[lfoIndex].load(std::memory_order_relaxed);
}

void OscirenderAudioProcessor::setLfoRateMode(int lfoIndex, LfoRateMode mode) {
    juce::SpinLock::ScopedLockType lock(lfoWaveformLock);
    lfoRateModes[lfoIndex] = mode;
}

void OscirenderAudioProcessor::setLfoTempoDivision(int lfoIndex, int divisionIndex) {
    juce::SpinLock::ScopedLockType lock(lfoWaveformLock);
    lfoTempoDivisions[lfoIndex] = divisionIndex;
}

LfoRateMode OscirenderAudioProcessor::getLfoRateMode(int lfoIndex) const {
    juce::SpinLock::ScopedLockType lock(lfoWaveformLock);
    return lfoRateModes[lfoIndex];
}

int OscirenderAudioProcessor::getLfoTempoDivision(int lfoIndex) const {
    juce::SpinLock::ScopedLockType lock(lfoWaveformLock);
    return lfoTempoDivisions[lfoIndex];
}

void OscirenderAudioProcessor::setLfoMode(int lfoIndex, LfoMode mode) {
    jassert(lfoIndex >= 0 && lfoIndex < NUM_LFOS);
    if (lfoIndex < 0 || lfoIndex >= NUM_LFOS) return;
    juce::SpinLock::ScopedLockType lock(lfoWaveformLock);
    lfoModes[lfoIndex] = mode;
}

void OscirenderAudioProcessor::setLfoPhaseOffset(int lfoIndex, float phase) {
    jassert(lfoIndex >= 0 && lfoIndex < NUM_LFOS);
    if (lfoIndex < 0 || lfoIndex >= NUM_LFOS) return;
    juce::SpinLock::ScopedLockType lock(lfoWaveformLock);
    lfoPhaseOffsets[lfoIndex] = juce::jlimit(0.0f, 1.0f, phase);
}

void OscirenderAudioProcessor::setLfoSmoothAmount(int lfoIndex, float seconds) {
    jassert(lfoIndex >= 0 && lfoIndex < NUM_LFOS);
    if (lfoIndex < 0 || lfoIndex >= NUM_LFOS) return;
    lfoSmoothAmounts[lfoIndex].store(juce::jlimit(0.0f, 16.0f, seconds), std::memory_order_relaxed);
}

float OscirenderAudioProcessor::getLfoSmoothAmount(int lfoIndex) const {
    jassert(lfoIndex >= 0 && lfoIndex < NUM_LFOS);
    if (lfoIndex < 0 || lfoIndex >= NUM_LFOS) return 0.005f;
    return lfoSmoothAmounts[lfoIndex].load(std::memory_order_relaxed);
}

void OscirenderAudioProcessor::setLfoDelayAmount(int lfoIndex, float seconds) {
    jassert(lfoIndex >= 0 && lfoIndex < NUM_LFOS);
    if (lfoIndex < 0 || lfoIndex >= NUM_LFOS) return;
    lfoDelayAmounts[lfoIndex].store(juce::jlimit(0.0f, 4.0f, seconds), std::memory_order_relaxed);
}

float OscirenderAudioProcessor::getLfoDelayAmount(int lfoIndex) const {
    jassert(lfoIndex >= 0 && lfoIndex < NUM_LFOS);
    if (lfoIndex < 0 || lfoIndex >= NUM_LFOS) return 0.0f;
    return lfoDelayAmounts[lfoIndex].load(std::memory_order_relaxed);
}

LfoMode OscirenderAudioProcessor::getLfoMode(int lfoIndex) const {
    jassert(lfoIndex >= 0 && lfoIndex < NUM_LFOS);
    if (lfoIndex < 0 || lfoIndex >= NUM_LFOS) return LfoMode::Free;
    juce::SpinLock::ScopedLockType lock(lfoWaveformLock);
    return lfoModes[lfoIndex];
}

float OscirenderAudioProcessor::getLfoPhaseOffset(int lfoIndex) const {
    jassert(lfoIndex >= 0 && lfoIndex < NUM_LFOS);
    if (lfoIndex < 0 || lfoIndex >= NUM_LFOS) return 0.0f;
    juce::SpinLock::ScopedLockType lock(lfoWaveformLock);
    return lfoPhaseOffsets[lfoIndex];
}

// === Global Envelope assignment system ===

void OscirenderAudioProcessor::addEnvAssignment(const EnvAssignment& assignment) {
    juce::SpinLock::ScopedLockType lock(envAssignmentLock);
    for (auto& a : envAssignments) {
        if (a.sourceIndex == assignment.sourceIndex && a.paramId == assignment.paramId) {
            a.depth = assignment.depth;
            a.bipolar = assignment.bipolar;
            return;
        }
    }
    envAssignments.push_back(assignment);
}

void OscirenderAudioProcessor::removeEnvAssignment(int envIndex, const juce::String& paramId) {
    juce::SpinLock::ScopedLockType lock(envAssignmentLock);
    envAssignments.erase(
        std::remove_if(envAssignments.begin(), envAssignments.end(),
            [&](const EnvAssignment& a) { return a.sourceIndex == envIndex && a.paramId == paramId; }),
        envAssignments.end());
}

std::vector<EnvAssignment> OscirenderAudioProcessor::getEnvAssignments() const {
    juce::SpinLock::ScopedLockType lock(envAssignmentLock);
    return envAssignments;
}

void OscirenderAudioProcessor::removeAllAssignmentsForEffect(const osci::Effect& effect) {
    auto belongsToEffect = [&](const juce::String& paramId) {
        for (auto* p : effect.parameters)
            if (p->paramID == paramId) return true;
        return false;
    };

    {
        juce::SpinLock::ScopedLockType lock(lfoAssignmentLock);
        lfoAssignments.erase(
            std::remove_if(lfoAssignments.begin(), lfoAssignments.end(),
                [&](const LfoAssignment& a) { return belongsToEffect(a.paramId); }),
            lfoAssignments.end());
    }
    {
        juce::SpinLock::ScopedLockType lock(envAssignmentLock);
        envAssignments.erase(
            std::remove_if(envAssignments.begin(), envAssignments.end(),
                [&](const EnvAssignment& a) { return belongsToEffect(a.paramId); }),
            envAssignments.end());
    }
    {
        juce::SpinLock::ScopedLockType lock(randomAssignmentLock);
        randomAssignments.erase(
            std::remove_if(randomAssignments.begin(), randomAssignments.end(),
                [&](const RandomAssignment& a) { return belongsToEffect(a.paramId); }),
            randomAssignments.end());
    }
}

void OscirenderAudioProcessor::autoAssignLfosForEffect(osci::Effect& effect) {

    // Snapshot current assignments (message thread, safe to take lock briefly)
    std::vector<LfoAssignment> currentAssignments;
    {
        juce::SpinLock::ScopedLockType lock(lfoAssignmentLock);
        currentAssignments = lfoAssignments;
    }

    // Count assignments per LFO index
    int assignmentCount[NUM_LFOS] = {};
    for (const auto& a : currentAssignments)
        if (a.sourceIndex >= 0 && a.sourceIndex < NUM_LFOS)
            assignmentCount[a.sourceIndex]++;

    // Helper: rate similarity (within 25%)
    auto ratesSimilar = [](float a, float b) -> bool {
        if (a <= 0.0f || b <= 0.0f) return false;
        float ratio = (a > b) ? a / b : b / a;
        return ratio < 1.25f;
    };

    // Single-pass LFO selection scoring: higher score = better match.
    // Score 4: already linked, matching preset + similar rate (exact reuse)
    // Score 3: idle, matching preset (not Custom)
    // Score 2: idle, factory preset (not Custom) — will reconfigure
    // Score 1: idle, any (including Custom) — will reconfigure
    auto findBestLfo = [&](LfoPreset desiredPreset, float desiredRate) -> int {
        int bestLfo = -1;
        int bestScore = 0;
        for (int i = 0; i < NUM_LFOS; ++i) {
            int score = 0;
            bool idle = assignmentCount[i] == 0;
            if (!idle && lfoPresets[i] == desiredPreset) {
                float currentRate = (lfoRate[i] != nullptr) ? lfoRate[i]->getValueUnnormalised() : 1.0f;
                if (ratesSimilar(currentRate, desiredRate))
                    score = 4;
            } else if (idle && lfoPresets[i] == desiredPreset && lfoPresets[i] != LfoPreset::Custom) {
                score = 3;
            } else if (idle && lfoPresets[i] != LfoPreset::Custom) {
                score = 2;
            } else if (idle) {
                score = 1;
            }
            if (score > bestScore) {
                bestScore = score;
                bestLfo = i;
                if (score == 4) break; // Can't do better
            }
        }
        return bestLfo;
    };

    auto configureLfo = [&](int lfoIdx, LfoPreset desiredPreset, float desiredRate, int score) {
        if (score <= 2) {
            // Need to reconfigure waveform
            juce::SpinLock::ScopedLockType lock(lfoWaveformLock);
            lfoPresets[lfoIdx] = desiredPreset;
            lfoWaveforms[lfoIdx] = createLfoPreset(desiredPreset);
        }
        if (score <= 3 && lfoRate[lfoIdx] != nullptr) {
            lfoRate[lfoIdx]->setUnnormalisedValueNotifyingHost(desiredRate);
        }
    };

    for (auto* param : effect.parameters) {
        if (param->lfoTypeDefault == osci::LfoType::Static)
            continue;

        LfoPreset desiredPreset = lfoTypeToLfoPreset(param->lfoTypeDefault);
        float desiredRate = param->lfoRateDefault_;

        int chosenLfo = findBestLfo(desiredPreset, desiredRate);
        if (chosenLfo < 0)
            continue;

        // Determine the score again to decide what to configure
        int score = 0;
        {
            bool idle = assignmentCount[chosenLfo] == 0;
            if (!idle) score = 4;
            else if (lfoPresets[chosenLfo] == desiredPreset && lfoPresets[chosenLfo] != LfoPreset::Custom) score = 3;
            else if (lfoPresets[chosenLfo] != LfoPreset::Custom) score = 2;
            else score = 1;
        }
        configureLfo(chosenLfo, desiredPreset, desiredRate, score);

        // Set the parameter value to its minimum so the LFO sweeps the full range
        param->setUnnormalisedValueNotifyingHost(param->min.load());

        // Create the assignment
        LfoAssignment assignment;
        assignment.sourceIndex = chosenLfo;
        assignment.paramId = param->paramID;
        assignment.depth = 1.0f;
        assignment.bipolar = false;
        addLfoAssignment(assignment);

        // Track locally so subsequent params see the updated count
        assignmentCount[chosenLfo]++;
    }
}

float OscirenderAudioProcessor::getEnvCurrentValue(int envIndex) const {
    if (envIndex < 0 || envIndex >= NUM_ENVELOPES) return 0.0f;
    return envCurrentValues[envIndex].load(std::memory_order_relaxed);
}

// === Global Random assignment system ===

void OscirenderAudioProcessor::addRandomAssignment(const RandomAssignment& assignment) {
    juce::SpinLock::ScopedLockType lock(randomAssignmentLock);
    for (auto& a : randomAssignments) {
        if (a.sourceIndex == assignment.sourceIndex && a.paramId == assignment.paramId) {
            a.depth = assignment.depth;
            a.bipolar = assignment.bipolar;
            return;
        }
    }
    randomAssignments.push_back(assignment);
}

void OscirenderAudioProcessor::removeRandomAssignment(int randomIndex, const juce::String& paramId) {
    juce::SpinLock::ScopedLockType lock(randomAssignmentLock);
    randomAssignments.erase(
        std::remove_if(randomAssignments.begin(), randomAssignments.end(),
            [&](const RandomAssignment& a) { return a.sourceIndex == randomIndex && a.paramId == paramId; }),
        randomAssignments.end());
}

std::vector<RandomAssignment> OscirenderAudioProcessor::getRandomAssignments() const {
    juce::SpinLock::ScopedLockType lock(randomAssignmentLock);
    return randomAssignments;
}

float OscirenderAudioProcessor::getRandomCurrentValue(int randomIndex) const {
    if (randomIndex < 0 || randomIndex >= NUM_RANDOM_SOURCES) return 0.0f;
    return randomCurrentValues[randomIndex].load(std::memory_order_relaxed);
}

bool OscirenderAudioProcessor::isRandomActive(int randomIndex) const {
    if (randomIndex < 0 || randomIndex >= NUM_RANDOM_SOURCES) return false;
    return randomActive[randomIndex].load(std::memory_order_relaxed);
}

int OscirenderAudioProcessor::drainRandomUIBuffer(int randomIndex, RandomUIRingBuffer::Entry* out, int maxEntries) {
    if (randomIndex < 0 || randomIndex >= NUM_RANDOM_SOURCES) return 0;
    return randomUIBuffers[randomIndex].drain(out, maxEntries);
}

void OscirenderAudioProcessor::setRandomRateMode(int randomIndex, LfoRateMode mode) {
    if (randomIndex < 0 || randomIndex >= NUM_RANDOM_SOURCES) return;
    juce::SpinLock::ScopedLockType lock(randomAssignmentLock);
    randomRateModes[randomIndex] = mode;
}

void OscirenderAudioProcessor::setRandomTempoDivision(int randomIndex, int divisionIndex) {
    if (randomIndex < 0 || randomIndex >= NUM_RANDOM_SOURCES) return;
    juce::SpinLock::ScopedLockType lock(randomAssignmentLock);
    randomTempoDivisions[randomIndex] = divisionIndex;
}

void OscirenderAudioProcessor::setRandomStyle(int randomIndex, RandomStyle style) {
    if (randomIndex < 0 || randomIndex >= NUM_RANDOM_SOURCES) return;
    juce::SpinLock::ScopedLockType lock(randomAssignmentLock);
    randomStyles[randomIndex] = style;
}

LfoRateMode OscirenderAudioProcessor::getRandomRateMode(int randomIndex) const {
    if (randomIndex < 0 || randomIndex >= NUM_RANDOM_SOURCES) return LfoRateMode::Seconds;
    juce::SpinLock::ScopedLockType lock(randomAssignmentLock);
    return randomRateModes[randomIndex];
}

int OscirenderAudioProcessor::getRandomTempoDivision(int randomIndex) const {
    if (randomIndex < 0 || randomIndex >= NUM_RANDOM_SOURCES) return 8;
    juce::SpinLock::ScopedLockType lock(randomAssignmentLock);
    return randomTempoDivisions[randomIndex];
}

RandomStyle OscirenderAudioProcessor::getRandomStyle(int randomIndex) const {
    if (randomIndex < 0 || randomIndex >= NUM_RANDOM_SOURCES) return RandomStyle::Perlin;
    juce::SpinLock::ScopedLockType lock(randomAssignmentLock);
    return randomStyles[randomIndex];
}

void OscirenderAudioProcessor::autoAssignLfosForPreview(const juce::String& effectId) {
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    clearPreviewLfoAssignmentsInternal();
    for (auto& eff : toggleableEffects) {
        if (eff->getId() == effectId) {
            // Save current parameter values before modifying them
            previewSavedParamValues.clear();
            for (auto* param : eff->parameters) {
                if (param->lfoTypeDefault != osci::LfoType::Static)
                    previewSavedParamValues.emplace_back(param->paramID, param->getValueUnnormalised());
            }
            autoAssignLfosForEffect(*eff);
            previewLfoEffectId = effectId;
            break;
        }
    }
}

void OscirenderAudioProcessor::clearPreviewLfoAssignments() {
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    clearPreviewLfoAssignmentsInternal();
}

void OscirenderAudioProcessor::clearPreviewLfoAssignmentsInternal() {
    if (previewLfoEffectId.isEmpty()) return;
    for (auto& eff : toggleableEffects) {
        if (eff->getId() == previewLfoEffectId) {
            removeAllAssignmentsForEffect(*eff);
            // Restore saved parameter values
            for (const auto& [paramId, savedValue] : previewSavedParamValues) {
                for (auto* param : eff->parameters) {
                    if (param->paramID == paramId) {
                        param->setUnnormalisedValueNotifyingHost(savedValue);
                        break;
                    }
                }
            }
            break;
        }
    }
    previewSavedParamValues.clear();
    previewLfoEffectId = juce::String();
}

void OscirenderAudioProcessor::promotePreviewLfoAssignments() {
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    // Keep the parameter values and assignments; just clear the preview tracking
    previewSavedParamValues.clear();
    previewLfoEffectId = juce::String();
}

juce::String OscirenderAudioProcessor::getParamDisplayName(const juce::String& paramId) const {
    auto search = [&](const std::vector<std::shared_ptr<osci::Effect>>& list) -> juce::String {
        for (auto& effect : list)
            for (auto* p : effect->parameters)
                if (p->paramID == paramId) return p->name;
        return {};
    };

    juce::String name = search(toggleableEffects);
    if (name.isNotEmpty()) return name;
    name = search(luaEffects);
    if (name.isNotEmpty()) return name;

    for (auto* p : frequencyEffect->parameters)
        if (p->paramID == paramId) return p->name;
    for (auto* p : perspective->parameters)
        if (p->paramID == paramId) return p->name;

    name = search(visualiserParameters.effects);
    if (name.isNotEmpty()) return name;

    return paramId;
}

void OscirenderAudioProcessor::buildParamLocationMap() {
    paramLocationMap.clear();
    auto registerEffects = [&](std::vector<std::shared_ptr<osci::Effect>>& list) {
        for (auto& effect : list) {
            for (int p = 0; p < (int)effect->parameters.size(); ++p) {
                const auto& pid = effect->parameters[p]->paramID;
                if (paramLocationMap.find(pid) == paramLocationMap.end())
                    paramLocationMap[pid] = { effect.get(), p };
            }
        }
    };
    auto registerSingle = [&](std::shared_ptr<osci::Effect>& effect) {
        for (int p = 0; p < (int)effect->parameters.size(); ++p) {
            const auto& pid = effect->parameters[p]->paramID;
            if (paramLocationMap.find(pid) == paramLocationMap.end())
                paramLocationMap[pid] = { effect.get(), p };
        }
    };
    registerEffects(toggleableEffects);
    registerEffects(permanentEffects);
    registerEffects(luaEffects);
    registerSingle(frequencyEffect);
    registerSingle(perspective);
    // NOTE: visualiserParameters.effects and .audioEffects are NOT registered here.
    // They are animated/modulated on the renderer thread, not the audio thread.
    // See visualiserParameters.applyExternalModulation.
}

void OscirenderAudioProcessor::applyModulationBuffers(
    int numSamples,
    const std::vector<ModAssignment>& assignments,
    const std::vector<float>* sourceBuffers,
    int maxSourceIndex)
{
    for (const auto& assignment : assignments) {
        if (assignment.sourceIndex < 0 || assignment.sourceIndex >= maxSourceIndex) continue;

        auto it = paramLocationMap.find(assignment.paramId);
        if (it == paramLocationMap.end()) continue;

        auto& loc = it->second;
        float* buf = loc.effect->getAnimatedValuesWritePointer(loc.paramIndex, numSamples);
        if (buf == nullptr) continue;

        const float* modData = sourceBuffers[assignment.sourceIndex].data();
        float paramMin = loc.effect->parameters[loc.paramIndex]->min;
        float paramMax = loc.effect->parameters[loc.paramIndex]->max;
        float range = paramMax - paramMin;
        float depth = assignment.depth;

        if (assignment.bipolar) {
            for (int s = 0; s < numSamples; ++s)
                buf[s] = juce::jlimit(paramMin, paramMax,
                    buf[s] + (modData[s] * 2.0f - 1.0f) * depth * range * 0.5f);
        } else {
            for (int s = 0; s < numSamples; ++s)
                buf[s] = juce::jlimit(paramMin, paramMax,
                    buf[s] + modData[s] * depth * range);
        }
    }
}

void OscirenderAudioProcessor::applyGlobalEnvModulation(int numSamples, double sampleRate) {
    // Aggregate per-voice envelope values: use max across active voices for each envelope
    for (int e = 0; e < NUM_ENVELOPES; ++e) {
        float maxVal = 0.0f;
        for (int v = 0; v < kMaxUiVoices; ++v) {
            if (uiVoiceEnvActive[e][v].load(std::memory_order_relaxed)) {
                float val = uiVoiceEnvValue[e][v].load(std::memory_order_relaxed);
                if (val > maxVal) maxVal = val;
            }
        }
        envCurrentValues[e].store(maxVal, std::memory_order_relaxed);

        // Fill per-sample buffer by interpolating from previous block's value
        float prev = envPrevBlockValues[e];
        float curr = maxVal;
        if ((int)envBlockBuffer[e].size() < numSamples)
            envBlockBuffer[e].resize(numSamples);
        float invN = 1.0f / (float)numSamples;
        for (int s = 0; s < numSamples; ++s) {
            float t = (float)(s + 1) * invN;
            envBlockBuffer[e][s] = prev + (curr - prev) * t;
        }
        envPrevBlockValues[e] = curr;
    }

    // Apply envelope modulation via generic helper
    std::vector<EnvAssignment> envAssnCopy;
    {
        juce::SpinLock::ScopedLockType assnLock(envAssignmentLock);
        envAssnCopy = envAssignments;
    }
    applyModulationBuffers(numSamples, envAssnCopy, envBlockBuffer.data(), NUM_ENVELOPES);
}

void OscirenderAudioProcessor::applyGlobalLfoModulation(int numSamples, double sampleRate, const juce::MidiBuffer& midi) {
    // Process MIDI note events to drive LFO triggering for non-Free modes
    bool hadNoteOnThisBlock = false;
    for (const auto metadata : midi) {
        auto msg = metadata.getMessage();
        if (msg.isNoteOn()) {
            hadNoteOnThisBlock = true;
            lfoActiveNoteCount++;
            for (int l = 0; l < NUM_LFOS; ++l) {
                LfoMode mode = lfoModes[l];
                if (mode != LfoMode::Free && mode != LfoMode::Sync) {
                    // If the LFO was already running, flag a retrigger so the UI can reset the trail
                    if (!lfoAudioStates[l].finished)
                        lfoRetriggered[l].store(true, std::memory_order_relaxed);
                    lfoAudioStates[l].noteOn(mode, lfoPhaseOffsets[l]);
                    lfoDelayElapsed[l] = 0.0f; // Reset delay timer on retrigger
                }
            }
        } else if (msg.isNoteOff()) {
            lfoActiveNoteCount = std::max(0, lfoActiveNoteCount - 1);
            if (lfoActiveNoteCount == 0) {
                for (int l = 0; l < NUM_LFOS; ++l) {
                    LfoMode mode = lfoModes[l];
                    if (mode != LfoMode::Free && mode != LfoMode::Sync) {
                        lfoAudioStates[l].noteOff(mode);
                    }
                }
            }
        } else if (msg.isAllNotesOff() || msg.isAllSoundOff()) {
            lfoActiveNoteCount = 0;
            for (int l = 0; l < NUM_LFOS; ++l) {
                LfoMode mode = lfoModes[l];
                if (mode != LfoMode::Free && mode != LfoMode::Sync) {
                    lfoAudioStates[l].noteOff(mode);
                }
            }
        }
    }

    // Compute LFO samples for this block
    {
        juce::SpinLock::ScopedLockType wfLock(lfoWaveformLock);
        double bpm = currentBpm.load(std::memory_order_relaxed);
        auto& divisions = getTempoDivisions();

        // Check if any synth voices are still active (including release tails).
        // Uses previous block's state which is fine — one block latency is inaudible.
        bool anyVoiceActive = false;
        for (int v = 0; v < kMaxUiVoices; ++v) {
            if (uiVoiceActive[v].load(std::memory_order_relaxed)) {
                anyVoiceActive = true;
                break;
            }
        }

        // Treat a noteOn in this block as "voice will be active" since the synth
        // hasn't rendered yet (voice activation lags by one block).
        bool effectiveVoiceActive = anyVoiceActive || hadNoteOnThisBlock;

        for (int l = 0; l < NUM_LFOS; ++l) {
            if ((int)lfoBlockBuffer[l].size() < numSamples)
                lfoBlockBuffer[l].resize(numSamples);
            float rate;
            if (lfoRateModes[l] == LfoRateMode::Seconds) {
                rate = lfoRate[l]->getValueUnnormalised();
            } else {
                int idx = juce::jlimit(0, (int)divisions.size() - 1, lfoTempoDivisions[l]);
                rate = (float)divisions[idx].toHz(bpm, lfoRateModes[l]);
            }
            float sr = (float)sampleRate;
            LfoMode mode = lfoModes[l];
            float phaseOff = lfoPhaseOffsets[l];

            // Determine if this LFO is actively modulating
            bool isActive;
            if (mode == LfoMode::Free) {
                isActive = true;  // Free is always active
            } else if (mode == LfoMode::Sync) {
                // Sync: phase always advances, but modulation only applies when voices are active
                isActive = effectiveVoiceActive;
            } else {
                // Note-dependent modes: active until voicesFinished sets finished=true
                isActive = !lfoAudioStates[l].finished;
            }

            // Apply startup delay: freeze phase until delay time has elapsed
            int delaySkipSamples = 0;
            float delaySecs = lfoDelayAmounts[l];
            if (delaySecs > 1e-6f) {
                float elapsed = lfoDelayElapsed[l];
                if (elapsed < delaySecs) {
                    float remainingSec = delaySecs - elapsed;
                    delaySkipSamples = juce::jmin(numSamples, (int)std::ceil(remainingSec * sr));
                    // Fill delay portion with the waveform value at the frozen phase
                    float heldValue = lfoWaveforms[l].evaluate(lfoAudioStates[l].phase);
                    for (int s = 0; s < delaySkipSamples; ++s)
                        lfoBlockBuffer[l][s] = heldValue;
                }
                float blockTimeSec = (float)numSamples / sr;
                lfoDelayElapsed[l] = elapsed + blockTimeSec;
            }

            // Only advance phase for the non-delay portion of the block
            int advanceSamples = numSamples - delaySkipSamples;
            if (advanceSamples > 0) {
                lfoAudioStates[l].advanceBlock(lfoBlockBuffer[l].data() + delaySkipSamples, advanceSamples,
                                               rate, sr, lfoWaveforms[l], mode, phaseOff);
            }
            // For Sync mode: zero modulation when no voice is active
            if (mode == LfoMode::Sync && !effectiveVoiceActive) {
                for (int s = 0; s < numSamples; ++s)
                    lfoBlockBuffer[l][s] = 0.0f;
            }

            // Apply exponential output smoothing (one-pole low-pass filter)
            float smoothSecs = lfoSmoothAmounts[l];
            if (smoothSecs > 1e-6f) {
                float alpha = 1.0f - std::exp(-1.0f / (smoothSecs * sr));
                float prev = lfoSmoothedOutput[l];
                for (int s = 0; s < numSamples; ++s) {
                    prev += alpha * (lfoBlockBuffer[l][s] - prev);
                    lfoBlockBuffer[l][s] = prev;
                }
                lfoSmoothedOutput[l] = prev;
            } else {
                lfoSmoothedOutput[l] = lfoBlockBuffer[l][numSamples - 1];
            }

            // Only call voicesFinished on the transition from active to inactive.
            // This prevents premature LFO stop when voice activation lags MIDI by one block.
            if (!effectiveVoiceActive && lfoPrevAnyVoiceActive) {
                lfoAudioStates[l].voicesFinished(mode);
            }

            // Publish most recent value, phase, and active state for thread-safe UI reads
            lfoCurrentValues[l].store(lfoBlockBuffer[l][numSamples - 1], std::memory_order_relaxed);
            lfoCurrentPhases[l].store(lfoAudioStates[l].phase, std::memory_order_relaxed);
            lfoActive[l].store(isActive, std::memory_order_relaxed);
        }

        lfoPrevAnyVoiceActive = effectiveVoiceActive;
    }

    // Apply LFO modulation via generic helper
    std::vector<LfoAssignment> lfoAssnCopy;
    {
        juce::SpinLock::ScopedLockType assnLock(lfoAssignmentLock);
        lfoAssnCopy = lfoAssignments;
    }
    applyModulationBuffers(numSamples, lfoAssnCopy, lfoBlockBuffer.data(), NUM_LFOS);
}

void OscirenderAudioProcessor::applyGlobalRandomModulation(int numSamples, double sampleRate, const juce::MidiBuffer& midi) {
    // Process MIDI note events to drive Random triggering (always note-dependent)
    bool hadNoteOnThisBlock = false;
    for (const auto metadata : midi) {
        auto msg = metadata.getMessage();
        if (msg.isNoteOn()) {
            hadNoteOnThisBlock = true;
            randomActiveNoteCount++;
            for (int r = 0; r < NUM_RANDOM_SOURCES; ++r) {
                if (!randomAudioStates[r].finished)
                    randomRetriggered[r].store(true, std::memory_order_relaxed);
                randomAudioStates[r].noteOn();
            }
        } else if (msg.isNoteOff()) {
            randomActiveNoteCount = std::max(0, randomActiveNoteCount - 1);
            if (randomActiveNoteCount == 0) {
                for (int r = 0; r < NUM_RANDOM_SOURCES; ++r) {
                    randomAudioStates[r].noteOff();
                }
            }
        } else if (msg.isAllNotesOff() || msg.isAllSoundOff()) {
            randomActiveNoteCount = 0;
            for (int r = 0; r < NUM_RANDOM_SOURCES; ++r) {
                randomAudioStates[r].noteOff();
            }
        }
    }

    // Compute Random samples for this block
    {
        double bpm = currentBpm.load(std::memory_order_relaxed);
        auto& divisions = getTempoDivisions();

        bool anyVoiceActive = false;
        for (int v = 0; v < kMaxUiVoices; ++v) {
            if (uiVoiceActive[v].load(std::memory_order_relaxed)) {
                anyVoiceActive = true;
                break;
            }
        }
        bool effectiveVoiceActive = anyVoiceActive || hadNoteOnThisBlock;

        for (int r = 0; r < NUM_RANDOM_SOURCES; ++r) {
            if ((int)randomBlockBuffer[r].size() < numSamples)
                randomBlockBuffer[r].resize(numSamples);

            float rate;
            if (randomRateModes[r] == LfoRateMode::Seconds) {
                rate = randomRate[r] ? randomRate[r]->getValueUnnormalised() : 1.0f;
            } else {
                int idx = juce::jlimit(0, (int)divisions.size() - 1, randomTempoDivisions[r]);
                rate = (float)divisions[idx].toHz(bpm, randomRateModes[r]);
            }

            randomAudioStates[r].style = randomStyles[r];
            bool isActive = !randomAudioStates[r].finished;

            randomAudioStates[r].advanceBlock(randomBlockBuffer[r].data(), numSamples,
                                              rate, (float)sampleRate);

            if (!effectiveVoiceActive && randomPrevAnyVoiceActive) {
                randomAudioStates[r].voicesFinished();
            }

            // Subsample the block into the UI ring buffer.
            // This gives the UI thread many intermediate points for smooth graphs.
            for (int s = kRandomUISubsampleInterval - 1; s < numSamples; s += kRandomUISubsampleInterval) {
                randomUIBuffers[r].push(randomBlockBuffer[r][s], isActive);
            }
            // Always push the last sample so the UI sees the final state.
            if (numSamples > 0) {
                int lastPushed = ((numSamples - 1) / kRandomUISubsampleInterval) * kRandomUISubsampleInterval + kRandomUISubsampleInterval - 1;
                if (lastPushed != numSamples - 1)
                    randomUIBuffers[r].push(randomBlockBuffer[r][numSamples - 1], isActive);
            }

            randomCurrentValues[r].store(randomBlockBuffer[r][numSamples - 1], std::memory_order_relaxed);
            randomActive[r].store(isActive, std::memory_order_relaxed);
        }

        randomPrevAnyVoiceActive = effectiveVoiceActive;
    }

    // Apply Random modulation via generic helper
    std::vector<RandomAssignment> randomAssnCopy;
    {
        juce::SpinLock::ScopedLockType assnLock(randomAssignmentLock);
        randomAssnCopy = randomAssignments;
    }
    applyModulationBuffers(numSamples, randomAssnCopy, randomBlockBuffer.data(), NUM_RANDOM_SOURCES);
}

DahdsrParams OscirenderAudioProcessor::getCurrentDahdsrParams() const
{
    return getCurrentDahdsrParams(0);
}

DahdsrParams OscirenderAudioProcessor::getCurrentDahdsrParams(int envIndex) const
{
    jassert(envIndex >= 0 && envIndex < NUM_ENVELOPES);
    const auto& ep = envParams[juce::jlimit(0, NUM_ENVELOPES - 1, envIndex)];
    if (ep.delayTime == nullptr) return DahdsrParams{};
    return DahdsrParams{
        .delaySeconds = ep.delayTime->getValueUnnormalised(),
        .attackSeconds = ep.attackTime->getValueUnnormalised(),
        .holdSeconds = ep.holdTime->getValueUnnormalised(),
        .decaySeconds = ep.decayTime->getValueUnnormalised(),
        .sustainLevel = ep.sustainLevel->getValueUnnormalised(),
        .releaseSeconds = ep.releaseTime->getValueUnnormalised(),
        .attackCurve = ep.attackShape->getValueUnnormalised(),
        .decayCurve = ep.decayShape->getValueUnnormalised(),
        .releaseCurve = ep.releaseShape->getValueUnnormalised(),
    };
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new OscirenderAudioProcessor();
}

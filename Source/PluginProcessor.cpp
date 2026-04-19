/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"

#include "audio/AudioThreadGuard.h"
#include "PluginEditor.h"
#include "components/modulation/LfoComponent.h"
#include "components/modulation/EnvelopeComponent.h"
#include "components/modulation/RandomComponent.h"
#include "components/modulation/SidechainComponent.h"
#include "audio/effects/BitCrushEffect.h"
#include "audio/effects/BulgeEffect.h"
#include "audio/effects/TwistEffect.h"
#include "audio/effects/PolygonizerEffect.h"
#include "audio/effects/SpiralBitCrushEffect.h"
#include "audio/effects/DistortEffect.h"
#include "audio/effects/UnfoldEffect.h"
#include "audio/effects/MultiplexEffect.h"
#include "audio/effects/SmoothEffect.h"
#include "audio/effects/WobbleEffect.h"
#include "audio/effects/DuplicatorEffect.h"
#include "audio/effects/DashedLineEffect.h"
#include "audio/effects/VectorCancellingEffect.h"
#include "audio/effects/ScaleEffect.h"
#include "audio/effects/RotateEffect.h"
#include "audio/effects/TranslateEffect.h"
#include "audio/effects/RippleEffect.h"
#include "audio/effects/SwirlEffect.h"
#include "audio/effects/BounceEffect.h"
#include "audio/effects/SkewEffect.h"
#include "audio/effects/KaleidoscopeEffect.h"
#include "audio/effects/VortexEffect.h"
#include "audio/effects/GodRayEffect.h"
#include "parser/FileParser.h"
#include "parser/FrameProducer.h"
#include "audio/modulation/LfoPresetManager.h"

#if (JUCE_MAC || JUCE_WINDOWS) && OSCI_PREMIUM
#include "parser/img/ImageParser.h"
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
#if OSCI_PREMIUM
    osciPermanentEffects.push_back(fractalDepthEffect);
#endif

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

    // --- Premium mode: strip per-parameter LFO dropdowns and sidechain from all effects ---
    // These are only used in the free version. In premium, modulation is done via
    // the global LFO/ENV module panels with drag-and-drop assignments.
#if OSCI_PREMIUM
    for (auto& effect : effects) {
        for (auto* param : effect->parameters) {
            param->disableLfo();
            param->disableSidechain();
        }
    }
#endif

    booleanParameters.push_back(midiEnabled);
    booleanParameters.push_back(inputEnabled);
    booleanParameters.push_back(animateFrames);
    booleanParameters.push_back(loopAnimation);
    booleanParameters.push_back(animationSyncBPM);
    booleanParameters.push_back(invertImage);

    // Adopt envelope parameters
    for (auto* p : envelopeParameters.getFloatParameters())
        floatParameters.push_back(p);
    floatParameters.push_back(animationRate);
    floatParameters.push_back(animationOffset);
    floatParameters.push_back(standaloneBpm);

    // Adopt parameters from modulation state classes (premium only)
#if OSCI_PREMIUM
    for (auto* p : lfoParameters.getFloatParameters())
        floatParameters.push_back(p);
    for (auto* p : lfoParameters.getIntParameters())
        intParameters.push_back(p);

    // Adopt Random parameters from state class (premium only)
    for (auto* p : randomParameters.getFloatParameters())
        floatParameters.push_back(p);
    for (auto* p : randomParameters.getIntParameters())
        intParameters.push_back(p);

    // Adopt Sidechain parameters from state class (premium only)
    for (auto* p : sidechainParameters.getFloatParameters())
        floatParameters.push_back(p);

    // Apply global default LFO preset if set
    {
        auto defaultFactory = getGlobalStringValue("defaultLfoPreset");
        auto defaultFile = getGlobalStringValue("defaultLfoPresetFile");
        if (defaultFile.isNotEmpty()) {
            juce::File file(defaultFile);
            if (file.existsAsFile()) {
                LfoPresetManager tempManager(applicationFolder.getChildFile("LFO Presets"));
                LfoWaveform waveform;
                juce::String name;
                if (tempManager.loadPreset(file, waveform, name)) {
                    for (int i = 0; i < NUM_LFOS; ++i) {
                        lfoParameters.waveformChanged(i, waveform);
                        lfoParameters.setIsCustom(i, true);
                    }
                }
            }
        } else if (defaultFactory.isNotEmpty()) {
            auto parsed = stringToLfoPreset(defaultFactory);
            if (parsed.has_value()) {
                for (int i = 0; i < NUM_LFOS; ++i) {
                    lfoParameters.setPreset(i, *parsed);
                    lfoParameters.waveformChanged(i, createLfoPreset(*parsed));
                }
            }
        }
    }
#endif

    intParameters.push_back(voices);
    intParameters.push_back(fileSelect);
#if OSCI_PREMIUM
    intParameters.push_back(pitchBendRange);
#endif
    floatParameters.push_back(velocityTracking);
#if OSCI_PREMIUM
    floatParameters.push_back(glideTime);
    floatParameters.push_back(glideSlope);
    booleanParameters.push_back(alwaysGlide);
    booleanParameters.push_back(legato);
    booleanParameters.push_back(octaveScale);
#endif

    voices->addListener(this);
#if OSCI_PREMIUM
    legato->addListener(this);
#endif
    envelopeParameters.params[0].addListenerToAll(this);

    // Start the background voice builder thread.
    voiceBuilder = std::make_unique<VoiceBuilder>(*this);
    int initialVoices = voices->getValueUnnormalised();
    synth.setClient(this);
    synth.setPolyphony(initialVoices);
#if OSCI_PREMIUM
    synth.setLegato(legato->getBoolValue());
#endif
    voiceBuilder->setTargetVoiceCount(initialVoices + 1); // +1 overlap voice for kill-fade
    voiceBuilder->startThread(juce::Thread::Priority::low);

    for (int i = 0; i < luaEffects.size(); i++) {
        luaEffects[i]->parameters[0]->addListener(this);
    }

    defaultSound = new ShapeSound(*this, std::make_shared<FileParser>(*this));
    synth.addSound(defaultSound.get());

    activeShapeSound.store(defaultSound.get(), std::memory_order_release);

    fileSelectionNotifier = std::make_unique<FileSelectionAsyncNotifier>(*this);

    // Default to MIDI enabled when running as a plugin (VST/AU)
    if (!juce::JUCEApplicationBase::isStandaloneApp()) {
        midiEnabled->setBoolValue(true);
    }

    addAllParameters();

    buildParamLocationMap();

    // Register modulation sources with the engine.
    // Order determines stacking order (LFO first, then ENV, Random, Sidechain).
    modulationEngine.addSource(&lfoParameters);
    modulationEngine.addSource(&envelopeParameters);
    modulationEngine.addSource(&randomParameters);
    modulationEngine.addSource(&sidechainParameters);

    // Set colour functions (defined in UI component headers, so set here to avoid
    // coupling the audio-layer headers to UI headers).
    lfoParameters.setColourFunction(&LfoComponent::getLfoColour);
    envelopeParameters.setColourFunction(&EnvelopeComponent::getEnvColour);
    randomParameters.setColourFunction(&RandomComponent::getRandomColour);
    sidechainParameters.setColourFunction(&SidechainComponent::getSidechainColour);

    // Wire undo manager to modulation sources so assignment changes are undoable.
    lfoParameters.setUndoManager(&undoManager);
    lfoParameters.setUndoSuppressedFlag(&undoSuppressed);
    lfoParameters.setUndoGroupingFlag(&undoGrouping);
    envelopeParameters.setUndoManager(&undoManager);
    envelopeParameters.setUndoSuppressedFlag(&undoSuppressed);
    envelopeParameters.setUndoGroupingFlag(&undoGrouping);
    randomParameters.setUndoManager(&undoManager);
    randomParameters.setUndoSuppressedFlag(&undoSuppressed);
    randomParameters.setUndoGroupingFlag(&undoGrouping);
    sidechainParameters.setUndoManager(&undoManager);
    sidechainParameters.setUndoSuppressedFlag(&undoSuppressed);
    sidechainParameters.setUndoGroupingFlag(&undoGrouping);

    // Wire up renderer-side modulation for visualiser effects.
    // The renderer calls this after animateValues() to apply modulation from all sources.
    // Uses modulationEngine.getSources() so that any newly registered source type is
    // automatically included — no manual per-type code required.
    visualiserParameters.applyExternalModulation = [this](int numSamples) {
        auto applyAssignments = [&](const std::vector<ModAssignment>& assignments, ModulationSource* source) {
            int maxIndex = source->getSourceCount();
            for (const auto& assignment : assignments) {
                if (assignment.sourceIndex < 0 || assignment.sourceIndex >= maxIndex) continue;

                auto findAndApply = [&](std::vector<std::shared_ptr<osci::Effect>>& effectList) {
                    for (auto& effect : effectList) {
                        for (int p = 0; p < (int)effect->parameters.size(); ++p) {
                            if (effect->parameters[p]->paramID != assignment.paramId) continue;

                            float* buf = effect->getAnimatedValuesWritePointer(p, numSamples);
                            if (buf == nullptr) continue;

                            float val = source->getCurrentValue(assignment.sourceIndex);
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

        for (auto* source : modulationEngine.getSources()) {
            juce::SpinLock::ScopedLockType assnLock(source->assignments.lock);
            applyAssignments(source->assignments.items, source);
        }
    };
}

// ---------------------------------------------------------------------------
// VoiceBuilder::run() — defined here because it needs the full
// OscirenderAudioProcessor definition (header is forward-declared).
// ---------------------------------------------------------------------------

void VoiceBuilder::run() {
    while (!threadShouldExit()) {
        wait(-1);
        if (threadShouldExit()) break;

        // Build or remove voices one at a time, re-checking the target
        // between each operation to handle rapid slider changes.
        while (!threadShouldExit()) {
            const int target = targetCount.load(std::memory_order_acquire);
            const int current = processor.synth.getNumVoices();

            if (current == target)
                break;

            if (current < target) {
                // Build one voice (the expensive part — runs off the
                // message and audio threads).
                auto* voice = new ShapeVoice(processor, processor.inputBuffer, current);

                // Re-check: is this voice still needed?
                if (targetCount.load(std::memory_order_acquire) > current) {
                    processor.synth.addVoice(voice); // internally locked
                } else {
                    delete voice;
                }
            } else {
                // Removal is cheap — just do it directly.
                processor.synth.removeVoice(current - 1); // internally locked
            }
        }
    }
}

// ---------------------------------------------------------------------------

OscirenderAudioProcessor::~OscirenderAudioProcessor() {
    // Stop the voice builder before tearing down any processor state it references.
    voiceBuilder.reset();

    for (int i = luaEffects.size() - 1; i >= 0; i--) {
        luaEffects[i]->parameters[0]->removeListener(this);
    }
    // Clear all effect vectors that may reference luaEffectState before it is
    // destroyed (it's a member of this class, destroyed after the body).
    // Without this, ~CommonAudioProcessor destroys the vectors AFTER
    // luaEffectState is freed, causing a use-after-free in ~CustomEffect.
    luaEffects.clear();
    toggleableEffects.clear();
    permanentEffects.clear();
    effects.clear();
    envelopeParameters.params[0].removeListenerFromAll(this);
#if OSCI_PREMIUM
    legato->removeListener(this);
#endif
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

    defaultEnvelopeState.smoothedLevel = 0.0f;
    synth.handleMidiEvent(juce::MidiMessage::allSoundOff(1));
    synth.setCurrentPlaybackSampleRate(sampleRate);
    retriggerMidi = true;

    modulationEngine.prepareToPlay(sampleRate, samplesPerBlock);
    
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

void OscirenderAudioProcessor::applyEffectOrder(const std::vector<juce::String>& order) {
    {
        juce::SpinLock::ScopedLockType lock(effectsLock);
        std::unordered_map<juce::String, std::shared_ptr<osci::Effect>> idMap;
        for (auto& e : toggleableEffects)
            idMap[e->getId()] = e;
        int idx = 0;
        for (auto& id : order) {
            auto it = idMap.find(id);
            if (it != idMap.end()) {
                it->second->setPrecedence(idx++);
                idMap.erase(it);
            }
        }
        for (auto& effect : toggleableEffects) {
            auto it = idMap.find(effect->getId());
            if (it != idMap.end()) {
                effect->setPrecedence(idx++);
                idMap.erase(it);
            }
        }
        updateEffectPrecedence();
    }
    broadcaster.sendChangeMessage();
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
    if (currentFile < 0 || currentFile >= parsers.size()) {
        return nullptr;
    }
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
        if (voice && !voice->isSilent()) {
            voice->setPreviewEffect(simplePreview);
        }
    }
}

void OscirenderAudioProcessor::clearPreviewEffect() {
    previewEffect.reset();
    // Clear preview effect from active voices
    for (int i = 0; i < synth.getNumVoices(); i++) {
        auto voice = dynamic_cast<ShapeVoice*>(synth.getVoice(i));
        if (voice && !voice->isSilent()) {
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
    AudioThreadGuard::ScopedAudioThread audioThreadGuard;

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

    // Process MIDI CC → parameter mappings (always active, even when synth MIDI is off)
    midiCCManager.processMidiBuffer(midiMessages);

#if OSCI_PREMIUM
    // Parse MTS SysEx from incoming MIDI for microtuning support
    mtsClient.parseMidiBuffer(midiMessages);
#endif

    bool usingInput = inputEnabled->getBoolValue();

    bool usingMidi = midiEnabled->getBoolValue();
    if (!usingMidi) {
        midiMessages.clear();
    }

    // if midi enabled has changed state, kill all voices immediately
    // (allSoundOff, not allNotesOff, so voices don't linger in release)
    if (prevMidiEnabled != usingMidi) {
        for (int i = 1; i <= 16; i++) {
            midiMessages.addEvent(juce::MidiMessage::allSoundOff(i), i);
        }
    }

    // if midi has just been disabled or we need to retrigger
    if (!usingMidi && (retriggerMidi || prevMidiEnabled)) {
        midiMessages.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 17);
        retriggerMidi = false;
    }

    prevMidiEnabled = usingMidi;

    const double EPSILON = 0.00001;

    inputBuffer.setSize(totalNumInputChannels, buffer.getNumSamples(), false, false, true);
    for (auto channel = 0; channel < totalNumInputChannels; channel++) {
        inputBuffer.copyFrom(channel, 0, buffer, channel, 0, buffer.getNumSamples());
    }

    // Step 1: Peak-rectify the input audio into rectifiedInputBuffer.
    // This is the raw per-sample amplitude: max(|L|, |R|) for stereo, |x| for mono.
    // No smoothing — envelope followers downstream apply their own attack/release.
    rectifiedInputBuffer.setSize(1, numSamples, false, false, true);
    {
        auto* rectData = rectifiedInputBuffer.getWritePointer(0);
        if (totalNumInputChannels >= 2) {
            const float* leftData = inputBuffer.getReadPointer(0);
            const float* rightData = inputBuffer.getReadPointer(1);
            for (int i = 0; i < numSamples; ++i)
                rectData[i] = juce::jmax(std::abs(leftData[i]), std::abs(rightData[i]));
        } else if (totalNumInputChannels == 1) {
            const float* monoData = inputBuffer.getReadPointer(0);
            juce::FloatVectorOperations::abs(rectData, monoData, numSamples);
        } else {
            juce::FloatVectorOperations::clear(rectData, numSamples);
        }
    }

    // Step 2: Produce the smoothed volume buffer for free-version per-parameter sidechain
    // and for effects that receive volumeInput. Uses a default envelope follower with
    // fixed attack/release (same time constant as the old 100ms EMA).
    currentVolumeBuffer.setSize(1, numSamples, false, false, true);
    {
        defaultEnvelopeState.advanceBlock(
            currentVolumeBuffer.getWritePointer(0),
            rectifiedInputBuffer.getReadPointer(0),
            numSamples,
            kDefaultEnvelopeAttack, kDefaultEnvelopeRelease,
            (float)sampleRate,
            kIdentityCurve);
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

#if OSCI_PREMIUM
        // Fill modulation block buffers (type-specific generation)
        lfoParameters.fillBlockBuffers(numSamples, sampleRate, midiMessages,
                                       currentBpm.load(std::memory_order_relaxed), uiVoiceActive);
        envelopeParameters.fillBlockBuffers(numSamples, uiVoiceEnvActive, uiVoiceEnvValue);
        randomParameters.fillBlockBuffers(numSamples, sampleRate, midiMessages,
                                          currentBpm.load(std::memory_order_relaxed), uiVoiceActive);
#endif

        // Always run the sidechain envelope follower so the UI display
        // (graph marker + output pill) stays current in both modes.
        sidechainParameters.fillBlockBuffers(numSamples, sampleRate, rectifiedInputBuffer.getReadPointer(0));

        // Apply all modulation buffers to animated parameter values (generic)
        modulationEngine.applyAllModulation(numSamples);
    }

    outputBuffer3d.setSize(6, buffer.getNumSamples(), false, false, true);
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
            osci::Point point = syphonImageParser.getSample(sample);
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
            [&] (const juce::MidiMessageMetadata& meta) { synth.handleMidiEvent(meta.getMessage()); }
        );

		// Apply toggleable effects directly in input mode (no synth voices involved)
		{
			juce::SpinLock::ScopedLockType lock(effectsLock);

            inputFrequencyBuffer.setSize(1, numSamples, false, false, true);
            {
                const float* freqBuf = frequencyEffect->getAnimatedValuesReadPointer(0, numSamples);
                if (freqBuf) {
                    juce::FloatVectorOperations::copy(inputFrequencyBuffer.getWritePointer(0), freqBuf, numSamples);
                } else {
                    juce::FloatVectorOperations::fill(inputFrequencyBuffer.getWritePointer(0), frequencyEffect->getValue(), numSamples);
                }
            }

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
    
    applyVolumeAndThreshold(outputArray, numSamples);
    
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
    xml->setAttribute("premiumProject", (bool) OSCI_PREMIUM);

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

    // Save global LFO waveforms & assignments (premium only)
#if OSCI_PREMIUM
    lfoParameters.saveToXml(xml.get());

    envelopeParameters.saveToXml(xml.get());

    randomParameters.saveToXml(xml.get());
    sidechainParameters.saveToXml(xml.get());
#endif

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

    midiCCManager.save(xml.get());

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

        // Load global LFO waveforms & assignments (premium only)
#if OSCI_PREMIUM
        lfoParameters.loadFromXml(xml.get());

        // If this is a free project, convert per-parameter LFOs to global LFO assignments
        if (!xml->getBoolAttribute("premiumProject", false)) {
            convertFreeProjectLfos(xml->getChildByName("effects"));
        }
#else
        lfoParameters.resetAudioState();
#endif

        // Load envelope assignments (premium only)
#if OSCI_PREMIUM
        envelopeParameters.loadFromXml(xml.get());
#endif

        // Load random modulator state (premium only)
#if OSCI_PREMIUM
        randomParameters.loadFromXml(xml.get());
#endif

        // Load sidechain modulator state (premium only)
#if OSCI_PREMIUM
        sidechainParameters.loadFromXml(xml.get());
#endif

        recordingParameters.load(xml.get());

        loadProperties(*xml);
        objectServer.reload();

        loadMidiCCState(xml.get());
#if OSCI_PREMIUM
        rebindAllModDepthCCMappings();
#endif

        broadcaster.sendChangeMessage();
        prevMidiEnabled = !midiEnabled->getBoolValue();
        undoManager.clearUndoHistory();

#if !OSCI_PREMIUM
        if (xml->getBoolAttribute("premiumProject", false)) {
            juce::Logger::writeToLog("setStateInformation: premium project loaded in free build, some features unavailable");
            juce::MessageManager::callAsync([]() {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::InfoIcon,
                    "Premium Project",
                    "This project was saved with the premium version of osci-render. "
                    "Some features (global LFOs, envelopes, random/sidechain modulation, "
                    "glide, legato, and premium effects) will not be available.",
                    "OK");
            });
        }
#endif

        juce::Logger::writeToLog("setStateInformation: state restore complete");
}

void OscirenderAudioProcessor::parameterValueChanged(int parameterIndex, float newValue) {
    if (parameterIndex == voices->getParameterIndex()) {
        int numVoices = voices->getValueUnnormalised();
        synth.setPolyphony(numVoices);
        const int currentVoices = synth.getNumVoices();
        if (numVoices != currentVoices) {
            // Reset UI telemetry for voices that are about to be added/removed.
            const int lo = std::min(numVoices, currentVoices);
            const int hi = std::min(std::max(numVoices, currentVoices), kMaxUiVoices);
            for (int i = lo; i < hi; i++) {
                uiVoiceActive[i].store(false, std::memory_order_relaxed);
                uiVoiceEnvelopeTimeSeconds[i].store(0.0, std::memory_order_relaxed);
            }
            voiceBuilder->setTargetVoiceCount(numVoices + 1); // +1 overlap voice for kill-fade
        }
#if OSCI_PREMIUM
    } else if (parameterIndex == legato->getParameterIndex()) {
        synth.setLegato(legato->getBoolValue());
#endif
    }

    // Envelope UI listens to these parameters.
}

void OscirenderAudioProcessor::parameterGestureChanged(int parameterIndex, bool gestureIsStarting) {}

void OscirenderAudioProcessor::removeAllAssignmentsForEffect(const osci::Effect& effect) {
    modulationEngine.removeAllAssignmentsForEffect(effect);
}

// --- Modulation-depth MIDI CC helpers ---

juce::String OscirenderAudioProcessor::modDepthCustomId(const juce::String& typeId,
                                                        int sourceIndex,
                                                        const juce::String& paramId) {
    return "mod:" + typeId + ":" + juce::String(sourceIndex) + ":" + paramId;
}

std::function<void(float)> OscirenderAudioProcessor::buildModDepthSetter(
        const juce::String& typeId, int sourceIndex, const juce::String& paramId) {
    // Find the modulation source matching typeId. Sources are stable after
    // construction so capturing the pointer is safe for the processor's lifetime.
    ModulationSource* matched = nullptr;
    for (auto* src : modulationEngine.getSources()) {
        if (src->getTypeId() == typeId) { matched = src; break; }
    }
    if (matched == nullptr) return {};

    return [matched, sourceIndex, paramId](float normValue) {
        jassert(juce::MessageManager::existsAndIsCurrentThread());
        // Map CC [0,1] to depth [-1,1].
        float depth = juce::jlimit(-1.0f, 1.0f, normValue * 2.0f - 1.0f);
        // Preserve the existing bipolar flag if an assignment is already present.
        bool bipolar = false;
        auto all = matched->getAssignments();
        for (const auto& a : all) {
            if (a.sourceIndex == sourceIndex && a.paramId == paramId) {
                bipolar = a.bipolar;
                break;
            }
        }
        matched->addAssignment({ sourceIndex, paramId, depth, bipolar });
    };
}

void OscirenderAudioProcessor::rebindAllModDepthCCMappings() {
    for (auto* src : modulationEngine.getSources()) {
        const auto typeId = src->getTypeId();
        for (const auto& a : src->getAssignments()) {
            auto id = modDepthCustomId(typeId, a.sourceIndex, a.paramId);
            auto setter = buildModDepthSetter(typeId, a.sourceIndex, a.paramId);
            if (setter)
                midiCCManager.rebindCustomSetter(id, std::move(setter));
        }
    }
}

std::vector<ModulationSourceBinding> OscirenderAudioProcessor::getModulationSourceBindings() {
    return modulationEngine.getModulationSourceBindings();
}

void OscirenderAudioProcessor::autoAssignLfosForEffect(osci::Effect& effect) {
    lfoParameters.autoAssignForEffect(effect);
}

#if OSCI_PREMIUM
void OscirenderAudioProcessor::convertFreeProjectLfos(const juce::XmlElement* effectsXml) {
    if (effectsXml == nullptr) return;

    // Parse per-parameter LFO settings from the free project's effects XML
    // and convert them into global LFO assignments.
    struct PerParamLfo {
        juce::String paramId;
        LfoPreset preset;
        float rate;
        float startPercent;
        float endPercent;
    };
    std::vector<PerParamLfo> conversions;

    for (auto* effectXml : effectsXml->getChildIterator()) {
        for (auto* paramXml : effectXml->getChildIterator()) {
            auto paramId = paramXml->getStringAttribute("id");
            if (paramId.isEmpty()) continue;

            auto* lfoXml = paramXml->getChildByName("lfo");
            if (lfoXml == nullptr) continue;

            auto lfoTypeStr = lfoXml->getStringAttribute("lfo", "Static");
            auto lfoType = stringToLfoType(lfoTypeStr);
            if (lfoType == osci::LfoType::Static) continue;

            float lfoRate = (float)lfoXml->getDoubleAttribute("value", 1.0);

            // Read LFO start/end percentages (defaults: 0%, 100%)
            float startPct = 0.0f;
            float endPct = 100.0f;
            auto* lfoStartXml = paramXml->getChildByName("lfoStart");
            if (lfoStartXml != nullptr)
                startPct = (float)lfoStartXml->getDoubleAttribute("value", 0.0);
            auto* lfoEndXml = paramXml->getChildByName("lfoEnd");
            if (lfoEndXml != nullptr)
                endPct = (float)lfoEndXml->getDoubleAttribute("value", 100.0);

            conversions.push_back({ paramId, lfoTypeToLfoPreset(lfoType), lfoRate, startPct, endPct });
        }
    }

    if (conversions.empty()) {
        juce::Logger::writeToLog("convertFreeProjectLfos: no per-parameter LFOs to convert");
        return;
    }

    juce::Logger::writeToLog("convertFreeProjectLfos: converting " + juce::String((int)conversions.size()) + " per-parameter LFOs to global LFO assignments");

    // Track how many assignments each global LFO slot has
    int assignmentCount[NUM_LFOS] = {};

    auto ratesSimilar = [](float a, float b) -> bool {
        if (a <= 0.0f || b <= 0.0f) return false;
        float ratio = (a > b) ? a / b : b / a;
        return ratio < 1.25f;
    };

    struct LfoMatch { int index = -1; int score = 0; };

    auto findBestLfo = [&](LfoPreset desiredPreset, float desiredRate) -> LfoMatch {
        LfoMatch best;
        for (int i = 0; i < NUM_LFOS; ++i) {
            int score = 0;
            bool idle = assignmentCount[i] == 0;
            LfoPreset currentPreset = lfoParameters.getPreset(i);
            bool isCustom = lfoParameters.getIsCustom(i);
            if (!idle && currentPreset == desiredPreset) {
                float currentRate = (lfoParameters.rate[i] != nullptr) ? lfoParameters.rate[i]->getValueUnnormalised() : 1.0f;
                if (ratesSimilar(currentRate, desiredRate))
                    score = 4;
            } else if (idle && currentPreset == desiredPreset && !isCustom) {
                score = 3;
            } else if (idle && !isCustom) {
                score = 2;
            } else if (idle) {
                score = 1;
            }
            if (score > best.score) {
                best = { i, score };
                if (score == 4) break;
            }
        }
        return best;
    };

    for (auto& conv : conversions) {
        auto match = findBestLfo(conv.preset, conv.rate);
        if (match.index < 0) {
            juce::Logger::writeToLog("convertFreeProjectLfos: no free LFO slot for param " + conv.paramId);
            continue;
        }

        // Configure the LFO slot if needed
        if (match.score <= 2) {
            juce::SpinLock::ScopedLockType lock(lfoParameters.waveformLock);
            lfoParameters.setPreset(match.index, conv.preset);
            lfoParameters.waveforms[match.index] = createLfoPreset(conv.preset);
        }
        if (match.score <= 3 && lfoParameters.rate[match.index] != nullptr) {
            lfoParameters.rate[match.index]->setUnnormalisedValueNotifyingHost(conv.rate);
        }

        // Convert the per-param LFO range to a global unipolar assignment.
        // Per-param LFO sweeps between [startPct, endPct] % of the parameter range.
        // Global unipolar modulation: output = base + modData * depth * paramRange.
        // So: base = paramMin + startPct/100 * range, depth = (endPct - startPct) / 100.
        float startNorm = juce::jlimit(0.0f, 1.0f, conv.startPercent / 100.0f);
        float endNorm = juce::jlimit(0.0f, 1.0f, conv.endPercent / 100.0f);
        float depth = endNorm - startNorm;

        // Only convert if the parameter belongs to an effect that is actually
        // present (selected) in the project — skip effects that weren't added.
        osci::EffectParameter* effectParam = nullptr;
        bool effectIsSelected = false;
        for (auto& effect : effects) {
            effectParam = effect->getParameter(conv.paramId);
            if (effectParam != nullptr) {
                // For toggleable effects, require the effect to be selected (present in the project).
                // Permanent effects and lua effects are always considered present.
                bool isToggleable = std::find(toggleableEffects.begin(), toggleableEffects.end(), effect) != toggleableEffects.end();
                effectIsSelected = !isToggleable || (effect->selected != nullptr && effect->selected->getBoolValue());
                break;
            }
        }
        if (effectParam == nullptr || !effectIsSelected) {
            juce::Logger::writeToLog("convertFreeProjectLfos: param " + conv.paramId + " not found or effect not selected, skipping");
            continue;
        }
        {
            float paramMin = effectParam->min.load();
            float paramRange = effectParam->max.load() - paramMin;
            float baseValue = paramMin + startNorm * paramRange;
            effectParam->setUnnormalisedValueNotifyingHost(baseValue);
        }

        LfoAssignment assignment;
        assignment.sourceIndex = match.index;
        assignment.paramId = conv.paramId;
        assignment.depth = std::abs(depth);
        assignment.bipolar = false;
        lfoParameters.addAssignment(assignment);

        assignmentCount[match.index]++;
    }

    juce::Logger::writeToLog("convertFreeProjectLfos: conversion complete");
}
#endif

void OscirenderAudioProcessor::autoAssignLfosForPreview(const juce::String& effectId) {
    ScopedFlag suppress(undoSuppressed);
    lfoParameters.startPreview(effectId, toggleableEffects,
                               [this](const osci::Effect& e) { removeAllAssignmentsForEffect(e); });
}

void OscirenderAudioProcessor::clearPreviewLfoAssignments() {
    ScopedFlag suppress(undoSuppressed);
    lfoParameters.stopPreview(toggleableEffects,
                              [this](const osci::Effect& e) { removeAllAssignmentsForEffect(e); });
}

void OscirenderAudioProcessor::promotePreviewLfoAssignments() {
    lfoParameters.promotePreview();
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

DahdsrParams OscirenderAudioProcessor::getCurrentDahdsrParams() const
{
    return envelopeParameters.getDahdsrParams(0);
}

DahdsrParams OscirenderAudioProcessor::getCurrentDahdsrParams(int envIndex) const
{
    return envelopeParameters.getDahdsrParams(envIndex);
}

int OscirenderAudioProcessor::getNumPressedNotes() const {
    return synth.getNumPressedNotes();
}

double OscirenderAudioProcessor::getLastPlayedNoteFreq() const {
    return synth.getLastPlayedNoteFreq();
}

// ==========================================================================
// VoiceManagerClient implementation
// ==========================================================================

void OscirenderAudioProcessor::voiceActivated(ManagedVoice& mv, bool isLegato) {
    if (auto* sv = dynamic_cast<ShapeVoice*>(mv.getJuceVoice()))
        sv->voiceActivated(mv.getState(), isLegato);
}

void OscirenderAudioProcessor::voiceDeactivated(ManagedVoice& mv) {
    if (auto* sv = dynamic_cast<ShapeVoice*>(mv.getJuceVoice()))
        sv->voiceDeactivated();
}

void OscirenderAudioProcessor::voiceKilled(ManagedVoice& mv) {
    if (auto* sv = dynamic_cast<ShapeVoice*>(mv.getJuceVoice()))
        sv->voiceKilled();
}

bool OscirenderAudioProcessor::isVoiceSilent(ManagedVoice& mv) const {
    if (auto* sv = dynamic_cast<ShapeVoice*>(mv.getJuceVoice()))
        return sv->isSilent();
    return true;
}

double OscirenderAudioProcessor::getVoiceFrequency(const ManagedVoice& mv) const {
    if (auto* sv = dynamic_cast<ShapeVoice*>(mv.getJuceVoice()))
        return sv->getFrequency();
    return 0.0;
}

void OscirenderAudioProcessor::captureDrawingState(ManagedVoice& mv) {
    if (auto* sv = dynamic_cast<ShapeVoice*>(mv.getJuceVoice()))
        sv->captureDrawingState();
}

void OscirenderAudioProcessor::restoreDrawingState(ManagedVoice& target, const ManagedVoice& source) {
    auto* targetVoice = dynamic_cast<ShapeVoice*>(target.getJuceVoice());
    auto* sourceVoice = dynamic_cast<ShapeVoice*>(source.getJuceVoice());
    if (targetVoice != nullptr)
        targetVoice->restoreDrawingState(sourceVoice);
}

double OscirenderAudioProcessor::noteToFrequency(int note, int channel) {
#if OSCI_PREMIUM
    return mtsClient.noteToFrequency(note, channel);
#else
    return juce::MidiMessage::getMidiNoteInHertz(note);
#endif
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new OscirenderAudioProcessor();
}

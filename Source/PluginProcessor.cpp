/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"

#include "PluginEditor.h"
#include "audio/BitCrushEffect.h"
#include "audio/BulgeEffect.h"
#include "audio/DistortEffect.h"
#include "audio/SmoothEffect.h"
#include "audio/VectorCancellingEffect.h"
#include "parser/FileParser.h"
#include "parser/FrameProducer.h"

#if (JUCE_MAC || JUCE_WINDOWS) && OSCI_PREMIUM
#include "img/ImageParser.h"
#endif

//==============================================================================
OscirenderAudioProcessor::OscirenderAudioProcessor() : CommonAudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::namedChannelSet(2), true).withOutput("Output", juce::AudioChannelSet::stereo(), true)) {
    // locking isn't necessary here because we are in the constructor

    toggleableEffects.push_back(std::make_shared<osci::Effect>(
        std::make_shared<BitCrushEffect>(),
        new osci::EffectParameter("Bit Crush", "Limits the resolution of points drawn to the screen, making the object look pixelated, and making the audio sound more 'digital' and distorted.", "bitCrush", VERSION_HINT, 0.6, 0.0, 1.0)));
    toggleableEffects.push_back(std::make_shared<osci::Effect>(
        std::make_shared<BulgeEffect>(),
        new osci::EffectParameter("Bulge", "Applies a bulge that makes the centre of the image larger, and squishes the edges of the image. This applies a distortion to the audio.", "bulge", VERSION_HINT, 0.5, 0.0, 1.0)));
    toggleableEffects.push_back(std::make_shared<osci::Effect>(
        std::make_shared<VectorCancellingEffect>(),
        new osci::EffectParameter("Vector Cancelling", "Inverts the audio and image every few samples to 'cancel out' the audio, making the audio quiet, and distorting the image.", "vectorCancelling", VERSION_HINT, 0.1111111, 0.0, 1.0)));
    auto scaleEffect = std::make_shared<osci::Effect>(
        [this](int index, osci::Point input, const std::vector<std::atomic<double>>& values, double sampleRate) {
            return input * osci::Point(values[0], values[1], values[2]);
        },
        std::vector<osci::EffectParameter*>{
        new osci::EffectParameter("Scale X", "Scales the object in the horizontal direction.", "scaleX", VERSION_HINT, 1.0, -3.0, 3.0),
            new osci::EffectParameter("Scale Y", "Scales the object in the vertical direction.", "scaleY", VERSION_HINT, 1.0, -3.0, 3.0),
            new osci::EffectParameter("Scale Z", "Scales the depth of the object.", "scaleZ", VERSION_HINT, 1.0, -3.0, 3.0),
    });
    scaleEffect->markLockable(true);
    booleanParameters.push_back(scaleEffect->linked);
    toggleableEffects.push_back(scaleEffect);
    auto distortEffect = std::make_shared<osci::Effect>(
        [this](int index, osci::Point input, const std::vector<std::atomic<double>>& values, double sampleRate) {
            int flip = index % 2 == 0 ? 1 : -1;
            osci::Point jitter = osci::Point(flip * values[0], flip * values[1], flip * values[2]);
            return input + jitter;
        },
        std::vector<osci::EffectParameter*>{
            new osci::EffectParameter("Distort X", "Distorts the image in the horizontal direction by jittering the audio sample being drawn.", "distortX", VERSION_HINT, 0.0, 0.0, 1.0),
            new osci::EffectParameter("Distort Y", "Distorts the image in the vertical direction by jittering the audio sample being drawn.", "distortY", VERSION_HINT, 0.0, 0.0, 1.0),
            new osci::EffectParameter("Distort Z", "Distorts the depth of the image by jittering the audio sample being drawn.", "distortZ", VERSION_HINT, 0.1, 0.0, 1.0),
        });
    distortEffect->markLockable(false);
    booleanParameters.push_back(distortEffect->linked);
    toggleableEffects.push_back(distortEffect);
    auto rippleEffect = std::make_shared<osci::Effect>(
        [this](int index, osci::Point input, const std::vector<std::atomic<double>>& values, double sampleRate) {
            double phase = values[1] * std::numbers::pi;
            double distance = 100 * values[2] * (input.x * input.x + input.y * input.y);
            input.z += values[0] * std::sin(phase + distance);
            return input;
        },
        std::vector<osci::EffectParameter*>{
            new osci::EffectParameter("Ripple Depth", "Controls how large the ripples applied to the image are.", "rippleDepth", VERSION_HINT, 0.2, 0.0, 1.0),
            new osci::EffectParameter("Ripple Phase", "Controls the position of the ripple. Animate this to see a moving ripple effect.", "ripplePhase", VERSION_HINT, 0.0, -1.0, 1.0),
            new osci::EffectParameter("Ripple Amount", "Controls how many ripples are applied to the image.", "rippleAmount", VERSION_HINT, 0.1, 0.0, 1.0),
        });
    rippleEffect->getParameter("ripplePhase")->lfo->setUnnormalisedValueNotifyingHost((int)osci::LfoType::Sawtooth);
    toggleableEffects.push_back(rippleEffect);
    auto rotateEffect = std::make_shared<osci::Effect>(
        [this](int index, osci::Point input, const std::vector<std::atomic<double>>& values, double sampleRate) {
            input.rotate(values[0] * std::numbers::pi, values[1] * std::numbers::pi, values[2] * std::numbers::pi);
            return input;
        },
        std::vector<osci::EffectParameter*>{
            new osci::EffectParameter("Rotate X", "Controls the rotation of the object in the X axis.", "rotateX", VERSION_HINT, 0.0, -1.0, 1.0),
            new osci::EffectParameter("Rotate Y", "Controls the rotation of the object in the Y axis.", "rotateY", VERSION_HINT, 0.0, -1.0, 1.0),
            new osci::EffectParameter("Rotate Z", "Controls the rotation of the object in the Z axis.", "rotateZ", VERSION_HINT, 0.0, -1.0, 1.0),
        });
    rotateEffect->getParameter("rotateY")->lfo->setUnnormalisedValueNotifyingHost((int)osci::LfoType::Sawtooth);
    rotateEffect->getParameter("rotateY")->lfoRate->setUnnormalisedValueNotifyingHost(0.2);
    toggleableEffects.push_back(rotateEffect);
    toggleableEffects.push_back(std::make_shared<osci::Effect>(
        [this](int index, osci::Point input, const std::vector<std::atomic<double>>& values, double sampleRate) {
            return input + osci::Point(values[0], values[1], values[2]);
        },
        std::vector<osci::EffectParameter*>{
            new osci::EffectParameter("Translate X", "Moves the object horizontally.", "translateX", VERSION_HINT, 0.0, -1.0, 1.0),
            new osci::EffectParameter("Translate Y", "Moves the object vertically.", "translateY", VERSION_HINT, 0.0, -1.0, 1.0),
            new osci::EffectParameter("Translate Z", "Moves the object away from the camera.", "translateZ", VERSION_HINT, 0.0, -1.0, 1.0),
        }));
    toggleableEffects.push_back(std::make_shared<osci::Effect>(
        [this](int index, osci::Point input, const std::vector<std::atomic<double>>& values, double sampleRate) {
            double length = 10 * values[0] * input.magnitude();
            double newX = input.x * std::cos(length) - input.y * std::sin(length);
            double newY = input.x * std::sin(length) + input.y * std::cos(length);
            return osci::Point(newX, newY, input.z);
        },
        std::vector<osci::EffectParameter*>{
            new osci::EffectParameter("Swirl", "Swirls the image in a spiral pattern.", "swirl", VERSION_HINT, 0.3, -1.0, 1.0),
        }));
    toggleableEffects.push_back(std::make_shared<osci::Effect>(
        std::make_shared<SmoothEffect>(),
        new osci::EffectParameter("Smoothing", "This works as a low-pass frequency filter that removes high frequencies, making the image look smoother, and audio sound less harsh.", "smoothing", VERSION_HINT, 0.75, 0.0, 1.0)));
    std::shared_ptr<osci::Effect> wobble = std::make_shared<osci::Effect>(
        wobbleEffect,
        std::vector<osci::EffectParameter*>{
            new osci::EffectParameter("Wobble Amount", "Adds a sine wave of the prominent frequency in the audio currently playing. The sine wave's frequency is slightly offset to create a subtle 'wobble' in the image. Increasing the slider increases the strength of the wobble.", "wobble", VERSION_HINT, 0.3, 0.0, 1.0),
            new osci::EffectParameter("Wobble Phase", "Controls the phase of the wobble.", "wobblePhase", VERSION_HINT, 0.0, -1.0, 1.0),
        });
    wobble->getParameter("wobblePhase")->lfo->setUnnormalisedValueNotifyingHost((int)osci::LfoType::Sawtooth);
    toggleableEffects.push_back(wobble);
    toggleableEffects.push_back(std::make_shared<osci::Effect>(
        delayEffect,
        std::vector<osci::EffectParameter*>{
            new osci::EffectParameter("Delay Decay", "Adds repetitions, delays, or echos to the audio. This slider controls the volume of the echo.", "delayDecay", VERSION_HINT, 0.4, 0.0, 1.0),
            new osci::EffectParameter("Delay Length", "Controls the time in seconds between echos.", "delayLength", VERSION_HINT, 0.5, 0.0, 1.0)}));
    toggleableEffects.push_back(std::make_shared<osci::Effect>(
        dashedLineEffect,
        std::vector<osci::EffectParameter*>{
            new osci::EffectParameter("Dash Length", "Controls the length of the dashed line.", "dashLength", VERSION_HINT, 0.2, 0.0, 1.0),
        }));
    toggleableEffects.push_back(custom);
    toggleableEffects.push_back(trace);
    trace->getParameter("traceLength")->lfo->setUnnormalisedValueNotifyingHost((int)osci::LfoType::Sawtooth);

    for (int i = 0; i < toggleableEffects.size(); i++) {
        auto effect = toggleableEffects[i];
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
        synth.addVoice(new ShapeVoice(*this));
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

    juce::AudioBuffer<float> inputBuffer = juce::AudioBuffer<float>(totalNumInputChannels, buffer.getNumSamples());
    for (auto channel = 0; channel < totalNumInputChannels; channel++) {
        inputBuffer.copyFrom(channel, 0, buffer, channel, 0, buffer.getNumSamples());
    }

    juce::AudioBuffer<float> outputBuffer3d = juce::AudioBuffer<float>(3, buffer.getNumSamples());
    outputBuffer3d.clear();

    {
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
                          [&](const juce::MidiMessageMetadata& meta) {
                              synth.publicHandleMidiEvent(meta.getMessage());
                          });
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
                    if (effect->enabled->getValue()) {
                        channels = effect->apply(sample, channels, currentVolume);
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

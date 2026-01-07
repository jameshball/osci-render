#include "ShapeVoice.h"
#include "../PluginProcessor.h"

ShapeVoice::ShapeVoice(OscirenderAudioProcessor& p, juce::AudioSampleBuffer& externalAudio, int voiceIndex)
    : audioProcessor(p), voiceIndex(voiceIndex), externalAudio(externalAudio) {
    initializeEffectsFromGlobal();
}

void ShapeVoice::initializeEffectsFromGlobal() {
    voiceEffectsMap.clear();
    for (auto& globalEffect : audioProcessor.toggleableEffects) {
        auto simpleEffect = std::dynamic_pointer_cast<osci::SimpleEffect>(globalEffect);
        if (simpleEffect) {
            auto cloned = simpleEffect->cloneWithSharedParameters();
            // Initialize the effect with current sample rate
            if (audioProcessor.currentSampleRate > 0) {
                cloned->prepareToPlay(audioProcessor.currentSampleRate, 512);
            }
            voiceEffectsMap[globalEffect->getId()] = cloned;
        }
    }
}

void ShapeVoice::setPreviewEffect(std::shared_ptr<osci::SimpleEffect> effect) {
    if (effect) {
        voicePreviewEffect = effect->cloneWithSharedParameters();
        // Initialize the effect with current sample rate
        if (audioProcessor.currentSampleRate > 0) {
            voicePreviewEffect->prepareToPlay(audioProcessor.currentSampleRate, 512);
        }
    } else {
        voicePreviewEffect = nullptr;
    }
}

void ShapeVoice::clearPreviewEffect() {
    voicePreviewEffect = nullptr;
}

void ShapeVoice::prepareToPlay(double sampleRate, int samplesPerBlock) {
    // Update sample rate for all voice effects
    for (auto& pair : voiceEffectsMap) {
        pair.second->prepareToPlay(sampleRate, samplesPerBlock);
    }
    // Update sample rate for preview effect if set
    if (voicePreviewEffect) {
        voicePreviewEffect->prepareToPlay(sampleRate, samplesPerBlock);
    }
}

bool ShapeVoice::canPlaySound(juce::SynthesiserSound* sound) {
    return dynamic_cast<ShapeSound*> (sound) != nullptr;
}

void ShapeVoice::startNote(int midiNoteNumber, float velocity, juce::SynthesiserSound* sound, int currentPitchWheelPosition) {
    this->velocity = velocity;
    pitchWheelMoved(currentPitchWheelPosition);
    auto* shapeSound = dynamic_cast<ShapeSound*>(sound);

    currentlyPlaying = true;
    this->sound = shapeSound;

    if (voiceIndex >= 0 && voiceIndex < OscirenderAudioProcessor::kMaxUiVoices) {
        audioProcessor.uiVoiceActive[voiceIndex].store(true, std::memory_order_relaxed);
        audioProcessor.uiVoiceEnvelopeTimeSeconds[voiceIndex].store(0.0, std::memory_order_relaxed);
    }
    
    // Sync preview effect state: set if there's one cached, clear if not
    auto cachedPreview = audioProcessor.getCachedPreviewEffect();
    if (cachedPreview) {
        setPreviewEffect(cachedPreview);
    } else {
        clearPreviewEffect();
    }

    auto parser = this->sound.load()->parser;
    renderingSample = parser != nullptr && parser->isSample();

    if (shapeSound != nullptr) {
        frame.clear();
        frameLength = 0.0;
        currentShape = 0;
        shapeDrawn = 0.0;
        frameDrawn = 0.0;
        int tries = 0;
        while (frame.empty() && tries < 50) {
            frameLength = shapeSound->updateFrame(frame);
            tries++;
        }
        // Snapshot DAHDSR parameters per-voice (Vital-style; peak fixed at 1.0)
        dahdsr = audioProcessor.getCurrentDahdsrParams();
        envState.reset(dahdsr);

        if (audioProcessor.midiEnabled->getBoolValue()) {
            // Match the frequency slider behavior: tiny epsilon prevents a weird mac glitch at certain exact frequencies.
            frequency = juce::MidiMessage::getMidiNoteInHertz(midiNoteNumber) + osci_audio::kMacFrequencyEpsilonHz;
        }
    }
}

// TODO this is the slowest part of the program - any way to improve this would help!
void ShapeVoice::incrementShapeDrawing() {
    if (frame.size() <= 0) return;
    double length = currentShape < frame.size() ? frame[currentShape]->len : 0.0;
    frameDrawn += lengthIncrement;
    shapeDrawn += lengthIncrement;

    // Need to skip all shapes that the lengthIncrement draws over.
    // This is especially an issue when there are lots of small lines being
    // drawn.
    while (shapeDrawn > length) {
        shapeDrawn -= length;
        currentShape++;
        if (currentShape >= frame.size()) {
            currentShape = 0;
        }
        // POTENTIAL TODO: Think of a way to make this more efficient when iterating
        // this loop many times
        length = frame[currentShape]->len;
    }
}

double ShapeVoice::getFrequency() {
    return actualFrequency;
}

// should be called if the current file is changed so that we interrupt
// any currently playing sounds / voices
void ShapeVoice::updateSound(juce::SynthesiserSound* sound) {
    if (currentlyPlaying) {
        this->sound = dynamic_cast<ShapeSound*>(sound);
        auto parser = this->sound.load()->parser;
        renderingSample = parser != nullptr && parser->isSample();
    }
}

void ShapeVoice::renderNextBlock(juce::AudioSampleBuffer& outputBuffer, int startSample, int numSamples) {
    juce::ScopedNoDenormals noDenormals;

    // Early exit if voice is not active - this shouldn't normally be called for inactive voices
    // but we check just in case to avoid unnecessary processing
    if (!isVoiceActive()) {
        if (voiceIndex >= 0 && voiceIndex < OscirenderAudioProcessor::kMaxUiVoices) {
            audioProcessor.uiVoiceActive[voiceIndex].store(false, std::memory_order_relaxed);
            audioProcessor.uiVoiceEnvelopeTimeSeconds[voiceIndex].store(0.0, std::memory_order_relaxed);
        }
        return;
    }

    int numChannels = outputBuffer.getNumChannels();

    if (audioProcessor.midiEnabled->getBoolValue()) {
        actualFrequency = frequency * pitchWheelAdjustment;
    } else {
        actualFrequency = audioProcessor.frequency.load();
    }

    // Prepare working buffers for effect processing
    voiceBuffer.setSize(numChannels, numSamples, false, false, true);
    voiceBuffer.clear();
    frequencyBuffer.setSize(1, numSamples, false, false, true);
    volumeBuffer.setSize(1, numSamples, false, false, true);

    const bool midiEnabled = audioProcessor.midiEnabled->getBoolValue();
    const double dt = 1.0 / audioProcessor.currentSampleRate;

    // First pass: generate raw audio samples (without gain) and fill frequency buffer + per-sample envelope
    for (int i = 0; i < numSamples; ++i) {
        int sample = startSample + i;
        lengthIncrement = juce::jmax(frameLength / (audioProcessor.currentSampleRate / actualFrequency), MIN_LENGTH_INCREMENT);

        osci::Point channels;

        if (sound.load() != nullptr) {
            auto parser = sound.load()->parser;

            if (renderingSample) {
                vars.sampleRate = audioProcessor.currentSampleRate;
                vars.frequency = actualFrequency;
                vars.ext_x = 0;
                vars.ext_y = 0;
                
                if (externalAudio.getNumSamples() >= 1) {
                    double sampleIndex = sample % externalAudio.getNumSamples();
                    int extNumChannels = externalAudio.getNumChannels();
                    if (extNumChannels >= 1) {
                        vars.ext_x = externalAudio.getSample(0, sampleIndex);
                    }
                    if (extNumChannels >= 2) {
                        vars.ext_y = externalAudio.getSample(1, sampleIndex);
                    }
                }
                std::copy(std::begin(audioProcessor.luaValues), std::end(audioProcessor.luaValues), std::begin(vars.sliders));

                channels = parser->nextSample(L, vars);
            } else if (currentShape < frame.size()) {
                auto& shape = frame[currentShape];
                double length = shape->length();
                double drawingProgress = length == 0.0 ? 1 : shapeDrawn / length;
                channels = shape->nextVector(drawingProgress);
            }
        }

        const float envValue = midiEnabled ? envState.advance(dt) : 1.0f;
        volumeBuffer.setSample(0, i, envValue);

        if (midiEnabled && envState.getStage() == DahdsrState::Stage::Done)
        {
            const int remainingSamples = numSamples - (i + 1);
            if (remainingSamples > 0)
            {
                const int startSample2 = i + 1;
                if (numChannels >= 1) juce::FloatVectorOperations::clear(voiceBuffer.getWritePointer(0) + startSample2, remainingSamples);
                if (numChannels >= 2) juce::FloatVectorOperations::clear(voiceBuffer.getWritePointer(1) + startSample2, remainingSamples);
                if (numChannels >= 3) juce::FloatVectorOperations::clear(voiceBuffer.getWritePointer(2) + startSample2, remainingSamples);
                juce::FloatVectorOperations::fill(frequencyBuffer.getWritePointer(0) + startSample2, (float) actualFrequency, remainingSamples);
                juce::FloatVectorOperations::clear(volumeBuffer.getWritePointer(0) + startSample2, remainingSamples);
            }
            noteStopped();
            break;
        }

        // NOTE: gain is applied AFTER effects (host-visible behavior).
        if (numChannels >= 3) {
            voiceBuffer.setSample(0, i, channels.x);
            voiceBuffer.setSample(1, i, channels.y);
            voiceBuffer.setSample(2, i, channels.z);
        } else if (numChannels == 2) {
            voiceBuffer.setSample(0, i, channels.x);
            voiceBuffer.setSample(1, i, channels.y);
        } else if (numChannels == 1) {
            voiceBuffer.setSample(0, i, channels.x);
        }

        // Fill frequency buffer with per-sample frequency
        frequencyBuffer.setSample(0, i, (float) actualFrequency);

        if (!renderingSample) {
            incrementShapeDrawing();
        }

        if (!renderingSample && frameDrawn >= frameLength) {
            double currentShapeLength = 0;
            if (currentShape < frame.size()) {
                currentShapeLength = frame[currentShape]->len;
            }
            if (sound.load() != nullptr && currentlyPlaying) {
                frameLength = sound.load()->updateFrame(frame);
            }
            double prevFrameLength = frameLength;
            frameDrawn -= prevFrameLength;
            currentShape = 0;
        }
    }

    if (voiceIndex >= 0 && voiceIndex < OscirenderAudioProcessor::kMaxUiVoices) {
        audioProcessor.uiVoiceActive[voiceIndex].store(isVoiceActive(), std::memory_order_relaxed);
        audioProcessor.uiVoiceEnvelopeTimeSeconds[voiceIndex].store(midiEnabled ? envState.getUiTimeSeconds() : 0.0, std::memory_order_relaxed);
    }

    audioProcessor.applyToggleableEffectsToBuffer(voiceBuffer, audioProcessor.getInputBuffer(), &volumeBuffer, &frequencyBuffer, &voiceEffectsMap, voicePreviewEffect);

    // Add processed samples to output buffer (apply envelope/velocity gain AFTER effects)
    for (int i = 0; i < numSamples; ++i) {
        const float gain = (float)velocity * volumeBuffer.getSample(0, i);
        int sample = startSample + i;
        if (numChannels >= 3) {
            outputBuffer.addSample(0, sample, voiceBuffer.getSample(0, i) * gain);
            outputBuffer.addSample(1, sample, voiceBuffer.getSample(1, i) * gain);
            outputBuffer.addSample(2, sample, voiceBuffer.getSample(2, i) * gain);
        } else if (numChannels == 2) {
            outputBuffer.addSample(0, sample, voiceBuffer.getSample(0, i) * gain);
            outputBuffer.addSample(1, sample, voiceBuffer.getSample(1, i) * gain);
        } else if (numChannels == 1) {
            outputBuffer.addSample(0, sample, voiceBuffer.getSample(0, i) * gain);
        }
    }
}

void ShapeVoice::stopNote(float velocity, bool allowTailOff) {
    if (!allowTailOff || !audioProcessor.midiEnabled->getBoolValue()) {
        currentlyPlaying = false;
        noteStopped();
        return;
    }

    envState.beginRelease();
}

void ShapeVoice::noteStopped() {
    currentlyPlaying = false;
    clearCurrentNote();
    sound = nullptr;

    if (voiceIndex >= 0 && voiceIndex < OscirenderAudioProcessor::kMaxUiVoices) {
        audioProcessor.uiVoiceActive[voiceIndex].store(false, std::memory_order_relaxed);
        audioProcessor.uiVoiceEnvelopeTimeSeconds[voiceIndex].store(0.0, std::memory_order_relaxed);
    }
}

void ShapeVoice::pitchWheelMoved(int newPitchWheelValue) {
    pitchWheelAdjustment = 1.0 + (newPitchWheelValue - 8192.0) / 65536.0;
}

void ShapeVoice::controllerMoved(int controllerNumber, int newControllerValue) {}

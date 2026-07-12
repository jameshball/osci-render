#include "ShapeVoice.h"
#include "VoiceManager.h"
#include "../../PluginProcessor.h"
#include "../../parser/FileParser.h"
#include "../AudioThreadGuard.h"

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
            const double sampleRate = audioProcessor.getEffectiveSampleRate();
            if (sampleRate > 0) {
                cloned->prepareToPlay(sampleRate, 512);
            }
            voiceEffectsMap[globalEffect->getId()] = cloned;
        }
    }
}

void ShapeVoice::setPreviewEffect(std::shared_ptr<osci::SimpleEffect> effect) {
    if (effect) {
        voicePreviewEffect = effect->cloneWithSharedParameters();
        // Initialize the effect with current sample rate
        const double sampleRate = audioProcessor.getEffectiveSampleRate();
        if (sampleRate > 0) {
            voicePreviewEffect->prepareToPlay(sampleRate, 512);
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

void ShapeVoice::startNote(int, float, juce::SynthesiserSound*, int) {
    // Not used — lifecycle is driven by VoiceManager via voiceActivated().
}

void ShapeVoice::captureDrawingState() {
    savedDrawingState.currentShape = currentShape;
    savedDrawingState.shapeDrawn = shapeDrawn;
    savedDrawingState.frameDrawn = frameDrawn;
}

void ShapeVoice::restoreDrawingState(const ShapeVoice* source) {
    if (source == nullptr || frame.empty()) return;

    // Restore from the source's SAVED snapshot (captured before kill).
    currentShape = source->savedDrawingState.currentShape % (int)frame.size();
    frameDrawn = source->savedDrawingState.frameDrawn;

    double length = currentShape < (int)frame.size() ? frame[currentShape]->len : 0.0;
    shapeDrawn = std::min(source->savedDrawingState.shapeDrawn, length);
}

void ShapeVoice::voiceActivated(const VoiceState& vs, bool isLegato) {
    this->velocity = vs.velocity;
    this->currentMidiNote = vs.midiNote;

    auto* shapeSound = audioProcessor.getActiveShapeSound();
    if (shapeSound == nullptr) return;

    currentlyPlaying = true;
    this->sound = shapeSound;

    if (voiceIndex >= 0 && voiceIndex < OscirenderAudioProcessor::kMaxUiVoices) {
        audioProcessor.uiVoiceActive[voiceIndex].store(true, std::memory_order_relaxed);
        audioProcessor.uiVoiceEnvelopeTimeSeconds[voiceIndex].store(0.0, std::memory_order_relaxed);
    }

    // Sync preview effect state
    auto cachedPreview = audioProcessor.getCachedPreviewEffect();
    if (cachedPreview) {
        setPreviewEffect(cachedPreview);
    } else {
        clearPreviewEffect();
    }

    auto* currentSound = this->sound.load();
    auto parser = currentSound != nullptr ? currentSound->parser : nullptr;
    renderingSample = parser != nullptr && parser->isSample();

    if (!isLegato) {
        // Non-legato: full reset — reload frame, reset drawing position,
        // retrigger envelopes.
        frame.clear();
        frameLength = 0.0;
        int tries = 0;
        while (frame.empty() && tries < 50) {
            if (shapeSound->updateFrame(frame)) {
                frameLength = shapeSound->getFrameLength();
            }
            tries++;
        }

        currentShape = 0;
        shapeDrawn = 0.0;
        frameDrawn = 0.0;
        pendingFrameStart = true;
        pendingNoteOn = true;

        dahdsr = audioProcessor.getCurrentDahdsrParams();
        envState.reset(dahdsr);
        for (int e = 1; e < NUM_ENVELOPES; ++e) {
            envDahdsr[e] = audioProcessor.getCurrentDahdsrParams(e);
            envStates[e].reset(envDahdsr[e]);
        }
    }
    // Legato: skip frame/envelope reset — only the frequency changes below.

    killFading = false;
    killFadeGain = 1.0f;

    if (audioProcessor.midiEnabled->getBoolValue()) {
        double newFreq = audioProcessor.noteToFrequency(vs.midiNote, vs.channel) + osci_audio::kMacFrequencyEpsilonHz;
#if OSCI_PREMIUM
        double glideTimeSec = audioProcessor.glideTime->getValueUnnormalised();

        // Determine glide source and whether to glide.

        double glideSource;
        bool hasDifferentSource;

        if (isLegato) {
            // Legato: glide from current pitch (the voice was already playing).
            glideSource = frequency;
            hasDifferentSource = (std::abs(glideSource - newFreq) > 0.01);
        } else if (vs.revoicing && vs.revoiceSourceFreq > 0.0) {
            // Re-voicing: glide from the dying voice's current pitch.
            glideSource = vs.revoiceSourceFreq;
            hasDifferentSource = (std::abs(glideSource - newFreq) > 0.01);
        } else {
            // Fresh noteOn: glide from the previously played note.
            glideSource = audioProcessor.noteToFrequency(static_cast<int>(vs.lastNote), vs.channel);
            hasDifferentSource = (static_cast<int>(vs.lastNote) != vs.midiNote);
        }

        bool shouldGlide = glideTimeSec > 0.0
                           && hasDifferentSource
                           && (audioProcessor.alwaysGlide->getBoolValue()
                               || isLegato
                               || vs.revoicing
                               || audioProcessor.getNumPressedNotes() > 1);

        if (shouldGlide) {
            glideSourceFreq = glideSource;
            glideTargetFreq = newFreq;
            glideElapsed = 0.0;
            glideDuration = glideTimeSec;
            if (audioProcessor.octaveScale->getBoolValue()) {
                double octaves = std::abs(std::log2(newFreq / glideSource));
                glideDuration *= octaves;
            }
            glideSlopePower = audioProcessor.glideSlope->getValueUnnormalised();
            glideActive = true;
        } else {
            frequency = newFreq;
            glideActive = false;
        }
#else
        frequency = newFreq;
        glideActive = false;
#endif
    }
}

void ShapeVoice::voiceDeactivated() {
    envState.beginRelease();
    for (int e = 1; e < NUM_ENVELOPES; ++e)
        envStates[e].beginRelease();
}

void ShapeVoice::voiceKilled() {
    // Start a kill fade instead of stopping instantly (avoids clicks).
    killFading = true;
    killFadeGain = 1.0f;
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
    AudioThreadGuard::ScopedAudioThread audioThreadGuard;

    // Early exit if voice is not currently playing
    if (!currentlyPlaying) {
        if (voiceIndex >= 0 && voiceIndex < OscirenderAudioProcessor::kMaxUiVoices) {
            audioProcessor.uiVoiceActive[voiceIndex].store(false, std::memory_order_relaxed);
            audioProcessor.uiVoiceEnvelopeTimeSeconds[voiceIndex].store(0.0, std::memory_order_relaxed);
            for (int e = 0; e < NUM_ENVELOPES; ++e) {
                audioProcessor.uiVoiceEnvActive[e][voiceIndex].store(false, std::memory_order_relaxed);
                audioProcessor.uiVoiceEnvTimeSeconds[e][voiceIndex].store(0.0, std::memory_order_relaxed);
                audioProcessor.uiVoiceEnvValue[e][voiceIndex].store(0.0f, std::memory_order_relaxed);
            }
        }
        return;
    }

    int numChannels = outputBuffer.getNumChannels();

    // Recompute pitch wheel adjustment using current bend range parameter
    pitchWheelMoved(rawPitchWheelValue);

    // Per-sample frequency animated buffer pointer for non-MIDI mode
    const float* freqAnimBuf = (!audioProcessor.midiEnabled->getBoolValue())
        ? audioProcessor.frequencyEffect->getAnimatedValuesReadPointer(0, numSamples) : nullptr;

    if (audioProcessor.midiEnabled->getBoolValue()) {
        // Glide is advanced per-sample below; set initial frequency here
        if (!glideActive) {
            actualFrequency = frequency * pitchWheelAdjustment;
        }
    } else {
        // Non-MIDI: initial frequency from animated buffer (first sample).
        // Per-sample updates happen inside the rendering loop below.
        actualFrequency = freqAnimBuf ? (double)freqAnimBuf[0] + 0.000001 : audioProcessor.frequencyEffect->getValue() + 0.000001;
    }

    // Prepare working buffers for effect processing
    voiceBuffer.setSize(numChannels, numSamples, false, false, true);
    voiceBuffer.clear();
    frequencyBuffer.setSize(1, numSamples, false, false, true);
    envelopeBuffer.setSize(1, numSamples, false, false, true);
    frameSyncBuffer.setSize(1, numSamples, false, false, true);
    frameSyncBuffer.clear();

    const bool midiEnabled = audioProcessor.midiEnabled->getBoolValue();
    const double sampleRate = audioProcessor.getEffectiveSampleRate();
    const double dt = 1.0 / sampleRate;

    // Snapshot DAW transport once per block (constant within a processBlock call)
    const auto& dawPosition = audioProcessor.dawPosition;
    const double blockBpm = dawPosition.bpm.load(std::memory_order_relaxed);
    const double blockPlayTime = dawPosition.seconds.load(std::memory_order_relaxed);
    const double blockPlayTimeBeats = dawPosition.beats.load(std::memory_order_relaxed);
    const bool blockIsPlaying = dawPosition.isPlaying.load(std::memory_order_relaxed);
    const int blockTimeSigNum = dawPosition.timeSigNumerator.load(std::memory_order_relaxed);
    const int blockTimeSigDen = dawPosition.timeSigDenominator.load(std::memory_order_relaxed);

    // Snapshot the sound pointer once per block.  The underlying ShapeSound
    // is ref-counted so it stays alive even if another thread swaps it out.
    auto* currentSound = sound.load();

    // If the producer flushed stale frames and pushed a fresh one, grab it
    // immediately instead of waiting until the current frame finishes.
    if (!renderingSample && currentSound != nullptr && currentlyPlaying && currentSound->consumeFreshFrame()) {
        if (currentSound->updateFrame(frame)) {
            frameLength = currentSound->getFrameLength();
            currentShape = 0;
            frameDrawn = 0;
            shapeDrawn = 0;
            pendingFrameStart = true;
        }
    }

    // First pass: generate raw audio samples (without gain) and fill frequency buffer + per-sample envelope
    for (int i = 0; i < numSamples; ++i) {
        if (pendingFrameStart) {
            frameSyncBuffer.setSample(0, i, 1.0f);
            pendingFrameStart = false;
        }

        // Advance glide (portamento) per sample
        if (glideActive && midiEnabled) {
            glideElapsed += dt;
            if (glideElapsed >= glideDuration) {
                frequency = glideTargetFreq;
                glideActive = false;
            } else {
                float t = osci_audio::powerScale(
                    (float)juce::jlimit(0.0, 1.0, glideElapsed / glideDuration),
                    glideSlopePower);
                // Glide in log-frequency space for perceptually uniform pitch transition
                double logSource = std::log(glideSourceFreq);
                double logTarget = std::log(glideTargetFreq);
                frequency = std::exp(logSource + t * (logTarget - logSource));
            }
            actualFrequency = frequency * pitchWheelAdjustment;
        }

        // Per-sample frequency update from animated buffer in non-MIDI mode
        if (freqAnimBuf) {
            actualFrequency = (double)freqAnimBuf[i] + 0.000001;
        }

        int sample = startSample + i;
        lengthIncrement = juce::jmax(frameLength / (sampleRate / actualFrequency), MIN_LENGTH_INCREMENT);

        osci::Point channels;

        if (currentSound != nullptr) {
            auto parser = currentSound->parser;

            if (renderingSample) {
                vars.sampleRate = sampleRate;
                vars.frequency = actualFrequency;
                vars.ext_x = 0;
                vars.ext_y = 0;

                // MIDI context
                vars.midiNote = currentMidiNote;
                vars.velocity = velocity;
                vars.voiceIndex = voiceIndex;
                vars.noteOn = (i == 0 && pendingNoteOn);

                // DAW transport (snapshotted once per block)
                vars.bpm = blockBpm;
                vars.playTime = blockPlayTime;
                vars.playTimeBeats = blockPlayTimeBeats;
                vars.isPlaying = blockIsPlaying;
                vars.timeSigNumerator = blockTimeSigNum;
                vars.timeSigDenominator = blockTimeSigDen;

                // Envelope
                vars.envelope = envState.getCurrentValue();
                vars.envelopeStage = static_cast<int>(envState.getStage());

                // Block-relative sample index for per-sample parameter reads
                vars.blockSampleIndex = i;
                
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
                // Read Lua slider values per-sample from animated buffers
                for (int s = 0; s < 26 && s < (int)audioProcessor.luaEffects.size(); ++s) {
                    vars.sliders[s] = audioProcessor.luaEffects[s]->getAnimatedValue(0, static_cast<size_t>(i));
                }

                channels = parser->nextSample(L, vars);
            } else if (currentShape < frame.size()) {
                auto& shape = frame[currentShape];
                double length = shape->length();
                double drawingProgress = length == 0.0 ? 1 : shapeDrawn / length;
                channels = shape->nextVector(drawingProgress);
            }
            if (pendingNoteOn) pendingNoteOn = false;
        }

        const float envValue = midiEnabled ? envState.advance(dt) : 1.0f;
        // Advance modulation envelopes 1..N (envelope 0 == envState)
        if (midiEnabled) {
            for (int e = 1; e < NUM_ENVELOPES; ++e)
                envStates[e].advance(dt);
        }
        envelopeBuffer.setSample(0, i, envValue);

        if (midiEnabled && envState.getStage() == DahdsrState::Stage::Done)
        {
            const int remainingSamples = numSamples - (i + 1);
            if (remainingSamples > 0)
            {
                const int startSample2 = i + 1;
                if (numChannels >= 1) juce::FloatVectorOperations::clear(voiceBuffer.getWritePointer(0) + startSample2, remainingSamples);
                if (numChannels >= 2) juce::FloatVectorOperations::clear(voiceBuffer.getWritePointer(1) + startSample2, remainingSamples);
                if (numChannels >= 3) juce::FloatVectorOperations::clear(voiceBuffer.getWritePointer(2) + startSample2, remainingSamples);
                // Fill colour channels with the "no colour" sentinel so the
                // tail samples are not interpreted as explicit black.
                if (numChannels >= 4) juce::FloatVectorOperations::fill(voiceBuffer.getWritePointer(3) + startSample2, -1.0f, remainingSamples);
                if (numChannels >= 5) juce::FloatVectorOperations::fill(voiceBuffer.getWritePointer(4) + startSample2, -1.0f, remainingSamples);
                if (numChannels >= 6) juce::FloatVectorOperations::fill(voiceBuffer.getWritePointer(5) + startSample2, -1.0f, remainingSamples);
                juce::FloatVectorOperations::fill(frequencyBuffer.getWritePointer(0) + startSample2, (float) actualFrequency, remainingSamples);
                juce::FloatVectorOperations::clear(envelopeBuffer.getWritePointer(0) + startSample2, remainingSamples);
            }
            noteStopped();
            break;
        }

        // NOTE: gain is applied AFTER effects (host-visible behavior).
        if (numChannels >= 1) voiceBuffer.setSample(0, i, channels.x);
        if (numChannels >= 2) voiceBuffer.setSample(1, i, channels.y);
        if (numChannels >= 3) voiceBuffer.setSample(2, i, channels.z);
        if (numChannels >= 4) voiceBuffer.setSample(3, i, channels.r);
        if (numChannels >= 5) voiceBuffer.setSample(4, i, channels.g);
        if (numChannels >= 6) voiceBuffer.setSample(5, i, channels.b);

        // Fill frequency buffer with per-sample frequency
        frequencyBuffer.setSample(0, i, (float) actualFrequency);

        if (!renderingSample) {
            incrementShapeDrawing();
        }

        if (!renderingSample && frameDrawn >= frameLength) {
            double prevFrameLength = frameLength;
            if (currentSound != nullptr && currentlyPlaying) {
                if (currentSound->updateFrame(frame)) {
                    frameLength = currentSound->getFrameLength();
                }
            }
            frameDrawn -= prevFrameLength;
            currentShape = 0;

            // The first sample of the new frame is the *next* sample.
            pendingFrameStart = true;
        }
    }

    if (voiceIndex >= 0 && voiceIndex < OscirenderAudioProcessor::kMaxUiVoices) {
        audioProcessor.uiVoiceActive[voiceIndex].store(currentlyPlaying, std::memory_order_relaxed);
        audioProcessor.uiVoiceEnvelopeTimeSeconds[voiceIndex].store(midiEnabled ? envState.getUiTimeSeconds() : 0.0, std::memory_order_relaxed);
        // Envelope 0 telemetry from envState (it IS envelope 0)
        audioProcessor.uiVoiceEnvActive[0][voiceIndex].store(currentlyPlaying, std::memory_order_relaxed);
        audioProcessor.uiVoiceEnvTimeSeconds[0][voiceIndex].store(midiEnabled ? envState.getUiTimeSeconds() : 0.0, std::memory_order_relaxed);
        audioProcessor.uiVoiceEnvValue[0][voiceIndex].store(midiEnabled ? envState.getCurrentValue() : 0.0f, std::memory_order_relaxed);
        // Envelopes 1..N telemetry from envStates
        for (int e = 1; e < NUM_ENVELOPES; ++e) {
            audioProcessor.uiVoiceEnvActive[e][voiceIndex].store(currentlyPlaying, std::memory_order_relaxed);
            audioProcessor.uiVoiceEnvTimeSeconds[e][voiceIndex].store(midiEnabled ? envStates[e].getUiTimeSeconds() : 0.0, std::memory_order_relaxed);
            audioProcessor.uiVoiceEnvValue[e][voiceIndex].store(midiEnabled ? envStates[e].getCurrentValue() : 0.0f, std::memory_order_relaxed);
        }
    }

    audioProcessor.applyToggleableEffectsToBuffer(voiceBuffer, audioProcessor.getInputBuffer(), &envelopeBuffer, &frequencyBuffer, &frameSyncBuffer, &voiceEffectsMap, voicePreviewEffect);

    // Add processed samples to output buffer (apply envelope/velocity gain AFTER effects)
    // Velocity tracking: at 0% velocity has no effect (gain=1), at 100% full velocity,
    // at -100% inverted velocity
    const float velTrack = audioProcessor.velocityTracking->getValueUnnormalised();
    const float velGain = 1.0f + velTrack * ((float)velocity - 1.0f);

    // Kill-fade: per-sample linear ramp from 1→0 over kKillFadeTimeSec.
    const float killFadeDecPerSample = killFading
        ? static_cast<float>(1.0 / (kKillFadeTimeSec * sampleRate))
        : 0.0f;

    for (int i = 0; i < numSamples; ++i) {
        // Apply kill-fade attenuation
        float killMul = 1.0f;
        if (killFading) {
            killMul = std::max(0.0f, killFadeGain);
            killFadeGain -= killFadeDecPerSample;
            if (killFadeGain <= 0.0f) {
                killFadeGain = 0.0f;
                killFading = false;
                currentlyPlaying = false;
                currentMidiNote = -1;
                // Zero remaining samples in voiceBuffer and break
                const int remaining = numSamples - (i + 1);
                if (remaining > 0) {
                    const int startIdx = i + 1;
                    for (int ch = 0; ch < numChannels; ++ch)
                        juce::FloatVectorOperations::clear(voiceBuffer.getWritePointer(ch) + startIdx, remaining);
                }
                noteStopped();
                break;
            }
        }

        float gain = velGain * envelopeBuffer.getSample(0, i) * killMul;

        int sample = startSample + i;
        // Spatial channels are scaled by gain
        if (numChannels >= 1) outputBuffer.addSample(0, sample, voiceBuffer.getSample(0, i) * gain);
        if (numChannels >= 2) outputBuffer.addSample(1, sample, voiceBuffer.getSample(1, i) * gain);
        if (numChannels >= 3) outputBuffer.addSample(2, sample, voiceBuffer.getSample(2, i) * gain);
        // Colour channels: override (not additive) — only write when the
        // voice carries an explicit colour (r >= 0).  When r < 0 the
        // sentinel means "no colour" and we leave the output buffer's
        // existing value (initialised to -1 by processBlock) intact.
        if (numChannels >= 4) {
            float voiceR = voiceBuffer.getSample(3, i);
            if (voiceR >= 0.0f) {
                outputBuffer.setSample(3, sample, voiceR);
                if (numChannels >= 5) outputBuffer.setSample(4, sample, voiceBuffer.getSample(4, i));
                if (numChannels >= 6) outputBuffer.setSample(5, sample, voiceBuffer.getSample(5, i));
            }
        }
    }
}

void ShapeVoice::stopNote(float velocity, bool allowTailOff) {
    // stopNote is not called by VoiceManager directly, but handle
    // gracefully in case it's triggered via residual JUCE infrastructure.
    if (!allowTailOff || !audioProcessor.midiEnabled->getBoolValue()) {
        currentlyPlaying = false;
        noteStopped();
        return;
    }

    envState.beginRelease();
    for (int e = 1; e < NUM_ENVELOPES; ++e)
        envStates[e].beginRelease();
}

void ShapeVoice::noteStopped() {
    if (sound == nullptr && !currentlyPlaying)
        return;

    currentlyPlaying = false;
    currentMidiNote = -1;
    sound = nullptr;

    if (voiceIndex >= 0 && voiceIndex < OscirenderAudioProcessor::kMaxUiVoices) {
        audioProcessor.uiVoiceActive[voiceIndex].store(false, std::memory_order_relaxed);
        audioProcessor.uiVoiceEnvelopeTimeSeconds[voiceIndex].store(0.0, std::memory_order_relaxed);
        for (int e = 0; e < NUM_ENVELOPES; ++e) {
            audioProcessor.uiVoiceEnvActive[e][voiceIndex].store(false, std::memory_order_relaxed);
            audioProcessor.uiVoiceEnvTimeSeconds[e][voiceIndex].store(0.0, std::memory_order_relaxed);
            audioProcessor.uiVoiceEnvValue[e][voiceIndex].store(0.0f, std::memory_order_relaxed);
        }
    }
}

void ShapeVoice::pitchWheelMoved(int newPitchWheelValue) {
    rawPitchWheelValue = newPitchWheelValue;
#if OSCI_PREMIUM
    int bendSemitones = audioProcessor.pitchBendRange->getValueUnnormalised();
#else
    int bendSemitones = 2; // Free version: fixed 2-semitone bend range
#endif
    double bendNorm = (newPitchWheelValue - 8192.0) / 8192.0; // -1..+1
    double bendInSemitones = bendNorm * bendSemitones;
    pitchWheelAdjustment = std::pow(2.0, bendInSemitones / 12.0);
}

void ShapeVoice::controllerMoved(int controllerNumber, int newControllerValue) {}

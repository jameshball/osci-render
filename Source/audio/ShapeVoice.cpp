#include "ShapeVoice.h"
#include "../PluginProcessor.h"

ShapeVoice::ShapeVoice(OscirenderAudioProcessor& p, juce::AudioSampleBuffer& externalAudio) : audioProcessor(p), externalAudio(externalAudio) {}

bool ShapeVoice::canPlaySound(juce::SynthesiserSound* sound) {
    return dynamic_cast<ShapeSound*> (sound) != nullptr;
}

void ShapeVoice::startNote(int midiNoteNumber, float velocity, juce::SynthesiserSound* sound, int currentPitchWheelPosition) {
    this->velocity = velocity;
    pitchWheelMoved(currentPitchWheelPosition);
    auto* shapeSound = dynamic_cast<ShapeSound*>(sound);

    currentlyPlaying = true;
    this->sound = shapeSound;

    auto parser = this->sound.load()->parser;
    renderingSample = parser != nullptr && parser->isSample();

    if (shapeSound != nullptr) {
        int tries = 0;
        while (frame.empty() && tries < 50) {
            frameLength = shapeSound->updateFrame(frame);
            tries++;
        }
        adsr = audioProcessor.adsrEnv;
        time = 0.0;
        releaseTime = 0.0;
        endTime = 0.0;
        waitingForRelease = true;
        std::vector<double> times = adsr.getTimes();
        for (int i = 0; i < times.size(); i++) {
            if (i < adsr.getReleaseNode()) {
                releaseTime += times[i];
            }
            endTime += times[i];
        }
        if (audioProcessor.midiEnabled->getBoolValue()) {
            frequency = juce::MidiMessage::getMidiNoteInHertz(midiNoteNumber);
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

    int numChannels = outputBuffer.getNumChannels();

    if (audioProcessor.midiEnabled->getBoolValue()) {
        actualFrequency = frequency * pitchWheelAdjustment;
    } else {
        actualFrequency = audioProcessor.frequency.load();
    }

    for (auto sample = startSample; sample < startSample + numSamples; ++sample) {
        lengthIncrement = juce::jmax(frameLength / (audioProcessor.currentSampleRate / actualFrequency), MIN_LENGTH_INCREMENT);

        osci::Point channels;
        double x = 0.0;
        double y = 0.0;
        double z = 0.0;

        if (sound.load() != nullptr) {
            auto parser = sound.load()->parser;

            if (renderingSample) {
                vars.sampleRate = audioProcessor.currentSampleRate;
                vars.frequency = actualFrequency;
                vars.ext_x = 0;
                vars.ext_y = 0;
                
                if (externalAudio.getNumSamples() >= 1) {
                    double sampleIndex = sample % externalAudio.getNumSamples();
                    int numChannels = externalAudio.getNumChannels();
                    if (numChannels >= 1) {
                        vars.ext_x = externalAudio.getSample(0, sampleIndex);
                    }
                    if (numChannels >= 2) {
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

        x = channels.x;
        y = channels.y;
        z = channels.z;

        time += 1.0 / audioProcessor.currentSampleRate;

        if (waitingForRelease) {
            time = juce::jmin(time, releaseTime);
        } else if (time >= endTime) {
            noteStopped();
            break;
        }

        double gain = audioProcessor.midiEnabled->getBoolValue() ? adsr.lookup(time) : 1.0;
        gain *= velocity;

        if (numChannels >= 3) {
            outputBuffer.addSample(0, sample, x * gain);
            outputBuffer.addSample(1, sample, y * gain);
            outputBuffer.addSample(2, sample, z * gain);
        } else if (numChannels == 2) {
            outputBuffer.addSample(0, sample, x * gain);
            outputBuffer.addSample(1, sample, y * gain);
        } else if (numChannels == 1) {
            outputBuffer.addSample(0, sample, x * gain);
        }

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
}

void ShapeVoice::stopNote(float velocity, bool allowTailOff) {
    currentlyPlaying = false;
    waitingForRelease = false;
    if (!allowTailOff) {
        noteStopped();
    }
}

void ShapeVoice::noteStopped() {
    clearCurrentNote();
    sound = nullptr;
}

void ShapeVoice::pitchWheelMoved(int newPitchWheelValue) {
    pitchWheelAdjustment = 1.0 + (newPitchWheelValue - 8192.0) / 65536.0;
}

void ShapeVoice::controllerMoved(int controllerNumber, int newControllerValue) {}

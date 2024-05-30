#include "ShapeVoice.h"
#include "../PluginProcessor.h"

ShapeVoice::ShapeVoice(OscirenderAudioProcessor& p) : audioProcessor(p) {
    actualTraceMin = audioProcessor.traceMin->getValue();
    actualTraceLen = audioProcessor.traceLen->getValue();
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
    if (shapeSound != nullptr) {
        int tries = 0;
        flushCount = 5;
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
    double length = frame[(currentShape % frame.size() + frame.size()) % frame.size()]->len;
    frameDrawn += lengthIncrement;
    shapeDrawn += lengthIncrement;

    // Need to skip all shapes that the lengthIncrement draws over.
    // This is especially an issue when there are lots of small lines being
    // drawn.
    while (shapeDrawn > length) {
        shapeDrawn -= length;
        currentShape++;
        // POTENTIAL TODO: Think of a way to make this more efficient when iterating
        // this loop many times
        length = frame[(currentShape % frame.size() + frame.size()) % frame.size()]->len;
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
    }
}

void ShapeVoice::renderNextBlock(juce::AudioSampleBuffer& outputBuffer, int startSample, int numSamples) {
    juce::ScopedNoDenormals noDenormals;

    int numChannels = outputBuffer.getNumChannels();

    // this is for debugging, it gets optimized out when building for release
    if (currentlyPlaying) {
        numChannels = numChannels + 0;
    }

    if (audioProcessor.midiEnabled->getBoolValue()) {
        actualFrequency = frequency * pitchWheelAdjustment;
    } else {
        actualFrequency = audioProcessor.frequency;
    }

    for (auto sample = startSample; sample < startSample + numSamples; ++sample) {
        bool traceMinEnabled = audioProcessor.traceMin->enabled->getBoolValue();
        bool traceLenEnabled = audioProcessor.traceLen->enabled->getBoolValue();

        // update length increment
        double traceMin = traceMinEnabled ? actualTraceMin : 0.0;
        double traceLen = traceLenEnabled ? actualTraceLen : 1.0;
        double proportionalLength = (traceLen) * frameLength;
        lengthIncrement = juce::jmax(proportionalLength / (audioProcessor.currentSampleRate / actualFrequency), MIN_LENGTH_INCREMENT);

        Point channels;
        double x = 0.0;
        double y = 0.0;
        double z = 0.0;

        bool renderingSample = true;
        auto s = sound.load();

        if (s != nullptr) {
            auto parser = s->parser;
            renderingSample = (parser != nullptr) && parser->isSample();
        }

        if (!renderingSample) {
            incrementShapeDrawing();
        }

        double drawnFrameLength = (traceMin + traceLen) * frameLength;

        if (!renderingSample && frameDrawn >= drawnFrameLength) {
            if (s != nullptr && currentlyPlaying) {
                // Flush the frame buffer if stale or if just starting a note, otherwise just pull from it
                if (flushCount > 0 || s->checkStale()) {
                    frameLength = s->flushFrame(frame);
                    flushCount--;
                }
                else frameLength = s->updateFrame(frame);
            }
            //frameDrawn -= drawnFrameLength;
            frameDrawn = 0;
            shapeDrawn = 0;
            currentShape = 0;

            // TODO: updateFrame already iterates over all the shapes,
            // so we can improve performance by calculating frameDrawn
            // and shapeDrawn directly. frameDrawn is simply actualTraceMin * frameLength
            // but shapeDrawn is the amount of the current shape that has been drawn so
            // we need to iterate over all the shapes to calculate it.
            float targ = traceMin * frameLength;
            while (frameDrawn < targ) {
                incrementShapeDrawing();
            }
        }

        if (s != nullptr) {
            auto parser = s->parser;
            if (renderingSample) {
                vars.sampleRate = audioProcessor.currentSampleRate;
                vars.frequency = actualFrequency;
                std::copy(std::begin(audioProcessor.luaValues), std::end(audioProcessor.luaValues), std::begin(vars.sliders));

                channels = parser->nextSample(L, vars);
            } else {
                auto cs = (currentShape % frame.size() + frame.size()) % frame.size();
                auto& shape = frame[cs];
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

        double traceMinValue = audioProcessor.traceMin->getActualValue();
        double traceLenValue = audioProcessor.traceLen->getActualValue();
        traceMinValue = traceMinEnabled ? traceMinValue : 0.0;
        traceLenValue = traceLenEnabled ? traceLenValue : 1.0;
        actualTraceMin = juce::jmax(MIN_TRACE, traceMinValue);
        actualTraceLen = traceLenValue;
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

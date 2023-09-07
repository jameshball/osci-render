#include "ShapeVoice.h"
#include "../PluginProcessor.h"

ShapeVoice::ShapeVoice(OscirenderAudioProcessor& p) : audioProcessor(p) {
    actualTraceMin = audioProcessor.traceMin->getValue();
    actualTraceMax = audioProcessor.traceMax->getValue();
}

bool ShapeVoice::canPlaySound(juce::SynthesiserSound* sound) {
    return dynamic_cast<ShapeSound*> (sound) != nullptr;
}

void ShapeVoice::startNote(int midiNoteNumber, float velocity, juce::SynthesiserSound* sound, int currentPitchWheelPosition) {
    auto* shapeSound = dynamic_cast<ShapeSound*>(sound);

    currentlyPlaying = true;
    this->sound = shapeSound;
    if (shapeSound != nullptr) {
        int tries = 0;
        while (frame.empty() && tries < 50) {
            frameLength = shapeSound->updateFrame(frame);
            tries++;
        }
        tailOff = 0.0;
        if (audioProcessor.midiEnabled->getBoolValue()) {
            frequency = juce::MidiMessage::getMidiNoteInHertz(midiNoteNumber);
        }
    }
}

// TODO this is the slowest part of the program - any way to improve this would help!
void ShapeVoice::incrementShapeDrawing() {
    double length = currentShape < frame.size() ? frame[currentShape]->len : 0.0;
    // hard cap on how many times it can be over the length to
    // prevent audio stuttering
    auto increment = juce::jmin(lengthIncrement, 20 * length);
    frameDrawn += increment;
    shapeDrawn += increment;

    // Need to skip all shapes that the lengthIncrement draws over.
    // This is especially an issue when there are lots of small lines being
    // drawn.
    while (shapeDrawn > length) {
        shapeDrawn -= length;
        currentShape++;
        if (currentShape >= frame.size()) {
            currentShape = 0;
            break;
        }
        // POTENTIAL TODO: Think of a way to make this more efficient when iterating
        // this loop many times
        length = frame[currentShape]->len;
    }
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
    
    if (!audioProcessor.midiEnabled->getBoolValue()) {
        frequency = audioProcessor.frequency;
    }

    for (auto sample = startSample; sample < startSample + numSamples; ++sample) {
        bool traceMinEnabled = audioProcessor.traceMin->enabled->getBoolValue();
        bool traceMaxEnabled = audioProcessor.traceMax->enabled->getBoolValue();

        // update length increment
        double traceMax = traceMaxEnabled ? actualTraceMax : 1.0;
        double traceMin = traceMinEnabled ? actualTraceMin : 0.0;
        double proportionalLength = (traceMax - traceMin) * frameLength;
        // double frequency = audioProcessor.frequencyEffect->getActualValue();
        lengthIncrement = juce::jmax(proportionalLength / (audioProcessor.currentSampleRate / frequency), MIN_LENGTH_INCREMENT);

        Vector2 channels;
        double x = 0.0;
        double y = 0.0;

        bool renderingSample = true;

        if (sound.load() != nullptr) {
            renderingSample = sound.load()->parser->isSample();

            if (renderingSample) {
                channels = sound.load()->parser->nextSample();
            } else if (currentShape < frame.size()) {
                auto& shape = frame[currentShape];
                double length = shape->length();
                double drawingProgress = length == 0.0 ? 1 : shapeDrawn / length;
                channels = shape->nextVector(drawingProgress);
            }
        }

        x = channels.x;
        y = channels.y;

        if (tailOff > 0.0) {
            tailOff *= 0.99999;

            if (tailOff < 0.005) {
                clearCurrentNote();
                sound = nullptr;
                break;
            }
        }

        double gain = tailOff == 0.0 ? 1.0 : tailOff;

        if (numChannels >= 2) {
            outputBuffer.addSample(0, sample, x * gain);
            outputBuffer.addSample(1, sample, y * gain);
        } else if (numChannels == 1) {
            outputBuffer.addSample(0, sample, x * gain);
        }

        double traceMinValue = audioProcessor.traceMin->getActualValue();
        double traceMaxValue = audioProcessor.traceMax->getActualValue();
        actualTraceMax = juce::jmax(actualTraceMin + MIN_TRACE, juce::jmin(traceMaxValue, 1.0));
        actualTraceMin = juce::jmax(MIN_TRACE, juce::jmin(traceMinValue, actualTraceMax - MIN_TRACE));

        if (!renderingSample) {
            incrementShapeDrawing();
        }

        double drawnFrameLength = traceMaxEnabled ? actualTraceMax * frameLength : frameLength;

        if (!renderingSample && frameDrawn >= drawnFrameLength) {
            if (sound.load() != nullptr) {
                frameLength = sound.load()->updateFrame(frame);
            }
            // TODO: updateFrame already iterates over all the shapes,
            // so we can improve performance by calculating frameDrawn
            // and shapeDrawn directly. frameDrawn is simply actualTraceMin * frameLength
            // but shapeDrawn is the amount of the current shape that has been drawn so
            // we need to iterate over all the shapes to calculate it.
            if (traceMinEnabled) {
                while (frameDrawn < actualTraceMin * frameLength) {
                    incrementShapeDrawing();
                }
            }
        }
    }
}

void ShapeVoice::stopNote(float velocity, bool allowTailOff) {
    currentlyPlaying = false;
    if (allowTailOff) {
        if (tailOff == 0.0) {
            tailOff = 1.0;
        }
    } else {
        clearCurrentNote();
        sound = nullptr;
    }
}

void ShapeVoice::pitchWheelMoved(int newPitchWheelValue) {}

void ShapeVoice::controllerMoved(int controllerNumber, int newControllerValue) {}

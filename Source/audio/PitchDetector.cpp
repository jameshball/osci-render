#include "PitchDetector.h"
#include "../PluginProcessor.h"

PitchDetector::PitchDetector(OscirenderAudioProcessor& audioProcessor) : juce::Thread("PitchDetector"), audioProcessor(audioProcessor) {
    consumer = audioProcessor.consumerRegister(fftSize);
    startThread();
}

PitchDetector::~PitchDetector() {
    {
        juce::CriticalSection::ScopedLockType scope(consumerLock);
        audioProcessor.consumerStop(consumer);
    }
    stopThread(1000);
}

void PitchDetector::run() {
	while (!threadShouldExit()) {
        audioProcessor.consumerRead(consumer);

        // buffer is for 2 channels, so we need to only use one
        for (int i = 0; i < fftSize; i++) {
            fftData[i] = consumer->getFullBuffer()[i].x;
        }

        forwardFFT.performFrequencyOnlyForwardTransform(fftData.data());

        // get frequency of the peak
        int maxIndex = 0;
        for (int i = 0; i < fftSize / 2; ++i) {
            if (frequencyFromIndex(i) < 20 || frequencyFromIndex(i) > 20000) {
                continue;
            }

            auto current = fftData[i];
            if (current > fftData[maxIndex]) {
                maxIndex = i;
            }
        }

        frequency = frequencyFromIndex(maxIndex);
        triggerAsyncUpdate();
	}
}

void PitchDetector::handleAsyncUpdate() {
    juce::SpinLock::ScopedLockType scope(lock);
    for (auto& callback : callbacks) {
        callback(frequency);
    }
}

int PitchDetector::addCallback(std::function<void(float)> callback) {
    juce::SpinLock::ScopedLockType scope(lock);
    callbacks.push_back(callback);
    return callbacks.size() - 1;
}

void PitchDetector::removeCallback(int index) {
    juce::SpinLock::ScopedLockType scope(lock);
    callbacks.erase(callbacks.begin() + index);
}

void PitchDetector::setSampleRate(float sampleRate) {
    this->sampleRate = sampleRate;
}

float PitchDetector::frequencyFromIndex(int index) {
    auto binWidth = sampleRate / fftSize;
    return index * binWidth;
}

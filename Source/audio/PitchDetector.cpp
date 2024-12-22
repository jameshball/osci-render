#include "PitchDetector.h"
#include "../PluginProcessor.h"

PitchDetector::PitchDetector(OscirenderAudioProcessor& audioProcessor) : AudioBackgroundThread("PitchDetector", audioProcessor.threadManager), audioProcessor(audioProcessor) {}

void PitchDetector::runTask(const std::vector<OsciPoint>& points) {
    // buffer is for 2 channels, so we need to only use one
    for (int i = 0; i < fftSize; i++) {
        fftData[i] = points[i].x;
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

int PitchDetector::prepareTask(double sampleRate, int samplesPerBlock) {
    this->sampleRate = sampleRate;
    return fftSize;
}

void PitchDetector::stopTask() {}

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

float PitchDetector::frequencyFromIndex(int index) {
    auto binWidth = sampleRate / fftSize;
    return index * binWidth;
}

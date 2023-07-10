#include "PitchDetector.h"
#include "PitchDetector.h"

PitchDetector::PitchDetector(OscirenderAudioProcessor& p, std::function<void(float)> frequencyCallback) : juce::Thread("PitchDetector"), audioProcessor(p), frequencyCallback(frequencyCallback) {
    startThread();
}

PitchDetector::~PitchDetector() {
    audioProcessor.audioProducer.unregisterConsumer(consumer);
    stopThread(1000);
}

void PitchDetector::run() {
    audioProcessor.audioProducer.registerConsumer(consumer);

	while (!threadShouldExit()) {
		auto buffer = consumer->startProcessing();

        // buffer is for 2 channels, so we need to only use one
        for (int i = 0; i < fftSize; i++) {
            fftData[i] = buffer->at(2 * i);
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

		consumer->finishedProcessing();
        triggerAsyncUpdate();
	}
}

void PitchDetector::handleAsyncUpdate() {
    frequencyCallback(frequency);
}

float PitchDetector::frequencyFromIndex(int index) {
    auto binWidth = audioProcessor.currentSampleRate / fftSize;
    return index * binWidth;
}

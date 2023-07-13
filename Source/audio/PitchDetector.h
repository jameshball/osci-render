#pragma once
#include <JuceHeader.h>
#include "../concurrency/BufferConsumer.h"
#include "../concurrency/BufferProducer.h"


class PitchDetector : public juce::Thread, public juce::AsyncUpdater {
public:
	PitchDetector(BufferProducer& producer);
	~PitchDetector();

    void run() override;
    void handleAsyncUpdate() override;
    int addCallback(std::function<void(float)> callback);
    void removeCallback(int index);
    void setSampleRate(float sampleRate);

    std::atomic<float> frequency = 0.0f;

private:
	static constexpr int fftOrder = 15;
	static constexpr int fftSize = 1 << fftOrder;

    std::shared_ptr<BufferConsumer> consumer = std::make_shared<BufferConsumer>(fftSize);
    juce::dsp::FFT forwardFFT{fftOrder};
    std::array<float, fftSize * 2> fftData;
    BufferProducer& producer;
    std::vector<std::function<void(float)>> callbacks;
    juce::SpinLock lock;
    float sampleRate = 192000.0f;

    float frequencyFromIndex(int index);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PitchDetector)
};
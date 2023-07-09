#pragma once
#include <JuceHeader.h>
#include "../concurrency/BufferConsumer.h"
#include "../PluginProcessor.h"

class PitchDetector : public juce::Thread, public juce::AsyncUpdater {
public:
	PitchDetector(OscirenderAudioProcessor& p, std::function<void(float)> frequencyCallback);
	~PitchDetector();

    void run() override;
    void handleAsyncUpdate() override;

    std::atomic<float> frequency = 0.0f;

private:
	static constexpr int fftOrder = 15;
	static constexpr int fftSize = 1 << fftOrder;

    std::shared_ptr<BufferConsumer> consumer = std::make_shared<BufferConsumer>(fftSize);
    juce::dsp::FFT forwardFFT{fftOrder};
    std::array<float, fftSize * 2> fftData;
    OscirenderAudioProcessor& audioProcessor;
    std::function<void(float)> frequencyCallback;

    float frequencyFromIndex(int index);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PitchDetector)
};
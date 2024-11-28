#pragma once
#include <JuceHeader.h>
#include "../concurrency/AudioBackgroundThread.h"

class OscirenderAudioProcessor;
class PitchDetector : public AudioBackgroundThread, public juce::AsyncUpdater {
public:
	PitchDetector(OscirenderAudioProcessor& audioProcessor);

    int prepareTask(double sampleRate, int samplesPerBlock) override;
    void runTask(const std::vector<OsciPoint>& points) override;
    void stopTask() override;
    void handleAsyncUpdate() override;
    int addCallback(std::function<void(float)> callback);
    void removeCallback(int index);

    std::atomic<float> frequency = 0.0f;

private:
	static constexpr int fftOrder = 15;
	static constexpr int fftSize = 1 << fftOrder;

    juce::dsp::FFT forwardFFT{fftOrder};
    std::array<float, fftSize * 2> fftData;
    OscirenderAudioProcessor& audioProcessor;
    std::vector<std::function<void(float)>> callbacks;
    juce::SpinLock lock;
    float sampleRate = 192000.0f;

    float frequencyFromIndex(int index);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PitchDetector)
};

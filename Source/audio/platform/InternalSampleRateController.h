#pragma once

#include <JuceHeader.h>

class InternalSampleRateController
{
public:
    struct PreparedSpec
    {
        double sampleRate = 0.0;
        int blockSize = 0;
        int latencySamples = 0;
    };

    static constexpr const char* settingKey = "internalSampleRateRatio";

    void restoreSavedRatio (double savedRatio) noexcept
    {
        ratio.store (osci::IntegerRatioSampleRateAdapter::isRatioSupported (savedRatio)
            ? osci::IntegerRatioSampleRateAdapter::normaliseRatio (savedRatio)
            : 1.0);
    }

    [[nodiscard]] double getRatio() const noexcept { return ratio.load(); }
    [[nodiscard]] double getLastDeviceSampleRate() const noexcept { return lastDeviceSampleRate; }
    [[nodiscard]] int getLastDeviceBlockSize() const noexcept { return lastDeviceBlockSize; }
    [[nodiscard]] bool hasPreparedDevice() const noexcept { return lastDeviceSampleRate > 0.0 && lastDeviceBlockSize > 0; }

    [[nodiscard]] bool canSetRatio (double newRatio) const noexcept
    {
        if (! osci::IntegerRatioSampleRateAdapter::isRatioSupported (newRatio))
            return false;

        newRatio = osci::IntegerRatioSampleRateAdapter::normaliseRatio (newRatio);
        return osci::IntegerRatioSampleRateAdapter::isRatioAllowed (lastDeviceSampleRate, newRatio);
    }

    PreparedSpec prepare (double deviceSampleRate, int maxDeviceBlockSize, int numChannels, bool enabled)
    {
        lastDeviceSampleRate = deviceSampleRate;
        lastDeviceBlockSize = maxDeviceBlockSize;

        const auto requestedRatio = enabled ? ratio.load() : 1.0;
        const auto effectiveRatio = enabled && osci::IntegerRatioSampleRateAdapter::isRatioAllowed (deviceSampleRate, requestedRatio)
            ? osci::IntegerRatioSampleRateAdapter::normaliseRatio (requestedRatio)
            : 1.0;

        ratio.store (effectiveRatio);
        adapter.prepare ({ deviceSampleRate, effectiveRatio, maxDeviceBlockSize, numChannels });

        return {
            adapter.getProcessingSampleRate(),
            adapter.getMaxProcessingBlockSize(),
            adapter.getLatencySamples()
        };
    }

    bool setRatio (double newRatio) noexcept
    {
        if (! canSetRatio (newRatio))
            return false;

        newRatio = osci::IntegerRatioSampleRateAdapter::normaliseRatio (newRatio);
        if (std::abs (newRatio - ratio.load()) < 0.000001)
            return false;

        ratio.store (newRatio);
        return true;
    }

    template <typename ProcessInternal>
    osci::IntegerRatioSampleRateAdapter::ProcessResult process (juce::AudioBuffer<float>& buffer,
                                                                juce::MidiBuffer& midi,
                                                                ProcessInternal&& processInternal) noexcept
    {
        if (adapter.isActive())
            return adapter.process (buffer, midi, std::forward<ProcessInternal> (processInternal));

        osci::IntegerRatioSampleRateAdapter::ProcessResult result;
        processInternal (buffer, midi);
        result.internalSamplesProcessed = buffer.getNumSamples();
        return result;
    }

private:
    std::atomic<double> ratio { 1.0 };
    double lastDeviceSampleRate = 0.0;
    int lastDeviceBlockSize = 0;
    osci::IntegerRatioSampleRateAdapter adapter;
};

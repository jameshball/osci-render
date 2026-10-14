#pragma once

#include <JuceHeader.h>

#include <cmath>
#include <memory>
#include <vector>

// Runs a processor at a rate of its own choosing (an osci-motion project's),
// whatever the audio device runs at. What it makes is resampled down to the
// device's rate times the largest power of two at or under it, then filtered
// down to the device by JUCE's oversampler; a rate that is such a multiple
// exactly needs only the second step. Content the first step folds over lands
// above the device's band, where the second step removes it. The processor
// generates its signal (osci-motion draws it): its input is silence.
class ProjectRateAdapter {
public:
    static constexpr double lowestRate = 22050.0, highestRate = 768000.0;
    static constexpr int largestFactor = 16;

    void prepare(double deviceRate, double rate, int maxDeviceBlock, int channels) {
        deviceSampleRate = deviceRate;
        processingSampleRate = juce::jlimit(lowestRate, highestRate, rate);
        numChannels = juce::jmax(1, channels);
        maxDeviceBlockSize = juce::jmax(1, maxDeviceBlock);
        factor = 1;
        while (factor < largestFactor && deviceRate * factor * 2 <= processingSampleRate * (1 + 1.0e-9)) { factor *= 2; }
        // Below the device's rate the signal is resampled up, as it is down.
        speed = deviceRate > 0 ? processingSampleRate / (deviceRate * factor) : 1.0;
        exact = std::abs(speed - 1.0) < 1.0e-9;
        if (factor > 1) {
            oversampler = std::make_unique<juce::dsp::Oversampling<float>>((size_t) numChannels, (size_t) stagesOf(factor),
                juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, false, true);
            oversampler->initProcessing((size_t) maxDeviceBlockSize);
            latencySamples = (int) std::ceil(oversampler->getLatencyInSamples());
        } else {
            oversampler.reset();
            latencySamples = 0;
        }
        const auto intermediate = maxDeviceBlockSize * factor;
        maxProcessingBlockSize = (int) std::ceil(intermediate * juce::jmax(1.0, speed)) + guard;
        pending.setSize(numChannels, 2 * maxProcessingBlockSize + guard, false, true, false);
        made.setSize(numChannels, maxProcessingBlockSize, false, true, false);
        direct.setSize(numChannels, intermediate, false, true, false);
        interpolators = std::make_unique<juce::LagrangeInterpolator[]>((size_t) numChannels);
        internalMidi.ensureSize(4096);
        reset();
    }
    void reset() noexcept {
        if (oversampler != nullptr) { oversampler->reset(); }
        for (int channel = 0; interpolators != nullptr && channel < numChannels; ++channel) { interpolators[(size_t) channel].reset(); }
        pendingCount = 0;
        pending.clear();
        internalMidi.clear();
    }

    double getProcessingSampleRate() const noexcept { return processingSampleRate; }
    int getMaxProcessingBlockSize() const noexcept { return maxProcessingBlockSize; }
    int getLatencySamples() const noexcept { return latencySamples; }

    // Realtime: makes what the device block needs at the processor's rate,
    // in blocks of up to its largest, and brings it down to the device.
    template <typename ProcessInternal>
    int process(juce::AudioBuffer<float>& deviceBuffer, juce::MidiBuffer& deviceMidi, ProcessInternal&& processInternal) noexcept {
        const auto deviceSamples = deviceBuffer.getNumSamples();
        if (deviceSamples <= 0 || deviceSamples > maxDeviceBlockSize || deviceBuffer.getNumChannels() > numChannels) {
            deviceBuffer.clear();
            deviceMidi.clear();
            return 0;
        }
        const auto wanted = deviceSamples * factor;
        // Enough made for the resampler, which looks a few samples ahead.
        const auto needed = exact ? wanted : (int) std::ceil(wanted * speed) + lookAhead;
        internalMidi.clear();
        internalMidi.addEvents(deviceMidi, 0, -1, 0);
        deviceMidi.clear();
        int processed = 0;
        while (pendingCount < needed) {
            const auto count = juce::jmin(maxProcessingBlockSize, needed - pendingCount);
            juce::AudioBuffer<float> block(made.getArrayOfWritePointers(), numChannels, count);
            block.clear();
            processInternal(block, internalMidi);
            internalMidi.clear();
            for (int channel = 0; channel < numChannels; ++channel) { pending.copyFrom(channel, pendingCount, block, channel, 0, count); }
            pendingCount += count;
            processed += count;
        }
        // The block at the device's rate times `factor`: the oversampler's
        // own, when there is one, to filter down from.
        juce::dsp::AudioBlock<float> deviceBlock {deviceBuffer};
        juce::dsp::AudioBlock<float> intermediate = factor > 1 ? oversampler->processSamplesUp(deviceBlock) : juce::dsp::AudioBlock<float>(direct).getSubBlock(0, (size_t) wanted);
        int used = wanted;
        for (int channel = 0; channel < numChannels; ++channel) {
            auto* out = intermediate.getChannelPointer((size_t) channel);
            if (exact) {
                juce::FloatVectorOperations::copy(out, pending.getReadPointer(channel), wanted);
            } else {
                used = interpolators[(size_t) channel].process(speed, pending.getReadPointer(channel), out, wanted);
            }
        }
        // What was used goes; the rest waits for the next block.
        used = juce::jlimit(0, pendingCount, used);
        for (int channel = 0; channel < numChannels; ++channel) {
            auto* data = pending.getWritePointer(channel);
            std::memmove(data, data + used, sizeof(float) * (size_t) (pendingCount - used));
        }
        pendingCount -= used;
        if (factor > 1) {
            oversampler->processSamplesDown(deviceBlock);
        } else {
            for (int channel = 0; channel < deviceBuffer.getNumChannels(); ++channel) { deviceBuffer.copyFrom(channel, 0, direct, channel, 0, deviceSamples); }
        }
        return processed;
    }

private:
    static int stagesOf(int value) noexcept {
        int stages = 0;
        for (; value > 1; value >>= 1) { ++stages; }
        return stages;
    }
    static constexpr int lookAhead = 6, guard = 16;

    double deviceSampleRate = 0, processingSampleRate = 0, speed = 1;
    bool exact = true;
    int factor = 1, numChannels = 1, maxDeviceBlockSize = 1, maxProcessingBlockSize = 1, latencySamples = 0, pendingCount = 0;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    std::unique_ptr<juce::LagrangeInterpolator[]> interpolators;
    juce::AudioBuffer<float> pending, made, direct;
    juce::MidiBuffer internalMidi;
};

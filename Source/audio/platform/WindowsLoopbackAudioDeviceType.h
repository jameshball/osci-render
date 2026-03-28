/*
  ==============================================================================

    WindowsLoopbackAudioDeviceType.h
    Created for osci-render

    Custom JUCE AudioIODeviceType and AudioIODevice for capturing Windows system
    output (WASAPI loopback) through miniaudio.

  ==============================================================================
*/

#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

#if JUCE_WINDOWS && OSCI_PREMIUM

struct WindowsLoopbackBackend;

class WindowsLoopbackAudioDevice : public juce::AudioIODevice
{
public:
    WindowsLoopbackAudioDevice (const juce::String& endpointDisplayName,
                                const juce::MemoryBlock& endpointDeviceId,
                                bool useDefaultEndpoint);
    ~WindowsLoopbackAudioDevice() override;

    const juce::String& getSelectedEndpointName() const noexcept { return selectedEndpointName; }

    juce::StringArray getOutputChannelNames() override;
    juce::StringArray getInputChannelNames() override;
    juce::Array<double> getAvailableSampleRates() override;
    juce::Array<int> getAvailableBufferSizes() override;
    int getDefaultBufferSize() override;

    juce::String open (const juce::BigInteger& inputChannels,
                       const juce::BigInteger& outputChannels,
                       double sampleRate,
                       int bufferSizeSamples) override;
    void close() override;
    bool isOpen() override;

    void start (juce::AudioIODeviceCallback* callback) override;
    void stop() override;
    bool isPlaying() override;

    juce::String getLastError() override;

    int getCurrentBufferSizeSamples() override;
    double getCurrentSampleRate() override;
    int getCurrentBitDepth() override;

    juce::BigInteger getActiveOutputChannels() const override;
    juce::BigInteger getActiveInputChannels() const override;

    int getOutputLatencyInSamples() override;
    int getInputLatencyInSamples() override;

private:
    std::unique_ptr<WindowsLoopbackBackend> backend;
    juce::String selectedEndpointName;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WindowsLoopbackAudioDevice)
};

class WindowsLoopbackAudioDeviceType : public juce::AudioIODeviceType
{
public:
    WindowsLoopbackAudioDeviceType();
    ~WindowsLoopbackAudioDeviceType() override;

    const juce::StringArray& getInputChoiceNames() const noexcept { return inputChoiceNames; }
    const juce::StringArray& getOutputChoiceNames() const noexcept { return outputChoiceNames; }

    static juce::String makeCombinedDeviceName (const juce::String& inputName,
                                                const juce::String& outputName)
    {
        return inputName + " -> " + outputName;
    }

    void scanForDevices() override;
    juce::StringArray getDeviceNames (bool wantInputNames) const override;
    int getDefaultDeviceIndex (bool forInput) const override;
    int getIndexOfDevice (juce::AudioIODevice* device, bool asInput) const override;
    bool hasSeparateInputsAndOutputs() const override;
    juce::AudioIODevice* createDevice (const juce::String& outputDeviceName,
                                       const juce::String& inputDeviceName) override;

private:
    juce::StringArray inputChoiceNames;
    juce::StringArray outputChoiceNames;
    juce::StringArray combinedDeviceNames;
    juce::Array<juce::MemoryBlock> outputChoiceDeviceIds;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WindowsLoopbackAudioDeviceType)
};

#endif // JUCE_WINDOWS && OSCI_PREMIUM

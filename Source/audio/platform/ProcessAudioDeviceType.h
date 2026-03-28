/*
  ==============================================================================

    ProcessAudioDeviceType.h
    Created for osci-render

    Custom JUCE AudioIODeviceType and AudioIODevice for capturing audio from
    other applications on macOS 14.2+ using CoreAudio process taps.

    This header is pure C++ (no Obj-C) and safe to include from .cpp files.
    All Obj-C++ implementation lives in ProcessAudioDeviceType.mm via pimpl.

  ==============================================================================
*/

#pragma once

// Include JUCE target platform first so JUCE_MAC is defined before our guard.
// Use the specific module header (not JuceHeader.h) because this header is
// included from CustomStandalone.cpp which has its own targeted module includes.
#include <juce_audio_devices/juce_audio_devices.h>

#if JUCE_MAC && OSCI_PREMIUM

// Forward declaration of the Obj-C++ backend (defined in ProcessAudioDeviceType.mm)
struct ProcessTapBackend;

//==============================================================================
/** Metadata about a capturable audio process. */
struct ProcessAudioInfo
{
    uint32_t audioObjectID = 0;
    int32_t  pid = -1;
    juce::String name;
    juce::String bundleID;
    bool isSystemAudio = false;
};

//==============================================================================
/**
    An AudioIODevice that captures audio from a specific process (or system-wide)
    using the macOS 14.2+ CoreAudio process tap API.

    The device creates an aggregate device combining:
    - A real output device (for audio output to speakers/headphones)
    - A process tap (providing captured audio as input channels)
*/
class ProcessAudioDevice : public juce::AudioIODevice
{
public:
    ProcessAudioDevice (const juce::String& outputDeviceName,
                        const ProcessAudioInfo& processInfo,
                        uint32_t outputCoreAudioDeviceID,
                        bool muteOutputToDevice);
    ~ProcessAudioDevice() override;

    const juce::String& getSelectedProcessName() const noexcept { return selectedProcessName; }
    const juce::String& getSelectedOutputName() const noexcept  { return selectedOutputName; }
    bool isOutputNoneSelected() const noexcept                  { return outputNoneSelected; }

    // juce::AudioIODevice interface
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
    std::unique_ptr<ProcessTapBackend> backend;

    juce::String selectedProcessName;
    juce::String selectedOutputName;
    bool outputNoneSelected = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ProcessAudioDevice)
};

//==============================================================================
/**
    AudioIODeviceType for macOS process audio capture.

    When selected in the AudioDeviceSelectorComponent:
    - Output devices show normal speakers/headphones (mirrors CoreAudio)
    - Input devices show running audio processes + "System Audio"

    Requires macOS 14.2+ (checked at registration time via ProcessAudioPermissions).
*/
class ProcessAudioDeviceType : public juce::AudioIODeviceType
{
public:
    ProcessAudioDeviceType();
    ~ProcessAudioDeviceType() override;

    const juce::StringArray& getProcessChoiceNames() const noexcept { return inputChoiceNames; }
    const juce::StringArray& getOutputChoiceNames() const noexcept { return outputChoiceNames; }
    static juce::String makeCombinedDeviceName (const juce::String& processName,
                                                const juce::String& outputName)
    {
        return processName + " -> " + outputName;
    }

    // juce::AudioIODeviceType interface
    void scanForDevices() override;
    juce::StringArray getDeviceNames (bool wantInputNames) const override;
    int getDefaultDeviceIndex (bool forInput) const override;
    int getIndexOfDevice (juce::AudioIODevice* device, bool asInput) const override;
    bool hasSeparateInputsAndOutputs() const override;
    juce::AudioIODevice* createDevice (const juce::String& outputDeviceName,
                                       const juce::String& inputDeviceName) override;

private:
    // Cached from scanForDevices().
    juce::StringArray inputChoiceNames;        // display names ("System Audio" + process names)
    juce::Array<ProcessAudioInfo> inputChoices;

    juce::StringArray outputChoiceNames;       // display names ("Default Output" + device names)
    juce::Array<uint32_t> outputChoiceIDs;     // CoreAudio AudioDeviceID for outputChoiceNames

    juce::StringArray combinedDeviceNames;     // process + output combined for JUCE selector


    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ProcessAudioDeviceType)
};

#endif // JUCE_MAC && OSCI_PREMIUM

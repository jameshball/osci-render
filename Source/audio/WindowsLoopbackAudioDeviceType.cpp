/*
  ==============================================================================

    WindowsLoopbackAudioDeviceType.cpp
    Created for osci-render

  ==============================================================================
*/

#include "WindowsLoopbackAudioDeviceType.h"

#if JUCE_WINDOWS && OSCI_PREMIUM

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio/miniaudio.h"

#include <atomic>
#include <cstring>
#include <thread>

namespace
{
juce::MemoryBlock serializeDeviceId (const ma_device_id& id)
{
    return { &id, sizeof (ma_device_id) };
}

bool deserializeDeviceId (const juce::MemoryBlock& data, ma_device_id& id)
{
    if (data.getSize() != sizeof (ma_device_id))
        return false;

    std::memcpy (&id, data.getData(), sizeof (ma_device_id));
    return true;
}
} // namespace

struct WindowsLoopbackBackend
{
    ma_context context {};
    ma_device device {};

    bool contextInitialised = false;
    bool deviceInitialised = false;
    bool isDeviceOpen = false;

    bool useDefaultEndpoint = true;
    ma_device_id endpointId {};

    std::atomic<juce::AudioIODeviceCallback*> callback { nullptr };
    std::atomic<bool> playing { false };
    std::atomic<bool> callbackActive { false };
    std::atomic<bool> stopRequestedFromCallbackThread { false };
    std::atomic<std::thread::id> callbackThreadId {};

    int activeInputChannels = 2;
    int activeOutputChannels = 0;
    int currentBufferSize = 512;
    double currentSampleRate = 48000.0;
    juce::String lastError;

    juce::AudioBuffer<float> inputBuffer;
    juce::HeapBlock<const float*> inputChannelPointers;

    static void dataCallback (ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount)
    {
        juce::ignoreUnused (pOutput);

        auto* self = static_cast<WindowsLoopbackBackend*> (pDevice->pUserData);
        if (self == nullptr)
            return;

        self->callbackThreadId.store (std::this_thread::get_id());
        self->callbackActive.store (true, std::memory_order_release);

        auto* cb = self->callback.load (std::memory_order_acquire);
        if (cb == nullptr)
        {
            self->callbackActive.store (false, std::memory_order_release);
            return;
        }

        if (self->stopRequestedFromCallbackThread.load (std::memory_order_acquire))
        {
            self->callbackActive.store (false, std::memory_order_release);
            return;
        }

        const auto maxFrames = (ma_uint32) self->inputBuffer.getNumSamples();
        const auto numFrames = juce::jmin (frameCount, maxFrames);
        const auto channels = juce::jmax (1, self->activeInputChannels);

        self->inputBuffer.clear (0, (int) numFrames);

        if (pInput != nullptr)
        {
            const auto* in = static_cast<const float*> (pInput);
            for (int channel = 0; channel < channels; ++channel)
            {
                auto* dst = self->inputBuffer.getWritePointer (channel);
                for (ma_uint32 i = 0; i < numFrames; ++i)
                    dst[i] = in[(size_t) i * (size_t) channels + (size_t) channel];
            }
        }

        for (int channel = 0; channel < channels; ++channel)
            self->inputChannelPointers[(size_t) channel] = self->inputBuffer.getReadPointer (channel);

        cb->audioDeviceIOCallbackWithContext (self->inputChannelPointers,
                                              channels,
                                              nullptr,
                                              0,
                                              (int) numFrames,
                                              {});

        self->callbackActive.store (false, std::memory_order_release);
    }

    static void notificationCallback (const ma_device_notification* pNotification)
    {
        if (pNotification == nullptr || pNotification->pDevice == nullptr)
            return;

        auto* self = static_cast<WindowsLoopbackBackend*> (pNotification->pDevice->pUserData);
        if (self == nullptr)
            return;

        if (pNotification->type == ma_device_notification_type_stopped)
            self->playing.store (false);
    }

    bool initContext()
    {
        if (contextInitialised)
            return true;

        const ma_backend backends[] = { ma_backend_wasapi };
        auto contextConfig = ma_context_config_init();

        const auto result = ma_context_init (backends, 1, &contextConfig, &context);
        if (result != MA_SUCCESS)
        {
            lastError = "Failed to initialize WASAPI context (" + juce::String ((int) result) + ").";
            return false;
        }

        contextInitialised = true;
        return true;
    }

    void closeDevice()
    {
        stopDevice();

        if (deviceInitialised)
        {
            ma_device_uninit (&device);
            deviceInitialised = false;
        }

        isDeviceOpen = false;
    }

    void uninitContext()
    {
        if (contextInitialised)
        {
            ma_context_uninit (&context);
            contextInitialised = false;
        }
    }

    void stopDevice()
    {
        if (! playing.load())
            return;

        callback.store (nullptr, std::memory_order_release);

        // miniaudio (WASAPI) warns against calling stop from inside the callback
        // thread. If that happens, defer and let non-callback shutdown perform stop.
        if (std::this_thread::get_id() == callbackThreadId.load())
        {
            stopRequestedFromCallbackThread.store (true, std::memory_order_release);
            playing.store (false, std::memory_order_release);
            return;
        }

        stopRequestedFromCallbackThread.store (false, std::memory_order_release);
        ma_device_stop (&device);
        playing.store (false, std::memory_order_release);
    }
};

WindowsLoopbackAudioDevice::WindowsLoopbackAudioDevice (const juce::String& endpointDisplayName,
                                                        const juce::MemoryBlock& endpointDeviceId,
                                                        bool useDefaultEndpoint)
    : juce::AudioIODevice ("System Audio -> " + endpointDisplayName, "Windows Loopback"),
      backend (std::make_unique<WindowsLoopbackBackend>())
{
    backend->useDefaultEndpoint = useDefaultEndpoint;
    deserializeDeviceId (endpointDeviceId, backend->endpointId);
    selectedEndpointName = endpointDisplayName;
}

WindowsLoopbackAudioDevice::~WindowsLoopbackAudioDevice()
{
    close();
    backend->uninitContext();
}

juce::StringArray WindowsLoopbackAudioDevice::getOutputChannelNames()
{
    return {};
}

juce::StringArray WindowsLoopbackAudioDevice::getInputChannelNames()
{
    return { "Loopback L", "Loopback R" };
}

juce::Array<double> WindowsLoopbackAudioDevice::getAvailableSampleRates()
{
    return { 44100.0, 48000.0, 88200.0, 96000.0 };
}

juce::Array<int> WindowsLoopbackAudioDevice::getAvailableBufferSizes()
{
    return { 128, 256, 512, 1024, 2048 };
}

int WindowsLoopbackAudioDevice::getDefaultBufferSize()
{
    return 512;
}

juce::String WindowsLoopbackAudioDevice::open (const juce::BigInteger& inputChannels,
                                               const juce::BigInteger& outputChannels,
                                               double sampleRate,
                                               int bufferSizeSamples)
{
    juce::ignoreUnused (outputChannels);
    close();

    if (! backend->initContext())
        return backend->lastError;

    auto config = ma_device_config_init (ma_device_type_loopback);
    config.sampleRate = (ma_uint32) (sampleRate > 0.0 ? sampleRate : 48000.0);
    config.periodSizeInFrames = (ma_uint32) (bufferSizeSamples > 0 ? bufferSizeSamples : backend->currentBufferSize);
    config.capture.format = ma_format_f32;
    config.capture.channels = 2;
    config.dataCallback = WindowsLoopbackBackend::dataCallback;
    config.notificationCallback = WindowsLoopbackBackend::notificationCallback;
    config.pUserData = backend.get();

    if (! backend->useDefaultEndpoint)
        config.capture.pDeviceID = &backend->endpointId;

    const auto result = ma_device_init (&backend->context, &config, &backend->device);
    if (result != MA_SUCCESS)
    {
        backend->lastError = "Failed to initialize loopback device (" + juce::String ((int) result) + ").";
        return backend->lastError;
    }

    backend->deviceInitialised = true;
    backend->isDeviceOpen = true;
    backend->activeInputChannels = juce::jmax (1, inputChannels.countNumberOfSetBits());
    backend->activeInputChannels = juce::jmin (backend->activeInputChannels, 2);
    backend->activeOutputChannels = 0;
    backend->currentSampleRate = (double) backend->device.sampleRate;
    backend->currentBufferSize = (int) backend->device.capture.internalPeriodSizeInFrames;

    if (backend->currentSampleRate <= 0.0)
        backend->currentSampleRate = 48000.0;
    if (backend->currentBufferSize <= 0)
        backend->currentBufferSize = 512;

    const auto maxFrames = juce::jmax (backend->currentBufferSize * 2, 8192);
    backend->inputBuffer.setSize (backend->activeInputChannels, maxFrames);
    backend->inputBuffer.clear();
    backend->inputChannelPointers.malloc ((size_t) backend->activeInputChannels);

    return {};
}

void WindowsLoopbackAudioDevice::close()
{
    backend->closeDevice();
}

bool WindowsLoopbackAudioDevice::isOpen()
{
    return backend->isDeviceOpen;
}

void WindowsLoopbackAudioDevice::start (juce::AudioIODeviceCallback* callback)
{
    if (! backend->isDeviceOpen || callback == nullptr)
        return;

    backend->callback.store (callback, std::memory_order_release);
    callback->audioDeviceAboutToStart (this);

    const auto result = ma_device_start (&backend->device);
    if (result != MA_SUCCESS)
    {
        backend->callback.store (nullptr, std::memory_order_release);
        callback->audioDeviceStopped();
        backend->lastError = "Failed to start loopback device (" + juce::String ((int) result) + ").";
        return;
    }

    backend->playing.store (true, std::memory_order_release);
}

void WindowsLoopbackAudioDevice::stop()
{
    auto* cb = backend->callback.exchange (nullptr, std::memory_order_acq_rel);
    backend->stopDevice();

    if (cb != nullptr)
        cb->audioDeviceStopped();
}

bool WindowsLoopbackAudioDevice::isPlaying()
{
    return backend->playing.load (std::memory_order_acquire);
}

juce::String WindowsLoopbackAudioDevice::getLastError()
{
    return backend->lastError;
}

int WindowsLoopbackAudioDevice::getCurrentBufferSizeSamples()
{
    return backend->currentBufferSize;
}

double WindowsLoopbackAudioDevice::getCurrentSampleRate()
{
    return backend->currentSampleRate;
}

int WindowsLoopbackAudioDevice::getCurrentBitDepth()
{
    return 32;
}

juce::BigInteger WindowsLoopbackAudioDevice::getActiveOutputChannels() const
{
    return {};
}

juce::BigInteger WindowsLoopbackAudioDevice::getActiveInputChannels() const
{
    juce::BigInteger channels;
    channels.setRange (0, backend->activeInputChannels, true);
    return channels;
}

int WindowsLoopbackAudioDevice::getOutputLatencyInSamples()
{
    return 0;
}

int WindowsLoopbackAudioDevice::getInputLatencyInSamples()
{
    return (int) backend->device.capture.internalPeriodSizeInFrames;
}

WindowsLoopbackAudioDeviceType::WindowsLoopbackAudioDeviceType()
    : juce::AudioIODeviceType ("Windows Loopback")
{
}

WindowsLoopbackAudioDeviceType::~WindowsLoopbackAudioDeviceType() = default;

void WindowsLoopbackAudioDeviceType::scanForDevices()
{
    inputChoiceNames = { "System Audio" };
    outputChoiceNames.clear();
    outputChoiceDeviceIds.clear();
    combinedDeviceNames.clear();

    const ma_backend backends[] = { ma_backend_wasapi };
    auto contextConfig = ma_context_config_init();

    ma_context context {};
    if (ma_context_init (backends, 1, &contextConfig, &context) != MA_SUCCESS)
    {
        outputChoiceNames.add ("Default Output");
        outputChoiceDeviceIds.add (juce::MemoryBlock());
        combinedDeviceNames.add (makeCombinedDeviceName ("System Audio", "Default Output"));
        return;
    }

    ma_device_info* playbackInfos = nullptr;
    ma_uint32 playbackCount = 0;
    if (ma_context_get_devices (&context, &playbackInfos, &playbackCount, nullptr, nullptr) == MA_SUCCESS)
    {
        outputChoiceNames.add ("Default Output");
        outputChoiceDeviceIds.add (juce::MemoryBlock());

        for (ma_uint32 i = 0; i < playbackCount; ++i)
        {
            auto name = juce::String (playbackInfos[i].name);
            if (name.isNotEmpty())
            {
                outputChoiceNames.add (name);
                outputChoiceDeviceIds.add (serializeDeviceId (playbackInfos[i].id));
            }
        }
    }

    outputChoiceNames.appendNumbersToDuplicates (false, true);

    if (outputChoiceNames.isEmpty())
    {
        outputChoiceNames.add ("Default Output");
        outputChoiceDeviceIds.add (juce::MemoryBlock());
    }

    for (const auto& outputName : outputChoiceNames)
        combinedDeviceNames.add (makeCombinedDeviceName ("System Audio", outputName));

    ma_context_uninit (&context);
}

juce::StringArray WindowsLoopbackAudioDeviceType::getDeviceNames (bool wantInputNames) const
{
    juce::ignoreUnused (wantInputNames);
    return combinedDeviceNames;
}

int WindowsLoopbackAudioDeviceType::getDefaultDeviceIndex (bool forInput) const
{
    juce::ignoreUnused (forInput);
    const auto preferred = makeCombinedDeviceName ("System Audio", "Default Output");
    const auto index = combinedDeviceNames.indexOf (preferred);
    return index >= 0 ? index : (combinedDeviceNames.isEmpty() ? -1 : 0);
}

int WindowsLoopbackAudioDeviceType::getIndexOfDevice (juce::AudioIODevice* device, bool asInput) const
{
    juce::ignoreUnused (asInput);

    if (device == nullptr)
        return -1;

    return combinedDeviceNames.indexOf (device->getName());
}

bool WindowsLoopbackAudioDeviceType::hasSeparateInputsAndOutputs() const
{
    return false;
}

juce::AudioIODevice* WindowsLoopbackAudioDeviceType::createDevice (const juce::String& outputDeviceName,
                                                                   const juce::String& inputDeviceName)
{
    if (outputChoiceNames.isEmpty() || combinedDeviceNames.isEmpty())
        scanForDevices();

    juce::String inputName = inputDeviceName;
    juce::String selectedOutputName = outputDeviceName;

    const auto combinedName = outputDeviceName.isNotEmpty() ? outputDeviceName : inputDeviceName;
    if (combinedName.contains (" -> "))
    {
        const auto arrowPos = combinedName.indexOf (" -> ");
        if (arrowPos > 0)
        {
            inputName = combinedName.substring (0, arrowPos).trim();
            selectedOutputName = combinedName.substring (arrowPos + 4).trim();
        }
    }

    if (inputName.isEmpty())
        inputName = "System Audio";

    auto outputIndex = outputChoiceNames.indexOf (selectedOutputName);
    if (outputIndex < 0)
        outputIndex = outputChoiceNames.indexOf ("Default Output");
    if (outputIndex < 0)
        outputIndex = 0;

    const auto displayOutput = outputChoiceNames[outputIndex];
    const auto& endpointId = outputChoiceDeviceIds.getReference (outputIndex);
    const bool useDefaultEndpoint = endpointId.getSize() == 0;

    return new WindowsLoopbackAudioDevice (displayOutput, endpointId, useDefaultEndpoint);
}

#endif // JUCE_WINDOWS && OSCI_PREMIUM

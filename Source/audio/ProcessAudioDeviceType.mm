/*
  ==============================================================================

    ProcessAudioDeviceType.mm
    Created for osci-render

    Obj-C++ implementation of the macOS 14.2+ process audio capture device type.
    Uses CoreAudio process taps to capture audio from other applications.

    Reference: https://github.com/insidegui/AudioCap

  ==============================================================================
*/

#include "ProcessAudioDeviceType.h"

#if JUCE_MAC && OSCI_PREMIUM

#import <CoreAudio/CoreAudio.h>
#import <AudioToolbox/AudioToolbox.h>

// These headers require __OBJC__ — guaranteed by the .mm extension.
#import <CoreAudio/CATapDescription.h>
#import <CoreAudio/AudioHardwareTapping.h>
#import <AppKit/NSWorkspace.h>
#import <AppKit/NSRunningApplication.h>
#import <dispatch/dispatch.h>

#include "ProcessAudioPermissions.h"

using namespace juce;

//==============================================================================
// MARK: - CoreAudio Property Helpers
//==============================================================================

static constexpr auto kElementMain =
#if defined (MAC_OS_VERSION_12_0)
    kAudioObjectPropertyElementMain;
#else
    kAudioObjectPropertyElementMaster;
#endif

static OSStatus caGetPropertyDataSize (AudioObjectID objectID,
                                       AudioObjectPropertySelector selector,
                                       AudioObjectPropertyScope scope,
                                       UInt32& outSize)
{
    AudioObjectPropertyAddress addr { selector, scope, kElementMain };
    return AudioObjectGetPropertyDataSize (objectID, &addr, 0, nullptr, &outSize);
}

static OSStatus caGetPropertyData (AudioObjectID objectID,
                                   AudioObjectPropertySelector selector,
                                   AudioObjectPropertyScope scope,
                                   UInt32& ioSize,
                                   void* outData)
{
    AudioObjectPropertyAddress addr { selector, scope, kElementMain };
    return AudioObjectGetPropertyData (objectID, &addr, 0, nullptr, &ioSize, outData);
}

template <typename T>
static bool caReadProperty (AudioObjectID objectID,
                            AudioObjectPropertySelector selector,
                            AudioObjectPropertyScope scope,
                            T& outValue)
{
    UInt32 size = sizeof (T);
    return caGetPropertyData (objectID, selector, scope, size, &outValue) == noErr;
}

static String caReadStringProperty (AudioObjectID objectID,
                                    AudioObjectPropertySelector selector,
                                    AudioObjectPropertyScope scope = kAudioObjectPropertyScopeGlobal)
{
    CFStringRef cfStr = nullptr;
    UInt32 size = sizeof (CFStringRef);

    AudioObjectPropertyAddress addr { selector, scope, kElementMain };

    if (AudioObjectGetPropertyData (objectID, &addr, 0, nullptr, &size, &cfStr) == noErr && cfStr != nullptr)
    {
        auto result = String::fromCFString (cfStr);
        CFRelease (cfStr);
        return result;
    }

    return {};
}

static AudioDeviceID caGetDefaultOutputDevice()
{
    AudioDeviceID deviceID = 0;
    caReadProperty (kAudioObjectSystemObject, kAudioHardwarePropertyDefaultOutputDevice,
                    kAudioObjectPropertyScopeGlobal, deviceID);
    return deviceID;
}

static String caGetDeviceUID (AudioDeviceID deviceID)
{
    return caReadStringProperty (deviceID, kAudioDevicePropertyDeviceUID);
}

static int caGetNumChannels (AudioDeviceID deviceID, bool isInput)
{
    auto scope = isInput ? kAudioObjectPropertyScopeInput : kAudioObjectPropertyScopeOutput;
    UInt32 size = 0;

    if (caGetPropertyDataSize (deviceID, kAudioDevicePropertyStreamConfiguration, scope, size) != noErr)
        return 0;

    HeapBlock<AudioBufferList> bufList;
    bufList.calloc (size, 1);

    if (caGetPropertyData (deviceID, kAudioDevicePropertyStreamConfiguration, scope, size, bufList) != noErr)
        return 0;

    int total = 0;
    for (UInt32 i = 0; i < bufList->mNumberBuffers; ++i)
        total += (int) bufList->mBuffers[i].mNumberChannels;

    return total;
}

//==============================================================================
// MARK: - Process Enumeration
//==============================================================================

static Array<ProcessAudioInfo> enumerateAudioProcesses()
{
    Array<ProcessAudioInfo> result;

    // Add "System Audio" as the first entry
    ProcessAudioInfo systemAudio;
    systemAudio.name = "System Audio";
    systemAudio.isSystemAudio = true;
    result.add (systemAudio);

    // Read the process object list from CoreAudio
    UInt32 dataSize = 0;

    if (caGetPropertyDataSize (kAudioObjectSystemObject,
                               kAudioHardwarePropertyProcessObjectList,
                               kAudioObjectPropertyScopeGlobal, dataSize) != noErr)
        return result;

    const int numProcesses = (int) (dataSize / sizeof (AudioObjectID));
    if (numProcesses == 0)
        return result;

    HeapBlock<AudioObjectID> objectIDs ((size_t) numProcesses);

    if (caGetPropertyData (kAudioObjectSystemObject,
                           kAudioHardwarePropertyProcessObjectList,
                           kAudioObjectPropertyScopeGlobal, dataSize, objectIDs) != noErr)
        return result;

    // Get running applications for name matching
    NSArray<NSRunningApplication*>* runningApps = [[NSWorkspace sharedWorkspace] runningApplications];
    pid_t ownPid = [[NSProcessInfo processInfo] processIdentifier];

    for (int i = 0; i < numProcesses; ++i)
    {
        AudioObjectID objID = objectIDs[i];

        // Read the PID for this process object
        pid_t pid = -1;
        if (! caReadProperty (objID, kAudioProcessPropertyPID,
                              kAudioObjectPropertyScopeGlobal, pid))
            continue;

        // Skip our own process
        if (pid == ownPid)
            continue;

        // Check if this process is producing audio output
        UInt32 isRunning = 0;
        caReadProperty (objID, kAudioProcessPropertyIsRunningOutput,
                        kAudioObjectPropertyScopeGlobal, isRunning);

        ProcessAudioInfo info;
        info.audioObjectID = objID;
        info.pid = pid;

        // Try to read bundle ID
        info.bundleID = caReadStringProperty (objID, kAudioProcessPropertyBundleID);

        // Try to match with a running application for name
        bool foundApp = false;
        for (NSRunningApplication* app in runningApps)
        {
            if (app.processIdentifier == pid)
            {
                if (app.localizedName != nil)
                    info.name = String::fromUTF8 ([app.localizedName UTF8String]);
                else if (app.bundleIdentifier != nil)
                    info.name = String::fromUTF8 ([app.bundleIdentifier UTF8String]);

                foundApp = true;
                break;
            }
        }

        if (! foundApp || info.name.isEmpty())
        {
            // Fall back to CoreAudio bundle ID (e.g. "com.apple.Safari" -> "Safari")
            if (info.bundleID.isNotEmpty())
            {
                auto lastComponent = info.bundleID.fromLastOccurrenceOf (".", false, false);
                info.name = lastComponent.isNotEmpty() ? lastComponent : info.bundleID;
            }
            else
            {
                info.name = "Process " + String (pid);
            }
        }

        result.add (info);
    }

    // Sort: audio-active processes first, then alphabetically
    // ("System Audio" stays at index 0)
    std::sort (result.begin() + 1, result.end(),
               [] (const ProcessAudioInfo& a, const ProcessAudioInfo& b)
               {
                   return a.name.compareIgnoreCase (b.name) < 0;
               });

    return result;
}

//==============================================================================
// MARK: - Output Device Enumeration
//==============================================================================

struct OutputDeviceInfo
{
    StringArray names;
    Array<AudioDeviceID> deviceIDs;
};

static OutputDeviceInfo enumerateOutputDevices()
{
    OutputDeviceInfo result;

    UInt32 dataSize = 0;
    if (caGetPropertyDataSize (kAudioObjectSystemObject,
                               kAudioHardwarePropertyDevices,
                               kAudioObjectPropertyScopeWildcard, dataSize) != noErr)
        return result;

    const int numDevices = (int) (dataSize / sizeof (AudioDeviceID));
    if (numDevices == 0)
        return result;

    HeapBlock<AudioDeviceID> deviceIDs ((size_t) numDevices);

    if (caGetPropertyData (kAudioObjectSystemObject,
                           kAudioHardwarePropertyDevices,
                           kAudioObjectPropertyScopeWildcard, dataSize, deviceIDs) != noErr)
        return result;

    for (int i = 0; i < numDevices; ++i)
    {
        // Skip aggregate and virtual devices (we create our own aggregate device
        // and don't want it appearing in the output device list)
        UInt32 transportType = 0;
        caReadProperty (deviceIDs[i], kAudioDevicePropertyTransportType,
                        kAudioObjectPropertyScopeGlobal, transportType);

        if (transportType == kAudioDeviceTransportTypeAggregate
            || transportType == kAudioDeviceTransportTypeVirtual)
            continue;

        if (caGetNumChannels (deviceIDs[i], false) > 0)
        {
            auto name = caReadStringProperty (deviceIDs[i], kAudioDevicePropertyDeviceNameCFString,
                                              kAudioObjectPropertyScopeWildcard);
            if (name.isNotEmpty())
            {
                result.names.add (name);
                result.deviceIDs.add (deviceIDs[i]);
            }
        }
    }

    result.names.appendNumbersToDuplicates (false, true);
    return result;
}

//==============================================================================
// MARK: - ProcessTapBackend
//==============================================================================

struct ProcessTapBackend
{
    ProcessTapBackend (const ProcessAudioInfo& processInfo, AudioDeviceID outputDeviceID)
        : process (processInfo), outputDevice (outputDeviceID)
    {
    }

    ~ProcessTapBackend()
    {
        destroyAll();
    }

    //==============================================================================
    String createTapAndAggregateDevice()
    {
        juce::Logger::writeToLog ("ProcessTap: createTapAndAggregateDevice for process='" + process.name + "' pid=" + String (process.pid));

        if (! __builtin_available (macOS 14.2, *))
        {
            juce::Logger::writeToLog ("ProcessTap: macOS version too old for process taps");
            return "Process audio capture requires macOS 14.2 or later.";
        }

        // Audio Capture permission (macOS) can cause taps to succeed but deliver silence.
        // Preflight/request it explicitly so the user gets a prompt and a clear failure mode.
        {
            const auto status = ProcessAudioPermissions::getAudioCapturePermissionStatus();
            juce::Logger::writeToLog ("ProcessTap: audio capture permission status=" + String ((int) status));

            if (status != ProcessAudioPermissions::AudioCapturePermissionStatus::authorized)
            {
                juce::Logger::writeToLog ("ProcessTap: permission denied — aborting tap creation");
                return "Audio Capture permission is required. Enable it in System Settings > Privacy & Security > Audio Capture, then reselect the device.";
            }
        }

        // Step 1: Create the process tap
        CATapDescription* tapDesc = nil;

        if (process.isSystemAudio)
        {
            tapDesc = [[CATapDescription alloc]
                initStereoGlobalTapButExcludeProcesses:@[]];
        }
        else
        {
            // Process audio object IDs can change across rescans. Resolve the
            // currently valid audio object ID using PID to avoid building an
            // exclude-list that accidentally excludes everything (silence).
            const auto processesNow = enumerateAudioProcesses();
            AudioObjectID selectedObjectID = 0;

            for (const auto& p : processesNow)
            {
                if (! p.isSystemAudio && p.pid == process.pid)
                {
                    selectedObjectID = (AudioObjectID) p.audioObjectID;
                    break;
                }
            }

            if (selectedObjectID == 0)
                return "Selected process is no longer available for capture.";

            process.audioObjectID = (uint32_t) selectedObjectID;

            // Using stereoMixdownOfProcesses can cause AudioDeviceStart() on the
            // resulting aggregate device to block until the process actually
            // starts producing audio. For an interactive plugin we want the
            // device to start immediately and output silence until audio exists.
            //
            // Workaround: create a global tap but exclude all *other* processes,
            // leaving only the selected process.
            NSMutableArray<NSNumber*>* excluded = [NSMutableArray array];
            for (const auto& p : processesNow)
            {
                if (p.isSystemAudio)
                    continue;

                if (p.audioObjectID == 0 || (AudioObjectID) p.audioObjectID == selectedObjectID)
                    continue;

                [excluded addObject:@(p.audioObjectID)];
            }

            tapDesc = [[CATapDescription alloc]
                initStereoGlobalTapButExcludeProcesses:excluded];
        }

        tapDesc.UUID = [NSUUID UUID];
        tapDesc.muteBehavior = CATapUnmuted;
        tapDesc.privateTap = YES;
        tapDesc.name = [NSString stringWithUTF8String: process.name.toUTF8()];

        AudioObjectID newTapID = kAudioObjectUnknown;
        OSStatus err = AudioHardwareCreateProcessTap (tapDesc, &newTapID);

        if (err != noErr)
        {
            juce::Logger::writeToLog ("ProcessTap: AudioHardwareCreateProcessTap failed, error=" + String ((int) err));
            return "Failed to create process tap (error " + String ((int) err) + "). "
                   "You may need to grant audio capture permission in System Settings.";
        }

        tapID = newTapID;
        juce::Logger::writeToLog ("ProcessTap: tap created, tapID=" + String ((int) tapID));

        // Read the tap's audio format
        UInt32 formatSize = sizeof (AudioStreamBasicDescription);
        AudioObjectPropertyAddress formatAddr {
            kAudioTapPropertyFormat,
            kAudioObjectPropertyScopeGlobal,
            kElementMain
        };

        if (AudioObjectGetPropertyData (tapID, &formatAddr, 0, nullptr, &formatSize, &tapFormat) != noErr)
        {
            juce::Logger::writeToLog ("ProcessTap: failed to read tap audio format");
            destroyAll();
            return "Failed to read tap audio format.";
        }

        juce::Logger::writeToLog ("ProcessTap: tap format: sampleRate=" + String (tapFormat.mSampleRate)
                                  + " channels=" + String ((int) tapFormat.mChannelsPerFrame)
                                  + " bitsPerChannel=" + String ((int) tapFormat.mBitsPerChannel));

        // Step 2: Get the output device UID
        String outputUID = caGetDeviceUID (outputDevice);
        if (outputUID.isEmpty())
        {
            // Fallback to default output device
            outputDevice = caGetDefaultOutputDevice();
            outputUID = caGetDeviceUID (outputDevice);

            if (outputUID.isEmpty())
            {
                destroyAll();
                return "No audio output device available.";
            }
        }

        // Step 3: Create aggregate device
        NSString* tapUUID = [tapDesc.UUID UUIDString];
        NSString* outputUIDStr = [NSString stringWithUTF8String: outputUID.toUTF8()];
        NSString* aggUID = [[NSUUID UUID] UUIDString];
        NSString* aggName = [NSString stringWithFormat:@"osci-render Tap (%s)", process.name.toUTF8()];

        NSDictionary* description = @{
            @(kAudioAggregateDeviceNameKey): aggName,
            @(kAudioAggregateDeviceUIDKey): aggUID,
            @(kAudioAggregateDeviceMainSubDeviceKey): outputUIDStr,
            @(kAudioAggregateDeviceIsPrivateKey): @YES,
            @(kAudioAggregateDeviceIsStackedKey): @NO,
            // Important: when enabled, AudioDeviceStart() will wait for the first tap audio,
            // which can block indefinitely for silent/stalled processes and will starve the
            // rest of the audio engine. We want the device to start immediately and deliver
            // silence until the tapped process produces audio.
            @(kAudioAggregateDeviceTapAutoStartKey): @NO,
            @(kAudioAggregateDeviceSubDeviceListKey): @[
                @{ @(kAudioSubDeviceUIDKey): outputUIDStr }
            ],
            @(kAudioAggregateDeviceTapListKey): @[
                @{
                    @(kAudioSubTapDriftCompensationKey): @YES,
                    @(kAudioSubTapUIDKey): tapUUID
                }
            ]
        };

        AudioObjectID newAggID = kAudioObjectUnknown;
        err = AudioHardwareCreateAggregateDevice ((__bridge CFDictionaryRef) description, &newAggID);

        if (err != noErr)
        {
            juce::Logger::writeToLog ("ProcessTap: AudioHardwareCreateAggregateDevice failed, error=" + String ((int) err));
            destroyAll();
            return "Failed to create aggregate device (error " + String ((int) err) + ").";
        }

        aggregateDeviceID = newAggID;
        juce::Logger::writeToLog ("ProcessTap: aggregate device created, aggID=" + String ((int) aggregateDeviceID));
        return {}; // success
    }

    //==============================================================================
    void startIO (AudioIODeviceCallback* juceCallback, AudioIODevice* ownerDevice)
    {
        juce::Logger::writeToLog ("ProcessTap: startIO");

        if (aggregateDeviceID == kAudioObjectUnknown || juceCallback == nullptr)
        {
            juce::Logger::writeToLog ("ProcessTap: startIO aborted — aggID=" + String ((int) aggregateDeviceID)
                                      + " callback=" + String (juceCallback != nullptr ? "valid" : "null"));
            return;
        }

        callback.store (juceCallback, std::memory_order_release);

        // Read the actual buffer size and sample rate from the aggregate device.
        // These must never be 0 because JUCE's CallbackMaxSizeEnforcer will
        // otherwise spin forever (blockLength becomes 0).
        UInt32 bufSize = 0;
        const bool gotBuf = caReadProperty (aggregateDeviceID, kAudioDevicePropertyBufferFrameSize,
                            kAudioObjectPropertyScopeGlobal, bufSize);
        currentBufferSize = (gotBuf && bufSize > 0 ? (int) bufSize : 512);

        Float64 sr = 0;
        const bool gotSr = caReadProperty (aggregateDeviceID, kAudioDevicePropertyNominalSampleRate,
                           kAudioObjectPropertyScopeGlobal, sr);
        currentSampleRate = (gotSr && sr > 0.0 ? (double) sr : 44100.0);

        // Determine input channel count from tap format (always stereo from our tap)
        const int availableInputChans = ((int) tapFormat.mChannelsPerFrame > 0 ? (int) tapFormat.mChannelsPerFrame : 2);

        // Determine output channel count from the output device
        const int outputChans = caGetNumChannels (outputDevice, false);
        const int availableOutputChans = (outputChans > 0 ? outputChans : 2);

        // Use the channel counts requested by JUCE in open() (or all available if unspecified)
        numInputChannels  = jlimit (0, availableInputChans,  activeInputChannels);
        numOutputChannels = jlimit (0, availableOutputChans, activeOutputChannels);

        // Keep getActive*Channels() consistent with the values we actually
        // allocate and pass to the JUCE callback.  JUCE's internal
        // CallbackMaxSizeEnforcer sizes its channel-pointer vectors from
        // getActiveOutputChannels() at audioDeviceAboutToStart time; if those
        // return a larger count than what we provide in the IO callback,
        // un-written vector entries remain nullptr and propagate as null output
        // channel pointers into initialiseIoBuffers, causing a SIGSEGV.
        activeInputChannels  = numInputChannels;
        activeOutputChannels = numOutputChannels;

        juce::Logger::writeToLog ("ProcessTap: startIO config: sampleRate=" + String (currentSampleRate)
                                  + " bufferSize=" + String (currentBufferSize)
                                  + " inChans=" + String (numInputChannels)
                                  + " outChans=" + String (numOutputChannels));


        // Pre-allocate temp buffers with generous size to avoid allocation on audio thread.
        // Use at least 8192 samples to cover any reasonable buffer size.
        int preAllocSize = jmax (currentBufferSize * 2, 8192);
        inputBuffer.setSize (numInputChannels, preAllocSize);
        outputBuffer.setSize (numOutputChannels, preAllocSize);
        inputBuffer.clear();
        outputBuffer.clear();

        // Pre-allocate channel pointer arrays so the IO callback never allocates.
        inputChannelPointers.malloc ((size_t) jmax (1, numInputChannels));
        outputChannelPointers.malloc ((size_t) jmax (1, numOutputChannels));
        inputChannelPointers[0] = nullptr;
        outputChannelPointers[0] = nullptr;

        if (ioQueue == nullptr)
        {
            ioQueue = dispatch_queue_create ("osci-render.process-tap-io", DISPATCH_QUEUE_SERIAL);

            // Best-effort: bump scheduling priority without calling QoS floor APIs
            // (some libdispatch builds assert in dispatch_set_qos_class_* here).
            dispatch_set_target_queue (ioQueue, dispatch_get_global_queue (QOS_CLASS_USER_INTERACTIVE, 0));
        }


        // Create IO proc with block callback
        AudioDeviceIOBlock ioBlock = ^(const AudioTimeStamp* inNow,
                                       const AudioBufferList* inInputData,
                                       const AudioTimeStamp* inInputTime,
                                       AudioBufferList* outOutputData,
                                       const AudioTimeStamp* inOutputTime)
        {
            this->audioIOCallback (inInputData, outOutputData);
        };

        OSStatus err = AudioDeviceCreateIOProcIDWithBlock (&ioProcID, aggregateDeviceID, ioQueue, ioBlock);
        if (err != noErr)
        {
            lastError = "Failed to create IO proc (error " + String ((int) err) + ").";
            juce::Logger::writeToLog ("ProcessTap: " + lastError);
            return;
        }

        // Notify JUCE callback that audio is about to start BEFORE starting the device.
        // This ensures the callback's internal state is ready when the first IO proc fires.
        juceCallback->audioDeviceAboutToStart (ownerDevice);
        playing.store (true);

        // AudioDeviceStart can block until the tapped process begins producing audio.
        // If this happens on JUCE's message thread it will freeze the UI, so we start
        // the device asynchronously.
        dispatch_async (dispatch_get_global_queue (QOS_CLASS_USER_INTERACTIVE, 0), ^{
            const OSStatus startErr = AudioDeviceStart (aggregateDeviceID, ioProcID);

            if (startErr != noErr)
            {
                playing.store (false);
                if (auto* cb = callback.exchange (nullptr))
                    cb->audioDeviceStopped();

                if (aggregateDeviceID != kAudioObjectUnknown && ioProcID != nullptr)
                {
                    AudioDeviceDestroyIOProcID (aggregateDeviceID, ioProcID);
                    ioProcID = nullptr;
                }

                lastError = "Failed to start audio device (error " + String ((int) startErr) + ").";
                juce::Logger::writeToLog ("ProcessTap: " + lastError);
            }
            else
            {
                juce::Logger::writeToLog ("ProcessTap: AudioDeviceStart succeeded");
            }
        });
    }

    void stopIO()
    {
        if (! playing)
            return;

        juce::Logger::writeToLog ("ProcessTap: stopIO");
        playing.store (false);

        if (aggregateDeviceID != kAudioObjectUnknown && ioProcID != nullptr)
        {
            AudioDeviceStop (aggregateDeviceID, ioProcID);
            AudioDeviceDestroyIOProcID (aggregateDeviceID, ioProcID);
            ioProcID = nullptr;
        }

        if (auto* cb = callback.exchange (nullptr))
            cb->audioDeviceStopped();
    }

    //==============================================================================
    void destroyAll()
    {
        juce::Logger::writeToLog ("ProcessTap: destroyAll");
        stopIO();

        if (aggregateDeviceID != kAudioObjectUnknown)
        {
            AudioHardwareDestroyAggregateDevice (aggregateDeviceID);
            aggregateDeviceID = kAudioObjectUnknown;
        }

        if (tapID != kAudioObjectUnknown)
        {
            if (__builtin_available (macOS 14.2, *))
            {
                AudioHardwareDestroyProcessTap (tapID);
            }
            tapID = kAudioObjectUnknown;
        }
    }

    //==============================================================================
    void audioIOCallback (const AudioBufferList* inInputData,
                          AudioBufferList* outOutputData)
    {
        auto* cb = callback.load (std::memory_order_acquire);
        if (cb == nullptr)
        {
            // Silence output
            if (outOutputData != nullptr)
            {
                for (UInt32 i = 0; i < outOutputData->mNumberBuffers; ++i)
                    zeromem (outOutputData->mBuffers[i].mData,
                             outOutputData->mBuffers[i].mDataByteSize);
            }
            return;
        }

        int numSamples = currentBufferSize;

        // Determine actual number of samples from the buffers
        if (outOutputData != nullptr && outOutputData->mNumberBuffers > 0)
        {
            auto& buf = outOutputData->mBuffers[0];
            if (buf.mNumberChannels > 0)
            {
                int samplesInBuf = (int) (buf.mDataByteSize / (sizeof (float) * buf.mNumberChannels));
                if (samplesInBuf > 0)
                    numSamples = samplesInBuf;
            }
        }
        else if (inInputData != nullptr && inInputData->mNumberBuffers > 0)
        {
            auto& buf = inInputData->mBuffers[0];
            if (buf.mNumberChannels > 0)
            {
                int samplesInBuf = (int) (buf.mDataByteSize / (sizeof (float) * buf.mNumberChannels));
                if (samplesInBuf > 0)
                    numSamples = samplesInBuf;
            }
        }

        // If the buffer is somehow larger than our pre-allocated size, clamp it.
        // Never allocate on the audio thread.
        int maxSamples = inputBuffer.getNumSamples();
        if (numSamples > maxSamples)
            numSamples = maxSamples;

        // Guard: nothing useful to do with 0 samples, and downstream enforcers
        // can spin forever (blockLength stays 0) if we pass 0.
        if (numSamples <= 0)
        {
            if (outOutputData != nullptr)
                for (UInt32 i = 0; i < outOutputData->mNumberBuffers; ++i)
                    zeromem (outOutputData->mBuffers[i].mData,
                             outOutputData->mBuffers[i].mDataByteSize);
            return;
        }

        // Copy input data from CoreAudio buffers to our deinterleaved JUCE buffer
        inputBuffer.clear (0, numSamples);
        if (inInputData != nullptr)
        {
            int channelIndex = 0;
            for (UInt32 b = 0; b < inInputData->mNumberBuffers && channelIndex < numInputChannels; ++b)
            {
                auto& abuf = inInputData->mBuffers[b];
                const float* src = static_cast<const float*> (abuf.mData);
                int chansInBuf = (int) abuf.mNumberChannels;

                // Skip buffers with null data (can happen during device
                // reconfiguration or feedback-loop starvation).
                if (src == nullptr)
                {
                    channelIndex += jmax (1, chansInBuf);
                    continue;
                }

                if (chansInBuf == 1)
                {
                    if (channelIndex < numInputChannels)
                        FloatVectorOperations::copy (inputBuffer.getWritePointer (channelIndex++), src, numSamples);
                }
                else
                {
                    for (int ch = 0; ch < chansInBuf && channelIndex < numInputChannels; ++ch)
                    {
                        float* dest = inputBuffer.getWritePointer (channelIndex++);
                        for (int s = 0; s < numSamples; ++s)
                            dest[s] = src[s * chansInBuf + ch];
                    }
                }
            }
        }

        const int numIn = numInputChannels;
        const int numOut = numOutputChannels;

        for (int i = 0; i < numIn; ++i)
            inputChannelPointers[i] = inputBuffer.getReadPointer (i);

        outputBuffer.clear (0, numSamples);
        for (int i = 0; i < numOut; ++i)
            outputChannelPointers[i] = outputBuffer.getWritePointer (i);

        // Call JUCE audio callback
        cb->audioDeviceIOCallbackWithContext (inputChannelPointers, numIn,
                             outputChannelPointers, numOut,
                                             numSamples, {});

        // By default, do not send any audio to the real output device in this mode.
        // We still run the callback (so the processor updates visuals/state), but
        // we silence the hardware output.
        if (muteOutputToDevice && outOutputData != nullptr)
        {
            for (UInt32 i = 0; i < outOutputData->mNumberBuffers; ++i)
                zeromem (outOutputData->mBuffers[i].mData,
                         outOutputData->mBuffers[i].mDataByteSize);
            return;
        }

        // Copy output from JUCE buffer to CoreAudio output buffers
        if (outOutputData != nullptr)
        {
            int channelIndex = 0;
            for (UInt32 b = 0; b < outOutputData->mNumberBuffers; ++b)
            {
                auto& abuf = outOutputData->mBuffers[b];
                float* dest = static_cast<float*> (abuf.mData);
                int chansInBuf = (int) abuf.mNumberChannels;

                if (chansInBuf == 1)
                {
                    if (channelIndex < numOut)
                        FloatVectorOperations::copy (dest, outputBuffer.getReadPointer (channelIndex++), numSamples);
                    else
                        FloatVectorOperations::clear (dest, numSamples);
                }
                else
                {
                    for (int ch = 0; ch < chansInBuf; ++ch)
                    {
                        if (channelIndex < numOut)
                        {
                            const float* src = outputBuffer.getReadPointer (channelIndex++);
                            for (int s = 0; s < numSamples; ++s)
                                dest[s * chansInBuf + ch] = src[s];
                        }
                        else
                        {
                            for (int s = 0; s < numSamples; ++s)
                                dest[s * chansInBuf + ch] = 0.0f;
                        }
                    }
                }
            }
        }
    }

    //==============================================================================
    Array<double> getAvailableSampleRates()
    {
        // Process taps effectively dictate the sample-rate. Expose only the tap's
        // sample-rate so the device selector can't pick unsupported values.
        if (tapFormat.mSampleRate > 0.0)
            return { (double) tapFormat.mSampleRate };

        // If we haven't created the tap yet, fall back to the last-known rate.
        if (currentSampleRate > 0.0)
            return { currentSampleRate };

        return { 48000.0 };
    }

    Array<int> getAvailableBufferSizes()
    {
        Array<int> sizes;

        AudioDeviceID devID = (aggregateDeviceID != kAudioObjectUnknown) ? aggregateDeviceID : outputDevice;

        AudioValueRange range { 0, 0 };
        if (caReadProperty (devID, kAudioDevicePropertyBufferFrameSizeRange,
                            kAudioObjectPropertyScopeGlobal, range))
        {
            int lo = jmax (32, (int) range.mMinimum);
            int hi = jmin (16384, (int) range.mMaximum);

            for (int sz = lo; sz <= hi; sz *= 2)
                sizes.add (sz);

            if (sizes.isEmpty() || sizes.getLast() < hi)
                sizes.add (hi);
        }

        if (sizes.isEmpty())
        {
            sizes.add (256);
            sizes.add (512);
            sizes.add (1024);
        }

        return sizes;
    }

    int getDefaultBufferSize()
    {
        auto sizes = getAvailableBufferSizes();

        // Prefer a low-latency default if the device supports it.
        for (auto s : sizes)
            if (s >= 128)
                return s;

        return sizes.isEmpty() ? 128 : sizes[0];
    }

    //==============================================================================
    ProcessAudioInfo process;
    AudioDeviceID outputDevice = 0;
    AudioObjectID tapID = kAudioObjectUnknown;
    AudioObjectID aggregateDeviceID = kAudioObjectUnknown;
    AudioDeviceIOProcID ioProcID = nullptr;
    AudioStreamBasicDescription tapFormat {};

    dispatch_queue_t ioQueue = nullptr;

    std::atomic<AudioIODeviceCallback*> callback { nullptr };
    std::atomic<bool> playing { false };
    bool isDeviceOpen = false;

    int activeInputChannels = 2;
    int activeOutputChannels = 2;
    int numInputChannels = 2;
    int numOutputChannels = 2;
    int currentBufferSize = 512;
    double currentSampleRate = 44100.0;

    juce::AudioBuffer<float> inputBuffer;
    juce::AudioBuffer<float> outputBuffer;

    HeapBlock<const float*> inputChannelPointers;
    HeapBlock<float*> outputChannelPointers;

    String lastError;

    bool muteOutputToDevice = true;
};

//==============================================================================
// MARK: - ProcessAudioDevice Implementation
//==============================================================================

ProcessAudioDevice::ProcessAudioDevice (const String& outputDeviceName,
                                        const ProcessAudioInfo& processInfo,
                                                                                uint32_t outputCoreAudioDeviceID,
                                                                                bool muteOutputToDevice)
    : AudioIODevice (processInfo.name + " -> " + outputDeviceName, "Process Audio"),
      backend (std::make_unique<ProcessTapBackend> (processInfo, (AudioDeviceID) outputCoreAudioDeviceID))
{
        backend->muteOutputToDevice = muteOutputToDevice;

    selectedProcessName = processInfo.name;
    selectedOutputName = outputDeviceName;
    outputNoneSelected = muteOutputToDevice;
}

ProcessAudioDevice::~ProcessAudioDevice()
{
    close();
    backend->destroyAll();
}

StringArray ProcessAudioDevice::getOutputChannelNames()
{
    if (outputNoneSelected)
        return {};

    StringArray names;
    int n = caGetNumChannels ((AudioDeviceID) backend->outputDevice, false);
    if (n <= 0) n = 2;
    for (int i = 0; i < n; ++i)
        names.add ("Output " + String (i + 1));
    return names;
}

StringArray ProcessAudioDevice::getInputChannelNames()
{
    return { "Capture L", "Capture R" };
}

Array<double> ProcessAudioDevice::getAvailableSampleRates()
{
    return backend->getAvailableSampleRates();
}

Array<int> ProcessAudioDevice::getAvailableBufferSizes()
{
    return backend->getAvailableBufferSizes();
}

int ProcessAudioDevice::getDefaultBufferSize()
{
    return backend->getDefaultBufferSize();
}

String ProcessAudioDevice::open (const BigInteger& inputChannels,
                                 const BigInteger& outputChannels,
                                 double sampleRate,
                                 int bufferSizeSamples)
{
    juce::Logger::writeToLog ("ProcessAudioDevice::open: inChans=" + String (inputChannels.countNumberOfSetBits())
                              + " outChans=" + String (outputChannels.countNumberOfSetBits())
                              + " sampleRate=" + String (sampleRate)
                              + " bufferSize=" + String (bufferSizeSamples));
    close();

    // Cache requested channel counts (JUCE uses these to represent which
    // channels the user enabled in the device selector).
    // If JUCE passes 0 (meaning "use default"), we'll enable all available later.
    backend->activeInputChannels  = inputChannels.countNumberOfSetBits();
    backend->activeOutputChannels = outputChannels.countNumberOfSetBits();

    // If the user selected "<< none >>" for output, keep this device input-only
    // from JUCE's perspective. We still use a real output device internally for
    // clocking, but we don't expose output channels.
    if (outputNoneSelected)
        backend->activeOutputChannels = 0;

    // JUCE may pass 0 for sampleRate/bufferSizeSamples when it wants the device
    // to choose a default. Ensure we always choose valid non-zero values.
    double desiredSampleRate = sampleRate;
    if (desiredSampleRate <= 0.0)
    {
        Float64 sr = 0;
        if (caReadProperty (backend->outputDevice, kAudioDevicePropertyNominalSampleRate,
                            kAudioObjectPropertyScopeGlobal, sr)
            && sr > 0.0)
            desiredSampleRate = (double) sr;
        else
            desiredSampleRate = 44100.0;
    }

    int desiredBufferSize = bufferSizeSamples;
    if (desiredBufferSize <= 0)
        desiredBufferSize = backend->getDefaultBufferSize();

    // Set buffer size on the real output device BEFORE creating the aggregate
    // so the sub-device starts at the right block size.
    if (desiredBufferSize > 0)
    {
        UInt32 bs = (UInt32) desiredBufferSize;
        AudioObjectPropertyAddress addr {
            kAudioDevicePropertyBufferFrameSize,
            kAudioObjectPropertyScopeGlobal,
            kElementMain
        };
        AudioObjectSetPropertyData (backend->outputDevice, &addr, 0, nullptr, sizeof (UInt32), &bs);
    }

    // Do NOT set the output device's sample rate yet — we need to learn the
    // tap's native rate first.  Changing the output device rate at this point
    // would alter the system-wide audio clock before the aggregate exists,
    // which can cause other applications (e.g. Spotify) to play at the wrong
    // speed.

    auto error = backend->createTapAndAggregateDevice();
    if (error.isNotEmpty())
    {
        juce::Logger::writeToLog ("ProcessAudioDevice::open: createTapAndAggregateDevice failed: " + error);
        backend->lastError = error;
        return error;
    }

    // If no explicit channel set was requested, enable all available.
    // (Input: tap is stereo, output: derived from chosen output device.)
    if (backend->activeInputChannels <= 0)
        backend->activeInputChannels = ((int) backend->tapFormat.mChannelsPerFrame > 0 ? (int) backend->tapFormat.mChannelsPerFrame : 2);

    if (! outputNoneSelected && backend->activeOutputChannels <= 0)
    {
        const int availOut = caGetNumChannels (backend->outputDevice, false);
        backend->activeOutputChannels = (availOut > 0 ? availOut : 2);
    }

    // Force the device sample rate to match the tap's format where available.
    // Process taps commonly run at 48kHz, and attempting to coerce to 44.1kHz can
    // lead to hangs/blocked starts or silent input.
    if (backend->tapFormat.mSampleRate > 0.0)
        desiredSampleRate = backend->tapFormat.mSampleRate;

    // Now set BOTH the output device and aggregate to the tap-derived rate,
    // so everything runs at the same clock and no system-wide mismatch occurs.
    if (desiredSampleRate > 0.0)
    {
        Float64 sr = (Float64) desiredSampleRate;
        AudioObjectPropertyAddress addr {
            kAudioDevicePropertyNominalSampleRate,
            kAudioObjectPropertyScopeGlobal,
            kElementMain
        };
        AudioObjectSetPropertyData (backend->outputDevice, &addr, 0, nullptr, sizeof (Float64), &sr);

        if (backend->aggregateDeviceID != kAudioObjectUnknown)
            AudioObjectSetPropertyData (backend->aggregateDeviceID, &addr, 0, nullptr, sizeof (Float64), &sr);
    }

    if (desiredBufferSize > 0 && backend->aggregateDeviceID != kAudioObjectUnknown)
    {
        UInt32 bs = (UInt32) desiredBufferSize;
        AudioObjectPropertyAddress addr {
            kAudioDevicePropertyBufferFrameSize,
            kAudioObjectPropertyScopeGlobal,
            kElementMain
        };
        AudioObjectSetPropertyData (backend->aggregateDeviceID, &addr, 0, nullptr, sizeof (UInt32), &bs);
    }

    // Read back the actual values
    Float64 actualSR = 0;
    caReadProperty (backend->aggregateDeviceID, kAudioDevicePropertyNominalSampleRate,
                    kAudioObjectPropertyScopeGlobal, actualSR);
    // Prefer reporting the tap's sample-rate to the host/UI.
    backend->currentSampleRate = (backend->tapFormat.mSampleRate > 0.0
                                      ? (double) backend->tapFormat.mSampleRate
                                      : (actualSR > 0.0 ? (double) actualSR : desiredSampleRate));

    UInt32 actualBS = 0;
    caReadProperty (backend->aggregateDeviceID, kAudioDevicePropertyBufferFrameSize,
                    kAudioObjectPropertyScopeGlobal, actualBS);
    backend->currentBufferSize = (actualBS > 0 ? (int) actualBS : desiredBufferSize);

    // Defensive: never allow 0 to escape.
    if (backend->currentBufferSize <= 0)
        backend->currentBufferSize = 512;
    if (backend->currentSampleRate <= 0.0)
        backend->currentSampleRate = 44100.0;

    backend->isDeviceOpen = true;
    juce::Logger::writeToLog ("ProcessAudioDevice::open: success — sampleRate=" + String (backend->currentSampleRate)
                              + " bufferSize=" + String (backend->currentBufferSize)
                              + " inChans=" + String (backend->activeInputChannels)
                              + " outChans=" + String (backend->activeOutputChannels));
    return {};
}

void ProcessAudioDevice::close()
{
    juce::Logger::writeToLog ("ProcessAudioDevice::close");
    stop();
    backend->destroyAll();
    backend->isDeviceOpen = false;
}

bool ProcessAudioDevice::isOpen()
{
    return backend->isDeviceOpen;
}

void ProcessAudioDevice::start (AudioIODeviceCallback* cb)
{
    juce::Logger::writeToLog ("ProcessAudioDevice::start");
    if (backend->isDeviceOpen && cb != nullptr)
        backend->startIO (cb, this);
    else
        juce::Logger::writeToLog ("ProcessAudioDevice::start: skipped — isOpen=" + String ((int) backend->isDeviceOpen)
                                  + " callback=" + String (cb != nullptr ? "valid" : "null"));
}

void ProcessAudioDevice::stop()
{
    juce::Logger::writeToLog ("ProcessAudioDevice::stop");
    backend->stopIO();
}

bool ProcessAudioDevice::isPlaying()
{
    return backend->playing.load();
}

String ProcessAudioDevice::getLastError()
{
    return backend->lastError;
}

int ProcessAudioDevice::getCurrentBufferSizeSamples()
{
    return backend->currentBufferSize;
}

double ProcessAudioDevice::getCurrentSampleRate()
{
    if (backend->tapFormat.mSampleRate > 0.0)
        return (double) backend->tapFormat.mSampleRate;

    return backend->currentSampleRate;
}

int ProcessAudioDevice::getCurrentBitDepth()
{
    return 32; // float32
}

BigInteger ProcessAudioDevice::getActiveOutputChannels() const
{
    BigInteger chans;
    chans.setRange (0, backend->activeOutputChannels, true);
    return chans;
}

BigInteger ProcessAudioDevice::getActiveInputChannels() const
{
    BigInteger chans;
    chans.setRange (0, backend->activeInputChannels, true);
    return chans;
}

int ProcessAudioDevice::getOutputLatencyInSamples()
{
    UInt32 latency = 0;
    caReadProperty (backend->outputDevice, kAudioDevicePropertyLatency,
                    kAudioObjectPropertyScopeOutput, latency);
    return (int) latency;
}

int ProcessAudioDevice::getInputLatencyInSamples()
{
    return 0; // Process taps have minimal latency
}

//==============================================================================
// MARK: - ProcessAudioDeviceType Implementation
//==============================================================================

ProcessAudioDeviceType::ProcessAudioDeviceType()
    : AudioIODeviceType ("Process Audio")
{
}

ProcessAudioDeviceType::~ProcessAudioDeviceType()
{
}

void ProcessAudioDeviceType::scanForDevices()
{
    juce::Logger::writeToLog ("ProcessAudioDeviceType::scanForDevices");
    inputChoiceNames.clear();
    inputChoices.clear();
    outputChoiceNames.clear();
    outputChoiceIDs.clear();
    combinedDeviceNames.clear();

    // Enumerate processes (these are our "input" choices)
    auto processInfos = enumerateAudioProcesses();
    for (const auto& proc : processInfos)
    {
        inputChoiceNames.add (proc.name);
        inputChoices.add (proc);
    }

    // Make names unique and keep ProcessAudioInfo::name in sync so the device
    // name can be parsed back to a stable selector choice.
    inputChoiceNames.appendNumbersToDuplicates (false, true);
    for (int i = 0; i < inputChoices.size(); ++i)
        inputChoices.getReference (i).name = inputChoiceNames[i];

    // Enumerate possible output choices
    const AudioDeviceID defaultOut = caGetDefaultOutputDevice();
    outputChoiceNames.add ("<< none >>");
    outputChoiceIDs.add ((uint32_t) defaultOut);

    outputChoiceNames.add ("Default Output");
    outputChoiceIDs.add ((uint32_t) defaultOut);

    auto outputs = enumerateOutputDevices();
    for (int i = 0; i < outputs.names.size(); ++i)
    {
        outputChoiceNames.add (outputs.names[i]);
        outputChoiceIDs.add ((uint32_t) outputs.deviceIDs[i]);
    }

    outputChoiceNames.appendNumbersToDuplicates (false, true);

    for (int i = 0; i < inputChoiceNames.size(); ++i)
    {
        for (int o = 0; o < outputChoiceNames.size(); ++o)
            combinedDeviceNames.add (makeCombinedDeviceName (inputChoiceNames[i], outputChoiceNames[o]));
    }

    juce::Logger::writeToLog ("ProcessAudioDeviceType::scanForDevices: found " + String (inputChoices.size())
                              + " processes, " + String (outputChoiceNames.size()) + " outputs");
}

StringArray ProcessAudioDeviceType::getDeviceNames (bool wantInputNames) const
{
    juce::ignoreUnused (wantInputNames);
    return combinedDeviceNames;
}

int ProcessAudioDeviceType::getDefaultDeviceIndex (bool forInput) const
{
    juce::ignoreUnused (forInput);
    const auto defaultCombined = makeCombinedDeviceName ("System Audio", "<< none >>");
    const int idx = combinedDeviceNames.indexOf (defaultCombined);
    return idx >= 0 ? idx : (combinedDeviceNames.isEmpty() ? -1 : 0);
}

int ProcessAudioDeviceType::getIndexOfDevice (AudioIODevice* device, bool asInput) const
{
    if (device == nullptr)
        return -1;

    juce::ignoreUnused (asInput);
    return combinedDeviceNames.indexOf (device->getName());
}

bool ProcessAudioDeviceType::hasSeparateInputsAndOutputs() const
{
    return false;
}

AudioIODevice* ProcessAudioDeviceType::createDevice (const String& outputDeviceName,
                                                     const String& inputDeviceName)
{
    // Defensive: JUCE may call createDevice with stale names while type switching.
    // Ensure we have a fresh snapshot of process/output choices.
    if (inputChoiceNames.isEmpty() || inputChoices.isEmpty() || outputChoiceNames.isEmpty() || outputChoiceIDs.isEmpty())
        scanForDevices();

    // Backwards compatibility: older saved configs used a combined device name.
    String procName = inputDeviceName;
    String outName  = outputDeviceName;

    const auto isNoneChoice = [] (const String& s)
    {
        auto t = s.trim();
        if (t.isEmpty())
            return true;

        return t == "<< none >>";
    };

    const auto combinedName = (outputDeviceName.isNotEmpty() ? outputDeviceName : inputDeviceName);
    if (combinedName.contains (" -> "))
    {
        const auto arrowPos = combinedName.indexOf (" -> ");
        if (arrowPos > 0)
        {
            procName = combinedName.substring (0, arrowPos).trim();
            outName  = combinedName.substring (arrowPos + 4).trim();
        }
    }

    if (isNoneChoice (procName))
        procName = "System Audio";

    const bool outputNoneSelected = isNoneChoice (outputDeviceName) || isNoneChoice (outName);
    if (isNoneChoice (outName))
        outName = "Default Output";

    int procIndex = inputChoiceNames.indexOf (procName);

    if (procIndex < 0)
        procIndex = inputChoiceNames.indexOf ("System Audio");

    if (procIndex < 0 && ! inputChoices.isEmpty())
        procIndex = 0;

    int outIndex = outputChoiceNames.indexOf (outName);
    if (outIndex < 0)
    {
        outName = "Default Output";
        outIndex = outputChoiceNames.indexOf (outName);
    }

    if (outIndex < 0 && ! outputChoiceIDs.isEmpty())
        outIndex = 0;

    if (procIndex < 0 || outIndex < 0)
    {
        juce::Logger::writeToLog ("ProcessAudioDeviceType::createDevice: no matching process/output — procIndex=" + String (procIndex) + " outIndex=" + String (outIndex));
        return nullptr;
    }

    const auto displayOutName = outputNoneSelected ? String ("<< none >>") : outName;

    juce::Logger::writeToLog ("ProcessAudioDeviceType::createDevice: process='" + procName + "' output='" + displayOutName + "'");
    return new ProcessAudioDevice (displayOutName,
                                   inputChoices.getReference (procIndex),
                                   outputChoiceIDs.getReference (outIndex),
                                   /*muteOutputToDevice*/ outputNoneSelected);
}

#endif // JUCE_MAC && OSCI_PREMIUM

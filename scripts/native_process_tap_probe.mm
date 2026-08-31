// Standalone CoreAudio reproduction: no JUCE, visualiser, DSP, or audio buffering.
#import <Cocoa/Cocoa.h>
#import <CoreAudio/CoreAudio.h>
#import <CoreAudio/AudioHardwareTapping.h>
#import <CoreAudio/CATapDescription.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>
#include <mach/mach_time.h>
#include <stdexcept>
#include <string>
#include <thread>

static void check(OSStatus status, const char* operation) {
    if (status != noErr) {
        throw std::runtime_error(std::string(operation) + ": " + std::to_string(status));
    }
}

template <typename T> static T readProperty(AudioObjectID object, AudioObjectPropertySelector selector) {
    T value {};
    UInt32 size = sizeof(value);
    AudioObjectPropertyAddress address { selector, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
    check(AudioObjectGetPropertyData(object, &address, 0, nullptr, &size, &value), "get property");
    return value;
}

template <typename T> static void writeProperty(AudioObjectID object, AudioObjectPropertySelector selector, T value) {
    AudioObjectPropertyAddress address { selector, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
    check(AudioObjectSetPropertyData(object, &address, 0, nullptr, sizeof(value), &value), "set property");
}

struct Devices {
    AudioObjectID tap = 0, aggregate = 0, physical = 0;
    AudioDeviceIOProcID callback = nullptr;
    UInt32 originalFrames = 0;
    bool started = false;
    ~Devices() {
        if (callback != nullptr) {
            if (started) {
                AudioDeviceStop(aggregate, callback);
            }
            AudioDeviceDestroyIOProcID(aggregate, callback);
        }
        if (aggregate != 0) {
            AudioHardwareDestroyAggregateDevice(aggregate);
        }
        if (tap != 0) {
            AudioHardwareDestroyProcessTap(tap);
        }
        if (physical != 0 && originalFrames != 0) {
            AudioObjectPropertyAddress address { kAudioDevicePropertyBufferFrameSize, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
            AudioObjectSetPropertyData(physical, &address, 0, nullptr, sizeof(originalFrames), &originalFrames);
        }
    }
};

struct Measurements {
    std::atomic<bool> enabled { false };
    uint64_t callbacks = 0, zeros = 0, nulls = 0, timestampGaps = 0, silentFrames = 0, longestSilentFrames = 0;
    uint64_t previousEntry = 0, maxGapTicks = 0, maxDurationTicks = 0, totalDurationTicks = 0;
    double previousSampleTime = 0;
    int previousFrames = 0, minFrames = std::numeric_limits<int>::max(), maxFrames = 0;
    bool previousTimeValid = false;
    float peak = 0;

    void process(const AudioBufferList* input, const AudioTimeStamp* timestamp, AudioBufferList* output) noexcept {
        const auto entered = mach_absolute_time();
        if (output != nullptr) {
            for (UInt32 b = 0; b < output->mNumberBuffers; ++b) {
                if (output->mBuffers[b].mData != nullptr) {
                    std::memset(output->mBuffers[b].mData, 0, output->mBuffers[b].mDataByteSize);
                }
            }
        }
        if (!enabled.load(std::memory_order_relaxed)) {
            return;
        }
        int frames = 0;
        float blockPeak = 0;
        bool nullInput = input == nullptr || input->mNumberBuffers == 0;
        if (input != nullptr) {
            for (UInt32 b = 0; b < input->mNumberBuffers; ++b) {
                const auto& buffer = input->mBuffers[b];
                const auto* samples = static_cast<const float*>(buffer.mData);
                if (b == 0 && buffer.mNumberChannels != 0) {
                    frames = int(buffer.mDataByteSize / (sizeof(float) * buffer.mNumberChannels));
                }
                nullInput |= samples == nullptr;
                if (samples != nullptr) {
                    for (size_t i = 0; i < buffer.mDataByteSize / sizeof(float); ++i) {
                        blockPeak = std::max(blockPeak, std::abs(samples[i]));
                    }
                }
            }
        }
        ++callbacks;
        zeros += blockPeak == 0;
        nulls += nullInput;
        peak = std::max(peak, blockPeak);
        minFrames = std::min(minFrames, frames);
        maxFrames = std::max(maxFrames, frames);
        silentFrames = blockPeak == 0 ? silentFrames + frames : 0;
        longestSilentFrames = std::max(longestSilentFrames, silentFrames);
        const bool valid = timestamp != nullptr && (timestamp->mFlags & kAudioTimeStampSampleTimeValid) != 0;
        if (previousEntry != 0) {
            maxGapTicks = std::max(maxGapTicks, entered - previousEntry);
            if (valid && previousTimeValid) {
                timestampGaps += std::abs(timestamp->mSampleTime - previousSampleTime - previousFrames) > 0.5;
            }
        }
        previousEntry = entered;
        previousTimeValid = valid;
        previousSampleTime = valid ? timestamp->mSampleTime : 0;
        previousFrames = frames;
        const auto duration = mach_absolute_time() - entered;
        maxDurationTicks = std::max(maxDurationTicks, duration);
        totalDurationTicks += duration;
    }
};

static void waitSeconds(double seconds) {
    const auto end = CFAbsoluteTimeGetCurrent() + seconds;
    while (CFAbsoluteTimeGetCurrent() < end) {
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.1, false);
    }
}

int main(int argc, const char* argv[]) {
    @autoreleasepool {
        if (argc != 3) {
            return 2;
        }
        [NSApplication sharedApplication];
        NSMutableDictionary* result = [NSMutableDictionary dictionary];
        int exitCode = 0;
        try {
            NSData* data = [NSData dataWithContentsOfFile:@(argv[1])];
            NSDictionary* config = [NSJSONSerialization JSONObjectWithData:data options:0 error:nil];
            if (config == nil) {
                throw std::runtime_error("Cannot read configuration");
            }
            result[@"config"] = config;
            Measurements measurements;
            Devices devices;
            devices.physical = readProperty<AudioDeviceID>(kAudioObjectSystemObject, kAudioHardwarePropertyDefaultOutputDevice);
            devices.originalFrames = readProperty<UInt32>(devices.physical, kAudioDevicePropertyBufferFrameSize);
            const UInt32 requested = [config[@"frames"] unsignedIntValue];
            const UInt32 physicalFrames = [config[@"physicalFrames"] unsignedIntValue];
            writeProperty(devices.physical, kAudioDevicePropertyBufferFrameSize, physicalFrames);
            CATapDescription* tap = [[CATapDescription alloc] initStereoGlobalTapButExcludeProcesses:@[]];
            tap.UUID = [NSUUID UUID];
            tap.name = @"Native process-tap probe";
            tap.privateTap = YES;
            tap.muteBehavior = CATapUnmuted;
            check(AudioHardwareCreateProcessTap(tap, &devices.tap), "create tap");
            const auto format = readProperty<AudioStreamBasicDescription>(devices.tap, kAudioTapPropertyFormat);
            if (format.mFormatID != kAudioFormatLinearPCM || (format.mFormatFlags & kAudioFormatFlagIsFloat) == 0
                || (format.mFormatFlags & kAudioFormatFlagIsBigEndian) != 0 || format.mBitsPerChannel != 32) {
                throw std::runtime_error("Probe requires native Float32 PCM");
            }
            result[@"sampleRate"] = @(format.mSampleRate);
            NSMutableDictionary* description = [@{
                @(kAudioAggregateDeviceNameKey): @"Native process-tap probe",
                @(kAudioAggregateDeviceUIDKey): NSUUID.UUID.UUIDString,
                @(kAudioAggregateDeviceIsPrivateKey): @YES,
                @(kAudioAggregateDeviceIsStackedKey): @NO,
                @(kAudioAggregateDeviceTapAutoStartKey): config[@"autoStart"],
                @(kAudioAggregateDeviceTapListKey): @[@{
                    @(kAudioSubTapUIDKey): tap.UUID.UUIDString,
                    @(kAudioSubTapDriftCompensationKey): config[@"drift"]
                }]
            } mutableCopy];
            if ([config[@"attachedOutput"] boolValue]) {
                CFStringRef uid = readProperty<CFStringRef>(devices.physical, kAudioDevicePropertyDeviceUID);
                NSString* outputUID = CFBridgingRelease(uid);
                description[@(kAudioAggregateDeviceMainSubDeviceKey)] = outputUID;
                description[@(kAudioAggregateDeviceSubDeviceListKey)] = @[@{ @(kAudioSubDeviceUIDKey): outputUID }];
            }
            check(AudioHardwareCreateAggregateDevice((__bridge CFDictionaryRef) description, &devices.aggregate), "create aggregate");
            waitSeconds(0.5);
            writeProperty(devices.aggregate, kAudioDevicePropertyNominalSampleRate, format.mSampleRate);
            writeProperty(devices.aggregate, kAudioDevicePropertyBufferFrameSize, requested);
            writeProperty(devices.physical, kAudioDevicePropertyBufferFrameSize, physicalFrames);
            auto* stats = &measurements;
            check(AudioDeviceCreateIOProcIDWithBlock(&devices.callback, devices.aggregate, nullptr,
                ^(const AudioTimeStamp*, const AudioBufferList* input, const AudioTimeStamp* time, AudioBufferList* output, const AudioTimeStamp*) {
                    stats->process(input, time, output);
                }), "create IOProc");
            // Let the main run loop service CoreAudio/TCC notifications during startup.
            // The IOProc itself is still invoked directly on CoreAudio's audio thread.
            std::atomic<bool> startFinished { false };
            OSStatus startStatus = noErr;
            std::thread starter([&] {
                startStatus = AudioDeviceStart(devices.aggregate, devices.callback);
                startFinished.store(true, std::memory_order_release);
            });
            while (!startFinished.load(std::memory_order_acquire)) {
                waitSeconds(0.1);
            }
            starter.join();
            check(startStatus, "start IOProc");
            devices.started = true;
            waitSeconds([config[@"warmup"] doubleValue]);
            result[@"physicalStart"] = @(readProperty<UInt32>(devices.physical, kAudioDevicePropertyBufferFrameSize));
            result[@"aggregateStart"] = @(readProperty<UInt32>(devices.aggregate, kAudioDevicePropertyBufferFrameSize));
            measurements.enabled.store(true, std::memory_order_relaxed);
            waitSeconds([config[@"seconds"] doubleValue]);
            check(AudioDeviceStop(devices.aggregate, devices.callback), "stop IOProc");
            devices.started = false;
            result[@"physicalEnd"] = @(readProperty<UInt32>(devices.physical, kAudioDevicePropertyBufferFrameSize));
            result[@"aggregateEnd"] = @(readProperty<UInt32>(devices.aggregate, kAudioDevicePropertyBufferFrameSize));
            mach_timebase_info_data_t timebase {};
            mach_timebase_info(&timebase);
            const double ticksToMs = double(timebase.numer) / timebase.denom / 1.0e6;
            result[@"callbacks"] = @(measurements.callbacks);
            result[@"zeroBlocks"] = @(measurements.zeros);
            result[@"timestampGaps"] = @(measurements.timestampGaps);
            result[@"nullInput"] = @(measurements.nulls);
            result[@"minFrames"] = @(measurements.callbacks == 0 ? 0 : measurements.minFrames);
            result[@"maxFrames"] = @(measurements.maxFrames);
            result[@"peak"] = @(measurements.peak);
            result[@"maxGapMs"] = @(measurements.maxGapTicks * ticksToMs);
            result[@"maxCallbackMs"] = @(measurements.maxDurationTicks * ticksToMs);
            result[@"longestZeroMs"] = @(1000.0 * measurements.longestSilentFrames / format.mSampleRate);
            result[@"zeroPercent"] = @(measurements.callbacks == 0 ? 0 : 100.0 * measurements.zeros / measurements.callbacks);
            result[@"validSignal"] = @(measurements.callbacks > 0 && measurements.peak > 0);
        } catch (const std::exception& error) {
            result[@"error"] = @(error.what());
            exitCode = 1;
        }
        NSData* json = [NSJSONSerialization dataWithJSONObject:result options:NSJSONWritingPrettyPrinted error:nil];
        [json writeToFile:@(argv[2]) atomically:YES];
        return exitCode;
    }
}

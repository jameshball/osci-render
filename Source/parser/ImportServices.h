#pragma once

#include <JuceHeader.h>

class FileParser;

// Shared source import boundary. Runtime getters are called during rendering and
// must not allocate, block, or perform I/O. Products retain their parameter state
// until rendering has stopped, and detach UI/owner services before owner teardown.
class ImportServices {
public:
    virtual ~ImportServices() = default;

    virtual double getSampleRate() const = 0;
    virtual float getImageThreshold(int blockSampleIndex) const = 0;
    virtual int getImageStride(int blockSampleIndex) const = 0;
    virtual bool getImageInverted() const = 0;
    virtual int getFractalDepth() const = 0;

    struct ImageSampleSettings {
        double sampleRate = 44100.0;
        float threshold = 0.0f;
        int stride = 1;
        bool inverted = false;
    };

    // Everything an image source reads per sample, fetched in one call so
    // implementations can take any owner lock once per sample.
    virtual ImageSampleSettings getImageSampleSettings(int blockSampleIndex) const {
        return { getSampleRate(), getImageThreshold(blockSampleIndex), getImageStride(blockSampleIndex), getImageInverted() };
    }

    virtual juce::File getFFmpegFile() const = 0;
    virtual void ensureFFmpegExists(std::function<void()> ready) = 0;
    virtual void showError(juce::String title, juce::String message) = 0;
    virtual void confirmLargeFile(juce::String message, std::function<void()> accepted, std::function<void()> cancelled) = 0;

    // Called on the message thread. Acquire product locks in their established
    // order, invoke load synchronously, then notify listeners after releasing them only if load returned true.
    // A detached service must discard the operation without invoking load.
    virtual void performDeferredLoad(std::function<bool()> load) = 0;
    virtual void removeSource(FileParser* parser) = 0;
};

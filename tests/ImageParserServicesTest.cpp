#include <JuceHeader.h>
#include "../Source/parser/img/ImageParser.h"

namespace {
class CountingImportServices final : public ImportServices {
public:
    double getSampleRate() const override {
        ++individualCalls;
        return 48000.0;
    }

    float getImageThreshold(int) const override {
        ++individualCalls;
        return 0.25f;
    }

    int getImageStride(int) const override {
        ++individualCalls;
        return 2;
    }

    bool getImageInverted() const override {
        ++individualCalls;
        return true;
    }

    int getFractalDepth() const override { return 1; }

    ImageSampleSettings getImageSampleSettings(int blockSampleIndex) const override {
        ++batchedCalls;
        lastBlockSampleIndex = blockSampleIndex;
        ImageSampleSettings settings;
        settings.sampleRate = 48000.0;
        return settings;
    }

    juce::File getFFmpegFile() const override { return {}; }
    void ensureFFmpegExists(std::function<void()>) override {}
    void showError(juce::String, juce::String) override {}
    void confirmLargeFile(juce::String, std::function<void()>, std::function<void()>) override {}
    void performDeferredLoad(std::function<bool()>) override {}
    void removeSource(FileParser*) override {}

    mutable int individualCalls = 0;
    mutable int batchedCalls = 0;
    mutable int lastBlockSampleIndex = -1;
};

// Relies on the base class composing the settings from the individual getters.
class DefaultImportServices final : public ImportServices {
public:
    double getSampleRate() const override { return 22050.0; }
    float getImageThreshold(int blockSampleIndex) const override { return static_cast<float>(blockSampleIndex) * 0.1f; }
    int getImageStride(int) const override { return 3; }
    bool getImageInverted() const override { return true; }
    int getFractalDepth() const override { return 1; }
    juce::File getFFmpegFile() const override { return {}; }
    void ensureFFmpegExists(std::function<void()>) override {}
    void showError(juce::String, juce::String) override {}
    void confirmLargeFile(juce::String, std::function<void()>, std::function<void()>) override {}
    void performDeferredLoad(std::function<bool()>) override {}
    void removeSource(FileParser*) override {}
};
}

class ImageParserServicesTest : public juce::UnitTest {
public:
    ImageParserServicesTest() : juce::UnitTest("Image parser import services", "Parser") {}

    void runTest() override {
        beginTest("Image samples read import settings once per sample");
        {
            auto services = std::make_shared<CountingImportServices>();
            ImageParser parser(services, 8, 8);
            std::vector<std::uint8_t> rgba(8 * 8 * 4, 255);
            parser.setSingleFrameFromRgba(rgba, 8, 8, false);

            constexpr int numSamples = 64;
            for (int i = 0; i < numSamples; ++i) {
                const auto point = parser.getSample(i);
                expect(point.x >= -1.0f && point.x <= 1.0f && point.y >= -1.0f && point.y <= 1.0f);
            }
            expectEquals(services->batchedCalls, numSamples);
            expectEquals(services->individualCalls, 0);
            expectEquals(services->lastBlockSampleIndex, numSamples - 1);
        }

        beginTest("Default import settings compose the individual getters");
        {
            DefaultImportServices services;
            const auto settings = services.getImageSampleSettings(5);
            expectEquals(settings.sampleRate, 22050.0);
            expectWithinAbsoluteError(settings.threshold, 0.5f, 1.0e-6f);
            expectEquals(settings.stride, 3);
            expect(settings.inverted);
        }
    }
};

static ImageParserServicesTest imageParserServicesTest;

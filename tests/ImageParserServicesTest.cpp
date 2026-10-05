#include <JuceHeader.h>
#include "../Source/parser/img/ImageParser.h"

namespace {
class CountingImportServices final : public ImportServices {
public:
    double getSampleRate() const override {
        ++individualCalls;
        return 48000.0;
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
    }
};

static ImageParserServicesTest imageParserServicesTest;

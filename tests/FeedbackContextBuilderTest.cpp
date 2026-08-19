#include "JuceHeader.h"

class FeedbackContextBuilderTest final : public juce::UnitTest {
public:
    FeedbackContextBuilderTest()
        : juce::UnitTest("Feedback context builder", "Feedback") {}

    void runTest() override {
        beginTest("Product and environment fields compose fluently");
        osci::FeedbackRequest request;
        osci::FeedbackContextBuilder(request)
            .withProduct("osci-render", "4.2.1.37", "premium")
            .withSystemInfo()
            .withPluginHost(juce::AudioProcessor::wrapperType_Standalone)
            .withDisplay({ 3840, 2160, 2.0 });

        expectEquals(request.productSlug, juce::String("osci-render"));
        expectEquals(request.productVersion, juce::String("4.2.1.37"));
        expectEquals(request.productBuild, juce::String("37"));
        expectEquals(request.productVariant, juce::String("premium"));
        expect(request.platform.isNotEmpty());
        expect(request.osName.isNotEmpty());
        expect(request.osVersion.isNotEmpty());
        expect(request.architecture.isNotEmpty());
        expectEquals(request.hostApplication, juce::String("Standalone"));
        expectEquals(request.pluginFormat, juce::String("Standalone"));
        expectEquals(request.displayWidth, 3840);
        expectEquals(request.displayHeight, 2160);
        expectWithinAbsoluteError(request.displayScale, 2.0, 0.0001);

        beginTest("Build extraction handles normal and empty versions");
        expectEquals(osci::FeedbackContextBuilder::buildFromVersion("2.5.0.104"), juce::String("104"));
        expectEquals(osci::FeedbackContextBuilder::buildFromVersion("7"), juce::String("7"));
        expect(osci::FeedbackContextBuilder::buildFromVersion({}).isEmpty());

        beginTest("Plugin formats use API values");
        expectEquals(osci::FeedbackContextBuilder::pluginFormat(juce::AudioProcessor::wrapperType_VST3), juce::String("VST3"));
        expectEquals(osci::FeedbackContextBuilder::pluginFormat(juce::AudioProcessor::wrapperType_AudioUnit), juce::String("AU"));
        expectEquals(osci::FeedbackContextBuilder::pluginFormat(juce::AudioProcessor::wrapperType_LV2), juce::String("LV2"));
        expectEquals(osci::FeedbackContextBuilder::pluginFormat(juce::AudioProcessor::wrapperType_Undefined), juce::String("Other"));
    }
};

static FeedbackContextBuilderTest feedbackContextBuilderTest;

#include <JuceHeader.h>

#include "../Source/recording/RecordingExporter.h"

namespace {

class RecordingExporterTest final : public juce::UnitTest {
public:
    RecordingExporterTest() : juce::UnitTest("Recording exporter", "Recording") {}

    void runTest() override {
        beginTest("Single media artifacts are copied directly");
        LiveRecordingArtifacts audioArtifacts;
        audioArtifacts.audio = std::make_unique<juce::TemporaryFile>(".wav");
        expect(audioArtifacts.audio->getFile().replaceWithText("audio"));
        juce::TemporaryFile audioDestination(".wav");
        RecordingExporter exporter({});
        expect(static_cast<bool>(exporter.exportRecording(audioArtifacts, audioDestination.getFile())));
        expectEquals(audioDestination.getFile().loadFileAsString(), juce::String("audio"));

        beginTest("Combined artifacts use the injected muxer");
        LiveRecordingArtifacts combinedArtifacts;
        combinedArtifacts.audio = std::make_unique<juce::TemporaryFile>(".wav");
        combinedArtifacts.video = std::make_unique<juce::TemporaryFile>(".mov");
        bool muxCalled = false;
        RecordingExporter muxingExporter([&muxCalled](const juce::File&, const juce::File&, const juce::File& destination,
                                                       const juce::StringArray&, juce::String&) {
            muxCalled = true;
            return destination.replaceWithText("muxed");
        });
        juce::TemporaryFile muxDestination(".mov");
        expect(static_cast<bool>(muxingExporter.exportRecording(combinedArtifacts, muxDestination.getFile())));
        expect(muxCalled);
        expectEquals(muxDestination.getFile().loadFileAsString(), juce::String("muxed"));

        beginTest("Missing artifacts report an error");
        LiveRecordingArtifacts missing;
        const auto result = exporter.exportRecording(missing, muxDestination.getFile());
        expect(!static_cast<bool>(result));
        expect(result.message.isNotEmpty());
    }
};

static RecordingExporterTest recordingExporterTest;

}

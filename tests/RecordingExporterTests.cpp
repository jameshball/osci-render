#include <JuceHeader.h>

#include "../Source/recording/RecordingExporter.h"
#include "../Source/video/FFmpegEncoderManager.h"

#include <thread>

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
        expect(audioDestination.getFile().replaceWithText("previous recording"));
        RecordingExporter exporter;
        expect(static_cast<bool>(exporter.exportRecording(audioArtifacts, audioDestination.getFile())));
        expectEquals(audioDestination.getFile().loadFileAsString(), juce::String("audio"));
        expectEquals(audioArtifacts.audio->getFile().loadFileAsString(), juce::String("audio"));

        beginTest("An unreadable source preserves the previous destination");
        expect(audioArtifacts.audio->getFile().deleteFile());
        const auto copyFailure = exporter.exportRecording(audioArtifacts, audioDestination.getFile());
        expect(!static_cast<bool>(copyFailure));
        expect(copyFailure.message.isNotEmpty());
        expectEquals(audioDestination.getFile().loadFileAsString(), juce::String("audio"));

        beginTest("Muxing requires an encoder manager");
        LiveRecordingArtifacts combinedArtifacts;
        combinedArtifacts.audio = std::make_unique<juce::TemporaryFile>(".wav");
        combinedArtifacts.video = std::make_unique<juce::TemporaryFile>(".mov");
        juce::TemporaryFile muxDestination(".mov");
        expect(muxDestination.getFile().replaceWithText("previous video"));
        const auto muxResult = exporter.exportRecording(combinedArtifacts, muxDestination.getFile());
        expect(!static_cast<bool>(muxResult));
        expect(muxResult.message.isNotEmpty());
        expectEquals(muxDestination.getFile().loadFileAsString(), juce::String("previous video"));

        beginTest("Missing artifacts report an error");
        LiveRecordingArtifacts missing;
        const auto result = exporter.exportRecording(missing, muxDestination.getFile());
        expect(!static_cast<bool>(result));
        expect(result.message.isNotEmpty());
        expectEquals(muxDestination.getFile().loadFileAsString(), juce::String("previous video"));
#if JUCE_MAC || JUCE_LINUX
        testMuxPipeDraining();
#endif
    }

private:
#if JUCE_MAC || JUCE_LINUX
    void testMuxPipeDraining() {
        beginTest("Mux drains more than a pipe of diagnostics and retains the final error");
        juce::TemporaryFile executable(".sh");
        expect(executable.getFile().replaceWithText(
            "#!/bin/sh\n"
            "if [ \"$1\" = -encoders ]; then exit 0; fi\n"
            "i=0\n"
            "while [ $i -lt 10000 ]; do printf 'A repeated encoder diagnostic message.\\n' >&2; i=$((i+1)); done\n"
            "printf 'FINAL MUX ERROR' >&2\n"
            "exit 1\n", false, false, "\n"));
        expect(executable.getFile().setExecutePermission(true));
        FFmpegEncoderManager manager(executable.getFile());
        juce::TemporaryFile video(".mov"), audio(".wav"), destination(".mov");
        expect(video.getFile().replaceWithText("video"));
        expect(audio.getFile().replaceWithText("audio"));
        juce::String error;
        std::atomic<bool> cancelled { false };
        juce::WaitableEvent completed;
        std::thread watchdog([&] {
            if (!completed.wait(5000)) {
                cancelled.store(true);
            }
        });
        const bool success = manager.muxAudioAndVideo(video.getFile(), audio.getFile(), destination.getFile(), {}, error, &cancelled);
        completed.signal();
        watchdog.join();
        expect(!success);
        expect(!cancelled.load(), "Mux must finish without filling its diagnostic pipe");
        expect(error.endsWith("FINAL MUX ERROR"), "Unexpected mux error: " + error);
        expect(error.getNumBytesAsUTF8() <= 64 * 1024);

        beginTest("Silent mux remains cancellable while its reader waits for output");
        expect(executable.getFile().replaceWithText(
            "#!/bin/sh\n"
            "if [ \"$1\" = -encoders ]; then exit 0; fi\n"
            "while :; do :; done\n", false, false, "\n"));
        expect(executable.getFile().setExecutePermission(true));
        cancelled.store(false);
        std::thread cancelThread([&] {
            juce::Thread::sleep(100);
            cancelled.store(true);
        });
        expect(!manager.muxAudioAndVideo(video.getFile(), audio.getFile(), destination.getFile(), {}, error, &cancelled));
        cancelThread.join();
        expectEquals(error, juce::String("Cancelled."));
    }
#endif

};

static RecordingExporterTest recordingExporterTest;

}

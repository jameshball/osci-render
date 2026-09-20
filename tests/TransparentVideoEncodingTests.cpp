#include <JuceHeader.h>

#include <array>

#include "../Source/video/FFmpegEncoderManager.h"

namespace {

juce::File findFFmpegExecutable() {
    juce::ChildProcess process;
#if JUCE_WINDOWS
    const juce::StringArray command {"where.exe", "ffmpeg.exe"};
#else
    const juce::StringArray command {"/usr/bin/which", "ffmpeg"};
#endif
    if (!process.start(command, juce::ChildProcess::wantStdOut)
        || !process.waitForProcessToFinish(5000)) {
        return {};
    }
    juce::StringArray matches;
    matches.addLines(process.readAllProcessOutput());
    return matches.isEmpty() ? juce::File() : juce::File(matches[0].trim());
}

bool runProcess(const juce::StringArray& command, juce::String& output) {
    juce::ChildProcess process;
    if (!process.start(command, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr)) {
        output = "Could not start process.";
        return false;
    }
    if (!process.waitForProcessToFinish(15000)) {
        process.kill();
        output = "Process timed out.";
        return false;
    }
    output = process.readAllProcessOutput();
    return process.getExitCode() == 0;
}

class TransparentVideoEncodingTest : public juce::UnitTest {
public:
    TransparentVideoEncodingTest() : juce::UnitTest("Transparent video encoding", "Video") {}

    void runTest() override {
#if JUCE_MAC || JUCE_LINUX
        beginTest("Encoder discovery refreshes after FFmpeg is installed");
        juce::TemporaryFile installedEncoder(".sh");
        FFmpegEncoderManager installedManager(installedEncoder.getFile());
        expect(!installedManager.supportsVideoCodec(VideoCodec::H264));
        const juce::String encoderScript = "#!/bin/sh\nprintf '%s\\n' header header header header header header header header header header ' V..... libx264 H.264'\n";
        expect(installedEncoder.getFile().replaceWithText(encoderScript, false, false, "\n"));
        expect(installedEncoder.getFile().setExecutePermission(true));
        installedManager.refreshAvailableEncoders();
        expect(installedManager.supportsVideoCodec(VideoCodec::H264));
        expect(installedEncoder.getFile().deleteFile());
        installedManager.refreshAvailableEncoders();
        expect(!installedManager.supportsVideoCodec(VideoCodec::H264));
#endif
        const auto ffmpeg = findFFmpegExecutable();
        if (!ffmpeg.existsAsFile()) {
            logMessage("Skipping transparent video tests because FFmpeg is not on PATH.");
            return;
        }

        FFmpegEncoderManager manager(ffmpeg);
        if (!manager.supportsTransparentVideoEncoding()) {
            logMessage("Skipping transparent video tests because FFmpeg does not provide prores_ks.");
            return;
        }

        beginTest("ProRes 4444 command preserves alpha");
        juce::TemporaryFile commandOutput(".mov");
        const auto command = manager.buildVideoEncodingCommand(VideoCodec::ProRes4444, 20, 2, 2, 1.0, "fast", commandOutput.getFile(), true);
        expect(command.contains("-c:v prores_ks"));
        expect(command.contains("-profile:v 4444"));
        expect(command.contains("-pix_fmt yuva444p10le"));

        beginTest("FFmpeg ProRes 4444 round trip preserves alpha");
        juce::TemporaryFile rawInput(".rgba");
        juce::TemporaryFile encodedVideo(".mov");
        juce::TemporaryFile decodedOutput(".rgba");
        const std::array<unsigned char, 16> pixels = {
            255, 0, 0, 0,
            0, 255, 0, 64,
            0, 0, 255, 128,
            255, 255, 255, 255,
        };
        expect(rawInput.getFile().replaceWithData(pixels.data(), pixels.size()));

        juce::String processOutput;
        const juce::StringArray encodeCommand {
            ffmpeg.getFullPathName(), "-v", "error", "-f", "rawvideo", "-pixel_format", "rgba",
            "-video_size", "2x2", "-framerate", "1", "-i", rawInput.getFile().getFullPathName(),
            "-frames:v", "1", "-c:v", "prores_ks", "-profile:v", "4444", "-pix_fmt", "yuva444p10le",
            "-y", encodedVideo.getFile().getFullPathName(),
        };
        expect(runProcess(encodeCommand, processOutput), processOutput);
        if (!encodedVideo.getFile().existsAsFile()) {
            return;
        }

        const juce::StringArray decodeCommand {
            ffmpeg.getFullPathName(), "-v", "error", "-i", encodedVideo.getFile().getFullPathName(),
            "-frames:v", "1", "-f", "rawvideo", "-pix_fmt", "rgba", "-y", decodedOutput.getFile().getFullPathName(),
        };
        expect(runProcess(decodeCommand, processOutput), processOutput);

        juce::MemoryBlock decoded;
        expect(decodedOutput.getFile().loadFileAsData(decoded));
        expectEquals(static_cast<int>(decoded.getSize()), 16);
        if (decoded.getSize() == 16) {
            const auto* decodedPixels = static_cast<const unsigned char*>(decoded.getData());
            for (size_t pixel = 0; pixel < 4; ++pixel) {
                expectWithinAbsoluteError(static_cast<int>(decodedPixels[pixel * 4 + 3]), static_cast<int>(pixels[pixel * 4 + 3]), 2);
            }
        }
    }
};

static TransparentVideoEncodingTest transparentVideoEncodingTest;

}

#include "FFmpegEncoderManager.h"
#include <cmath>
#include <string>
#include <thread>

FFmpegEncoderManager::FFmpegEncoderManager(const juce::File& ffmpegExecutable)
    : ffmpegExecutable(ffmpegExecutable) {
    refreshAvailableEncoders();
}

juce::String FFmpegEncoderManager::buildVideoEncodingCommand(
    VideoCodec codec,
    int crf,
    int width,
    int height,
    double frameRate,
    const juce::String& compressionPreset,
    const juce::File& outputFile,
    bool preserveAlpha) {
    if (!supportsVideoCodec(preserveAlpha ? VideoCodec::ProRes4444 : codec)) {
        return {};
    }
    if (preserveAlpha) {
        return buildProRes4444AlphaEncodingCommand(width, height, frameRate, outputFile);
    }

    switch (codec) {
        case VideoCodec::H264:
            return buildH264EncodingCommand(crf, width, height, frameRate, compressionPreset, outputFile);
        case VideoCodec::H265:
            return buildH265EncodingCommand(crf, width, height, frameRate, compressionPreset, outputFile);
        case VideoCodec::VP9:
            return buildVP9EncodingCommand(crf, width, height, frameRate, compressionPreset, outputFile);
        case VideoCodec::ProRes:
            return buildProResEncodingCommand(width, height, frameRate, outputFile);
        case VideoCodec::ProRes4444:
            return buildProRes4444AlphaEncodingCommand(width, height, frameRate, outputFile);
        default:
            // Default to H.264 if unknown codec
            return buildH264EncodingCommand(crf, width, height, frameRate, compressionPreset, outputFile);
    }
}

bool FFmpegEncoderManager::muxAudioAndVideo(const juce::File& videoInput, const juce::File& audioInput, const juce::File& output,
                                            const juce::StringArray& audioCodecArgs, juce::String& error, const std::atomic<bool>* cancelRequested) const {
    error.clear();
    if (!ffmpegExecutable.existsAsFile()) {
        error = "FFmpeg executable not found.";
        return false;
    }
    if (!videoInput.existsAsFile()) {
        error = "Temporary video file was not created.";
        return false;
    }
    if (!audioInput.existsAsFile()) {
        error = "Input audio file not found.";
        return false;
    }
    if (output.existsAsFile() && !output.deleteFile()) {
        error = "Could not replace output file.";
        return false;
    }

    juce::StringArray command { ffmpegExecutable.getFullPathName(),
                                "-hide_banner", "-loglevel", "error",
                                "-i", videoInput.getFullPathName(),
                                "-i", audioInput.getFullPathName(),
                                "-c:v", "copy" };
    command.addArray(audioCodecArgs);
    command.addArray({ "-shortest", "-y", output.getFullPathName() });

    juce::ChildProcess process;
    if (!process.start(command, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr)) {
        error = "Failed to start FFmpeg for audio mux.";
        return false;
    }

    // Draining a child pipe can block, so keep cancellation on this thread.
    // Only the reader touches output storage; join before inspecting it.
    std::string outputTail;
    std::thread outputReader([&] {
        constexpr std::size_t maxOutputBytes = 64 * 1024;
        char buffer[4096];
        for (;;) {
            const int count = process.readProcessOutput(buffer, sizeof(buffer));
            if (count <= 0) {
                return;
            }
            outputTail.append(buffer, static_cast<std::size_t>(count));
            if (outputTail.size() > maxOutputBytes) {
                outputTail.erase(0, outputTail.size() - maxOutputBytes);
            }
        }
    });

    bool cancelled = false;
    auto* exportJob = juce::ThreadPoolJob::getCurrentThreadPoolJob();
    while (process.isRunning()) {
        if ((cancelRequested != nullptr && cancelRequested->load()) || juce::Thread::currentThreadShouldExit()
            || (exportJob != nullptr && exportJob->shouldExit())) {
            process.kill();
            cancelled = true;
            break;
        }
        process.waitForProcessToFinish(200);
    }
    outputReader.join();
    if (cancelled) {
        process.waitForProcessToFinish(200);
        error = "Cancelled.";
        return false;
    }

    const auto processOutput = juce::String::fromUTF8(outputTail.data(), static_cast<int>(outputTail.size()));
    const bool succeeded = process.getExitCode() == 0 && output.existsAsFile() && output.getSize() > 0;
    if (!succeeded) {
        output.deleteFile();
        error = processOutput.isNotEmpty() ? processOutput : "FFmpeg mux failed.";
    }
    return succeeded;
}

int FFmpegEncoderManager::estimateBitrateForVideotoolbox(int width, int height, double frameRate, int crfValue) {
    // Heuristic to estimate bitrate from CRF, resolution, and frame rate.
    // CRF values typically range from 0 (lossless) to 51 (worst quality).
    // A common default/good quality is around 18-28. We use 23 as a reference.
    // Lower CRF means higher quality and thus higher bitrate.
    // Formula: bitrate = bitsPerPixelAtRefCrf * pixels_per_second * 2^((ref_crf - crf) / 6)

    double bitsPerPixelAtRefCrf = 0.1; // Adjust this factor based on desired quality baseline
    double referenceCrf = 23.0;

    double crfTerm = static_cast<double>(crfValue);
    // Ensure frameRate is not zero to avoid division by zero or extremely low bitrates
    if (frameRate <= 0.0) frameRate = 30.0; // Default to 30 fps if invalid

    double bitrateMultiplier = std::pow(2.0, (referenceCrf - crfTerm) / 6.0);

    long long calculatedBitrate = static_cast<long long>(
        bitsPerPixelAtRefCrf * static_cast<double>(width) * static_cast<double>(height) * frameRate * bitrateMultiplier
    );

    // Clamp bitrate to a reasonable range, e.g., 250 kbps to 100 Mbps
    // FFmpeg -b:v expects bits per second.
    int minBitrate = 250000;    // 250 kbps
    int maxBitrate = 100000000; // 100 Mbps

    return juce::jlimit(minBitrate, maxBitrate, static_cast<int>(calculatedBitrate));
}

juce::Array<FFmpegEncoderManager::EncoderDetails> FFmpegEncoderManager::getAvailableEncodersForCodec(VideoCodec codec) {
    // Return cached list of encoders if available
    auto it = availableEncoders.find(codec);
    if (it != availableEncoders.end()) {
        return it->second;
    }

    return {};
}

bool FFmpegEncoderManager::supportsVideoCodec(VideoCodec codec) const {
    const auto iterator = availableEncoders.find(codec);
    if (iterator == availableEncoders.end()) {
        return false;
    }
    for (const auto& encoder : iterator->second) {
        if (encoder.isSupported) {
            return true;
        }
    }
    return false;
}

bool FFmpegEncoderManager::supportsTransparentVideoEncoding() const {
    return supportsVideoCodec(VideoCodec::ProRes4444);
}

bool FFmpegEncoderManager::isHardwareEncoderAvailable(const juce::String& encoderName) {
    // Check if the encoder is available and supported
    for (auto& pair : availableEncoders) {
        for (auto& encoder : pair.second) {
            if (encoder.name == encoderName && encoder.isSupported && encoder.isHardwareAccelerated) {
                return true;
            }
        }
    }
    return false;
}

juce::String FFmpegEncoderManager::getBestEncoderForCodec(VideoCodec codec) {
    juce::Logger::writeToLog("FFmpeg: selecting best encoder for codec " + juce::String((int)codec));
    auto encoders = getAvailableEncodersForCodec(codec);

    // Define priority lists for each codec type
    juce::StringArray h264Encoders = {"h264_nvenc", "h264_amf", "h264_videotoolbox", "libx264"};
    juce::StringArray h265Encoders = {"hevc_nvenc", "hevc_amf", "hevc_videotoolbox", "libx265"};
    juce::StringArray vp9Encoders = {"libvpx-vp9"};
    juce::StringArray proResEncoders = {"prores_ks", "prores_aw"};
    juce::StringArray proRes4444Encoders = {"prores_ks"};

    // Select the appropriate priority list based on codec
    juce::StringArray* priorityList = nullptr;
    switch (codec) {
        case VideoCodec::H264:
            priorityList = &h264Encoders;
            break;
        case VideoCodec::H265:
            priorityList = &h265Encoders;
            break;
        case VideoCodec::VP9:
            priorityList = &vp9Encoders;
            break;
        case VideoCodec::ProRes:
            priorityList = &proResEncoders;
            break;
        case VideoCodec::ProRes4444:
            priorityList = &proRes4444Encoders;
            break;
        default:
            priorityList = &h264Encoders; // Default to H.264
    }

    // Find the highest priority encoder that is available and actually works
    for (const auto& encoderName : *priorityList) {
        for (const auto& encoder : encoders) {
            if (encoder.name == encoderName && encoder.isSupported) {
                // Test if the encoder actually works before selecting it
                if (testEncoderWorks(encoderName)) {
                    return encoderName;
                }
            }
        }
    }

    // Return default software encoder if no hardware encoder is available or working
    juce::String fallback;
    switch (codec) {
        case VideoCodec::H264:
            fallback = "libx264"; break;
        case VideoCodec::H265:
            fallback = "libx265"; break;
        case VideoCodec::VP9:
            fallback = "libvpx-vp9"; break;
        case VideoCodec::ProRes:
        case VideoCodec::ProRes4444:
            fallback = "prores_ks"; break;
        default:
            fallback = "libx264"; break;
    }
    juce::Logger::writeToLog("FFmpeg: no tested encoder worked, falling back to '" + fallback + "'");
    return fallback;
}

void FFmpegEncoderManager::refreshAvailableEncoders() {
    // Query available encoders using ffmpeg -encoders
    juce::String output = runFFmpegCommand({"-encoders", "-hide_banner"});
    parseEncoderList(output);
}

void FFmpegEncoderManager::parseEncoderList(const juce::String& output) {
    // Clear current encoders
    availableEncoders.clear();

    // Initialize codec-specific encoder arrays
    availableEncoders[VideoCodec::H264] = {};
    availableEncoders[VideoCodec::H265] = {};
    availableEncoders[VideoCodec::VP9] = {};
    availableEncoders[VideoCodec::ProRes] = {};
    availableEncoders[VideoCodec::ProRes4444] = {};

    // Split the output into lines
    juce::StringArray lines;
    lines.addLines(output);

    // Skip the first 10 lines (header information from ffmpeg -encoders)
    int linesToSkip = juce::jmin(10, lines.size());

    // Parse each line to find encoder information
    for (int i = linesToSkip; i < lines.size(); ++i) {
        const auto& line = lines[i];

        // Format: V..... libx264 H.264 / AVC / MPEG-4 AVC / MPEG-4 part 10
        juce::String flags = line.substring(0, 6).trim();
        juce::String name = line.substring(8).upToFirstOccurrenceOf(" ", false, true);
        juce::String description = line.substring(8 + name.length()).trim();

        EncoderDetails encoder;
        encoder.name = name;
        encoder.description = description;
        encoder.isHardwareAccelerated = name.contains("nvenc") || name.contains("amf") || name.contains("videotoolbox");
        encoder.isSupported = flags.contains("V"); // Video encoder

        // Add encoder to appropriate codec list
        if (name == "libx264" || name.startsWith("h264_")) {
            availableEncoders[VideoCodec::H264].add(encoder);
        } else if (name == "libx265" || name.startsWith("hevc_")) {
            availableEncoders[VideoCodec::H265].add(encoder);
        } else if (name == "libvpx-vp9") {
            availableEncoders[VideoCodec::VP9].add(encoder);
        } else if (name.startsWith("prores")) {
            availableEncoders[VideoCodec::ProRes].add(encoder);
            if (name == "prores_ks") {
                availableEncoders[VideoCodec::ProRes4444].add(encoder);
            }
        }
    }
}

juce::String FFmpegEncoderManager::runFFmpegCommand(const juce::StringArray& args) {
    if (!ffmpegExecutable.existsAsFile()) {
        return {};
    }
    juce::ChildProcess process;
    juce::StringArray command;

    command.add(ffmpegExecutable.getFullPathName());
    command.addArray(args);

    if (!process.start(command, juce::ChildProcess::wantStdOut)) {
        return {};
    }

    juce::String output = process.readAllProcessOutput();

    return output;
}

juce::String FFmpegEncoderManager::buildBaseEncodingCommand(
    int width,
    int height,
    double frameRate,
    VideoCodec codec) {
    const auto& codecInfo = VideoEncodingConstants::getVideoCodecInfo(codec);
    juce::String resolution = juce::String(width) + "x" + juce::String(height);
    juce::String cmd = "\"" + ffmpegExecutable.getFullPathName() + "\"" +
                       " -r " + juce::String(frameRate) +
                       " -f rawvideo" +
                       " -pix_fmt " + VideoEncodingConstants::PixelFormat::rgba8 +
                       " -s " + resolution +
                       " -i -" +
                       " -threads 4" +
                       " -y" +
                       " -pix_fmt " + codecInfo.outputPixelFormat +
                       " -vf vflip";

    return cmd;
}

juce::String FFmpegEncoderManager::addH264EncoderSettings(
    juce::String cmd,
    const juce::String& encoderName,
    int crf,
    const juce::String& compressionPreset,
    int width,
    int height,
    double frameRate)
{
    if (encoderName == "h264_nvenc") {
        cmd += " -c:v h264_nvenc";
        cmd += " -preset p7";
        cmd += " -profile:v high";
        cmd += " -rc vbr";
        cmd += " -cq " + juce::String(crf);
        cmd += " -b:v 0";
    } else if (encoderName == "h264_amf") {
        cmd += " -c:v h264_amf";
        cmd += " -quality quality";
        cmd += " -rc cqp";
        cmd += " -qp_i " + juce::String(crf);
        cmd += " -qp_p " + juce::String(crf);
    } else if (encoderName == "h264_videotoolbox") {
        int estimatedBitrate = estimateBitrateForVideotoolbox(width, height, frameRate, crf);
        cmd += " -c:v h264_videotoolbox";
        cmd += " -b:v " + juce::String(estimatedBitrate);
    } else { // libx264 (software)
        cmd += " -c:v libx264";
        cmd += " -preset " + compressionPreset;
        cmd += " -crf " + juce::String(crf);
    }

    return cmd;
}

juce::String FFmpegEncoderManager::addH265EncoderSettings(
    juce::String cmd,
    const juce::String& encoderName,
    int crf,
    const juce::String& compressionPreset,
    int width,
    int height,
    double frameRate)
{
    if (encoderName == "hevc_nvenc") {
        cmd += " -c:v hevc_nvenc";
        cmd += " -preset p7";
        cmd += " -profile:v main";
        cmd += " -rc vbr";
        cmd += " -cq " + juce::String(crf);
        cmd += " -b:v 0";
    } else if (encoderName == "hevc_amf") {
        cmd += " -c:v hevc_amf";
        cmd += " -quality quality";
        cmd += " -rc cqp";
        cmd += " -qp_i " + juce::String(crf);
        cmd += " -qp_p " + juce::String(crf);
    } else if (encoderName == "hevc_videotoolbox") {
        int estimatedBitrate = estimateBitrateForVideotoolbox(width, height, frameRate, crf); // Use crf for estimation
        cmd += " -c:v hevc_videotoolbox";
        cmd += " -b:v " + juce::String(estimatedBitrate);
        cmd += " -tag:v hvc1";
    } else { // libx265 (software)
        cmd += " -c:v libx265";
        cmd += " -preset " + compressionPreset;
        cmd += " -crf " + juce::String(crf);
    }

    return cmd;
}

juce::String FFmpegEncoderManager::buildH264EncodingCommand(
    int crf,
    int width,
    int height,
    double frameRate,
    const juce::String& compressionPreset,
    const juce::File& outputFile) {
    juce::String cmd = buildBaseEncodingCommand(width, height, frameRate, VideoCodec::H264);
    juce::String bestEncoder = getBestEncoderForCodec(VideoCodec::H264);

    // Pass width, height, and frameRate to addH264EncoderSettings
    cmd = addH264EncoderSettings(cmd, bestEncoder, crf, compressionPreset, width, height, frameRate);
    cmd += " \"" + outputFile.getFullPathName() + "\"";

    return cmd;
}

juce::String FFmpegEncoderManager::buildH265EncodingCommand(
    int crf,
    int width,
    int height,
    double frameRate,
    const juce::String& compressionPreset,
    const juce::File& outputFile) {
    juce::String cmd = buildBaseEncodingCommand(width, height, frameRate, VideoCodec::H265);
    juce::String bestEncoder = getBestEncoderForCodec(VideoCodec::H265);

    cmd = addH265EncoderSettings(cmd, bestEncoder, crf, compressionPreset, width, height, frameRate);
    cmd += " \"" + outputFile.getFullPathName() + "\"";

    return cmd;
}

juce::String FFmpegEncoderManager::buildVP9EncodingCommand(
    int crf,
    int width,
    int height,
    double frameRate,
    const juce::String& compressionPreset,
    const juce::File& outputFile) {
    juce::String cmd = buildBaseEncodingCommand(width, height, frameRate, VideoCodec::VP9);

    cmd += juce::String(" -c:v libvpx-vp9") +
           " -b:v 0" +
           " -crf " + juce::String(crf) +
           " -deadline good -cpu-used 2";

    cmd += " \"" + outputFile.getFullPathName() + "\"";

    return cmd;
}

juce::String FFmpegEncoderManager::buildProResEncodingCommand(
    int width,
    int height,
    double frameRate,
    const juce::File& outputFile) {
    juce::String cmd = buildBaseEncodingCommand(width, height, frameRate, VideoCodec::ProRes);
    juce::String bestEncoder = getBestEncoderForCodec(VideoCodec::ProRes);

    cmd += " -c:v " + bestEncoder +
           " -profile:v 3"; // ProRes 422 HQ

    cmd += " \"" + outputFile.getFullPathName() + "\"";

    return cmd;
}

juce::String FFmpegEncoderManager::buildProRes4444AlphaEncodingCommand(
    int width,
    int height,
    double frameRate,
    const juce::File& outputFile) {
    juce::String cmd = buildBaseEncodingCommand(width, height, frameRate, VideoCodec::ProRes4444);

    // prores_ks is available cross-platform in FFmpeg
    cmd += " -c:v prores_ks"
           " -profile:v 4444"
           " -vendor apl0"
           " -bits_per_mb 8000";

    cmd += " \"" + outputFile.getFullPathName() + "\"";

    return cmd;
}

bool FFmpegEncoderManager::testEncoderWorks(const juce::String& encoderName) {
    juce::Logger::writeToLog("FFmpeg: testing encoder '" + encoderName + "'");
    juce::ChildProcess process;
    juce::StringArray command;

    // Build a test command that will quickly verify if an encoder works
    // -v error: Only show errors
    // -f lavfi -i nullsrc: Generate a null input source
    // -t 1: Only encode 1 second
    // -c:v [encoderName]: Use the specified encoder
    // -f null -: Output to null device
    command.add(ffmpegExecutable.getFullPathName());
    command.add("-v");
    command.add("error");
    command.add("-f");
    command.add("lavfi");
    command.add("-i");
    command.add("nullsrc=s=640x360:r=30");
    command.add("-t");
    command.add("1");
    command.add("-c:v");
    command.add(encoderName);
    command.add("-f");
    command.add("null");
    command.add("-");

    // Start the process
    bool started = process.start(command, juce::ChildProcess::wantStdErr);

    if (!started) {
        juce::Logger::writeToLog("FFmpeg: failed to start test process for encoder '" + encoderName + "'");
        return false;
    }

    // Wait for the process to finish with a timeout
    if (!process.waitForProcessToFinish(5000)) { // 5 seconds timeout
        process.kill();
        juce::Logger::writeToLog("FFmpeg: encoder test timed out for '" + encoderName + "'");
        return false;
    }

    // Check exit code - 0 means success
    int exitCode = process.getExitCode();
    juce::String errorOutput = process.readAllProcessOutput();

    bool works = exitCode == 0 && errorOutput.isEmpty();
    if (works) {
        juce::Logger::writeToLog("FFmpeg: encoder '" + encoderName + "' works");
    } else {
        juce::Logger::writeToLog("FFmpeg: encoder '" + encoderName + "' failed (exit=" + juce::String(exitCode)
            + (errorOutput.isNotEmpty() ? ", stderr: " + errorOutput.substring(0, 200) : "") + ")");
    }
    return works;
}

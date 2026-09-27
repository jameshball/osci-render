#pragma once

#include "RasterBeamBuilder.h"
#include "VideoDecoderProcess.h"
#include "../model/RasterSettings.h"
#include <JuceHeader.h>

namespace motion {
// Worker-only conversion. FFmpeg writes bounded temporary RGBA data; the worker
// never blocks reading a pipe, so cancellation can terminate a stalled decoder.
class VideoSourcePreparer {
public:
    struct Limits {
        std::uint64_t rawBytes = 2ULL * 1024 * 1024 * 1024;
        int decodeMilliseconds = 120000;
    };
    static PreparedPointFrames::Result prepare(const juce::MemoryBlock& data, const juce::File& executable, const RasterSettings& settings,
        const std::atomic<bool>* cancel = nullptr, std::atomic<double>* progress = nullptr) {
        return prepare(data, executable, settings, cancel, progress, Limits{});
    }
    static PreparedPointFrames::Result prepare(const juce::MemoryBlock& data, const juce::File& executable, const RasterSettings& settings,
        const std::atomic<bool>* cancel, std::atomic<double>* progress, Limits limits) {
        const auto invalid = settings.validate();
        if (!invalid.empty()) { return {nullptr, invalid}; }
        const auto cancelled = [&] { return cancel != nullptr && cancel->load(std::memory_order_relaxed); };
        if (cancelled()) { return {nullptr, "Video preparation cancelled."}; }
        if (!executable.existsAsFile()) { return {nullptr, "Video decoding requires FFmpeg. Install it and prepare the source again."}; }
        if (data.getSize() == 0 || data.getSize() > 64 * 1024 * 1024) { return {nullptr, "Video source must contain 1 byte to 64 MiB."}; }
        const auto dimension = settings.resolution;
        const auto frameBytes = static_cast<std::uint64_t>(dimension) * dimension * 4;
        const auto maximumFrames = std::min({PreparedPointFrames::maximumFrames,
            static_cast<std::uint64_t>(PreparedPointFrames::maximumBytes / sizeof(PointSample) / settings.pointsPerFrame), limits.rawBytes / frameBytes});
        if (maximumFrames == 0 || limits.decodeMilliseconds <= 0) { return {nullptr, "Video preparation limits are invalid."}; }
        juce::TemporaryFile input(".mov"), output(".rgba");
        if (!input.getFile().replaceWithData(data.getData(), data.getSize())) { return {nullptr, "Cannot create the temporary video source."}; }
        const auto size = juce::String(dimension);
        // Display aspect ratio includes non-square source pixels. Autorotation
        // is handled by FFmpeg before filtering; pad with transparent black.
        const auto filter = "setpts=PTS-STARTPTS,fps=" + juce::String(settings.videoFrameRate, 8)
            + ":start_time=0,scale=w='if(gte(dar,1)," + size + ",max(1,trunc(" + size
            + "*dar)))':h='if(gte(dar,1),max(1,trunc(" + size + "/dar))," + size
            + ")':flags=area,setsar=1,format=rgba,pad=" + size + ":" + size + ":(ow-iw)/2:(oh-ih)/2:color=black@0";
        juce::StringArray args {executable.getFullPathName(), "-nostdin", "-hide_banner", "-loglevel", "error", "-xerror", "-y",
            "-protocol_whitelist", "file,pipe", "-i", input.getFile().getFullPathName(), "-map", "0:v:0", "-an", "-sn", "-dn",
            "-vf", filter, "-frames:v", juce::String(static_cast<juce::int64>(maximumFrames + 1)), "-pix_fmt", "rgba", "-f", "rawvideo", output.getFile().getFullPathName()};
        VideoDecoderProcess decoder;
        if (!decoder.start(args)) { return {nullptr, "Cannot start the video decoder."}; }
        const auto started = juce::Time::getMillisecondCounterHiRes();
        while (!decoder.waitForProcessToFinish(30)) {
            if (cancelled()) { return {nullptr, "Video preparation cancelled."}; }
            if (juce::Time::getMillisecondCounterHiRes() - started > limits.decodeMilliseconds) { return {nullptr, "Video decoding timed out. Try a shorter or lower-resolution source."}; }
            if (output.getFile().getSize() > static_cast<juce::int64>((maximumFrames + 1) * frameBytes)) { return {nullptr, "Decoded video exceeds the temporary storage limit."}; }
        }
        if (cancelled()) { return {nullptr, "Video preparation cancelled."}; }
        if (!decoder.wasSuccessful()) { return {nullptr, "Cannot decode this video. Check that it contains a supported, undamaged video stream."}; }
        const auto bytes = output.getFile().getSize();
        if (bytes <= 0 || static_cast<std::uint64_t>(bytes) % frameBytes != 0) { return {nullptr, "Video decoder returned no complete frames."}; }
        const auto frames = static_cast<std::uint64_t>(bytes) / frameBytes;
        if (frames > maximumFrames) { return {nullptr, "Video exceeds the preparation budget. Use fewer samples, lower detail or a shorter source."}; }
        const auto frameError = PreparedPointFrames::validate(settings.videoFrameRate, frames, settings.pointsPerFrame);
        if (!frameError.empty()) { return {nullptr, frameError}; }
        auto stream = output.getFile().createInputStream();
        if (!stream) { return {nullptr, "Cannot read decoded video frames."}; }
        std::vector<std::uint8_t> rgba(static_cast<std::size_t>(frameBytes));
        std::vector<PointSample> points;
        points.reserve(static_cast<std::size_t>(frames) * settings.pointsPerFrame);
        RasterBeamBuilder::Settings trace;
        trace.mode = settings.mode == RasterSettings::Mode::contours ? RasterBeamBuilder::Mode::contours : RasterBeamBuilder::Mode::scanlines;
        trace.threshold = settings.threshold; trace.invert = settings.invert; trace.pointsPerFrame = settings.pointsPerFrame;
        for (std::uint64_t frame = 0; frame < frames; ++frame) {
            if (cancelled()) { return {nullptr, "Video preparation cancelled."}; }
            if (stream->read(rgba.data(), static_cast<int>(rgba.size())) != static_cast<int>(rgba.size())) { return {nullptr, "Decoded video frame is truncated."}; }
            auto beam = RasterBeamBuilder::build(rgba.data(), rgba.size(), dimension, dimension, trace, cancel);
            if (!beam) { return {nullptr, "Video frame " + std::to_string(frame + 1) + ": " + beam.error}; }
            points.insert(points.end(), beam.points.begin(), beam.points.end());
            if (progress != nullptr) { progress->store(.2 + .8 * static_cast<double>(frame + 1) / frames); }
        }
        return PreparedPointFrames::create(settings.videoFrameRate, frames, settings.pointsPerFrame, std::move(points));
    }
};
}

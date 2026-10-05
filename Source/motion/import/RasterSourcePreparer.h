#pragma once

#include "../model/Cancellation.h"
#include "../model/PreparedSource.h"
#include "../model/RasterSettings.h"
#include "RasterBeamBuilder.h"
#include <osci_file_import/osci_file_import.h>
#include <atomic>

namespace motion {
class RasterSourcePreparer {
public:
    struct Result {
        std::shared_ptr<const PreparedSource> source;
        std::string error;
        explicit operator bool() const { return source != nullptr; }
    };

    static Result prepare(const void* data, std::size_t bytes, const RasterSettings& settings, const std::atomic<bool>* cancel = nullptr, std::atomic<double>* progress = nullptr) {
        const auto error = settings.validate();
        if (!error.empty()) { return {nullptr, error}; }
        const auto decoded = osci::RasterDecoder::decode(data, bytes, cancel);
        if (!decoded) { return {nullptr, decoded.error}; }
        const auto& image = *decoded.image;
        std::vector<std::uint32_t> delays;
        delays.reserve(image.frames.size());
        // Zero GIF delays do not define a usable playback interval. Use 100 ms
        // for those (and static images), preserving every nonzero authored delay.
        for (const auto& frame : image.frames) { delays.push_back(frame.delayMilliseconds == 0 ? 100 : frame.delayMilliseconds); }
        const auto timing = FrameTiming::create(delays);
        if (!timing) { return {nullptr, timing.error}; }
        const auto pointError = PreparedPointFrames::validate(timing.timing->averageFrameRate(), image.frames.size(), settings.pointsPerFrame);
        if (!pointError.empty()) { return {nullptr, pointError}; }
        const auto scale = std::min(1.0, static_cast<double>(settings.resolution) / std::max(image.width, image.height));
        const auto width = std::max(1, static_cast<int>(std::round(image.width * scale)));
        const auto height = std::max(1, static_cast<int>(std::round(image.height * scale)));
        std::vector<PointSample> points;
        points.reserve(image.frames.size() * settings.pointsPerFrame);
        RasterBeamBuilder::Settings trace;
        trace.mode = settings.mode == RasterSettings::Mode::contours ? RasterBeamBuilder::Mode::contours : RasterBeamBuilder::Mode::scanlines;
        trace.threshold = settings.threshold;
        trace.invert = settings.invert;
        trace.pointsPerFrame = settings.pointsPerFrame;
        std::vector<std::uint8_t> rgba(static_cast<std::size_t>(width) * height * 4);
        for (std::size_t frameIndex = 0; frameIndex < image.frames.size(); ++frameIndex) {
            const auto& frame = image.frames[frameIndex];
            // Area-average straight-alpha pixels through premultiplied RGB so
            // invisible pixel colours cannot bleed into the prepared outlines.
            for (int y = 0; y < height; ++y) {
                if (cancelled(cancel)) { return {nullptr, "Image preparation cancelled."}; }
                const int top = y * image.height / height, bottom = (y + 1) * image.height / height;
                for (int x = 0; x < width; ++x) {
                    const int left = x * image.width / width, right = (x + 1) * image.width / width;
                    std::uint64_t red = 0, green = 0, blue = 0, alpha = 0;
                    for (int sy = top; sy < bottom; ++sy) {
                        for (int sx = left; sx < right; ++sx) {
                            const auto index = (static_cast<std::size_t>(sy) * image.width + sx) * 4;
                            const auto a = frame.rgba[index + 3];
                            alpha += a;
                            red += frame.rgba[index] * a;
                            green += frame.rgba[index + 1] * a;
                            blue += frame.rgba[index + 2] * a;
                        }
                    }
                    const auto index = (static_cast<std::size_t>(y) * width + x) * 4;
                    const auto pixels = static_cast<unsigned>((bottom - top) * (right - left));
                    rgba[index] = static_cast<std::uint8_t>(alpha > 0 ? red / alpha : 0);
                    rgba[index + 1] = static_cast<std::uint8_t>(alpha > 0 ? green / alpha : 0);
                    rgba[index + 2] = static_cast<std::uint8_t>(alpha > 0 ? blue / alpha : 0);
                    rgba[index + 3] = static_cast<std::uint8_t>(alpha / pixels);
                }
            }
            auto beam = RasterBeamBuilder::build(rgba.data(), rgba.size(), width, height, trace, cancel);
            if (!beam) { return {nullptr, "Frame " + std::to_string(frameIndex + 1) + ": " + beam.error}; }
            points.insert(points.end(), beam.points.begin(), beam.points.end());
            if (progress != nullptr) { progress->store(0.2 + 0.8 * static_cast<double>(frameIndex + 1) / image.frames.size()); }
        }
        const auto prepared = PreparedPointFrames::create(timing.timing->averageFrameRate(), image.frames.size(), settings.pointsPerFrame, std::move(points));
        if (!prepared) { return {nullptr, prepared.error}; }
        return {std::make_shared<const PreparedSource>(prepared.source, timing.timing), {}};
    }
};
}

#pragma once

#include "BlenderCapture.h"
#include "../model/FrameTiming.h"
#include <atomic>

namespace motion {
// Portable little-endian doubles preserve the received vector geometry. Timing
// stores cumulative end boundaries, including the final hold through Stop.
// No gzip expansion or platform struct layout is involved in this format.
struct BlenderCaptureArchive {
    static constexpr std::size_t maximumBytes = 8 + 8 + BlenderCapture::maximumFrames * 16 + BlenderCapture::maximumSegments * 32;
    struct Decoded {
        std::vector<std::shared_ptr<const BlenderFrame>> frames;
        std::shared_ptr<const FrameTiming> timing;
        std::string error;
        explicit operator bool() const { return timing != nullptr; }
    };
    struct Encoded {
        std::vector<std::uint8_t> bytes;
        std::string error;
        explicit operator bool() const { return error.empty() && !bytes.empty(); }
    };
    static bool cancelled(const std::atomic<bool>* cancel) { return cancel != nullptr && cancel->load(); }
    static Encoded encode(const BlenderCapture& capture, const std::atomic<bool>* cancel = nullptr) {
        if (capture.failure != BlenderCapture::Failure::none) { return {{}, capture.error()}; }
        if (capture.frames.empty() || capture.frames.size() > BlenderCapture::maximumFrames
            || !std::isfinite(capture.duration) || capture.duration <= 0 || capture.duration > BlenderCapture::maximumSeconds) {
            return {{}, "Capture duration or frame count is invalid."};
        }
        std::vector<std::size_t> retained;
        std::size_t segments = 0;
        double previous = 0;
        for (std::size_t index = 0; index < capture.frames.size(); ++index) {
            if (cancelled(cancel)) { return {{}, "Capture cancelled."}; }
            const auto& frame = capture.frames[index];
            const auto end = index + 1 < capture.frames.size() ? capture.frames[index + 1].start : capture.duration;
            if (!std::isfinite(frame.start) || frame.start < previous || frame.start < 0 || end < frame.start || end > capture.duration
                || (index == 0 && frame.start != 0)) { return {{}, "Capture boundaries are invalid."}; }
            previous = frame.start;
            if (end == frame.start) { continue; }
            const auto count = frame.geometry != nullptr ? frame.geometry->segments.size() : 0;
            if (count > blender_frame_detail::maximumSegments || count > BlenderCapture::maximumSegments - segments) {
                return {{}, "Capture exceeds the geometry budget."};
            }
            segments += count;
            retained.push_back(index);
        }
        if (retained.empty()) { return {{}, "Capture has no duration."}; }
        Encoded result;
        auto& bytes = result.bytes;
        bytes.reserve(16 + retained.size() * 16 + segments * 32);
        const char magic[] = "MOTVEC01";
        bytes.insert(bytes.end(), magic, magic + 8);
        put(bytes, retained.size());
        for (const auto index : retained) {
            if (cancelled(cancel)) { return {{}, "Capture cancelled."}; }
            const auto& frame = capture.frames[index];
            const auto end = index + 1 < capture.frames.size() ? capture.frames[index + 1].start : capture.duration;
            number(bytes, end);
            put(bytes, frame.geometry != nullptr ? frame.geometry->segments.size() : 0);
            if (frame.geometry == nullptr) { continue; }
            for (const auto& segment : frame.geometry->segments) {
                for (const auto value : {segment.x1, segment.y1, segment.x2, segment.y2}) {
                    if (!blender_frame_detail::bounded(value)) { return {{}, "Capture contains invalid geometry."}; }
                    number(bytes, value);
                }
            }
        }
        return result;
    }
    static Decoded decode(std::span<const std::uint8_t> bytes, const std::atomic<bool>* cancel = nullptr) {
        if (bytes.size() > maximumBytes) { return {{}, nullptr, "Capture archive exceeds its size limit."}; }
        blender_frame_detail::Reader reader(bytes);
        if (!reader.tag("MOTVEC01")) { return {{}, nullptr, "Invalid capture archive header."}; }
        const auto count = reader.u64();
        if (!count || *count == 0 || *count > BlenderCapture::maximumFrames) { return {{}, nullptr, "Invalid capture frame count."}; }
        Decoded result;
        std::vector<double> ends;
        result.frames.reserve(static_cast<std::size_t>(*count));
        ends.reserve(static_cast<std::size_t>(*count));
        std::size_t total = 0;
        double previous = 0;
        for (std::size_t index = 0; index < *count; ++index) {
            if (cancelled(cancel)) { return {{}, nullptr, "Capture cancelled."}; }
            const auto end = reader.number();
            const auto size = reader.u64();
            if (!end || !std::isfinite(*end) || *end <= previous || *end > BlenderCapture::maximumSeconds
                || !size || *size > blender_frame_detail::maximumSegments || *size > BlenderCapture::maximumSegments - total) {
                return {{}, nullptr, "Invalid capture frame timing or geometry count."};
            }
            auto frame = std::make_shared<BlenderFrame>();
            frame->segments.reserve(static_cast<std::size_t>(*size));
            for (std::size_t segment = 0; segment < *size; ++segment) {
                const auto x1 = reader.number(), y1 = reader.number(), x2 = reader.number(), y2 = reader.number();
                if (!x1 || !y1 || !x2 || !y2 || !blender_frame_detail::bounded(*x1) || !blender_frame_detail::bounded(*y1)
                    || !blender_frame_detail::bounded(*x2) || !blender_frame_detail::bounded(*y2)) {
                    return {{}, nullptr, "Invalid or truncated capture geometry."};
                }
                frame->segments.push_back({*x1, *y1, *x2, *y2});
            }
            total += static_cast<std::size_t>(*size);
            ends.push_back(*end);
            previous = *end;
            result.frames.push_back(std::move(frame));
        }
        if (!reader.atEnd()) { return {{}, nullptr, "Unexpected data after capture archive."}; }
        auto timing = FrameTiming::createFromEndSeconds(ends);
        if (!timing) { return {{}, nullptr, timing.error}; }
        result.timing = std::move(timing.timing);
        return result;
    }
private:
    static void put(std::vector<std::uint8_t>& bytes, std::uint64_t value) {
        for (int index = 0; index < 8; ++index) { bytes.push_back(static_cast<std::uint8_t>(value >> (index * 8))); }
    }
    static void number(std::vector<std::uint8_t>& bytes, double value) { put(bytes, std::bit_cast<std::uint64_t>(value)); }
};
}

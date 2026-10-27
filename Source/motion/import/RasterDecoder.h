#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace motion {
struct RasterFrame {
    // Top-left origin, straight (not premultiplied) RGBA8, full logical canvas.
    std::vector<std::uint8_t> rgba;
    // Original GIF delay, including zero. Static images use zero. Playback may
    // apply its own minimum delay; decoding does not silently change timing.
    std::uint32_t delayMilliseconds = 0;
};
struct RasterImage {
    std::uint32_t width = 0, height = 0;
    std::vector<RasterFrame> frames;
};
class RasterDecoder {
public:
    static constexpr std::size_t maximumEncodedBytes = 64 * 1024 * 1024;
    static constexpr std::size_t maximumDecodedBytes = 256 * 1024 * 1024;
    static constexpr std::size_t maximumPixels = 16 * 1024 * 1024;
    static constexpr std::uint32_t maximumDimension = 4096;
    static constexpr std::size_t maximumFrames = 10000;
    struct Result {
        std::shared_ptr<const RasterImage> image;
        std::string error;
        explicit operator bool() const { return image != nullptr; }
    };
    // Worker-only. Cancellation is checked during preflight, GIF LZW decoding,
    // frame compositing and static decoder I/O/allocation; no partial result.
    // Caller retains encoded memory for the duration of this synchronous call.
    static Result decode(const void* data, std::size_t size, const std::atomic<bool>* cancel = nullptr);
};
}

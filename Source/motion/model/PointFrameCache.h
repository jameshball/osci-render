#pragma once

#include "PreparedPointFrames.h"
#include <array>
#include <bit>
#include <exception>

namespace motion {
// Portable uncompressed cache, independent of host endian and structure padding.
// Header: ASCII OSCIPFRM, version u32, rate f64, frames u64, stride u32.
// Payload: frame-major XYZRGB f32. Every numeric field is little endian.
class PointFrameCache {
public:
    static constexpr std::size_t headerBytes = 32;
    static constexpr std::size_t maximumEncodedBytes = headerBytes + PreparedPointFrames::maximumBytes;
    static constexpr std::uint32_t version = 1;
    struct EncodeResult {
        std::vector<std::uint8_t> bytes;
        std::string error;
        explicit operator bool() const { return !bytes.empty() && error.empty(); }
    };

    struct HeaderResult {
        double frameRate = 0;
        std::uint64_t frameCount = 0;
        std::size_t pointsPerFrame = 0, byteCount = 0;
        std::string error;
        explicit operator bool() const { return byteCount != 0 && error.empty(); }
    };
    static HeaderResult inspectHeader(const void* data, std::size_t size) {
        const auto fail = [](std::string error) { return HeaderResult { 0, 0, 0, 0, std::move(error) }; };
        if (data == nullptr || size < headerBytes) { return fail("Point cache header is missing or truncated."); }
        const auto* bytes = static_cast<const std::uint8_t*>(data);
        if (!std::equal(magic.begin(), magic.end(), bytes)) { return fail("Point cache signature is invalid."); }
        if (read(bytes + 8, 4) != version) { return fail("Point cache version is unsupported."); }
        const auto rate = std::bit_cast<double>(read(bytes + 12, 8));
        const auto frames = read(bytes + 20, 8);
        const auto stride = static_cast<std::size_t>(read(bytes + 28, 4));
        const auto error = PreparedPointFrames::validate(rate, frames, stride);
        if (!error.empty()) { return fail(error); }
        return { rate, frames, stride, headerBytes + static_cast<std::size_t>(frames) * stride * 24, {} };
    }

    static EncodeResult encode(const PreparedPointFrames& source) {
        try {
            const auto error = PreparedPointFrames::validate(source.frameRate(), source.frameCount(), source.pointsPerFrame());
            if (!error.empty()) { return { {}, error }; }
            const auto count = source.frameCount() * source.pointsPerFrame();
            if (source.data().size() != count) { return { {}, "Point cache source has an inconsistent sample count." }; }
            std::vector<std::uint8_t> bytes(headerBytes + count * 24);
            std::copy(magic.begin(), magic.end(), bytes.begin());
            write(bytes.data() + 8, version, 4);
            write(bytes.data() + 12, std::bit_cast<std::uint64_t>(source.frameRate()), 8);
            write(bytes.data() + 20, source.frameCount(), 8);
            write(bytes.data() + 28, source.pointsPerFrame(), 4);
            auto* destination = bytes.data() + headerBytes;
            for (const auto& point : source.data()) {
                for (const auto value : { point.x, point.y, point.z, point.r, point.g, point.b }) {
                    write(destination, std::bit_cast<std::uint32_t>(value), 4);
                    destination += 4;
                }
            }
            return { std::move(bytes), {} };
        } catch (const std::bad_alloc&) {
            return { {}, "Not enough memory to encode the point cache." };
        } catch (const std::exception&) {
            return { {}, "Point cache encoding failed." };
        }
    }

    // The caller must supply a readable buffer of size bytes. Reject size and
    // metadata before allocating, then let PreparedPointFrames validate content.
    // Truncation and trailing bytes are errors; no partial source is exposed.
    static PreparedPointFrames::Result decode(const void* data, std::size_t size) {
        try {
            if (size > maximumEncodedBytes) { return { nullptr, "Point cache exceeds the 256 MiB payload limit." }; }
            const auto header = inspectHeader(data, size);
            if (!header) { return { nullptr, header.error }; }
            if (size != header.byteCount) { return { nullptr, "Point cache byte count does not match its frame metadata." }; }
            const auto* bytes = static_cast<const std::uint8_t*>(data);
            const auto rate = header.frameRate;
            const auto frames = header.frameCount;
            const auto stride = header.pointsPerFrame;
            const auto count = static_cast<std::size_t>(frames) * stride;
            std::vector<PointSample> points(count);
            const auto* cursor = bytes + headerBytes;
            for (auto& point : points) {
                const auto next = [&cursor] {
                    const auto value = std::bit_cast<float>(static_cast<std::uint32_t>(read(cursor, 4)));
                    cursor += 4;
                    return value;
                };
                point.x = next(); point.y = next(); point.z = next();
                point.r = next(); point.g = next(); point.b = next();
            }
            return PreparedPointFrames::create(rate, frames, stride, std::move(points));
        } catch (const std::bad_alloc&) {
            return { nullptr, "Not enough memory to decode the point cache." };
        } catch (const std::exception&) {
            return { nullptr, "Point cache decoding failed." };
        }
    }

private:
    static_assert(sizeof(float) == 4 && sizeof(double) == 8
        && std::numeric_limits<float>::is_iec559 && std::numeric_limits<double>::is_iec559);
    static constexpr std::array<std::uint8_t, 8> magic { 'O', 'S', 'C', 'I', 'P', 'F', 'R', 'M' };
    static void write(std::uint8_t* output, std::uint64_t value, unsigned bytes) {
        for (unsigned index = 0; index < bytes; ++index) { output[index] = static_cast<std::uint8_t>(value >> (index * 8)); }
    }
    static std::uint64_t read(const std::uint8_t* input, unsigned bytes) {
        std::uint64_t result = 0;
        for (unsigned index = 0; index < bytes; ++index) { result |= static_cast<std::uint64_t>(input[index]) << (index * 8); }
        return result;
    }
};
}

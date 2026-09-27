#pragma once

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace motion {

struct BlenderSegment {
    double x1, y1, x2, y2;
};

struct BlenderFrame {
    std::vector<BlenderSegment> segments;
    double frameRate = 0;
};

struct BlenderFrameResult {
    std::optional<BlenderFrame> frame;
    std::string error;

    explicit operator bool() const { return frame.has_value(); }
};

namespace blender_frame_detail {

constexpr std::size_t maximumBytes = 8 * 1024 * 1024;
constexpr std::size_t maximumObjects = 256;
constexpr std::size_t maximumStrokes = 4096;
constexpr std::size_t maximumVertices = 65536;
constexpr std::size_t maximumSegments = 65536;
constexpr double maximumMagnitude = 1.0e6;

class Reader {
public:
    explicit Reader(std::span<const std::uint8_t> bytes) : data(bytes) {}

    bool tag(const char (&expected)[9]) {
        if (remaining() < 8 || std::memcmp(data.data() + position, expected, 8) != 0) {
            return false;
        }
        position += 8;
        return true;
    }

    std::optional<std::uint64_t> u64() {
        if (remaining() < 8) {
            return std::nullopt;
        }
        std::uint64_t value = 0;
        for (int index = 0; index < 8; ++index) {
            value |= static_cast<std::uint64_t>(data[position + static_cast<std::size_t>(index)]) << (index * 8);
        }
        position += 8;
        return value;
    }

    std::optional<double> number() {
        const auto bits = u64();
        if (!bits.has_value()) {
            return std::nullopt;
        }
        return std::bit_cast<double>(*bits);
    }

    bool atEnd() const { return position == data.size(); }

private:
    std::size_t remaining() const { return data.size() - position; }

    std::span<const std::uint8_t> data;
    std::size_t position = 0;
};

inline bool bounded(double value) {
    return std::isfinite(value) && std::abs(value) <= maximumMagnitude;
}

enum class ProjectionStatus { visible, behindCamera, invalid };

struct Projection {
    ProjectionStatus status = ProjectionStatus::invalid;
    std::array<double, 2> point {};
};

inline Projection project(const std::array<double, 16>& matrix, const std::array<double, 3>& point, double focalLength) {
    const auto x = point[0] * matrix[0] + point[1] * matrix[1] + point[2] * matrix[2] + matrix[3];
    const auto y = point[0] * matrix[4] + point[1] * matrix[5] + point[2] * matrix[6] + matrix[7];
    const auto z = point[0] * matrix[8] + point[1] * matrix[9] + point[2] * matrix[10] + matrix[11];
    if (!bounded(x) || !bounded(y) || !bounded(z)) {
        return {};
    }
    if (z >= 0) {
        return {ProjectionStatus::behindCamera, {}};
    }
    const auto projectedX = x * focalLength / z;
    const auto projectedY = y * focalLength / z;
    if (!bounded(projectedX) || !bounded(projectedY)) {
        return {};
    }
    return {ProjectionStatus::visible, {projectedX, projectedY}};
}

inline BlenderFrameResult failure(const char* message) {
    return {std::nullopt, message};
}

} // namespace blender_frame_detail

inline BlenderFrameResult decodeBlenderFrame(std::span<const std::uint8_t> data) {
    using namespace blender_frame_detail;
    if (data.size() > maximumBytes) {
        return failure("Blender frame exceeds the 8 MiB decoded limit.");
    }
    Reader reader(data);
    if (!reader.tag("GPLA    ")) {
        return failure("Blender frame is missing the GPLA header.");
    }
    const auto major = reader.u64();
    const auto minor = reader.u64();
    const auto patch = reader.u64();
    if (!major.has_value() || !minor.has_value() || !patch.has_value() || *major != 2 || *minor != 0 || *patch != 0) {
        return failure("Blender frame must use GPLA version 2.0.0.");
    }
    if (!reader.tag("FILE    ") || !reader.tag("fCount  ")) {
        return failure("Blender frame is missing file metadata.");
    }
    const auto count = reader.u64();
    if (!count.has_value() || *count != 1 || !reader.tag("fRate   ")) {
        return failure("Blender frame must contain exactly one frame.");
    }
    const auto frameRate = reader.u64();
    if (!frameRate.has_value() || *frameRate < 1 || *frameRate > 240 || !reader.tag("DONE    ") || !reader.tag("FRAME   ")
        || !reader.tag("focalLen")) {
        return failure("Blender frame has invalid timing metadata.");
    }
    const auto focalLength = reader.number();
    if (!focalLength.has_value() || !bounded(*focalLength) || *focalLength == 0 || !reader.tag("OBJECTS ")) {
        return failure("Blender frame has an invalid focal length or object section.");
    }

    BlenderFrame frame;
    frame.frameRate = static_cast<double>(*frameRate);
    std::size_t objects = 0;
    std::size_t strokes = 0;
    std::size_t vertices = 0;
    while (!reader.tag("DONE    ")) {
        if (++objects > maximumObjects || !reader.tag("OBJECT  ") || !reader.tag("MATRIX  ")) {
            return failure("Blender frame has too many objects or malformed object data.");
        }
        std::array<double, 16> matrix {};
        for (auto& value : matrix) {
            const auto number = reader.number();
            if (!number.has_value() || !bounded(*number)) {
                return failure("Blender frame has an invalid camera-space matrix.");
            }
            value = *number;
        }
        if (!reader.tag("DONE    ") || !reader.tag("STROKES ")) {
            return failure("Blender frame has incomplete object data.");
        }
        while (!reader.tag("DONE    ")) {
            if (++strokes > maximumStrokes || !reader.tag("STROKE  ") || !reader.tag("vertexCt")) {
                return failure("Blender frame has too many strokes or malformed stroke data.");
            }
            const auto vertexCount = reader.u64();
            if (!vertexCount.has_value() || *vertexCount > maximumVertices - vertices || !reader.tag("VERTICES")) {
                return failure("Blender frame has too many vertices or malformed vertex data.");
            }
            vertices += static_cast<std::size_t>(*vertexCount);
            std::optional<std::array<double, 2>> previous;
            for (std::uint64_t index = 0; index < *vertexCount; ++index) {
                std::array<double, 3> point {};
                for (auto& value : point) {
                    const auto number = reader.number();
                    if (!number.has_value() || !bounded(*number)) {
                        return failure("Blender frame has an invalid stroke coordinate.");
                    }
                    value = *number;
                }
                const auto projection = project(matrix, point, *focalLength);
                if (projection.status == ProjectionStatus::invalid) {
                    return failure("Blender frame has invalid transformed or projected geometry.");
                }
                if (previous.has_value() && projection.status == ProjectionStatus::visible) {
                    if (frame.segments.size() == maximumSegments) {
                        return failure("Blender frame has too many projected segments.");
                    }
                    frame.segments.push_back({(*previous)[0], (*previous)[1], projection.point[0], projection.point[1]});
                }
                previous = projection.status == ProjectionStatus::visible ? std::optional(projection.point) : std::nullopt;
            }
            if (!reader.tag("DONE    ") || !reader.tag("DONE    ")) {
                return failure("Blender frame has incomplete vertex data.");
            }
        }
        if (!reader.tag("DONE    ")) {
            return failure("Blender frame has incomplete stroke data.");
        }
    }
    if (!reader.tag("DONE    ") || !reader.tag("END GPLA") || !reader.atEnd()) {
        return failure("Blender frame has trailing or incomplete data.");
    }
    return {std::move(frame), {}};
}

} // namespace motion

#pragma once

#include "Animation.h"
#include "Vec3.h"
#include <array>
#include <cmath>
#include <numbers>
#include <vector>

namespace motion {
// Keys at the same times on all three axes of a group: the precondition for
// a spatial path or quaternion orientation.
inline bool keysAligned(const Curve& x, const Curve& y, const Curve& z) {
    const auto& a = x.keyframes();
    const auto& b = y.keyframes();
    const auto& c = z.keyframes();
    if (a.size() < 2 || a.size() != b.size() || a.size() != c.size()) { return false; }
    for (std::size_t index = 0; index < a.size(); ++index) {
        if (a[index].time != b[index].time || a[index].time != c[index].time) { return false; }
    }
    return true;
}

// Within a segment, progress follows the left key's interpolation: hold
// waits, linear and smooth travel at constant speed along the path, and
// cubic (Bezier) eases in and out.
inline double segmentProgress(Interpolation interpolation, double t) {
    t = std::clamp(t, 0.0, 1.0);
    switch (interpolation) {
        case Interpolation::hold: return 0.0;
        case Interpolation::cubic: return t * t * (3.0 - 2.0 * t);
        case Interpolation::linear:
        case Interpolation::smooth: return t;
    }
    return t;
}

// The key segment holding `time`, strictly between the first and last key
// times, and the progress through it.
struct KeySegment {
    std::size_t index;
    double progress;
};
inline KeySegment keySegmentAt(const std::vector<double>& times, const std::vector<Interpolation>& interpolation, double time) {
    const auto right = std::upper_bound(times.begin(), times.end(), time);
    const auto index = static_cast<std::size_t>(right - times.begin()) - 1;
    return {index, segmentProgress(interpolation[index], (time - times[index]) / (times[index + 1] - times[index]))};
}

// Position keys as one curve through space: a Catmull-Rom spline through the
// keyed points, reparameterised by arc length so each segment's progress
// maps to distance travelled rather than to separate per-axis timing.
class PreparedPath {
public:
    static std::shared_ptr<const PreparedPath> prepare(const Curve& x, const Curve& y, const Curve& z) {
        if (!keysAligned(x, y, z)) { return nullptr; }
        auto path = std::shared_ptr<PreparedPath>(new PreparedPath());
        const auto& kx = x.keyframes();
        const auto& ky = y.keyframes();
        const auto& kz = z.keyframes();
        const auto count = kx.size();
        std::vector<Vec3> points(count);
        for (std::size_t index = 0; index < count; ++index) {
            points[index] = {kx[index].value, ky[index].value, kz[index].value};
            path->times.push_back(kx[index].time);
            path->interpolation.push_back(kx[index].interpolation);
        }
        std::vector<Vec3> tangents(count);
        for (std::size_t index = 0; index < count; ++index) {
            const auto& before = points[index == 0 ? 0 : index - 1];
            const auto& after = points[index + 1 == count ? index : index + 1];
            const auto span = (index == 0 || index + 1 == count) ? 1.0 : 0.5;
            tangents[index] = (after - before) * span;
        }
        for (std::size_t index = 0; index + 1 < count; ++index) {
            Segment segment;
            segment.control = {points[index], points[index] + tangents[index] * (1.0 / 3.0), points[index + 1] - tangents[index + 1] * (1.0 / 3.0), points[index + 1]};
            segment.lengths[0] = 0;
            auto previous = segment.at(0);
            for (std::size_t sample = 1; sample < Segment::samples; ++sample) {
                const auto point = segment.at(static_cast<double>(sample) / (Segment::samples - 1));
                segment.lengths[sample] = segment.lengths[sample - 1] + (point - previous).length();
                previous = point;
            }
            path->segments.push_back(segment);
        }
        return path;
    }

    Vec3 at(double time) const {
        if (!std::isfinite(time) || time <= times.front()) { return segments.front().control[0]; }
        if (time >= times.back()) { return segments.back().control[3]; }
        const auto [index, progress] = keySegmentAt(times, interpolation, time);
        return segments[index].at(segments[index].parameterAt(progress));
    }

private:
    struct Segment {
        static constexpr std::size_t samples = 25;
        std::array<Vec3, 4> control;
        std::array<double, samples> lengths {};
        Vec3 at(double u) const {
            const auto v = 1.0 - u;
            return control[0] * (v * v * v) + control[1] * (3 * v * v * u) + control[2] * (3 * v * u * u) + control[3] * (u * u * u);
        }
        // Distance fraction -> Bezier parameter through the length table.
        double parameterAt(double fraction) const {
            const auto total = lengths.back();
            if (!(total > 0)) { return fraction; }
            const auto target = fraction * total;
            const auto found = std::lower_bound(lengths.begin(), lengths.end(), target);
            if (found == lengths.begin()) { return 0.0; }
            if (found == lengths.end()) { return 1.0; }
            const auto upper = static_cast<std::size_t>(found - lengths.begin());
            const auto span = lengths[upper] - lengths[upper - 1];
            const auto within = span > 0 ? (target - lengths[upper - 1]) / span : 0.0;
            return (static_cast<double>(upper - 1) + within) / (samples - 1);
        }
    };
    std::vector<double> times;
    std::vector<Interpolation> interpolation;
    std::vector<Segment> segments;
};

struct Quaternion {
    double w = 1, x = 0, y = 0, z = 0;
    // Matches osci::Point::rotate: X, then Y, then Z (R = Rz * Ry * Rx).
    static Quaternion fromEulerDegrees(double rx, double ry, double rz) {
        constexpr auto half = std::numbers::pi / 360.0;
        const Quaternion qx {std::cos(rx * half), std::sin(rx * half), 0, 0};
        const Quaternion qy {std::cos(ry * half), 0, std::sin(ry * half), 0};
        const Quaternion qz {std::cos(rz * half), 0, 0, std::sin(rz * half)};
        return qz * qy * qx;
    }
    Quaternion operator*(const Quaternion& o) const {
        return {w * o.w - x * o.x - y * o.y - z * o.z, w * o.x + x * o.w + y * o.z - z * o.y,
                w * o.y - x * o.z + y * o.w + z * o.x, w * o.z + x * o.y - y * o.x + z * o.w};
    }
    double dot(const Quaternion& o) const { return w * o.w + x * o.x + y * o.y + z * o.z; }
    Quaternion normalised() const {
        const auto length = std::sqrt(dot(*this));
        return length > 0 ? Quaternion {w / length, x / length, y / length, z / length} : Quaternion {};
    }
    // Shortest-arc spherical interpolation.
    static Quaternion slerp(Quaternion a, Quaternion b, double t) {
        auto cosine = a.dot(b);
        if (cosine < 0) { b = {-b.w, -b.x, -b.y, -b.z}; cosine = -cosine; }
        if (cosine > 0.9995) {
            return Quaternion {a.w + (b.w - a.w) * t, a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t}.normalised();
        }
        const auto angle = std::acos(std::clamp(cosine, -1.0, 1.0));
        const auto sine = std::sin(angle);
        const auto wa = std::sin((1 - t) * angle) / sine, wb = std::sin(t * angle) / sine;
        return {a.w * wa + b.w * wb, a.x * wa + b.x * wb, a.y * wa + b.y * wb, a.z * wa + b.z * wb};
    }
    Vec3 rotate(double px, double py, double pz) const {
        // v' = v + 2w(q x v) + 2 q x (q x v)
        const auto cx = y * pz - z * py, cy = z * px - x * pz, cz = x * py - y * px;
        const auto ccx = y * cz - z * cy, ccy = z * cx - x * cz, ccz = x * cy - y * cx;
        return {px + 2 * (w * cx + ccx), py + 2 * (w * cy + ccy), pz + 2 * (w * cz + ccz)};
    }
};

// Rotation keys as orientations: each key's Euler angles become a quaternion
// and segments take the shortest arc between them, with no gimbal lock.
class PreparedOrientation {
public:
    static std::shared_ptr<const PreparedOrientation> prepare(const Curve& x, const Curve& y, const Curve& z) {
        if (!keysAligned(x, y, z)) { return nullptr; }
        auto result = std::shared_ptr<PreparedOrientation>(new PreparedOrientation());
        const auto& kx = x.keyframes();
        for (std::size_t index = 0; index < kx.size(); ++index) {
            result->times.push_back(kx[index].time);
            result->interpolation.push_back(kx[index].interpolation);
            result->keys.push_back(Quaternion::fromEulerDegrees(kx[index].value, y.keyframes()[index].value, z.keyframes()[index].value));
        }
        return result;
    }
    Quaternion at(double time) const {
        if (!std::isfinite(time) || time <= times.front()) { return keys.front(); }
        if (time >= times.back()) { return keys.back(); }
        const auto [index, progress] = keySegmentAt(times, interpolation, time);
        return Quaternion::slerp(keys[index], keys[index + 1], progress);
    }

private:
    std::vector<double> times;
    std::vector<Interpolation> interpolation;
    std::vector<Quaternion> keys;
};

// A clip's or group's prepared spatial motion; either part may be absent.
struct PreparedSpatial {
    std::shared_ptr<const PreparedPath> path;
    std::shared_ptr<const PreparedOrientation> orientation;
};
}

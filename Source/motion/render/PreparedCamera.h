#pragma once

#include "PreparedClip.h"
#include "HiddenLineDepth.h"
#include "../model/Vec3.h"
#include <array>
#include <cmath>
#include <numbers>
#include <optional>
#include <vector>

namespace motion {
// A transform chain from a clip or group up through its parent groups, used
// to place camera targets and parents. Groups run on the scope (project)
// clock; a clip link maps project time to its content time.
struct PreparedChain {
    struct Link {
        std::array<Curve, objectPropertySpecs.size()> curves;
        std::shared_ptr<const PreparedSpatial> spatial;
        ClipTiming clock;
        bool clip = false;
    };
    std::vector<Link> links; // inner to outer
    bool empty() const { return links.empty(); }
    Vec3 apply(Vec3 position, double time) const {
        osci::Point point(static_cast<float>(position.x), static_cast<float>(position.y), static_cast<float>(position.z));
        for (const auto& link : links) {
            const auto local = link.clip ? link.clock.localTime(time) : time;
            point = TransformPose::at(link.curves, local, link.spatial.get()).apply(point, false);
        }
        return {point.x, point.y, point.z};
    }
    // The innermost link's anchor in its own space, where a camera aims.
    Vec3 anchor(double time) const {
        if (links.empty()) { return {}; }
        const auto& link = links.front();
        const auto local = link.clip ? link.clock.localTime(time) : time;
        return {link.curves[anchorIndex].evaluate(local), link.curves[anchorIndex + 1].evaluate(local), link.curves[anchorIndex + 2].evaluate(local)};
    }
};

struct PreparedCamera {
    Id id;
    std::array<Curve, cameraPropertySpecs.size()> curves;
    PreparedChain target, parent;

    // How much it shades by hatching (0..1), and how many cells across.
    double hatching(double time) const { return fractionAt(curves[cameraHatchingIndex], time, 0.0); }
    int hatchDetail(double time) const {
        const auto& spec = cameraPropertySpecs[cameraHatchDetailIndex];
        const auto value = curves[cameraHatchDetailIndex].evaluate(time);
        return static_cast<int>(std::isfinite(value) ? spec.clamp(std::round(value)) : spec.defaultValue);
    }
    // Whether it may hatch at all.
    bool mayHatch() const { return TransformPose::mayDifferCurve(curves[cameraHatchingIndex], cameraPropertySpecs[cameraHatchingIndex].defaultValue); }

    struct Frame {
        Vec3 position, right, up, forward;
        double focalLength = 1;
    };
    // The fixed framing with no camera: four units in front of the middle,
    // looking back at it.
    static Frame fixedFrame() { return {{0, 0, 4}, {1, 0, 0}, {0, 1, 0}, {0, 0, -1}, 4}; }
    // The camera's world position and orthonormal basis at `time`: authored
    // position/rotation (X, then Y, then Z), carried by the parent group,
    // then aimed at the target's anchor with Z rotation kept as roll.
    std::optional<Frame> frame(double time) const {
        // Where it is and how it sees: its first seven.
        std::array<double, 7> values;
        for (std::size_t index = 0; index < values.size(); ++index) {
            values[index] = curves[index].evaluate(time);
            if (!std::isfinite(values[index])) { return std::nullopt; }
        }
        // Cubic interpolation can overshoot otherwise valid FOV key values.
        if (values[6] <= 0.0 || values[6] >= 180.0) { return std::nullopt; }
        constexpr auto radians = std::numbers::pi / 180.0;
        const auto axis = [&](float x, float y, float z) {
            osci::Point point(x, y, z);
            point.rotate(static_cast<float>(values[3] * radians), static_cast<float>(values[4] * radians), static_cast<float>(values[5] * radians));
            return Vec3 {point.x, point.y, point.z};
        };
        Frame result;
        result.position = {values[0], values[1], values[2]};
        result.right = axis(1, 0, 0);
        result.up = axis(0, 1, 0);
        result.forward = axis(0, 0, -1);
        result.focalLength = 1.0 / std::tan(values[6] * radians * 0.5);
        if (!parent.empty()) {
            const auto origin = parent.apply(result.position, time);
            const auto ahead = parent.apply(result.position + result.forward, time);
            const auto above = parent.apply(result.position + result.up, time);
            const auto forward = (ahead - origin).normalized(minimumAxis);
            if (!forward.has_value()) { return std::nullopt; }
            const auto right = forward->cross(above - origin).normalized(minimumAxis);
            if (!right.has_value()) { return std::nullopt; }
            result.position = origin;
            result.forward = *forward;
            result.right = *right;
            result.up = right->cross(*forward);
        }
        if (!target.empty()) {
            const auto aim = (target.apply(target.anchor(time), time) - result.position).normalized(minimumAxis);
            if (aim.has_value()) {
                // Level to world up unless looking straight up or down.
                auto right = aim->cross({0, 1, 0}).normalized(minimumAxis);
                if (!right.has_value()) { right = aim->cross(result.up).normalized(minimumAxis); }
                if (right.has_value()) {
                    const auto up = right->cross(*aim);
                    const auto roll = values[5] * radians;
                    const auto c = std::cos(roll), s = std::sin(roll);
                    result.forward = *aim;
                    result.right = *right * c + up * s;
                    result.up = up * c - *right * s;
                }
            }
        }
        return result;
    }
    // Shorter axes are degenerate: a parent scaled flat, or a target at the camera.
    static constexpr double minimumAxis = 1.0e-9;
    // Points nearer than this to a camera are not drawn.
    static constexpr double nearPlane = hidden::nearPlane;
    // The point on the camera's image plane; none at or behind the near plane.
    static std::optional<osci::Point> project(const Frame& frame, osci::Point point) {
        const auto offset = Vec3 {point.x, point.y, point.z} - frame.position;
        const auto depth = offset.dot(frame.forward);
        if (!std::isfinite(depth) || depth <= nearPlane) { return std::nullopt; }
        point.x = static_cast<float>(offset.dot(frame.right) * frame.focalLength / depth);
        point.y = static_cast<float>(offset.dot(frame.up) * frame.focalLength / depth);
        point.z = 1.0f;
        return point;
    }
};
}

#pragma once

#include "Timeline.h"
#include <array>
#include <numbers>

namespace motion {
inline constexpr std::array<const char*, 7> cameraPropertyNames {
    "position.x", "position.y", "position.z", "rotation.x", "rotation.y", "rotation.z", "fov"
};
inline const double defaultCameraFieldOfView = 2.0 * std::atan(0.25) * 180.0 / std::numbers::pi;

struct Camera {
    Camera() {
        for (const auto* property : cameraPropertyNames) {
            properties[property] = Curve(0.0);
        }
        properties["position.z"] = Curve(4.0);
        properties["fov"] = Curve(defaultCameraFieldOfView);
    }

    // Camera animation uses project time. Rotation uses the same XYZ Euler
    // convention as object transforms; an unrotated camera looks down -Z.
    Id id = 0;
    std::string name = "Camera";
    std::map<std::string, Curve> properties;

    bool valid() const {
        if (id == 0 || properties.size() != cameraPropertyNames.size()) {
            return false;
        }
        for (const auto* property : cameraPropertyNames) {
            const auto found = properties.find(property);
            if (found == properties.end() || !std::isfinite(found->second.base)) {
                return false;
            }
            const bool fieldOfView = found->first == "fov";
            const auto validValue = [fieldOfView](double value) {
                return std::isfinite(value) && (!fieldOfView || (value > 0.0 && value < 180.0));
            };
            if (!validValue(found->second.base)) {
                return false;
            }
            for (const auto& key : found->second.keyframes()) {
                if (!std::isfinite(key.time) || !validValue(key.value) || !std::isfinite(key.incomingSlope)
                    || !std::isfinite(key.outgoingSlope) || static_cast<int>(key.interpolation) < 0
                    || static_cast<int>(key.interpolation) > 3) {
                    return false;
                }
            }
        }
        return true;
    }
};

struct CameraCut {
    Id id = 0;
    Id camera = 0;
    double start = 0.0;
    double duration = 5.0;

    double end() const { return start + duration; }
    bool contains(double time) const { return time >= start && time < end(); }
    bool valid() const {
        return id != 0 && camera != 0 && std::isfinite(start) && start >= 0.0
            && std::isfinite(duration) && duration > 0.0 && std::isfinite(end());
    }
};
}

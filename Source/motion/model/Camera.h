#pragma once

#include "Timeline.h"
#include "PropertySpecs.h"

namespace motion {
struct Camera {
    Camera() : properties(defaultProperties(cameraPropertySpecs)) {}

    // Camera animation uses project time. Rotation uses the same XYZ Euler
    // convention as object transforms; an unrotated camera looks down -Z.
    Id id = 0;
    std::string name = "Camera";
    PropertyMap properties;
    // Look-at: a clip or group whose origin the camera aims at, keeping its
    // Z rotation as roll. Parent: a group whose transform carries the camera.
    Id target = 0;
    Id parent = 0;

    bool valid() const { return id != 0 && validProperties(properties, cameraPropertySpecs); }
};

struct CameraCut {
    Id id = 0;
    Id camera = 0;
    double start = 0.0;
    double duration = 5.0;

    double end() const { return start + duration; }
    bool contains(double time) const { return time >= start && time < end(); }
    bool operator==(const CameraCut&) const = default;
    bool valid() const {
        return id != 0 && camera != 0 && std::isfinite(start) && start >= 0.0
            && std::isfinite(duration) && duration > 0.0 && std::isfinite(end());
    }
};
}

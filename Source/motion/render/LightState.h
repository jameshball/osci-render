#pragma once

#include "../model/Generators.h"
#include "../model/Vec3.h"
#include <array>

namespace motion {
// A light as it is at a time, in the scene (before any camera): where it is,
// which way it shines, its colour and how it shines.
struct LightState {
    GeneratorKind kind = GeneratorKind::pointLight;
    Vec3 position, direction {0, 0, -1};
    std::array<double, 3> colour {1, 1, 1};
    double intensity = 1, range = 10, cone = 40, softness = 0, shadows = 1;
    bool ambient() const { return kind == GeneratorKind::ambientLight; }
    bool parallel() const { return kind == GeneratorKind::directionalLight; }
    bool spot() const { return kind == GeneratorKind::spotLight; }
    bool casts() const { return !ambient() && shadows > 0; }
};
}

#pragma once

#include "Timeline.h"
#include <array>

namespace motion {
// The Scope's picture, animated like any other target: each property is a
// visualiser parameter (named by its parameter ID, in its own units) whose
// curve drives the beam in project time.
inline constexpr std::array<const char*, 9> beamPropertyNames {
    "intensity", "focus", "persistence", "afterglow", "glow", "hue", "lineSaturation", "ambient", "visualiserSmoothing"
};

// Every project has one Scope; its identity is reserved, far above the ids
// projects hand out, so it never collides and needs no allocation.
inline constexpr Id beamIdentity = Id {1} << 62;

struct Beam {
    Beam() {
        for (std::size_t index = 0; index < beamPropertyNames.size(); ++index) { properties[beamPropertyNames[index]] = Curve(defaults[index]); }
    }

    // The visualiser's own defaults, in beamPropertyNames order.
    static constexpr std::array<double, 9> defaults {5.0, 1.0, 0.5, 1.0, 0.3, 125.0, 1.0, 0.0, 0.0};

    Id id = beamIdentity;
    std::map<std::string, Curve> properties;

    bool valid() const {
        if (id != beamIdentity || properties.size() != beamPropertyNames.size()) {
            return false;
        }
        for (const auto* property : beamPropertyNames) {
            const auto found = properties.find(property);
            if (found == properties.end() || !found->second.valid()) {
                return false;
            }
        }
        return true;
    }
};
}

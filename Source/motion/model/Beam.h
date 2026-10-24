#pragma once

#include "Timeline.h"
#include "PropertySpecs.h"

namespace motion {
// Every project has one Scope; its identity is reserved, far above the ids
// projects hand out, so it never collides and needs no allocation.
inline constexpr Id beamIdentity = Id {1} << 62;

// The Scope's picture, animated like any other target: each property is a
// visualiser parameter (named by its parameter ID, in its own units) whose
// curve drives the beam in project time.
struct Beam {
    Id id = beamIdentity;
    PropertyMap properties = defaultProperties(beamPropertySpecs);

    bool valid() const { return id == beamIdentity && validProperties(properties, beamPropertySpecs); }
    // How many times a second it draws the picture at `time`, by its keys
    // (modulation moves it only as it plays).
    double frequencyAt(double time) const {
        const auto& spec = beamPropertySpecs[beamFrequencyIndex];
        const auto found = properties.find(std::string(spec.id));
        return found != properties.end() ? spec.clamp(found->second.evaluate(time)) : spec.defaultValue;
    }
};
}

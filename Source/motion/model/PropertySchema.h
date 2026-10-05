#pragma once

#include "PropertyTarget.h"
#include "PropertySpecs.h"
#include <span>
#include <string_view>

namespace motion {
inline std::span<const PropertySpec> propertySpecs(const PropertyTarget& target) {
    if (target.beam) { return beamPropertySpecs; }
    if (target.camera) { return cameraPropertySpecs; }
    if (target.isAudio) { return audioPropertySpecs; }
    return objectPropertySpecs;
}

inline const PropertySpec* findPropertySpec(std::span<const PropertySpec> specs, std::string_view id) {
    for (const auto& spec : specs) {
        if (spec.id == id) { return &spec; }
    }
    return nullptr;
}

// Effects declare their own parameter ranges in the catalogue.
inline PropertySpec effectPropertySpec(const EffectParameterDefinition& parameter) {
    const auto span = parameter.max - parameter.min;
    return {parameter.id, parameter.name, "Effect", "", parameter.min, parameter.max, parameter.defaultValue, span / 200.0, span >= 50 ? 1 : 3, ""};
}
}

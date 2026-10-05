#pragma once

#include "PropertyTarget.h"
#include "PropertySpecs.h"
#include <optional>
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

// One of a target's properties as specified: an effect parameter, a
// property of its kind, or a Lua slider.
template <typename ProjectType>
std::optional<PropertySpec> specFor(const ProjectType& project, Id target, std::string_view property) {
    const auto* effect = findEffect(project, target);
    if (effect != nullptr) {
        const auto* definition = effectDefinition(effect->type);
        if (definition == nullptr) { return std::nullopt; }
        for (const auto& parameter : definition->parameters) {
            if (parameter.id == property) { return effectPropertySpec(parameter); }
        }
        return std::nullopt;
    }
    const auto found = findPropertyTarget(project, target);
    const auto* spec = found.has_value() ? findPropertySpec(propertySpecs(*found), property) : nullptr;
    if (spec == nullptr) { spec = findPropertySpec(luaSliderSpecs, property); }
    return spec != nullptr ? std::optional<PropertySpec>(*spec) : std::nullopt;
}

// A property's name as shown.
template <typename ProjectType>
juce::String propertyLabel(const ProjectType& project, Id target, std::string_view property) {
    const auto spec = specFor(project, target, property);
    return spec.has_value() ? juce::String(spec->label.data(), spec->label.size()) : juce::String(property.data(), property.size());
}

// "Owner · Property": what a route or link connects to.
template <typename ProjectType>
juce::String describeProperty(const ProjectType& project, Id target, std::string_view property) {
    const auto found = findPropertyTarget(project, target);
    const auto owner = found.has_value() ? juce::String(found->name.data(), found->name.size()) : juce::String("Missing");
    return owner + juce::String(juce::CharPointer_UTF8(" \xc2\xb7 ")) + propertyLabel(project, target, property);
}

}

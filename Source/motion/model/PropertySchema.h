#pragma once

#include "PropertyTarget.h"
#include "PropertySpecs.h"
#include "Generators.h"
#include <optional>
#include <span>
#include <string_view>

namespace motion {
inline std::span<const PropertySpec> propertySpecs(const PropertyTarget& target) {
    if (target.beam) { return beamPropertySpecs; }
    if (target.camera) { return cameraPropertySpecs; }
    if (target.isAudio) { return audioPropertySpecs; }
    return objectInspectorSpecs;
}

// Whether a clip made by `generator` (none for other sources) has a property
// of the object specs. A light draws nothing: it takes no share of the beam,
// lights or reflects nothing of itself, and has no size; it has a place
// (point and spot) and a way it shines (spot and directional), no more. A
// curve, or another source with no `surface` (see hasSurface), has nothing to
// reflect with or draw lines on.
inline bool hasProperty(const GeneratorDefinition* generator, std::string_view id, bool surface = true) {
    const auto category = generator != nullptr ? std::optional(generator->category) : std::nullopt;
    const auto onSurface = id == "reflects" || isLinesProperty(id);
    if (onSurface && !surface) { return false; }
    if (category == GeneratorCategory::curve) { return !onSurface; }
    if (category != GeneratorCategory::light) { return true; }
    if (onSurface || id == "beam" || id == "lit" || id.starts_with("scale.") || id.starts_with("anchor.")) { return false; }
    const auto kind = generator->kind;
    if (id.starts_with("position.")) { return kind == GeneratorKind::pointLight || kind == GeneratorKind::spotLight; }
    if (id.starts_with("rotation.")) { return kind == GeneratorKind::spotLight || kind == GeneratorKind::directionalLight; }
    return true;
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
    return {parameter.id, parameter.name, "Effect", "", parameter.min, parameter.max, parameter.defaultValue, span / 200.0, parameter.decimals >= 0 ? parameter.decimals : span >= 10 ? 1 : 2, parameter.unit};
}

// What generates a clip, when the target is a generator's clip. Only
// projects that hold assets have generators; `generatorOf` comes with their
// assets.
template <typename ProjectType>
const GeneratorDefinition* generatorFor(const ProjectType& project, Id target) {
    if constexpr (requires { project.assets; }) {
        for (const auto& track : project.tracks) {
            for (const auto& clip : track.clips) {
                if (clip.id != target || clip.asset == 0) { continue; }
                for (const auto& asset : project.assets) {
                    if (asset != nullptr && asset->id == clip.asset) { return generatorOf(*asset); }
                }
                return nullptr;
            }
        }
    }
    return nullptr;
}

// Whether a clip's source, other than a generator's (see hasProperty), has a
// surface: faces (a mesh, or a filled shape) for lines to be laid on and to
// reflect with. A source not made yet, or a composition, may have.
template <typename ProjectType>
bool hasSurface(const ProjectType& project, Id target) {
    if constexpr (requires { project.assets; }) {
        for (const auto& track : project.tracks) {
            for (const auto& clip : track.clips) {
                if (clip.id != target || clip.asset == 0) { continue; }
                for (const auto& asset : project.assets) {
                    if (asset == nullptr || asset->id != clip.asset) { continue; }
                    return generatorOf(*asset) != nullptr || asset->source == nullptr || asset->source->hasFaces();
                }
                return true;
            }
        }
    }
    return true;
}

// A generator clip's own parameter, when the target is one.
template <typename ProjectType>
const PropertySpec* generatorSpec(const ProjectType& project, Id target, std::string_view property) {
    const auto* generator = generatorFor(project, target);
    return generator != nullptr ? findPropertySpec(generator->parameters, property) : nullptr;
}

// One of a target's properties as specified: an effect parameter, a
// property of its kind, a Lua slider, or a generator's parameter.
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
    if (spec == nullptr) { spec = findPropertySpec(fractalPropertySpecs, property); }
    if (spec == nullptr) { spec = generatorSpec(project, target, property); }
    return spec != nullptr ? std::optional<PropertySpec>(*spec) : std::nullopt;
}

// A property's curve, made at its default when the project never stored it
// (a property added after the project was saved, such as the anchor).
template <typename ProjectType>
Curve* ensurePropertyCurve(ProjectType& project, Id id, std::string_view property) {
    auto target = findPropertyTarget(project, id);
    if (!target.has_value() || target->properties == nullptr) { return nullptr; }
    auto* curve = target->curve(property);
    if (curve != nullptr || target->isEffect) { return curve; }
    const auto specs = target->beam ? std::span<const PropertySpec>(beamPropertySpecs) : target->camera ? std::span<const PropertySpec>(cameraPropertySpecs)
        : target->isAudio ? std::span<const PropertySpec>(audioPropertySpecs) : std::span<const PropertySpec>(objectPropertySpecs);
    auto* spec = findPropertySpec(specs, property);
    if (spec == nullptr) { spec = generatorSpec(project, id, property); }
    if (spec == nullptr) { return nullptr; }
    return &target->properties->emplace(std::string(property), Curve(spec->defaultValue)).first->second;
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
    return owner + juce::String::fromUTF8(separator.data(), static_cast<int>(separator.size())) + propertyLabel(project, target, property);
}

}

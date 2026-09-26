#pragma once

#include "EditorCamera.h"
#include "../model/Group.h"
#include <array>

namespace motion::editor {
namespace transform_detail {
inline Vec3 rotateX(Vec3 value, double angle) {
    const auto c = std::cos(angle), s = std::sin(angle);
    return { value.x, c * value.y - s * value.z, s * value.y + c * value.z };
}
inline Vec3 rotateY(Vec3 value, double angle) {
    const auto c = std::cos(angle), s = std::sin(angle);
    return { c * value.x + s * value.z, value.y, -s * value.x + c * value.z };
}
inline Vec3 rotateZ(Vec3 value, double angle) {
    const auto c = std::cos(angle), s = std::sin(angle);
    return { c * value.x - s * value.y, s * value.x + c * value.y, value.z };
}
struct Affine {
    Vec3 position, rotation, scale;
    Vec3 direction(Vec3 value) const {
        value = { value.x * scale.x, value.y * scale.y, value.z * scale.z };
        return rotateZ(rotateY(rotateX(value, rotation.x), rotation.y), rotation.z);
    }
    Vec3 inverseDirection(Vec3 value) const {
        value = rotateX(rotateY(rotateZ(value, -rotation.z), -rotation.y), -rotation.x);
        return { value.x / scale.x, value.y / scale.y, value.z / scale.z };
    }
};
template <typename PropertyMap>
std::optional<Affine> evaluate(const PropertyMap& properties, double time, double bpm) {
    std::array<double, 9> values;
    for (std::size_t i = 0; i < values.size(); ++i) {
        const auto found = properties.find(propertyNames[i]);
        if (found != properties.end() && !found->second.valid()) { return std::nullopt; }
        values[i] = found == properties.end() ? (i >= 6 ? 1.0 : 0.0) : found->second.evaluate(time, bpm);
        if (!std::isfinite(values[i])) { return std::nullopt; }
    }
    constexpr auto radians = std::numbers::pi / 180.0;
    return Affine { { values[0], values[1], values[2] },
        { values[3] * radians, values[4] * radians, values[5] * radians },
        { values[6], values[7], values[8] } };
}
inline bool hasEffects(const std::vector<EffectInstance>& effects, double time, double bpm) {
    for (const auto& effect : effects) {
        if (!effect.enabled || (effect.range.has_value() && (time < effect.range->start || time >= effect.range->end()))) {
            continue;
        }
        const auto strength = effect.properties.find("strength");
        // Unknown/malformed effects are conservatively treated as active.
        if (strength == effect.properties.end()) { return true; }
        const auto value = strength->second.evaluate(time, bpm);
        if (!std::isfinite(value) || value > 0) { return true; }
    }
    return false;
}
}

struct TransformFrame {
    Vec3 worldOrigin;
    // Columns of the parent's linear transform, including scale/shear from
    // nested rotated non-uniform scales. These are deliberately not normalized.
    std::array<Vec3, 3> parentBasis { Vec3 { 1, 0, 0 }, Vec3 { 0, 1, 0 }, Vec3 { 0, 0, 1 } };
    bool hasPostTransformEffects = false;

    std::optional<Vec3> positionDelta(Vec3 worldDelta) const {
        if (!worldDelta.finite()) { return std::nullopt; }
        for (std::size_t i = parentCount; i > 0; --i) {
            worldDelta = parents[i - 1].inverseDirection(worldDelta);
            if (!worldDelta.finite()) { return std::nullopt; }
        }
        return worldDelta;
    }

private:
    std::array<transform_detail::Affine, maximumGroupDepth> parents {};
    std::size_t parentCount = 0;
    template <typename ProjectType>
    friend std::optional<TransformFrame> clipTransformFrame(const ProjectType&, Id, double);
};

// Affine authoring frame only: excludes effect processing and output-camera
// projection. Own clip rotation/scale do not affect its position property's
// world delta. A zero own scale is therefore valid; zero ancestor scales aren't.
template <typename ProjectType>
std::optional<TransformFrame> clipTransformFrame(const ProjectType& project, Id clipId, double projectTime) {
    if (clipId == 0 || !std::isfinite(projectTime) || !std::isfinite(project.bpm) || project.bpm <= 0) {
        return std::nullopt;
    }
    for (const auto& track : project.tracks) {
        if (track.kind != TrackKind::visual) { continue; }
        for (const auto& clip : track.clips) {
            if (clip.id != clipId) { continue; }
            const auto local = clip.localTime(projectTime);
            if (!std::isfinite(local)) { return std::nullopt; }
            const auto own = transform_detail::evaluate(clip.properties, local, project.bpm);
            if (!own.has_value()) { return std::nullopt; }
            TransformFrame frame;
            frame.worldOrigin = own->position;
            frame.hasPostTransformEffects = transform_detail::hasEffects(track.effects, projectTime, project.bpm)
                || transform_detail::hasEffects(project.effects, projectTime, project.bpm);
            auto groupId = track.group;
            std::array<Id, maximumGroupDepth> seen {};
            while (groupId != 0) {
                if (frame.parentCount >= maximumGroupDepth
                    || std::find(seen.begin(), seen.begin() + frame.parentCount, groupId) != seen.begin() + frame.parentCount) {
                    return std::nullopt;
                }
                const auto* group = findGroup(project, groupId);
                if (group == nullptr) { return std::nullopt; }
                const auto parent = transform_detail::evaluate(group->properties, projectTime, project.bpm);
                if (!parent.has_value() || parent->scale.x == 0 || parent->scale.y == 0 || parent->scale.z == 0) {
                    return std::nullopt;
                }
                seen[frame.parentCount] = groupId;
                frame.parents[frame.parentCount++] = *parent;
                frame.worldOrigin = parent->direction(frame.worldOrigin) + parent->position;
                if (!frame.worldOrigin.finite()) { return std::nullopt; }
                for (auto& basis : frame.parentBasis) {
                    basis = parent->direction(basis);
                    if (!basis.finite()) { return std::nullopt; }
                }
                frame.hasPostTransformEffects = frame.hasPostTransformEffects
                    || transform_detail::hasEffects(group->effects, projectTime, project.bpm);
                groupId = group->parent;
            }
            for (const auto axis : { Vec3 { 1, 0, 0 }, Vec3 { 0, 1, 0 }, Vec3 { 0, 0, 1 } }) {
                if (!frame.positionDelta(axis).has_value()) { return std::nullopt; }
            }
            return frame;
        }
    }
    return std::nullopt;
}
}

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
    Vec3 position, rotation, scale, anchor;
    // A point in the transform's own space, about its anchor.
    Vec3 point(Vec3 value) const { return direction(value - anchor) + position; }
    Vec3 direction(Vec3 value) const {
        value = { value.x * scale.x, value.y * scale.y, value.z * scale.z };
        return rotateZ(rotateY(rotateX(value, rotation.x), rotation.y), rotation.z);
    }
    Vec3 inverseDirection(Vec3 value) const {
        value = rotateX(rotateY(rotateZ(value, -rotation.z), -rotation.y), -rotation.x);
        return { value.x / scale.x, value.y / scale.y, value.z / scale.z };
    }
};
// Its position, rotation, scale and anchor, each three of the object
// properties from X, at `time`: none when any is not a usable number.
template <typename PropertyMap>
std::optional<Affine> evaluate(const PropertyMap& properties, double time) {
    constexpr auto positionIndex = specIndex(objectPropertySpecs, "position.x"), rotationIndex = specIndex(objectPropertySpecs, "rotation.x"), scaleIndex = specIndex(objectPropertySpecs, "scale.x");
    constexpr auto radians = std::numbers::pi / 180.0;
    bool usable = true;
    const auto vector = [&](std::size_t first, double unit) {
        std::array<double, 3> axes {};
        for (std::size_t axis = 0; axis < 3; ++axis) {
            const auto& spec = objectPropertySpecs[first + axis];
            const auto found = properties.find(spec.id);
            if (found != properties.end() && !found->second.valid()) { usable = false; }
            axes[axis] = (found == properties.end() ? spec.defaultValue : found->second.evaluate(time)) * unit;
            usable = usable && std::isfinite(axes[axis]);
        }
        return Vec3 {axes[0], axes[1], axes[2]};
    };
    const Affine affine {vector(positionIndex, 1), vector(rotationIndex, radians), vector(scaleIndex, 1), vector(anchorIndex, 1)};
    return usable ? std::optional<Affine>(affine) : std::nullopt;
}
}

struct TransformFrame {
    Vec3 worldOrigin;
    // Columns of the parent's linear transform, including scale/shear from
    // nested rotated non-uniform scales. These are deliberately not normalized.
    std::array<Vec3, 3> parentBasis { Vec3 { 1, 0, 0 }, Vec3 { 0, 1, 0 }, Vec3 { 0, 0, 1 } };

    std::optional<Vec3> positionDelta(Vec3 worldDelta) const {
        if (!worldDelta.finite()) { return std::nullopt; }
        for (std::size_t i = parentCount; i > 0; --i) {
            worldDelta = parents[i - 1].inverseDirection(worldDelta);
            if (!worldDelta.finite()) { return std::nullopt; }
        }
        return worldDelta;
    }

    // Carries the frame up through a group and its ancestors.
    template <typename ProjectType>
    bool addGroups(const ProjectType& project, Id groupId, double projectTime) {
        std::array<Id, maximumGroupDepth> seen {};
        std::size_t depth = 0;
        while (groupId != 0) {
            if (parentCount >= maximumGroupDepth || std::find(seen.begin(), seen.begin() + depth, groupId) != seen.begin() + depth) {
                return false;
            }
            const auto* group = findGroup(project, groupId);
            if (group == nullptr) { return false; }
            const auto parent = transform_detail::evaluate(group->properties, projectTime);
            if (!parent.has_value() || parent->scale.x == 0 || parent->scale.y == 0 || parent->scale.z == 0) {
                return false;
            }
            seen[depth++] = groupId;
            parents[parentCount++] = *parent;
            worldOrigin = parent->point(worldOrigin);
            if (!worldOrigin.finite()) { return false; }
            for (auto& basis : parentBasis) {
                basis = parent->direction(basis);
                if (!basis.finite()) { return false; }
            }
            groupId = group->parent;
        }
        for (const auto axis : { Vec3 { 1, 0, 0 }, Vec3 { 0, 1, 0 }, Vec3 { 0, 0, 1 } }) {
            if (!positionDelta(axis).has_value()) { return false; }
        }
        return true;
    }

private:
    std::array<transform_detail::Affine, maximumGroupDepth> parents {};
    std::size_t parentCount = 0;
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
            const auto local = clip.localTime(projectTime, project.tempo());
            if (!std::isfinite(local)) { return std::nullopt; }
            const auto own = transform_detail::evaluate(clip.properties, local);
            if (!own.has_value()) { return std::nullopt; }
            TransformFrame frame;
            frame.worldOrigin = own->position;
            if (!frame.addGroups(project, track.group, projectTime)) { return std::nullopt; }
            return frame;
        }
    }
    return std::nullopt;
}

// A group's own frame: its position in its parent groups' space. Groups run
// on project time.
template <typename ProjectType>
std::optional<TransformFrame> groupTransformFrame(const ProjectType& project, Id groupId, double projectTime) {
    if (!std::isfinite(projectTime)) { return std::nullopt; }
    const auto* group = findGroup(project, groupId);
    if (group == nullptr) { return std::nullopt; }
    const auto own = transform_detail::evaluate(group->properties, projectTime);
    if (!own.has_value()) { return std::nullopt; }
    TransformFrame frame;
    frame.worldOrigin = own->position;
    if (!frame.addGroups(project, group->parent, projectTime)) { return std::nullopt; }
    return frame;
}

// Parameter-aligned gizmo frame. Rotation rings exclude the edited axis and
// all earlier Euler rotations; scale axes include the full authored rotation.
struct EulerGizmoFrame {
    TransformFrame parent;
    Vec3 eulerRadians;
    Vec3 evaluatedScale { 1, 1, 1 };
    Vec3 evaluatedAnchor;

    std::optional<Vec3> axisDirection(int axis, bool rotationRing) const {
        if (!validAxis(axis) || !eulerRadians.finite()) { return std::nullopt; }
        auto direction = canonicalAxis(axis);
        direction = rotationRing ? postRotate(direction, axis)
            : transform_detail::rotateZ(transform_detail::rotateY(transform_detail::rotateX(direction, eulerRadians.x), eulerRadians.y), eulerRadians.z);
        const auto world = parentDirection(direction);
        return world.finite() && world.length() > 0 && std::isfinite(world.length()) ? std::optional<Vec3>(world) : std::nullopt;
    }

    // The canonical ray uses the same forward parameter as the supplied world
    // ray. Do not transform a plane normal as a direction under a scaled parent.
    std::optional<Ray> rotationRay(Ray world, int axis) const {
        if (!validAxis(axis) || !eulerRadians.finite() || !world.origin.finite() || !world.direction.finite()) {
            return std::nullopt;
        }
        const auto origin = parent.positionDelta(world.origin - parent.worldOrigin);
        const auto direction = parent.positionDelta(world.direction);
        if (!origin.has_value() || !direction.has_value()) { return std::nullopt; }
        const Ray result { undoPostRotate(*origin, axis), undoPostRotate(*direction, axis) };
        const auto length = result.direction.length();
        return result.origin.finite() && result.direction.finite() && std::isfinite(length) && length > 0
            ? std::optional<Ray>(result) : std::nullopt;
    }

    // Positive angles follow right-handed YZ, ZX, XY circles respectively.
    std::optional<Vec3> rotationRingPoint(int axis, double angleRadians, double radius) const {
        if (!validAxis(axis) || !eulerRadians.finite() || !std::isfinite(angleRadians) || !std::isfinite(radius) || radius <= 0) {
            return std::nullopt;
        }
        const auto c = std::cos(angleRadians) * radius;
        const auto s = std::sin(angleRadians) * radius;
        const auto canonical = axis == 0 ? Vec3 { 0, c, s } : (axis == 1 ? Vec3 { s, 0, c } : Vec3 { c, s, 0 });
        const auto result = parent.worldOrigin + parentDirection(postRotate(canonical, axis));
        return result.finite() ? std::optional<Vec3>(result) : std::nullopt;
    }

private:
    static bool validAxis(int axis) { return axis >= 0 && axis < 3; }
    static Vec3 canonicalAxis(int axis) { return axis == 0 ? Vec3 { 1, 0, 0 } : (axis == 1 ? Vec3 { 0, 1, 0 } : Vec3 { 0, 0, 1 }); }
    Vec3 parentDirection(Vec3 value) const {
        return parent.parentBasis[0] * value.x + parent.parentBasis[1] * value.y + parent.parentBasis[2] * value.z;
    }
    Vec3 postRotate(Vec3 value, int axis) const {
        if (axis == 0) { value = transform_detail::rotateY(value, eulerRadians.y); }
        if (axis <= 1) { value = transform_detail::rotateZ(value, eulerRadians.z); }
        return value;
    }
    Vec3 undoPostRotate(Vec3 value, int axis) const {
        if (axis <= 1) { value = transform_detail::rotateZ(value, -eulerRadians.z); }
        if (axis == 0) { value = transform_detail::rotateY(value, -eulerRadians.y); }
        return value;
    }
};

template <typename ProjectType>
std::optional<EulerGizmoFrame> gizmoFrameForClip(const ProjectType& project, Id clipId, double projectTime) {
    const auto parent = clipTransformFrame(project, clipId, projectTime);
    if (!parent.has_value()) { return std::nullopt; }
    for (const auto& track : project.tracks) {
        if (track.kind != TrackKind::visual) { continue; }
        for (const auto& clip : track.clips) {
            if (clip.id != clipId) { continue; }
            const auto transform = transform_detail::evaluate(clip.properties, clip.localTime(projectTime, project.tempo()));
            if (!transform.has_value()) { return std::nullopt; }
            return EulerGizmoFrame { *parent, transform->rotation, transform->scale, transform->anchor };
        }
    }
    return std::nullopt;
}

// A camera's: its position in its group's space (cameras run on project
// time), turned as an object is; it has no size or anchor.
template <typename ProjectType>
std::optional<EulerGizmoFrame> gizmoFrameForCamera(const ProjectType& project, Id cameraId, double projectTime) {
    const auto camera = std::find_if(project.cameras.begin(), project.cameras.end(), [cameraId](const auto& item) { return item.id == cameraId; });
    if (camera == project.cameras.end() || !std::isfinite(projectTime)) { return std::nullopt; }
    const auto own = transform_detail::evaluate(camera->properties, projectTime);
    if (!own.has_value()) { return std::nullopt; }
    TransformFrame frame;
    frame.worldOrigin = own->position;
    if (!frame.addGroups(project, camera->parent, projectTime)) { return std::nullopt; }
    return EulerGizmoFrame { frame, own->rotation, { 1, 1, 1 }, {} };
}

// The gizmo for whatever is selected: a clip, a camera, or a group, which
// moves all it holds.
template <typename ProjectType>
std::optional<EulerGizmoFrame> gizmoFrameFor(const ProjectType& project, Id id, double projectTime) {
    const auto* group = findGroup(project, id);
    if (group == nullptr) {
        if constexpr (requires { project.cameras; }) {
            const auto camera = gizmoFrameForCamera(project, id, projectTime);
            if (camera.has_value()) { return camera; }
        }
        return gizmoFrameForClip(project, id, projectTime);
    }
    const auto parent = groupTransformFrame(project, id, projectTime);
    const auto transform = transform_detail::evaluate(group->properties, projectTime);
    if (!parent.has_value() || !transform.has_value()) { return std::nullopt; }
    return EulerGizmoFrame { *parent, transform->rotation, transform->scale, transform->anchor };
}
}

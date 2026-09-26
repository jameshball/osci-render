#pragma once

#include "EditorCamera.h"

namespace motion::editor::gizmo {
namespace detail {
inline bool bounded(Vec3 value) {
    return value.finite() && std::abs(value.x) <= Camera::maximumCoordinate
        && std::abs(value.y) <= Camera::maximumCoordinate && std::abs(value.z) <= Camera::maximumCoordinate;
}
}

// Signed world distance along the normalized axis at the closest approach to
// the picking ray. Reject nearly parallel configurations instead of amplifying
// tiny mouse movements. The closest ray point must be in front of its origin.
inline std::optional<double> closestAxisParameter(const Ray& ray, Vec3 origin, Vec3 axis) {
    if (!detail::bounded(ray.origin) || !detail::bounded(origin)) { return std::nullopt; }
    const auto direction = ray.direction.normalized();
    const auto unitAxis = axis.normalized();
    if (!direction.has_value() || !unitAxis.has_value()) { return std::nullopt; }
    const auto offset = ray.origin - origin;
    const auto alignment = direction->dot(*unitAxis);
    const auto denominator = 1 - alignment * alignment;
    if (!std::isfinite(denominator) || denominator <= 1.0e-6) { return std::nullopt; }
    const auto rayOffset = direction->dot(offset);
    const auto axisOffset = unitAxis->dot(offset);
    const auto travel = (alignment * axisOffset - rayOffset) / denominator;
    const auto parameter = (axisOffset - alignment * rayOffset) / denominator;
    if (!std::isfinite(travel) || travel < 0 || !std::isfinite(parameter)
        || std::abs(parameter) > Camera::maximumCoordinate
        || !detail::bounded(origin + *unitAxis * parameter)
        || !detail::bounded(ray.origin + *direction * travel)) { return std::nullopt; }
    return parameter;
}

// NDC coordinates follow Camera (+Y up). Axis magnitude has no effect on the
// result. Convert the resulting world delta through TransformFrame::positionDelta
// when editing a clip inside transformed groups.
inline std::optional<double> axisDragDistance(const Camera& camera, Vec2 start, Vec2 end, Vec3 origin, Vec3 axis, double aspect = 1) {
    const auto firstRay = camera.ray(start, aspect), lastRay = camera.ray(end, aspect);
    if (!firstRay.has_value() || !lastRay.has_value()) { return std::nullopt; }
    const auto first = closestAxisParameter(*firstRay, origin, axis);
    const auto last = closestAxisParameter(*lastRay, origin, axis);
    if (!first.has_value() || !last.has_value()) { return std::nullopt; }
    const auto delta = *last - *first;
    return std::isfinite(delta) && std::abs(delta) <= Camera::maximumCoordinate ? std::optional<double>(delta) : std::nullopt;
}

// Signed shortest rotation in radians [-pi,pi], right-hand positive about axis.
// For multi-turn gestures, accumulate consecutive deltas in the UI rather than
// comparing every event to mouse-down. A ray through the ring center has no angle.
inline std::optional<double> rotationDragAngle(const Ray& firstRay, const Ray& lastRay, Vec3 origin, Vec3 axis) {
    const auto unitAxis = axis.normalized();
    const auto firstRayDirection = firstRay.direction.normalized(), lastRayDirection = lastRay.direction.normalized();
    if (!unitAxis.has_value() || !firstRayDirection.has_value() || !lastRayDirection.has_value()) { return std::nullopt; }
    // Normalize transformed rays before testing whether the ring is edge-on.
    if (std::abs(firstRayDirection->dot(*unitAxis)) <= 1.0e-6
        || std::abs(lastRayDirection->dot(*unitAxis)) <= 1.0e-6) { return std::nullopt; }
    const auto first = Camera::intersectPlane(firstRay, origin, *unitAxis);
    const auto last = Camera::intersectPlane(lastRay, origin, *unitAxis);
    if (!first.has_value() || !last.has_value()) { return std::nullopt; }
    const auto firstOffset = *first - origin, lastOffset = *last - origin;
    if (firstOffset.length() <= 1.0e-9 || lastOffset.length() <= 1.0e-9) { return std::nullopt; }
    const auto firstDirection = firstOffset.normalized(), lastDirection = lastOffset.normalized();
    if (!firstDirection.has_value() || !lastDirection.has_value()) { return std::nullopt; }
    const auto angle = std::atan2(unitAxis->dot(firstDirection->cross(*lastDirection)), firstDirection->dot(*lastDirection));
    return std::isfinite(angle) ? std::optional<double>(angle) : std::nullopt;
}

// Ray overload also accepts inverse-transformed, non-unit rays for authoring
// rotation in a parent's local coordinate frame under nonuniform scale.
inline std::optional<double> rotationDragAngle(const Camera& camera, Vec2 start, Vec2 end, Vec3 origin, Vec3 axis, double aspect = 1) {
    const auto firstRay = camera.ray(start, aspect), lastRay = camera.ray(end, aspect);
    if (!firstRay.has_value() || !lastRay.has_value()) { return std::nullopt; }
    return rotationDragAngle(*firstRay, *lastRay, origin, axis);
}

// Positive pixel delta grows uniformly. Exponential mapping never flips an axis
// or reaches zero. Finite extreme gestures saturate; invalid sensitivity rejects.
inline std::optional<double> uniformScaleFactor(double pixelDelta, double pixelsPerDoubling = 100) {
    if (!std::isfinite(pixelDelta) || !std::isfinite(pixelsPerDoubling) || pixelsPerDoubling <= 0) { return std::nullopt; }
    constexpr double limit = 13.287712379549449; // log2(10000)
    const auto exponent = pixelDelta / pixelsPerDoubling;
    if (exponent >= limit) { return 1.0e4; }
    if (exponent <= -limit) { return 1.0e-4; }
    return std::clamp(std::exp2(exponent), 1.0e-4, 1.0e4);
}
}

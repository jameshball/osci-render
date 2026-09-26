#pragma once

#include <algorithm>
#include <cmath>
#include <numbers>
#include <optional>

namespace motion::editor {
struct Vec3 {
    double x = 0, y = 0, z = 0;
    Vec3 operator+(Vec3 other) const { return { x + other.x, y + other.y, z + other.z }; }
    Vec3 operator-(Vec3 other) const { return { x - other.x, y - other.y, z - other.z }; }
    Vec3 operator*(double scalar) const { return { x * scalar, y * scalar, z * scalar }; }
    Vec3 operator/(double scalar) const { return { x / scalar, y / scalar, z / scalar }; }
    double dot(Vec3 other) const { return x * other.x + y * other.y + z * other.z; }
    Vec3 cross(Vec3 other) const { return { y * other.z - z * other.y, z * other.x - x * other.z, x * other.y - y * other.x }; }
    double length() const { return std::hypot(x, y, z); }
    bool finite() const { return std::isfinite(x) && std::isfinite(y) && std::isfinite(z); }
    std::optional<Vec3> normalized() const {
        const auto magnitude = length();
        if (!finite() || !std::isfinite(magnitude) || magnitude <= 0) {
            return std::nullopt;
        }
        return *this / magnitude;
    }
};
struct Vec2 { double x = 0, y = 0; };
struct Ray { Vec3 origin, direction; };

// Navigation-only camera: independent of authored project/output cameras.
// World is right-handed; yaw/pitch are radians, positive yaw looks right,
// positive pitch looks up. NDC is [-1,1] with +Y up; aspect is width/height.
class Camera {
public:
    static constexpr double nearPlane = 0.0001;
    static constexpr double minimumDistance = 0.001;
    static constexpr double maximumDistance = 1.0e9;
    static constexpr double maximumCoordinate = 1.0e12;
    static constexpr double pitchLimit = std::numbers::pi * 0.4972222222222222; // 89.5 degrees
    static constexpr double defaultFieldOfView = 28.072486935852957;

    Vec3 position { 0, 0, 4 };
    double yaw = 0, pitch = 0;
    double fovDegrees = defaultFieldOfView;
    Vec3 pivot;

    bool valid() const {
        return usable(position) && usable(pivot) && std::isfinite(yaw) && std::isfinite(pitch)
            && std::abs(pitch) <= pitchLimit && std::isfinite(fovDegrees) && fovDegrees >= 1 && fovDegrees <= 150;
    }
    Vec3 forward() const {
        if (!valid()) {
            return {};
        }
        return { std::sin(yaw) * std::cos(pitch), std::sin(pitch), -std::cos(yaw) * std::cos(pitch) };
    }
    Vec3 right() const {
        if (!valid()) {
            return {};
        }
        return { std::cos(yaw), 0, std::sin(yaw) };
    }
    Vec3 up() const { return right().cross(forward()); }
    double distance() const { return valid() ? (position - pivot).length() : 0; }

    std::optional<Vec2> project(Vec3 world, double aspect = 1) const {
        if (!valid() || !usable(world) || !validAspect(aspect)) {
            return std::nullopt;
        }
        const auto offset = world - position;
        const auto depth = offset.dot(forward());
        if (!std::isfinite(depth) || depth <= nearPlane) {
            return std::nullopt;
        }
        const auto scale = depth * tangent();
        Vec2 result { offset.dot(right()) / scale / aspect, offset.dot(up()) / scale };
        return std::isfinite(result.x) && std::isfinite(result.y) ? std::optional<Vec2>(result) : std::nullopt;
    }

    std::optional<Ray> ray(Vec2 screen, double aspect = 1) const {
        if (!valid() || !validAspect(aspect) || !std::isfinite(screen.x) || !std::isfinite(screen.y)) {
            return std::nullopt;
        }
        const auto direction = (forward() + right() * (screen.x * tangent() * aspect) + up() * (screen.y * tangent())).normalized();
        if (!direction.has_value()) {
            return std::nullopt;
        }
        return Ray { position, *direction };
    }

    static std::optional<Vec3> intersectPlane(const Ray& ray, Vec3 point, Vec3 normal) {
        if (!usable(ray.origin) || !usable(point)) {
            return std::nullopt;
        }
        const auto unitDirection = ray.direction.normalized();
        const auto unitNormal = normal.normalized();
        if (!unitDirection.has_value() || !unitNormal.has_value()) {
            return std::nullopt;
        }
        const auto denominator = unitDirection->dot(*unitNormal);
        if (!std::isfinite(denominator) || std::abs(denominator) < 1.0e-12) {
            return std::nullopt;
        }
        const auto travel = (point - ray.origin).dot(*unitNormal) / denominator;
        if (!std::isfinite(travel) || travel < 0) {
            return std::nullopt;
        }
        const auto result = ray.origin + *unitDirection * travel;
        return usable(result) ? std::optional<Vec3>(result) : std::nullopt;
    }

    // Drag a world anchor on a camera-facing plane; returns a world delta.
    std::optional<Vec3> translationOnFacingPlane(Vec2 start, Vec2 end, Vec3 anchor, double aspect = 1) const {
        const auto firstRay = ray(start, aspect);
        const auto lastRay = ray(end, aspect);
        if (!firstRay.has_value() || !lastRay.has_value()) {
            return std::nullopt;
        }
        const auto first = intersectPlane(*firstRay, anchor, forward());
        const auto last = intersectPlane(*lastRay, anchor, forward());
        if (!first.has_value() || !last.has_value()) {
            return std::nullopt;
        }
        const auto delta = *last - *first;
        return usable(delta) ? std::optional<Vec3>(delta) : std::nullopt;
    }

    // Rotate in place and move the orbit pivot along the new viewing direction.
    // Reject the entire operation if the resulting pivot exceeds world bounds.
    bool look(double yawDeltaRadians, double pitchDeltaRadians) {
        if (!valid() || !std::isfinite(yawDeltaRadians) || !std::isfinite(pitchDeltaRadians)) {
            return false;
        }
        const auto radius = distance();
        if (radius < minimumDistance || radius > maximumDistance) {
            return false;
        }
        auto next = *this;
        next.yaw = std::remainder(std::remainder(yaw, 2 * std::numbers::pi) + std::remainder(yawDeltaRadians, 2 * std::numbers::pi), 2 * std::numbers::pi);
        next.pitch = std::clamp(pitch + std::clamp(pitchDeltaRadians, -std::numbers::pi, std::numbers::pi), -pitchLimit, pitchLimit);
        next.pivot = position + next.forward() * radius;
        return commit(next);
    }

    bool orbit(double yawDeltaRadians, double pitchDeltaRadians) {
        if (!valid() || !std::isfinite(yawDeltaRadians) || !std::isfinite(pitchDeltaRadians)) {
            return false;
        }
        auto next = *this;
        const auto radius = distance();
        if (radius < minimumDistance || radius > maximumDistance) {
            return false;
        }
        next.yaw = std::remainder(std::remainder(yaw, 2 * std::numbers::pi) + std::remainder(yawDeltaRadians, 2 * std::numbers::pi), 2 * std::numbers::pi);
        // Reduce the increment before addition to avoid overflow for huge drags.
        next.pitch = std::clamp(pitch + std::clamp(pitchDeltaRadians, -std::numbers::pi, std::numbers::pi), -pitchLimit, pitchLimit);
        next.position = pivot - next.forward() * radius;
        return commit(next);
    }

    // Pixel deltas follow a scene-drag gesture (+Y down): move the camera
    // oppositely in its facing plane. Both camera and pivot translate together.
    bool pan(double pixelDeltaX, double pixelDeltaY, double viewportHeight) {
        if (!valid() || !std::isfinite(pixelDeltaX) || !std::isfinite(pixelDeltaY)
            || !std::isfinite(viewportHeight) || viewportHeight <= 0) {
            return false;
        }
        const auto scale = 2 * distance() * tangent() / viewportHeight;
        return translate(right() * (-pixelDeltaX * scale) + up() * (pixelDeltaY * scale));
    }

    // Positive logScale zooms out; negative zooms in. Finite extremes saturate
    // to bounded orbit distance without crossing the pivot or near plane.
    bool dolly(double logScale) {
        if (!valid() || !std::isfinite(logScale)) {
            return false;
        }
        const auto radial = (position - pivot).normalized();
        if (!radial.has_value()) {
            return false;
        }
        auto next = *this;
        const auto radius = std::clamp(distance() * std::exp(std::clamp(logScale, -50.0, 50.0)), minimumDistance, maximumDistance);
        next.position = pivot + *radial * radius;
        return commit(next);
    }

    // Local axes are right/up/forward. Unit input travels speed*dt; diagonal
    // input is normalized, fractional input retains its proportional speed.
    bool fly(Vec3 localDirection, double unitsPerSecond, double deltaSeconds) {
        if (!valid() || !localDirection.finite() || !std::isfinite(unitsPerSecond) || unitsPerSecond < 0
            || !std::isfinite(deltaSeconds) || deltaSeconds < 0) {
            return false;
        }
        const auto length = localDirection.length();
        if (!std::isfinite(length)) {
            return false;
        }
        if (length > 1) {
            localDirection = localDirection / length;
        }
        const auto travel = unitsPerSecond * deltaSeconds;
        if (!std::isfinite(travel)) {
            return false;
        }
        return translate((right() * localDirection.x + up() * localDirection.y + forward() * localDirection.z) * travel);
    }

    // Fit a bounding sphere with a 10% margin, preserving orientation. This is
    // a square/vertical fit; use radius/aspect for narrow portrait viewports.
    bool frame(Vec3 center, double radius) {
        if (!valid() || !usable(center) || !std::isfinite(radius) || radius < 0) {
            return false;
        }
        const auto required = radius * 1.1 / std::sin(fovDegrees * std::numbers::pi / 360);
        if (!std::isfinite(required) || required > maximumDistance) {
            return false;
        }
        auto next = *this;
        next.pivot = center;
        next.position = center - forward() * std::max(minimumDistance, required);
        return commit(next);
    }

private:
    static bool usable(Vec3 value) {
        return value.finite() && std::abs(value.x) <= maximumCoordinate && std::abs(value.y) <= maximumCoordinate && std::abs(value.z) <= maximumCoordinate;
    }
    static bool validAspect(double aspect) { return std::isfinite(aspect) && aspect >= 0.000001 && aspect <= 1000000; }
    double tangent() const { return std::tan(fovDegrees * std::numbers::pi / 360); }
    bool commit(const Camera& next) {
        if (!next.valid()) {
            return false;
        }
        *this = next;
        return true;
    }
    bool translate(Vec3 delta) {
        if (!usable(delta)) {
            return false;
        }
        auto next = *this;
        next.position = position + delta;
        next.pivot = pivot + delta;
        return commit(next);
    }
};
}

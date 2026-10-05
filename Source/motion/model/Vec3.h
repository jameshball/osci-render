#pragma once

#include <cmath>
#include <cstddef>
#include <optional>

namespace motion {
struct Vec3 {
    double x = 0, y = 0, z = 0;
    Vec3 operator+(Vec3 other) const { return { x + other.x, y + other.y, z + other.z }; }
    Vec3 operator-(Vec3 other) const { return { x - other.x, y - other.y, z - other.z }; }
    Vec3 operator*(double scalar) const { return { x * scalar, y * scalar, z * scalar }; }
    Vec3 operator/(double scalar) const { return { x / scalar, y / scalar, z / scalar }; }
    double operator[](std::size_t axis) const { return axis == 0 ? x : axis == 1 ? y : z; }
    double dot(Vec3 other) const { return x * other.x + y * other.y + z * other.z; }
    Vec3 cross(Vec3 other) const { return { y * other.z - z * other.y, z * other.x - x * other.z, x * other.y - y * other.x }; }
    double length() const { return std::hypot(x, y, z); }
    bool finite() const { return std::isfinite(x) && std::isfinite(y) && std::isfinite(z); }
    // Unit length, unless no longer than `minimumLength` or not finite.
    std::optional<Vec3> normalized(double minimumLength = 0) const {
        const auto magnitude = length();
        if (!finite() || !std::isfinite(magnitude) || magnitude <= minimumLength) {
            return std::nullopt;
        }
        return *this / magnitude;
    }
};
}

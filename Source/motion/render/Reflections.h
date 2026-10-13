#pragma once

#include "../model/Vec3.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <optional>
#include <tuple>
#include <utility>
#include <vector>

// Where the scene's lines are seen in a mirror. A surface that reflects is
// flat pieces, each a plane with an outline, or, when its corners all lie on
// one sphere, that sphere, smooth. A point of a line is seen in a mirror at
// the one point of it from which the line toward the eye and the line toward
// the point make equal angles with its face. Pure geometry, so it can be
// tested alone.
namespace motion::hidden {
// A flat piece of mirror: its plane (its normal toward the eye, and the
// normal's dot with any point on it) and its outline, edges no two of its
// faces share, so a piece with a hole or two pieces in one plane are one.
struct FlatMirror {
    Vec3 normal;
    double offset = 0;
    std::vector<std::array<Vec3, 2>> outline;

    double height(Vec3 point) const { return normal.dot(point) - offset; }
    // A point mirrored through the plane.
    Vec3 image(Vec3 point) const { return point - normal * (2 * height(point)); }
    // Where the line from the eye to a point's image crosses the plane: where
    // the point is seen in it. None for a point behind the mirror.
    std::optional<Vec3> reflectionOf(Vec3 eye, Vec3 point) const {
        const auto toEye = height(eye), toPoint = height(point);
        if (!(toEye > 0) || !(toPoint > 0)) { return std::nullopt; }
        return eye + (image(point) - eye) * (toEye / (toEye + toPoint));
    }

    // Whether a point on its plane is within its outline: an odd number of
    // its edges cross a line from it along the plane.
    bool contains(Vec3 point) const {
        // Any direction along the plane will do, away from its edges' own.
        const auto along = (std::abs(normal.x) < .9 ? Vec3 {1, 0, 0} : Vec3 {0, 1, 0}).cross(normal).normalized().value_or(Vec3 {1, 0, 0});
        const auto across = normal.cross(along);
        const auto u = [&](Vec3 at) { return (at - point).dot(along); };
        const auto v = [&](Vec3 at) { return (at - point).dot(across); };
        bool inside = false;
        for (const auto& [a, b] : outline) {
            const auto va = v(a), vb = v(b);
            if ((va > 0) == (vb > 0)) { continue; }
            const auto crossing = u(a) + (u(b) - u(a)) * (va / (va - vb));
            if (crossing > 0) { inside = !inside; }
        }
        return inside;
    }

    // Where a straight piece of line from `from` to `to` is seen in it, as
    // shares of its length: within the outline, in front of the mirror.
    std::vector<std::pair<double, double>> spans(Vec3 eye, Vec3 from, Vec3 to) const {
        std::vector<std::pair<double, double>> result;
        if (!(height(eye) > 0)) { return result; }
        // In front of the mirror: its height above the plane is linear along it.
        const auto rise = height(from), fall = height(to);
        auto low = 0.0, high = 1.0;
        if (rise <= 0 && fall <= 0) { return result; }
        if (rise <= 0) { low = rise / (rise - fall); }
        if (fall <= 0) { high = rise / (rise - fall); }
        // Its image runs straight too: each edge of the outline, with the
        // eye, makes a plane the image crosses where the reflection crosses
        // the edge. Between crossings it is wholly in or out.
        const auto imageFrom = image(from), imageTo = image(to);
        std::vector<double> cuts {low, high};
        for (const auto& [a, b] : outline) {
            const auto wedge = (a - eye).cross(b - eye);
            const auto start = wedge.dot(imageFrom - eye), end = wedge.dot(imageTo - eye);
            if ((start > 0) == (end > 0) || start == end) { continue; }
            const auto at = start / (start - end);
            if (at <= low || at >= high) { continue; }
            // The plane through the eye and the edge's line: only where the
            // reflection lands on the edge itself.
            const auto seen = reflectionOf(eye, from + (to - from) * at);
            if (!seen.has_value()) { continue; }
            const auto edge = b - a;
            const auto along = (*seen - a).dot(edge) / std::max(edge.dot(edge), 1.0e-300);
            if (along >= -1.0e-9 && along <= 1 + 1.0e-9) { cuts.push_back(at); }
        }
        std::sort(cuts.begin(), cuts.end());
        for (std::size_t index = 0; index + 1 < cuts.size(); ++index) {
            const auto a = cuts[index], b = cuts[index + 1];
            if (!(b > a)) { continue; }
            const auto seen = reflectionOf(eye, from + (to - from) * ((a + b) / 2));
            if (!seen.has_value() || !contains(*seen)) { continue; }
            if (!result.empty() && result.back().second >= a) {
                result.back().second = b;
            } else {
                result.emplace_back(a, b);
            }
        }
        return result;
    }
};

// A circle in the scene: its middle, two unit axes square to each other in
// its plane, and its radius.
struct Circle {
    Vec3 centre, first, second;
    double radius = 1;
    Vec3 at(double angle) const { return centre + (first * std::cos(angle) + second * std::sin(angle)) * radius; }
};

// A round mirror: a ball, seen from outside.
struct SphereMirror {
    Vec3 centre;
    double radius = 1;

    // Its rim as the eye sees it: where lines from the eye just graze it.
    // None for an eye inside it.
    std::optional<Circle> rimFrom(Vec3 eye) const {
        const auto toEye = eye - centre;
        const auto distance = toEye.length();
        if (!(distance > radius)) { return std::nullopt; }
        const auto axis = toEye / distance;
        const auto first = (std::abs(axis.x) < .9 ? Vec3 {1, 0, 0} : Vec3 {0, 1, 0}).cross(axis).normalized().value_or(Vec3 {0, 0, 1});
        const auto share = radius / distance;
        return Circle {centre + axis * (radius * share), first, axis.cross(first), radius * std::sqrt(1 - share * share)};
    }

    // Where a point is seen in it from the eye: Alhazen's problem. The point,
    // the eye and the centre make a plane the reflection is in; on the circle
    // the sphere makes there it lies between the directions to the eye and
    // to the point, where the angles to them from its normal are equal. On a
    // ball (convex) there is one such point. None for a point inside the
    // ball, the eye inside it, or a point the ball itself hides from where
    // it would be seen (beyond its rim from the eye, or from the point).
    // `near`, a reflection of a point close by, is where the search starts.
    std::optional<Vec3> reflectionOf(Vec3 eye, Vec3 point, std::optional<Vec3> near = std::nullopt) const {
        const auto toEye = eye - centre, toPoint = point - centre;
        const auto eyeDistance = toEye.length(), pointDistance = toPoint.length();
        if (!(eyeDistance > radius) || !(pointDistance > radius)) { return std::nullopt; }
        const auto first = toEye / eyeDistance;
        const auto aside = toPoint - first * toPoint.dot(first);
        const auto off = aside.length();
        // On the line through the eye and the centre: in front, it is seen
        // straight back; behind, as a ring (not a point), and so not at all.
        if (!(off > 1.0e-12 * pointDistance)) {
            if (toPoint.dot(first) > 0) { return centre + first * radius; }
            return std::nullopt;
        }
        const auto second = aside / off;
        // In that plane, round the circle from the eye's direction (at 0)
        // toward the point's (at `apart`): the reflection is where
        // reach(angle, e) and reach(apart - angle, p) balance, reach being
        // how squarely a point at distance d and that angle round from the
        // normal faces it: d sin a / sqrt(d^2 + r^2 - 2 r d cos a).
        const auto e = eyeDistance, p = pointDistance;
        const auto apart = std::atan2(off, toPoint.dot(first));
        const auto cosApart = toPoint.dot(first) / p, sinApart = off / p;
        // Each side's reach and its slope with the angle.
        const auto reach = [this](double d, double c, double s) {
            const auto squared = d * d + radius * radius - 2 * radius * d * c;
            const auto length = std::sqrt(squared);
            return std::pair {d * s / length, d * (c * squared - radius * d * s * s) / (squared * length)};
        };
        // Newton's method from halfway (where it is for a far eye and point)
        // or from where a point close by is seen, kept inside the bracket
        // that holds it.
        double low = 0, high = apart, angle = apart / 2;
        if (near.has_value()) {
            const auto towards = *near - centre;
            const auto guess = std::atan2(towards.dot(second), towards.dot(first));
            if (guess > 0 && guess < apart) { angle = guess; }
        }
        for (int step = 0; step < 32; ++step) {
            const auto c = std::cos(angle), s = std::sin(angle);
            const auto [toEyeReach, toEyeSlope] = reach(e, c, s);
            const auto [toPointReach, toPointSlope] = reach(p, cosApart * c + sinApart * s, sinApart * c - cosApart * s);
            const auto imbalance = toPointReach - toEyeReach;
            if (imbalance == 0) { break; }
            if (imbalance > 0) { low = angle; } else { high = angle; }
            const auto slope = -toPointSlope - toEyeSlope;
            auto next = slope != 0 ? angle - imbalance / slope : (low + high) / 2;
            if (!(next > low && next < high)) { next = (low + high) / 2; }
            const auto moved = std::abs(next - angle);
            angle = next;
            if (moved < 1.0e-11 || high - low < 1.0e-11) { break; }
        }
        const auto normal = first * std::cos(angle) + second * std::sin(angle);
        const auto seen = centre + normal * radius;
        if (!(normal.dot(eye - seen) > 0) || !(normal.dot(point - seen) > 0)) { return std::nullopt; }
        return seen;
    }
};

// A surface's corners and faces as a mirror: a sphere when every corner lies
// on one (whatever its faces), else its faces' flat pieces, faces in one
// plane taken together.
struct Mirror {
    std::optional<SphereMirror> sphere;
    std::vector<FlatMirror> flats;

    static Mirror of(const std::vector<std::optional<Vec3>>& corners, const std::vector<std::array<std::uint32_t, 3>>& faces) {
        Mirror mirror;
        mirror.sphere = sphereThrough(corners, faces);
        if (mirror.sphere.has_value()) { return mirror; }
        // Faces grouped by their plane, each group's outline the edges just
        // one of its faces has.
        struct Group {
            Vec3 normal;
            double offset;
            std::map<std::pair<std::uint32_t, std::uint32_t>, int> edges;
        };
        std::vector<Group> groups;
        std::map<std::tuple<long long, long long, long long, long long>, std::vector<std::size_t>> byPlane;
        double size = 0;
        for (const auto& corner : corners) {
            if (corner.has_value()) { size = std::max({size, std::abs(corner->x), std::abs(corner->y), std::abs(corner->z)}); }
        }
        const auto slack = 1.0e-7 * std::max(1.0, size);
        for (const auto& face : faces) {
            const auto& a = corners[face[0]];
            const auto& b = corners[face[1]];
            const auto& c = corners[face[2]];
            if (!a.has_value() || !b.has_value() || !c.has_value()) { continue; }
            const auto normal = (*b - *a).cross(*c - *a).normalized(1.0e-12);
            if (!normal.has_value()) { continue; }
            // Its plane's group, looked up by the plane rounded (its normal
            // turned one way), then matched exactly.
            auto unit = *normal;
            auto offset = unit.dot(*a);
            const auto flip = unit.x < 0 || (unit.x == 0 && (unit.y < 0 || (unit.y == 0 && unit.z < 0)));
            if (flip) {
                unit = unit * -1.0;
                offset = -offset;
            }
            const std::tuple key {std::llround(unit.x * 1.0e6), std::llround(unit.y * 1.0e6), std::llround(unit.z * 1.0e6), std::llround(offset / (slack * 8))};
            auto& candidates = byPlane[key];
            const auto match = std::find_if(candidates.begin(), candidates.end(), [&](std::size_t index) {
                const auto& group = groups[index];
                return std::abs(group.normal.dot(*normal)) > 1 - 1.0e-9 && std::abs(group.offset - group.normal.dot(*a)) < slack;
            });
            Group* found = nullptr;
            if (match != candidates.end()) {
                found = &groups[*match];
            } else {
                candidates.push_back(groups.size());
                found = &groups.emplace_back(Group {*normal, normal->dot(*a), {}});
            }
            for (std::size_t side = 0; side < 3; ++side) {
                const auto from = face[side], to = face[(side + 1) % 3];
                ++found->edges[{std::min(from, to), std::max(from, to)}];
            }
        }
        for (const auto& group : groups) {
            FlatMirror flat {group.normal, group.offset, {}};
            for (const auto& [edge, count] : group.edges) {
                if (count == 1) { flat.outline.push_back({*corners[edge.first], *corners[edge.second]}); }
            }
            if (!flat.outline.empty()) { mirror.flats.push_back(std::move(flat)); }
        }
        return mirror;
    }

    // Facing the eye: each flat piece's normal turned toward it (a flat
    // surface reflects on both sides).
    void face(Vec3 eye) {
        for (auto& flat : flats) {
            if (flat.height(eye) < 0) {
                flat.normal = flat.normal * -1.0;
                flat.offset = -flat.offset;
            }
        }
    }

private:
    // The sphere every used corner lies on, to within a hair of its size,
    // fitted by least squares: |p|^2 = 2 c.p + k for each corner p, linear
    // in the centre c and k = r^2 - |c|^2.
    static std::optional<SphereMirror> sphereThrough(const std::vector<std::optional<Vec3>>& corners, const std::vector<std::array<std::uint32_t, 3>>& faces) {
        std::vector<char> used(corners.size(), 0);
        for (const auto& face : faces) {
            for (const auto corner : face) {
                if (corner < used.size()) { used[corner] = 1; }
            }
        }
        std::vector<Vec3> points;
        for (std::size_t index = 0; index < corners.size(); ++index) {
            if (used[index] == 0) { continue; }
            if (!corners[index].has_value()) { return std::nullopt; }
            points.push_back(*corners[index]);
        }
        // Too few to be round rather than a box's or a cone's corners.
        if (points.size() < 12) { return std::nullopt; }
        Vec3 mean;
        for (const auto& point : points) { mean = mean + point; }
        mean = mean / static_cast<double>(points.size());
        // Solved about the mean, for accuracy: the system is 4 x 4.
        std::array<std::array<double, 5>, 4> system {};
        for (const auto& point : points) {
            const auto p = point - mean;
            const std::array<double, 4> row {2 * p.x, 2 * p.y, 2 * p.z, 1};
            const auto value = p.dot(p);
            for (std::size_t i = 0; i < 4; ++i) {
                for (std::size_t j = 0; j < 4; ++j) { system[i][j] += row[i] * row[j]; }
                system[i][4] += row[i] * value;
            }
        }
        for (std::size_t column = 0; column < 4; ++column) {
            auto pivot = column;
            for (auto row = column + 1; row < 4; ++row) {
                if (std::abs(system[row][column]) > std::abs(system[pivot][column])) { pivot = row; }
            }
            if (!(std::abs(system[pivot][column]) > 1.0e-300)) { return std::nullopt; }
            std::swap(system[column], system[pivot]);
            for (std::size_t row = 0; row < 4; ++row) {
                if (row == column) { continue; }
                const auto factor = system[row][column] / system[column][column];
                for (auto entry = column; entry < 5; ++entry) { system[row][entry] -= factor * system[column][entry]; }
            }
        }
        const Vec3 offset {system[0][4] / system[0][0], system[1][4] / system[1][1], system[2][4] / system[2][2]};
        const auto squared = system[3][4] / system[3][3] + offset.dot(offset);
        if (!(squared > 0)) { return std::nullopt; }
        const SphereMirror sphere {mean + offset, std::sqrt(squared)};
        for (const auto& point : points) {
            if (std::abs((point - sphere.centre).length() - sphere.radius) > 1.0e-4 * sphere.radius) { return std::nullopt; }
        }
        // All round it, not a cap of it (nor a flat ring, which lies on
        // every sphere through it): centred, and nearly as wide as the
        // sphere along every axis.
        if ((mean - sphere.centre).length() > .05 * sphere.radius) { return std::nullopt; }
        for (std::size_t axis = 0; axis < 3; ++axis) {
            const auto [least, most] = std::minmax_element(points.begin(), points.end(), [axis](const Vec3& a, const Vec3& b) { return a[axis] < b[axis]; });
            if ((*most)[axis] - (*least)[axis] < 1.7 * sphere.radius) { return std::nullopt; }
        }
        return sphere;
    }
};
}

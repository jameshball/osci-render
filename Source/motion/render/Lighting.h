#pragma once

#include "HiddenLineDepth.h"
#include "LightState.h"
#include "../model/Vec3.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <optional>
#include <vector>

// How lights light what the Scope draws. Each light that casts shadows sees
// the scene's surfaces from where it is, into depth buffers like the one a
// camera hides lines with: a point is in shadow where something is in front
// of it as the light sees it, the same test that hides a line from a camera.
// Pure geometry, so it can be tested alone.
namespace motion::hidden {
// An eye: where it is and which way it looks (right, up and forward, unit
// and square to each other).
struct Eye {
    Vec3 position, right {1, 0, 0}, up {0, 1, 0}, forward {0, 0, -1};
    Vec3 see(Vec3 world) const {
        const auto offset = world - position;
        return {offset.dot(right), offset.dot(up), offset.dot(forward)};
    }
    // Looking along `forward`, level to `above` where it can be.
    static Eye looking(Vec3 position, Vec3 forward, Vec3 above = {0, 1, 0}) {
        Eye eye;
        eye.position = position;
        eye.forward = forward.normalized(1.0e-12).value_or(Vec3 {0, 0, -1});
        auto right = eye.forward.cross(above).normalized(1.0e-6);
        if (!right.has_value()) { right = eye.forward.cross(Vec3 {1, 0, 0}).normalized(1.0e-6); }
        eye.right = right.value_or(Vec3 {1, 0, 0});
        eye.up = eye.right.cross(eye.forward);
        return eye;
    }
};

// How an eye's space lands on a depth buffer: in perspective, divided by
// depth (inverse depth is then linear across the buffer, and bigger nearer);
// or in parallel, as a directional light sees, its depth turned into a value
// that is just as linear, positive, and bigger nearer.
struct Projection {
    double focalLength = 1;
    bool parallel = false;
    double far = 0, span = 1;
    // Nothing nearer a perspective eye than this is seen or hides anything.
    static constexpr double nearPlane = .05;

    std::optional<Screen> operator()(Vec3 eye) const {
        if (parallel) { return Screen {eye.x * focalLength, eye.y * focalLength, 1 + (far - eye.z) / span}; }
        if (!(eye.z > nearPlane)) { return std::nullopt; }
        return Screen {eye.x / eye.z * focalLength, eye.y / eye.z * focalLength, 1 / eye.z};
    }
    std::optional<Screen> operator()(const std::optional<Vec3>& eye) const { return eye.has_value() ? (*this)(*eye) : std::nullopt; }
    // A point on the buffer back in the eye's space.
    Vec3 unproject(const Screen& at) const {
        if (parallel) { return {at.x / focalLength, at.y / focalLength, far - (at.w - 1) * span}; }
        return {at.x / at.w / focalLength, at.y / at.w / focalLength, 1 / at.w};
    }
    // Whether a corner is kept whole rather than cut where it comes into view.
    bool keeps(double depth) const { return parallel || depth > nearPlane * (1 + 1.0e-6); }
};

// What a light sees of the scene's surfaces, to find where it is blocked:
// one view for a spot or directional light, six (a cube round it) for a
// point light.
struct ShadowView {
    Eye eye;
    Projection projection;
    DepthBuffer* depth = nullptr;
};

// A light as it lights a frame: its state, and what it sees, when it casts
// shadows.
struct LightView {
    LightState state;
    std::vector<ShadowView> views;

    bool casts() const { return !views.empty() && state.shadows > 0; }
    // The view a point is seen through: a point light's cube face along the
    // axis the point is furthest along, else its only one.
    const ShadowView& viewOf(Vec3 world) const {
        if (views.size() != 6) { return views.front(); }
        const auto offset = world - state.position;
        std::size_t best = 0;
        for (std::size_t face = 1; face < views.size(); ++face) {
            if (offset.dot(views[face].eye.forward) > offset.dot(views[best].eye.forward)) { best = face; }
        }
        return views[best];
    }
    // How wide a pixel of its shadows is at a point, in the scene.
    double texel(Vec3 world) const {
        const auto& view = viewOf(world);
        const auto size = view.depth->pixelSize() / std::max(1.0e-9, view.projection.focalLength);
        return view.projection.parallel ? size : size * std::max(Projection::nearPlane, view.eye.see(world).z);
    }

    // How much of the light reaches a point (1 where nothing blocks it), as
    // the shadows' strength allows.
    double reaching(Vec3 world) const {
        if (!casts()) { return 1; }
        const auto& view = viewOf(world);
        const auto at = view.projection(view.eye.see(world));
        if (!at.has_value()) { return 1; }
        // A soft share at the shadow's edge, so it keeps still as the light moves.
        return 1 - state.shadows * (1 - view.depth->shownShare(*at));
    }
};

// The six faces of a cube round a point light: each sees a right angle.
inline std::array<Eye, 6> cubeEyes(Vec3 position) {
    return {Eye::looking(position, {1, 0, 0}), Eye::looking(position, {-1, 0, 0}), Eye::looking(position, {0, 1, 0}, {0, 0, -1}),
        Eye::looking(position, {0, -1, 0}, {0, 0, 1}), Eye::looking(position, {0, 0, 1}), Eye::looking(position, {0, 0, -1})};
}

// The faces a line runs along, to light it as they are lit: up to two (an
// edge between faces), in the scene, unit length; none for a line on no face.
struct Normals {
    std::array<Vec3, 2> values {};
    int count = 0;
    void add(std::optional<Vec3> normal) {
        if (normal.has_value() && count < 2) { values[static_cast<std::size_t>(count++)] = *normal; }
    }
};

// A face's unit normal from its corners; none for one with no area.
inline std::optional<Vec3> normalOf(Vec3 a, Vec3 b, Vec3 c) { return (b - a).cross(c - a).normalized(1.0e-12); }

// The light reaching a point, in each colour: the lights it is in the reach
// and cone of, each times how squarely the faces it runs along face it (on
// either side: an open surface is lit from both; a closed one's far side is
// in its own shadow), and less where something blocks it. A line on no face
// takes a light whichever way it comes from.
// How far, in shadow pixels, a line's shadow test is lifted off its faces.
inline constexpr double normalLift = 1.5;
inline std::array<double, 3> illumination(const std::vector<LightView>& lights, Vec3 point, const Normals& normals) {
    std::array<double, 3> total {};
    for (const auto& light : lights) {
        const auto& state = light.state;
        double strength = state.intensity;
        if (!(strength > 0)) { continue; }
        if (!state.ambient()) {
            Vec3 toward;
            if (state.parallel()) {
                toward = state.direction * -1.0;
            } else {
                const auto offset = state.position - point;
                const auto distance = offset.length();
                if (!(distance > 1.0e-9)) { continue; }
                toward = offset / distance;
                // Fading smoothly to nothing at its range.
                const auto reach = distance / std::max(1.0e-9, state.range);
                const auto falloff = std::max(0.0, 1 - reach * reach);
                strength *= falloff * falloff;
                if (state.spot()) {
                    constexpr double radians = 3.14159265358979323846 / 360;
                    const auto along = -toward.dot(state.direction);
                    const auto outer = std::cos(state.cone * radians), inner = std::cos(state.cone * (1 - state.softness) * radians);
                    if (inner - outer > 1.0e-9) {
                        const auto t = std::clamp((along - outer) / (inner - outer), 0.0, 1.0);
                        strength *= t * t * (3 - 2 * t);
                    } else {
                        strength *= along >= outer ? 1.0 : 0.0;
                    }
                }
            }
            // Each face a line runs along takes the light as squarely as it
            // faces it, times what reaches it there. A line lies on its
            // faces, so each is tested a little off its own face toward the
            // light: one turned to the light is then clear of itself, and
            // one turned away is inside what it bounds and in its shadow. An
            // edge on the line where a solid turns from the light so fades
            // out as its lit face turns edge on, rather than flicking between
            // lit and dark as the light moves.
            if (normals.count > 0) {
                const auto lift = light.casts() ? normalLift * light.texel(point) : 0.0;
                double reached = 0;
                for (int index = 0; index < normals.count; ++index) {
                    const auto& normal = normals.values[static_cast<std::size_t>(index)];
                    const auto facing = normal.dot(toward);
                    if (facing == 0) { continue; }
                    reached += std::abs(facing) * light.reaching(point + normal * (facing > 0 ? lift : -lift));
                }
                strength *= reached / normals.count;
            } else {
                strength *= light.reaching(point);
            }
        }
        for (std::size_t channel = 0; channel < 3; ++channel) { total[channel] += state.colour[channel] * strength; }
    }
    return total;
}

// Depth buffers for lights' shadows, kept from frame to frame: a point light's
// cube faces are coarser than a camera's buffer, a spot or directional light's
// as fine.
class ShadowBuffers {
public:
    static constexpr int cubeAcross = 512, singleAcross = 1024;
    // The buffers for the frame being lit, covering a square view, emptied.
    void reset() { usedCube = usedSingle = 0; }
    DepthBuffer& next(bool cube) {
        auto& pool = cube ? cubes : singles;
        auto& used = cube ? usedCube : usedSingle;
        if (used == pool.size()) { pool.emplace_back(Reach {}, cube ? cubeAcross : singleAcross); }
        auto& buffer = pool[used++];
        buffer.cover({});
        return buffer;
    }

private:
    std::deque<DepthBuffer> cubes, singles;
    std::size_t usedCube = 0, usedSingle = 0;
};
}

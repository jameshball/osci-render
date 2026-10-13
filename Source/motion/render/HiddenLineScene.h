#pragma once

#include "CompositionRenderer.h"
#include "HiddenLineDepth.h"
#include "HiddenLineShapes.h"
#include "Lighting.h"
#include "SceneRays.h"
#include "TrianglePoints.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>

// What culling, lighting and reflecting a frame share: the clips in it, how
// finely lines are walked, and how light colours them.
namespace motion::hidden {
// The shortest run that culling leaves kept, in output units (the frame
// spans -1 to 1): anything shorter would only draw a dot.
inline constexpr double shortestOnScreen = .012;
// How far apart, at most, the points a line is tested at along its true
// path are, on screen: a pixel of the camera's depth buffer.
inline constexpr double testStep = DepthBuffer::pixel;
// A piece this short at the end of a cut run (two pixels) is too small to
// see but would cost the beam a sample of its own, so it is trimmed.
inline constexpr double sliver = 2 * DepthBuffer::pixel;

struct Styled;

// A clip of the frame being culled or lit: its drawing at that time (or, for
// a point source, its frame of points), and whether anything bends it.
struct SceneItem {
    std::size_t index;
    const PreparedClip* clip;
    std::shared_ptr<const PreparedSource> source;
    const PreparedDrawing* drawing;
    const PreparedPointFrames* points;
    std::size_t frame;
    bool bent;
    // How much it mirrors this frame (0 to 1): its own lines show the rest.
    double mirrors = 0;
    // How it draws itself this frame, when not by its edges alone (see
    // LineStyle); `drawing` is then the one it draws instead.
    const Styled* styled = nullptr;
};

// Where a point at `weights` on a triangle of a clip's source is, from its
// corners there: its place alone.
inline osci::Point weighted(const osci::Point& a, const osci::Point& b, const osci::Point& c, const Weights& weights) {
    const auto axisAt = [&](float osci::Point::* axis) { return static_cast<float>(a.*axis * weights[0] + b.*axis * weights[1] + c.*axis * weights[2]); };
    return osci::Point(axisAt(&osci::Point::x), axisAt(&osci::Point::y), axisAt(&osci::Point::z));
}

// A shape's length on screen, from `probes` + 1 points evenly along it:
// `at(point)` puts each on screen (none where it can't), and what is between
// two that are both there counts. `start`, where its start is on screen,
// when already known.
template <typename At>
double screenLength(osci::Shape& shape, int probes, At&& at, std::optional<Screen> start) {
    double length = 0;
    auto previous = start;
    for (int probe = 1; probe <= probes; ++probe) {
        const auto next = at(shape.nextVector(probe / static_cast<float>(probes)));
        if (previous.has_value() && next.has_value()) { length += std::hypot(next->x - previous->x, next->y - previous->y); }
        previous = next;
    }
    return length;
}
template <typename At>
double screenLength(osci::Shape& shape, int probes, At&& at) {
    return screenLength(shape, probes, at, at(shape.nextVector(0.0f)));
}

// How many steps to take: `wanted` rounded up, within least and most, and
// defined however long a line is (a huge or NaN length casts to int badly).
inline int stepCount(double wanted, int least, int most) {
    if (std::isnan(wanted)) { return least; }
    return static_cast<int>(std::clamp(std::ceil(wanted), static_cast<double>(least), static_cast<double>(most)));
}

// How much of the beam's time a part takes for its length, beside the rest,
// from how bright it is drawn (its brightest light, its style's amount and
// how little its surface mirrors, together): less the dimmer it is, down to a
// little. Drawn more quickly where it is dim, it leaves its time to what is
// bright, and a part fading out gives its time back as it fades rather than
// all at once when it goes. Its colour stays as dim as it is, so what dims
// evenly is still dimmer; this only shares the beam among the parts.
// On a display with no brightness input (`xyOnly`), its time is all there is
// to show its brightness by, and its colour is not dimmed.
inline constexpr double leastLitWeight = .05;
inline double litWeight(double brightest, bool xyOnly = false) {
    const auto light = std::max(0.0, brightest);
    // As bright, for its length, as a line on a scope with brightness: there
    // it takes time as the square root of its light and is as bright as its
    // light, so light to the power of 1.5 in all.
    return std::clamp(xyOnly ? std::pow(std::min(1.0, light), 1.5) : std::sqrt(light), leastLitWeight, 1.0);
}

// Values at every step from 0 to `steps`, found by `find(step)` at every
// third, and at those between only where `differ(from, to)` says the found
// ones either side need them; elsewhere `blend(step, from, to, share)` fills
// them in. For what changes smoothly almost everywhere along a line.
template <typename Find, typename Differ, typename Blend>
void findSparsely(int steps, Find&& find, Differ&& differ, Blend&& blend) {
    constexpr int stride = 3;
    find(0);
    for (int from = 0; from < steps;) {
        const auto to = std::min(steps, from + stride);
        find(to);
        const auto fine = differ(from, to);
        for (auto step = from + 1; step < to; ++step) {
            if (fine) {
                find(step);
            } else {
                blend(step, from, to, static_cast<float>(step - from) / static_cast<float>(to - from));
            }
        }
        from = to;
    }
}

// Whether two stops found along a line differ enough in their light (or
// colour) for those between to be found too: what shadows take from them
// differs, or a channel by more than `alike`.
inline bool lightsDiffer(const std::array<float, 3>& a, const std::array<float, 3>& b, double shadeA, double shadeB, float alike) {
    return std::abs(shadeA - shadeB) > 1.0e-6 || std::abs(a[0] - b[0]) > alike || std::abs(a[1] - b[1]) > alike || std::abs(a[2] - b[2]) > alike;
}

// The samples from `low` to `high` to keep, into `kept` in order, so that
// blending straight between each two kept, none between strays by more than
// `limit`: split where one strays most, then each side again.
// `strayOf(from, middle, to)` says how far `middle` strays from the blend
// between `from` and `to`. `pending` is scratch.
template <typename StrayOf>
void splitWhereStraying(std::size_t low, std::size_t high, double limit, StrayOf&& strayOf, std::vector<std::size_t>& kept, std::vector<std::pair<std::size_t, std::size_t>>& pending) {
    kept.assign({low, high});
    pending.assign({{low, high}});
    while (!pending.empty()) {
        const auto [from, to] = pending.back();
        pending.pop_back();
        double worst = 0;
        auto at = from;
        for (auto middle = from + 1; middle < to; ++middle) {
            const auto stray = strayOf(from, middle, to);
            if (stray > worst) {
                worst = stray;
                at = middle;
            }
        }
        if (worst > limit) {
            kept.push_back(at);
            pending.push_back({from, at});
            pending.push_back({at, to});
        }
    }
    std::sort(kept.begin(), kept.end());
    kept.erase(std::unique(kept.begin(), kept.end()), kept.end());
}

// Yes-or-no answers at every step from 0 to `steps`, found by `find(step)`
// at every sixth and, between two that differ, by halving the steps between
// until the change is found; where two agree, the steps between take their
// answer (`fill(step, answer)`). For what changes rarely along a line, such
// as whether something is in the way.
template <typename Find, typename Fill>
void findChanges(int steps, Find&& find, Fill&& fill) {
    constexpr int stride = 6;
    const auto between = [&](auto&& self, int from, bool before, int to, bool after) -> void {
        if (to - from < 2) { return; }
        if (before == after) {
            for (auto step = from + 1; step < to; ++step) { fill(step, before); }
            return;
        }
        const auto middle = (from + to) / 2;
        const auto here = find(middle);
        self(self, from, before, middle, here);
        self(self, middle, here, to, after);
    };
    auto previous = find(0);
    for (int from = 0; from < steps;) {
        const auto to = std::min(steps, from + stride);
        const auto next = find(to);
        between(between, from, previous, to, next);
        previous = next;
        from = to;
    }
}

// The light reaching a point of a clip, in each colour: what the lights give
// it, as far as the clip is lit by lights (at 0 its own colour, 1 in every
// channel). `shade` is told what shadows take from it.
inline std::array<float, 3> lightAt(const std::vector<LightView>& lights, Vec3 point, const LineNormals& normals, float response, double* shade = nullptr) {
    const auto total = illumination(lights, point, normals, shade);
    std::array<float, 3> light {};
    for (std::size_t channel = 0; channel < 3; ++channel) { light[channel] = 1 + response * (static_cast<float>(total[channel]) - 1); }
    return light;
}

// Every face of the clips' surfaces whose corners `world` all places in the
// scene: `face(surface, index, a, b, c)`, with them there.
template <typename Face>
void forEachPlacedFace(const std::vector<SceneItem>& items, const std::vector<std::vector<std::optional<Vec3>>>& world, Face&& face) {
    for (std::uint32_t surface = 0; surface < items.size(); ++surface) {
        const auto& item = items[surface];
        if (item.drawing == nullptr) { continue; }
        const auto& triangles = item.drawing->faces().triangles;
        for (std::uint32_t index = 0; index < triangles.size(); ++index) {
            const auto& a = world[surface][triangles[index][0]];
            const auto& b = world[surface][triangles[index][1]];
            const auto& c = world[surface][triangles[index][2]];
            if (a.has_value() && b.has_value() && c.has_value()) { face(surface, index, *a, *b, *c); }
        }
    }
}

// What can stand between two points of the scene: every surface's faces.
inline void buildRays(const std::vector<SceneItem>& items, const std::vector<std::vector<std::optional<Vec3>>>& world, SceneRays& rays) {
    std::vector<SceneRays::Triangle> faces;
    forEachPlacedFace(items, world, [&](std::uint32_t surface, std::uint32_t, Vec3 a, Vec3 b, Vec3 c) { faces.push_back({a, b, c, surface}); });
    rays.build(std::move(faces));
}
}

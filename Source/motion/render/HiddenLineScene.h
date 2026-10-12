#pragma once

#include "CompositionRenderer.h"
#include "HiddenLineDepth.h"
#include "Lighting.h"
#include "SceneRays.h"
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
// Lines are tested every half pixel along their true path.
inline constexpr double testStep = DepthBuffer::pixel;
// A piece this short at the end of a cut run (two pixels) is too small to
// see but would cost the beam a sample of its own, so it is trimmed.
inline constexpr double sliver = 2 * DepthBuffer::pixel;
// Nothing nearer the eye than this is drawn or hides anything.
inline constexpr double nearPlane = Projection::nearPlane;

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
    // How it draws itself this frame, when not by its edges alone (see
    // LineStyle); `drawing` is then the one it draws instead.
    const Styled* styled = nullptr;
};

// How much of the beam's time a lit part takes for its length, beside the
// rest: less the dimmer its brightest light, down to a little. Drawn more
// quickly where it is dim, it leaves its time to what is bright, and a part
// fading out gives its time back as it fades rather than all at once when it
// goes. Its colour stays the light it takes, so what the light dims evenly is
// still dimmer; this only shares the beam among the parts.
// On a display with no brightness input (`xyOnly`), its time is all there is
// to show its light by: it takes time as it is bright, and its colour is not
// dimmed.
inline constexpr double leastLitWeight = .05;
inline double litWeight(double brightest, bool xyOnly = false) {
    const auto light = std::max(0.0, brightest);
    return std::clamp(xyOnly ? light : std::sqrt(light), leastLitWeight, 1.0);
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
inline std::array<float, 3> lightAt(const std::vector<LightView>& lights, Vec3 point, const Normals& normals, float response, double* shade = nullptr) {
    if (normals.unlit) { return {1, 1, 1}; }
    const auto total = illumination(lights, point, normals, shade);
    std::array<float, 3> light {};
    for (std::size_t channel = 0; channel < 3; ++channel) { light[channel] = 1 + response * (static_cast<float>(total[channel]) - 1); }
    return light;
}

// A point's own colour (white where it has none) times the light reaching
// it. Lit brighter than the beam can draw, it is as bright as it can be in
// its own colour: every channel scaled alike, not each cut off at full,
// which would turn any strong coloured light white.
inline osci::Point litColour(osci::Point point, const std::array<float, 3>& light) {
    point.r = (point.r < 0 ? 1.0f : point.r) * light[0];
    point.g = (point.g < 0 ? 1.0f : point.g) * light[1];
    point.b = (point.b < 0 ? 1.0f : point.b) * light[2];
    const auto brightest = std::max({point.r, point.g, point.b});
    if (brightest > 1) {
        point.r /= brightest;
        point.g /= brightest;
        point.b /= brightest;
    }
    return point;
}

// A shape drawn dimmer: a mirror's own lines, as it shows the scene in their
// place, or a surface's, as bright as its Lines say.
class DimmedShape final : public osci::Shape {
public:
    DimmedShape(std::unique_ptr<osci::Shape> whole, float gain) : shape(std::move(whole)), factor(gain) {}
    osci::Point nextVector(float progress) override {
        auto point = shape->nextVector(progress);
        point.r = (point.r < 0 ? 1.0f : point.r) * factor;
        point.g = (point.g < 0 ? 1.0f : point.g) * factor;
        point.b = (point.b < 0 ? 1.0f : point.b) * factor;
        return point;
    }
    void scale(float, float, float) override {}
    void translate(float, float, float) override {}
    float length() override { return shape->length(); }
    std::unique_ptr<osci::Shape> clone() override { return std::make_unique<DimmedShape>(shape->clone(), factor); }
    float gain() const { return factor; }
    std::string type() override { return "Dimmed"; }

private:
    std::unique_ptr<osci::Shape> shape;
    float factor;
};

// What can stand between two points of the scene: every surface's faces.
inline void buildRays(const std::vector<SceneItem>& items, const std::vector<std::vector<std::optional<Vec3>>>& world, SceneRays& rays) {
    std::vector<SceneRays::Triangle> faces;
    for (std::uint32_t surface = 0; surface < items.size(); ++surface) {
        const auto& item = items[surface];
        if (item.drawing == nullptr) { continue; }
        for (const auto& triangle : item.drawing->faces().triangles) {
            const auto& a = world[surface][triangle[0]];
            const auto& b = world[surface][triangle[1]];
            const auto& c = world[surface][triangle[2]];
            if (a.has_value() && b.has_value() && c.has_value()) { faces.push_back({*a, *b, *c, surface}); }
        }
    }
    rays.build(std::move(faces));
}
}

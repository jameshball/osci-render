#pragma once

#include "../model/Document.h"
#include "../model/SpatialMotion.h"
#include "../model/PreparedSource.h"
#include "../live/LiveSourceFrames.h"
#include "PreparedEffects.h"
#include "TimeCache.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <numbers>
#include <optional>
#include <vector>

namespace motion {
inline std::shared_ptr<const PreparedSpatial> prepareSpatial(bool path, bool quaternion, const std::array<Curve, objectPropertySpecs.size()>& curves) {
    auto spatial = std::make_shared<PreparedSpatial>();
    if (path) { spatial->path = PreparedPath::prepare(curves[0], curves[1], curves[2]); }
    if (quaternion) { spatial->orientation = PreparedOrientation::prepare(curves[3], curves[4], curves[5]); }
    if (spatial->path == nullptr && spatial->orientation == nullptr) { return nullptr; }
    return spatial;
}

// How a surface draws itself: its own lines (edges), its outline as the
// camera sees it, and patterns over it (in Pattern order: spiral, noise,
// slices, rings), each as a brightness 0..1, and how many
// lines across a pattern draws, and how far round its loop the noise has
// morphed (turns). Groups dim its edges, add their outline and patterns, and
// move its noise on by theirs.
struct LineStyle {
    double edges = 1, outline = 0;
    std::array<double, patternCount> patterns {};
    double detail = objectPropertySpecs[fillDetailIndex].defaultValue;
    double noiseTurn = 0;
    bool plain() const { return edges == 1 && outline == 0 && std::all_of(patterns.begin(), patterns.end(), [](double amount) { return amount == 0; }); }
    void include(const LineStyle& outer) {
        edges *= outer.edges;
        outline = std::max(outline, outer.outline);
        for (std::size_t pattern = 0; pattern < patternCount; ++pattern) { patterns[pattern] = std::max(patterns[pattern], outer.patterns[pattern]); }
        noiseTurn += outer.noiseTurn;
    }
};

// A percentage curve as 0..1 at a time; `otherwise` where it isn't finite.
inline double fractionAt(const Curve& curve, double time, double otherwise) {
    const auto value = curve.evaluate(time) / 100;
    return std::isfinite(value) ? std::clamp(value, 0.0, 1.0) : otherwise;
}

// A transform's curves evaluated at one time: the anchor taken away, scale,
// then rotation (Euler X, Y, Z, or a keyed orientation after modulation
// offsets), then translation (along a spatial path when there is one), and
// colour gains dimmed by the opacity. Evaluated once, it applies to any number
// of points.
struct TransformPose {
    using Matrix = std::array<std::array<float, 3>, 3>;
    std::array<float, 3> scale {1, 1, 1}, translation {}, colour {1, 1, 1}, anchor {};
    Matrix rotation {{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};

    static TransformPose at(const std::array<Curve, objectPropertySpecs.size()>& curves, double time, const PreparedSpatial* spatial) {
        TransformPose pose;
        constexpr auto radians = std::numbers::pi / 180.0;
        for (std::size_t axis = 0; axis < 3; ++axis) {
            pose.scale[axis] = static_cast<float>(curves[6 + axis].evaluate(time));
            pose.anchor[axis] = static_cast<float>(curves[anchorIndex + axis].evaluate(time));
        }
        // A linked rotation axis replaces its keys, so orientation interpolation
        // only applies while every rotation axis is keyed.
        const bool oriented = spatial != nullptr && spatial->orientation != nullptr && !curves[3].linked() && !curves[4].linked() && !curves[5].linked();
        if (oriented) {
            // Keys set the orientation; modulation adds Euler offsets in object
            // space before it.
            std::array<double, 3> extra {};
            for (std::size_t axis = 0; axis < 3; ++axis) {
                const auto& curve = curves[3 + axis];
                const auto keyed = curve.evaluateBase(time);
                extra[axis] = (curve.evaluateWith(keyed, time) - keyed) * radians;
            }
            const auto orientation = spatial->orientation->at(time);
            Matrix keyed {};
            for (std::size_t column = 0; column < 3; ++column) {
                const auto basis = orientation.rotate(column == 0, column == 1, column == 2);
                for (std::size_t row = 0; row < 3; ++row) { keyed[row][column] = static_cast<float>(basis[row]); }
            }
            pose.rotation = multiply(keyed, euler(extra[0], extra[1], extra[2]));
        } else {
            pose.rotation = euler(curves[3].evaluate(time) * radians, curves[4].evaluate(time) * radians, curves[5].evaluate(time) * radians);
        }
        if (spatial != nullptr && spatial->path != nullptr) {
            const auto position = spatial->path->at(time);
            for (std::size_t axis = 0; axis < 3; ++axis) {
                const auto& curve = curves[axis];
                pose.translation[axis] = static_cast<float>(curve.linked() ? curve.evaluate(time) : curve.evaluateWith(position[axis], time));
            }
        } else {
            for (std::size_t axis = 0; axis < 3; ++axis) { pose.translation[axis] = static_cast<float>(curves[axis].evaluate(time)); }
        }
        const auto opacity = static_cast<float>(opacityAt(curves, time));
        for (std::size_t channel = 0; channel < 3; ++channel) { pose.colour[channel] = static_cast<float>(curves[9 + channel].evaluate(time)) * opacity; }
        return pose;
    }
    // Opacity as a 0..1 gain; a modulated value past either end is clamped.
    static double opacityAt(const std::array<Curve, objectPropertySpecs.size()>& curves, double time) { return fractionAt(curves[opacityIndex], time, 0.0); }
    // How far lights light it, 0..1.
    static double litAt(const std::array<Curve, objectPropertySpecs.size()>& curves, double time) { return fractionAt(curves[litIndex], time, 1.0); }
    // How brightly it reflects, 0..1.
    static double reflectsAt(const std::array<Curve, objectPropertySpecs.size()>& curves, double time) { return fractionAt(curves[reflectsIndex], time, 0.0); }
    // How far its faces hide what is behind them, 0..1.
    static double solidAt(const std::array<Curve, objectPropertySpecs.size()>& curves, double time) { return fractionAt(curves[solidIndex], time, 0.0); }
    // Whether a property may be other than its default at some time: keys,
    // a link or modulators can move it.
    static bool mayDifferCurve(const Curve& curve, double usual) {
        return curve.base != usual || curve.drivers != nullptr || std::any_of(curve.keyframes().begin(), curve.keyframes().end(), [usual](const Keyframe& key) { return key.value != usual; });
    }
    static bool mayDiffer(const std::array<Curve, objectPropertySpecs.size()>& curves, std::size_t index) { return mayDifferCurve(curves[index], objectPropertySpecs[index].defaultValue); }
    static bool mayReflect(const std::array<Curve, objectPropertySpecs.size()>& curves) { return mayDiffer(curves, reflectsIndex); }
    // Solid unless it is a wireframe throughout.
    static bool mayBeSolid(const std::array<Curve, objectPropertySpecs.size()>& curves) { return mayDifferCurve(curves[solidIndex], 0); }
    // Whether it may draw itself other than by its edges alone.
    static bool mayRestyle(const std::array<Curve, objectPropertySpecs.size()>& curves) {
        for (auto index = edgesIndex; index < patternsIndex + patternCount; ++index) {
            if (mayDiffer(curves, index)) { return true; }
        }
        return false;
    }
    // How far round its loop the noise is at a time: its phase, and as many
    // turns as its flow has taken it by then.
    static double noiseTurnAt(const std::array<Curve, objectPropertySpecs.size()>& curves, double time) {
        const auto turn = curves[noisePhaseIndex].evaluate(time) / 360 + flowedBy(curves[noiseFlowIndex], time);
        return std::isfinite(turn) ? turn : 0.0;
    }
    // The turns a flow (turns a second) has carried the noise by `time`: the
    // area under it from 0, so a flow eased down slows the noise rather than
    // running it back. Exact for a steady flow; otherwise Simpson's rule
    // between its keys, finely enough to follow a modulator driving it.
    static double flowedBy(const Curve& flow, double time) {
        const auto driven = flow.drivers != nullptr && (flow.linked() || !flow.drivers->routes.empty());
        if (!flow.animated() && !driven) { return flow.evaluate(0) * time; }
        std::vector<double> stops {std::min(0.0, time), std::max(0.0, time)};
        for (const auto& key : flow.keyframes()) {
            if (key.time > stops[0] && key.time < stops[1]) { stops.push_back(key.time); }
        }
        std::sort(stops.begin(), stops.end());
        double area = 0;
        for (std::size_t index = 1; index < stops.size(); ++index) {
            const auto from = stops[index - 1], to = stops[index];
            const auto pairs = std::clamp(static_cast<int>(std::ceil((to - from) * (driven ? 60.0 : 8.0))), 8, 2048);
            const auto step = (to - from) / (2 * pairs);
            auto sum = flow.evaluate(from) + flow.evaluate(to);
            for (int sample = 1; sample < 2 * pairs; ++sample) { sum += flow.evaluate(from + sample * step) * (sample % 2 == 1 ? 4 : 2); }
            area += sum * step / 3;
        }
        return time < 0 ? -area : area;
    }
    // A Lines property, 0..1.
    static double linesAmount(const std::array<Curve, objectPropertySpecs.size()>& curves, std::size_t index, double time) {
        return fractionAt(curves[index], time, objectPropertySpecs[index].defaultValue / 100);
    }
    // How a clip or group's own Lines properties draw its surfaces.
    static LineStyle linesAt(const std::array<Curve, objectPropertySpecs.size()>& curves, double time) {
        LineStyle style;
        style.edges = linesAmount(curves, edgesIndex, time);
        style.outline = linesAmount(curves, outlineIndex, time);
        for (std::size_t pattern = 0; pattern < patternCount; ++pattern) { style.patterns[pattern] = linesAmount(curves, patternsIndex + pattern, time); }
        const auto detail = curves[fillDetailIndex].evaluate(time);
        style.detail = std::isfinite(detail) ? objectPropertySpecs[fillDetailIndex].clamp(std::round(detail)) : objectPropertySpecs[fillDetailIndex].defaultValue;
        style.noiseTurn = noiseTurnAt(curves, time);
        return style;
    }
    static double beamAt(const std::array<Curve, objectPropertySpecs.size()>& curves, double time) {
        const auto value = curves[beamIndex].evaluate(time);
        return std::isfinite(value) ? std::clamp(value, 0.0, maximumBeam) : 0.0;
    }

    // The source's colour (white when it has none) times the colour gains.
    osci::Point colourOf(osci::Point point) const {
        const auto uncoloured = point.r < 0;
        point.r = std::clamp((uncoloured ? 1.0f : point.r) * colour[0], 0.0f, 1.0f);
        point.g = std::clamp((uncoloured ? 1.0f : point.g) * colour[1], 0.0f, 1.0f);
        point.b = std::clamp((uncoloured ? 1.0f : point.b) * colour[2], 0.0f, 1.0f);
        return point;
    }
    // Scale and rotation turn about the anchor, which lands on the translation.
    osci::Point apply(osci::Point point, bool applyColour) const {
        const auto x = (point.x - anchor[0]) * scale[0], y = (point.y - anchor[1]) * scale[1], z = (point.z - anchor[2]) * scale[2];
        point.x = rotation[0][0] * x + rotation[0][1] * y + rotation[0][2] * z + translation[0];
        point.y = rotation[1][0] * x + rotation[1][1] * y + rotation[1][2] * z + translation[1];
        point.z = rotation[2][0] * x + rotation[2][1] * y + rotation[2][2] * z + translation[2];
        if (applyColour) { point = colourOf(point); }
        if (!point.isFinite()) {
            return { 0, 0, 0, 0, 0, 0 };
        }
        return point;
    }

private:
    // Rotation about X, then Y, then Z, as osci::Point::rotate.
    static Matrix euler(double x, double y, double z) {
        const auto cx = static_cast<float>(std::cos(x)), sx = static_cast<float>(std::sin(x));
        const auto cy = static_cast<float>(std::cos(y)), sy = static_cast<float>(std::sin(y));
        const auto cz = static_cast<float>(std::cos(z)), sz = static_cast<float>(std::sin(z));
        const Matrix aboutX {{{1, 0, 0}, {0, cx, -sx}, {0, sx, cx}}};
        const Matrix aboutY {{{cy, 0, sy}, {0, 1, 0}, {-sy, 0, cy}}};
        const Matrix aboutZ {{{cz, -sz, 0}, {sz, cz, 0}, {0, 0, 1}}};
        return multiply(aboutZ, multiply(aboutY, aboutX));
    }
    static Matrix multiply(const Matrix& a, const Matrix& b) {
        Matrix result {};
        for (std::size_t row = 0; row < 3; ++row) {
            for (std::size_t column = 0; column < 3; ++column) {
                result[row][column] = a[row][0] * b[0][column] + a[row][1] * b[1][column] + a[row][2] * b[2][column];
            }
        }
        return result;
    }
};

struct PreparedGroup {
    Id id;
    std::array<Curve, objectPropertySpecs.size()> curves;
    std::vector<PreparedEffect> effects;
    std::shared_ptr<const PreparedSpatial> spatial;

    explicit PreparedGroup(const Group& group) : id(group.id), effects(prepareEffects(group.effects)) {
        for (std::size_t index = 0; index < objectPropertySpecs.size(); ++index) {
            curves[index] = curveOrDefault(group.properties, objectPropertySpecs[index]);
        }
        spatial = prepareSpatial(group.spatialPath, group.quaternionRotation, curves);
    }
    const TransformPose& pose(double time) const {
        return poses.at(time, [&] { return TransformPose::at(curves, time, spatial.get()); });
    }
    osci::Point apply(osci::Point point, double time) const {
        return applyEffects(effects, pose(time).apply(point, true), time);
    }

private:
    TimeCache<TransformPose> poses;
};

// One authored clip's transform/effect scope. Runtime stages carry no document
// pointers; each clock maps main seconds directly to its authored coordinate.
struct PreparedClipStage {
    Id id = 0;
    double start = 0, end = 0;
    ClipTiming clock; // main seconds -> content, exact under a tempo map
    std::array<Curve, objectPropertySpecs.size()> curves;
    std::vector<PreparedEffect> effects, trackEffects, compositionEffects;
    std::vector<PreparedGroup> groups;
    std::shared_ptr<const PreparedSpatial> spatial;
    std::optional<ClipTiming> scopeClock;
    TimeCache<TransformPose> poses;

    double scopeTime(double time) const { return scopeClock.has_value() ? scopeClock->localTime(time) : time; }
    double localTime(double time) const { return clock.localTime(time); }
    // Visits its own curves at its content time, then each group's holding
    // it at the scope's time.
    template <typename Visit>
    void eachStageLayer(double time, Visit&& visit) const {
        visit(curves, localTime(time));
        for (const auto& group : groups) { visit(group.curves, scopeTime(time)); }
    }
    osci::Point processStage(osci::Point point, double time) const {
        const auto local = localTime(time);
        const auto& pose = poses.at(local, [&] { return TransformPose::at(curves, local, spatial.get()); });
        point = pose.colourOf(point);
        point = applyEffects(effects, point, local);
        point = pose.apply(point, false);
        point = applyEffects(trackEffects, point, scopeTime(time));
        for (const auto& group : groups) { point = group.apply(point, scopeTime(time)); }
        return applyEffects(compositionEffects, point, scopeTime(time));
    }
};

struct PreparedClip : PreparedClipStage {
    std::shared_ptr<const LiveSourceIdentity> liveIdentity;
    std::shared_ptr<const PreparedSource> source;
    std::vector<PreparedClipStage> ancestors; // inner-to-outer
    Id rootTrack = 0; // the main timeline track this clip plays on
    // A light, pictured in the Scene: the beam never draws it.
    bool light = false;

    Id editorId() const { return ancestors.empty() ? id : ancestors.back().id; }
    bool active(double time) const { return time >= start && time < end; }
    // Visits every layer of curves that shapes it, each at its own time: its
    // own first, then its groups', then its nested placements' and theirs.
    // Allocation-free, for the audio thread.
    template <typename Visit>
    void eachLayer(double time, Visit&& visit) const {
        eachStageLayer(time, visit);
        for (const auto& ancestor : ancestors) { ancestor.eachStageLayer(time, visit); }
    }
    // Whether the beam draws it at all: opacity dims a clip through its
    // colour, and only a fully transparent one gives up its beam time.
    bool visible(double time) const {
        bool shown = true;
        eachLayer(time, [&](const auto& layer, double at) { shown = shown && TransformPose::opacityAt(layer, at) > 0 && TransformPose::beamAt(layer, at) > 0; });
        return shown;
    }
    // The scale on its share of the beam's time: its own Beam times its
    // groups' and its nested placements'.
    double beam(double time) const {
        double value = 1;
        eachLayer(time, [&](const auto& layer, double at) { value *= TransformPose::beamAt(layer, at); });
        return std::isfinite(value) ? value : 0.0;
    }
    // How opaque it is: its own Opacity times its groups' and its nested
    // placements'. It dims its colour; on a display with no brightness
    // input (XY only), its share of the beam's time instead.
    double opacity(double time) const {
        double value = 1;
        eachLayer(time, [&](const auto& layer, double at) { value *= TransformPose::opacityAt(layer, at); });
        return std::isfinite(value) ? std::clamp(value, 0.0, 1.0) : 1.0;
    }
    // How far lights light it: its own Lit by lights times its groups' and
    // its nested placements'.
    double lit(double time) const {
        double value = 1;
        eachLayer(time, [&](const auto& layer, double at) { value *= TransformPose::litAt(layer, at); });
        return std::isfinite(value) ? value : 1.0;
    }
    // How its surfaces draw themselves, with its groups' and nested
    // placements' (see LineStyle). The detail is always its own.
    LineStyle lines(double time) const {
        auto style = TransformPose::linesAt(curves, localTime(time));
        bool own = true;
        eachLayer(time, [&](const auto& layer, double at) {
            if (own) {
                own = false;
                return;
            }
            style.include(TransformPose::linesAt(layer, at));
        });
        return style;
    }
    bool mayRestyle() const {
        bool may = false;
        eachLayer(0, [&](const auto& layer, double) { may = may || TransformPose::mayRestyle(layer); });
        return may;
    }
    // How brightly its surfaces reflect: its own Mirror, or its groups' or
    // nested placements', whichever is most.
    double reflects(double time) const {
        double value = 0;
        eachLayer(time, [&](const auto& layer, double at) { value = std::max(value, TransformPose::reflectsAt(layer, at)); });
        return value;
    }
    bool mayReflect() const {
        bool may = false;
        eachLayer(0, [&](const auto& layer, double) { may = may || TransformPose::mayReflect(layer); });
        return may;
    }
    // How far its faces hide what is behind them: its own Solid times its
    // groups' and its nested placements', so any of them made a wireframe
    // makes it one.
    double solid(double time) const {
        double value = 1;
        eachLayer(time, [&](const auto& layer, double at) { value *= TransformPose::solidAt(layer, at); });
        return value;
    }
    bool mayBeSolid() const {
        bool may = true;
        eachLayer(0, [&](const auto& layer, double) { may = may && TransformPose::mayBeSolid(layer); });
        return may;
    }
    const PreparedSource* resolveSource(const LiveSourceFrames* liveFrames = nullptr) const noexcept {
        if (liveIdentity != nullptr) { return liveFrames != nullptr ? liveFrames->resolve(liveIdentity.get()) : nullptr; }
        return source.get();
    }
    osci::Point sample(double time, double phase, double phaseSpan = 0, double timeSpan = 0, const LiveSourceFrames* liveFrames = nullptr) const {
        const auto* resolved = resolveSource(liveFrames);
        if (resolved == nullptr) { return {0, 0, 0, 0, 0, 0}; }
        return processPoint(resolved->sample(localTime(time), phase, phaseSpan, std::abs(clock.rate) * timeSpan), time);
    }
    osci::Point processPoint(osci::Point point, double time) const {
        point = processStage(point, time);
        for (const auto& ancestor : ancestors) { point = ancestor.processStage(point, time); }
        return point;
    }
};
}

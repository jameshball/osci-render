#pragma once

#include "../model/Cancellation.h"
#include "../model/Document.h"
#include "../model/GeneratorDrawing.h"
#include "../import/FractalDrawing.h"
#include "../model/CompositionGraph.h"
#include "PreparedEffects.h"
#include "PreparedSoundtrack.h"
#include "PreparedDrivers.h"
#include "../model/SpatialMotion.h"
#include "../model/Vec3.h"
#include "../live/LiveSourceFrames.h"
#include "HiddenLineBake.h"
#include "LightState.h"
#include "../../../modules/osci_gui/visualiser/osci_VisualiserGeometry.h"
#include "../model/PropertySchema.h"
#include "../model/LuaClipBake.h"
#include "SampleClock.h"
#include "TimeCache.h"
#include <array>
#include <span>
#include <numbers>
#include <optional>

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
    static double opacityAt(const std::array<Curve, objectPropertySpecs.size()>& curves, double time) {
        const auto value = curves[opacityIndex].evaluate(time) / 100;
        return std::isfinite(value) ? std::clamp(value, 0.0, 1.0) : 0.0;
    }
    // How far lights light it, 0..1.
    static double litAt(const std::array<Curve, objectPropertySpecs.size()>& curves, double time) {
        const auto value = curves[litIndex].evaluate(time) / 100;
        return std::isfinite(value) ? std::clamp(value, 0.0, 1.0) : 1.0;
    }
    // How brightly it reflects, 0..1.
    static double reflectsAt(const std::array<Curve, objectPropertySpecs.size()>& curves, double time) {
        const auto value = curves[reflectsIndex].evaluate(time) / 100;
        return std::isfinite(value) ? std::clamp(value, 0.0, 1.0) : 0.0;
    }
    // How far its faces hide what is behind them, 0..1.
    static double solidAt(const std::array<Curve, objectPropertySpecs.size()>& curves, double time) {
        const auto value = curves[solidIndex].evaluate(time) / 100;
        return std::isfinite(value) ? std::clamp(value, 0.0, 1.0) : 0.0;
    }
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
        const auto value = curves[index].evaluate(time) / 100;
        return std::isfinite(value) ? std::clamp(value, 0.0, 1.0) : objectPropertySpecs[index].defaultValue / 100;
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

inline osci::Point applyTransform(osci::Point point, const std::array<Curve, objectPropertySpecs.size()>& curves, double time, bool applyColour = true, const PreparedSpatial* spatial = nullptr) {
    return TransformPose::at(curves, time, spatial).apply(point, applyColour);
}

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
    bool visible(double time) const { return TransformPose::opacityAt(curves, time) > 0 && TransformPose::beamAt(curves, time) > 0; }
    double beam(double time) const { return TransformPose::beamAt(curves, time); }
    double opacity(double time) const { return TransformPose::opacityAt(curves, time); }
    LineStyle lines(double time) const { return TransformPose::linesAt(curves, time); }
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
    // The nearest time inside the clip's own interval.
    double clampToContent(double time) const { return std::clamp(time, start, std::nextafter(end, start)); }
    double localTime(double time) const { return clock.localTime(time); }
    // Shown unless it or a group holding it is fully transparent or has no beam.
    bool stageVisible(double time) const {
        const auto local = localTime(time);
        if (!(TransformPose::opacityAt(curves, local) > 0) || !(TransformPose::beamAt(curves, local) > 0)) { return false; }
        return std::all_of(groups.begin(), groups.end(), [&](const auto& group) { return group.visible(scopeTime(time)); });
    }
    double stageLit(double time) const {
        auto value = TransformPose::litAt(curves, localTime(time));
        for (const auto& group : groups) { value *= TransformPose::litAt(group.curves, scopeTime(time)); }
        return value;
    }
    // How it draws itself (see LineStyle): its own, with its groups' edges
    // dimming it and their outline and patterns adding to it.
    LineStyle stageLines(double time) const {
        auto style = TransformPose::linesAt(curves, localTime(time));
        for (const auto& group : groups) { style.include(group.lines(scopeTime(time))); }
        return style;
    }
    bool stageMayRestyle() const {
        return TransformPose::mayRestyle(curves) || std::any_of(groups.begin(), groups.end(), [](const auto& group) { return TransformPose::mayRestyle(group.curves); });
    }
    // Its own Mirror, or a group's holding it, whichever is more.
    double stageReflects(double time) const {
        auto value = TransformPose::reflectsAt(curves, localTime(time));
        for (const auto& group : groups) { value = std::max(value, TransformPose::reflectsAt(group.curves, scopeTime(time))); }
        return value;
    }
    bool stageMayReflect() const {
        return TransformPose::mayReflect(curves) || std::any_of(groups.begin(), groups.end(), [](const auto& group) { return TransformPose::mayReflect(group.curves); });
    }
    // Its own Solid times its groups'.
    double stageSolid(double time) const {
        auto value = TransformPose::solidAt(curves, localTime(time));
        for (const auto& group : groups) { value *= TransformPose::solidAt(group.curves, scopeTime(time)); }
        return value;
    }
    bool stageMayBeSolid() const {
        return TransformPose::mayBeSolid(curves) && std::all_of(groups.begin(), groups.end(), [](const auto& group) { return TransformPose::mayBeSolid(group.curves); });
    }
    double stageBeam(double time) const {
        auto value = TransformPose::beamAt(curves, localTime(time));
        for (const auto& group : groups) { value *= group.beam(scopeTime(time)); }
        return value;
    }
    double stageOpacity(double time) const {
        auto value = TransformPose::opacityAt(curves, localTime(time));
        for (const auto& group : groups) { value *= group.opacity(scopeTime(time)); }
        return value;
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
    // Whether the beam draws it at all: opacity dims a clip through its
    // colour, and only a fully transparent one gives up its beam time.
    bool visible(double time) const {
        return stageVisible(time) && std::all_of(ancestors.begin(), ancestors.end(), [&](const auto& ancestor) { return ancestor.stageVisible(time); });
    }
    // The scale on its share of the beam's time: its own Beam times its
    // groups' and its nested placements'.
    double beam(double time) const {
        auto value = stageBeam(time);
        for (const auto& ancestor : ancestors) { value *= ancestor.stageBeam(time); }
        return std::isfinite(value) ? value : 0.0;
    }
    // How opaque it is: its own Opacity times its groups' and its nested
    // placements'. It dims its colour; on a display with no brightness
    // input (XY only), its share of the beam's time instead.
    double opacity(double time) const {
        auto value = stageOpacity(time);
        for (const auto& ancestor : ancestors) { value *= ancestor.stageOpacity(time); }
        return std::isfinite(value) ? std::clamp(value, 0.0, 1.0) : 1.0;
    }
    // How far lights light it: its own Lit by lights times its groups' and
    // its nested placements'.
    double lit(double time) const {
        auto value = stageLit(time);
        for (const auto& ancestor : ancestors) { value *= ancestor.stageLit(time); }
        return std::isfinite(value) ? value : 1.0;
    }
    // How its surfaces draw themselves, with its groups' and nested
    // placements' (see LineStyle).
    LineStyle lines(double time) const {
        auto style = stageLines(time);
        for (const auto& ancestor : ancestors) { style.include(ancestor.stageLines(time)); }
        return style;
    }
    bool mayRestyle() const {
        return stageMayRestyle() || std::any_of(ancestors.begin(), ancestors.end(), [](const auto& ancestor) { return ancestor.stageMayRestyle(); });
    }
    // How brightly its surfaces reflect: its own Mirror, or its groups' or
    // nested placements', whichever is most.
    double reflects(double time) const {
        auto value = stageReflects(time);
        for (const auto& ancestor : ancestors) { value = std::max(value, ancestor.stageReflects(time)); }
        return value;
    }
    bool mayReflect() const {
        return stageMayReflect() || std::any_of(ancestors.begin(), ancestors.end(), [](const auto& ancestor) { return ancestor.stageMayReflect(); });
    }
    // How far its faces hide what is behind them: its own Solid times its
    // groups' and its nested placements', so any of them made a wireframe
    // makes it one.
    double solid(double time) const {
        auto value = stageSolid(time);
        for (const auto& ancestor : ancestors) { value *= ancestor.stageSolid(time); }
        return value;
    }
    bool mayBeSolid() const {
        return stageMayBeSolid() && std::all_of(ancestors.begin(), ancestors.end(), [](const auto& ancestor) { return ancestor.stageMayBeSolid(); });
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

// A light clip as it lights the frames: where it is and which way it shines
// come from its clip's transform (and the groups and placements holding it),
// its colour from its Colour and Opacity, and the rest from its parameters.
struct PreparedLight {
    GeneratorKind kind = GeneratorKind::pointLight;
    PreparedClip clip;
    std::span<const PropertySpec> specs;
    std::vector<Curve> parameters;

    using State = LightState;
    // A parameter at the clip's own time, within its range. A kind of light
    // without one (an ambient light's range, say) never uses it: 0.
    double parameter(std::string_view id, double time) const {
        for (std::size_t index = 0; index < specs.size() && index < parameters.size(); ++index) {
            if (specs[index].id != id) { continue; }
            const auto value = parameters[index].evaluate(clip.localTime(time));
            return std::isfinite(value) ? specs[index].clamp(value) : specs[index].defaultValue;
        }
        return 0;
    }
};

// A transform chain from a clip or group up through its parent groups, used
// to place camera targets and parents. Groups run on the scope (project)
// clock; a clip link maps project time to its content time.
struct PreparedChain {
    struct Link {
        std::array<Curve, objectPropertySpecs.size()> curves;
        std::shared_ptr<const PreparedSpatial> spatial;
        ClipTiming clock;
        bool clip = false;
    };
    std::vector<Link> links; // inner to outer
    bool empty() const { return links.empty(); }
    Vec3 apply(Vec3 position, double time) const {
        osci::Point point(static_cast<float>(position.x), static_cast<float>(position.y), static_cast<float>(position.z));
        for (const auto& link : links) {
            const auto local = link.clip ? link.clock.localTime(time) : time;
            point = applyTransform(point, link.curves, local, false, link.spatial.get());
        }
        return {point.x, point.y, point.z};
    }
    // The innermost link's anchor in its own space, where a camera aims.
    Vec3 anchor(double time) const {
        if (links.empty()) { return {}; }
        const auto& link = links.front();
        const auto local = link.clip ? link.clock.localTime(time) : time;
        return {link.curves[anchorIndex].evaluate(local), link.curves[anchorIndex + 1].evaluate(local), link.curves[anchorIndex + 2].evaluate(local)};
    }
};

struct PreparedCamera {
    Id id;
    std::array<Curve, cameraPropertySpecs.size()> curves;
    PreparedChain target, parent;

    // How much it shades by hatching (0..1), and how many cells across.
    double hatching(double time) const {
        const auto value = curves[cameraHatchingIndex].evaluate(time) / 100;
        return std::isfinite(value) ? std::clamp(value, 0.0, 1.0) : 0.0;
    }
    int hatchDetail(double time) const {
        const auto& spec = cameraPropertySpecs[cameraHatchDetailIndex];
        const auto value = curves[cameraHatchDetailIndex].evaluate(time);
        return static_cast<int>(std::isfinite(value) ? spec.clamp(std::round(value)) : spec.defaultValue);
    }
    // Whether it may hatch at all.
    bool mayHatch() const { return TransformPose::mayDifferCurve(curves[cameraHatchingIndex], cameraPropertySpecs[cameraHatchingIndex].defaultValue); }

    struct Frame {
        Vec3 position, right, up, forward;
        double focalLength = 1;
    };
    // The fixed framing with no camera: four units in front of the middle,
    // looking back at it.
    static Frame fixedFrame() { return {{0, 0, 4}, {1, 0, 0}, {0, 1, 0}, {0, 0, -1}, 4}; }
    // The camera's world position and orthonormal basis at `time`: authored
    // position/rotation (X, then Y, then Z), carried by the parent group,
    // then aimed at the target's anchor with Z rotation kept as roll.
    std::optional<Frame> frame(double time) const {
        // Where it is and how it sees: its first seven.
        std::array<double, 7> values;
        for (std::size_t index = 0; index < values.size(); ++index) {
            values[index] = curves[index].evaluate(time);
            if (!std::isfinite(values[index])) { return std::nullopt; }
        }
        // Cubic interpolation can overshoot otherwise valid FOV key values.
        if (values[6] <= 0.0 || values[6] >= 180.0) { return std::nullopt; }
        constexpr auto radians = std::numbers::pi / 180.0;
        const auto axis = [&](float x, float y, float z) {
            osci::Point point(x, y, z);
            point.rotate(static_cast<float>(values[3] * radians), static_cast<float>(values[4] * radians), static_cast<float>(values[5] * radians));
            return Vec3 {point.x, point.y, point.z};
        };
        Frame result;
        result.position = {values[0], values[1], values[2]};
        result.right = axis(1, 0, 0);
        result.up = axis(0, 1, 0);
        result.forward = axis(0, 0, -1);
        result.focalLength = 1.0 / std::tan(values[6] * radians * 0.5);
        if (!parent.empty()) {
            const auto origin = parent.apply(result.position, time);
            const auto ahead = parent.apply(result.position + result.forward, time);
            const auto above = parent.apply(result.position + result.up, time);
            const auto forward = (ahead - origin).normalized(minimumAxis);
            if (!forward.has_value()) { return std::nullopt; }
            const auto right = forward->cross(above - origin).normalized(minimumAxis);
            if (!right.has_value()) { return std::nullopt; }
            result.position = origin;
            result.forward = *forward;
            result.right = *right;
            result.up = right->cross(*forward);
        }
        if (!target.empty()) {
            const auto aim = (target.apply(target.anchor(time), time) - result.position).normalized(minimumAxis);
            if (aim.has_value()) {
                // Level to world up unless looking straight up or down.
                auto right = aim->cross({0, 1, 0}).normalized(minimumAxis);
                if (!right.has_value()) { right = aim->cross(result.up).normalized(minimumAxis); }
                if (right.has_value()) {
                    const auto up = right->cross(*aim);
                    const auto roll = values[5] * radians;
                    const auto c = std::cos(roll), s = std::sin(roll);
                    result.forward = *aim;
                    result.right = *right * c + up * s;
                    result.up = up * c - *right * s;
                }
            }
        }
        return result;
    }
    // Shorter axes are degenerate: a parent scaled flat, or a target at the camera.
    static constexpr double minimumAxis = 1.0e-9;
    // Points nearer than this to a camera are not drawn.
    static constexpr double nearPlane = hidden::nearPlane;
    // The point on the camera's image plane; none at or behind the near plane.
    static std::optional<osci::Point> project(const Frame& frame, osci::Point point) {
        const auto offset = Vec3 {point.x, point.y, point.z} - frame.position;
        const auto depth = offset.dot(frame.forward);
        if (!std::isfinite(depth) || depth <= nearPlane) { return std::nullopt; }
        point.x = static_cast<float>(offset.dot(frame.right) * frame.focalLength / depth);
        point.y = static_cast<float>(offset.dot(frame.up) * frame.focalLength / depth);
        point.z = 1.0f;
        return point;
    }
};

// signal: drawn to the Scope or exported, baking its hidden lines alongside.
// culling: prepared just as the signal is, by what bakes those hidden lines
// (a bake's workers, the Scene's preview), so it starts no bake of its own.
// editorGeometry: the editor's shapes, rebuilt during drags.
enum class CompositionPurpose { signal, culling, editorGeometry };

// Whole beam cycles per video frame, at least 45 Hz so scopes do not flicker.
inline double beamCycleRate(double frameRate) {
    if (!std::isfinite(frameRate) || frameRate <= 0) { return 60; }
    return frameRate * std::max(1.0, std::ceil(45.0 / frameRate - 1.0e-9));
}

// The Scope's picture in project time: each beam property's curve with its
// routes, clamped to the visualiser's range (keys and modulation can
// overshoot). Allocation-free, for the audio and offline render threads.
struct PreparedBeam {
    PreparedBeam() {
        for (std::size_t index = 0; index < curves.size(); ++index) { curves[index] = Curve(beamPropertySpecs[index].defaultValue); }
    }
    std::array<Curve, beamPropertySpecs.size()> curves;
    float value(std::size_t property, double time) const {
        return static_cast<float>(beamPropertySpecs[property].clamp(curves[property].evaluate(time)));
    }
    std::array<float, beamPropertySpecs.size()> at(double time) const {
        std::array<float, beamPropertySpecs.size()> values {};
        for (std::size_t index = 0; index < curves.size(); ++index) { values[index] = value(index, time); }
        return values;
    }
};

// What the Scope shows of a square canvas: a little past -1 to 1.
inline hidden::Reach squareScope() { return hidden::Reach::ofCanvas(1, 1, VisualiserGeometry::signalGain); }

struct PreparedComposition {
    explicit PreparedComposition(const Project& project, double destinationSampleRate = 48000, const std::atomic<bool>* cancel = nullptr, CompositionPurpose purpose = CompositionPurpose::signal, hidden::Reach reach = squareScope()) : duration(project.duration), sampleRate(destinationSampleRate), beamRate(beamCycleRate(project.frameRate)), scope(project.scope), soundtrack(project, cancel), hiddenLineReach(reach), effects(prepareEffects(project.effects)) {
        if (!soundtrack.preparationError.empty()) { preparationError = soundtrack.preparationError; return; }
        // Editor geometry is rebuilt during drags; loudness modulation only
        // matters to the signal, so the envelope is built there alone.
        PreparedDrivers drivers([this, cancel, purpose]() -> std::shared_ptr<const SoundtrackEnvelope> {
            return purpose != CompositionPurpose::editorGeometry ? loudnessEnvelope(cancel) : nullptr;
        });
        const ClipTiming mainClock(0, project.duration);
        drivers.driveEffects(effects, project.effects, project, mainClock);
        const auto chainFor = [&](Id id, bool groupsOnly) {
            PreparedChain chain;
            Id groupId = 0;
            const auto load = [&](PreparedChain::Link& link, const PropertyMap& properties, Id owner) {
                for (std::size_t index = 0; index < objectPropertySpecs.size(); ++index) {
                    link.curves[index] = curveOrDefault(properties, objectPropertySpecs[index]);
                    drivers.drive(link.curves[index], project, mainClock, owner, objectPropertySpecs[index].id);
                }
            };
            for (const auto& track : project.tracks) {
                if (groupsOnly || track.kind != TrackKind::visual) { continue; }
                for (const auto& clip : track.clips) {
                    if (clip.id != id) { continue; }
                    PreparedChain::Link link;
                    load(link, clip.properties, clip.id);
                    link.spatial = prepareSpatial(clip.spatialPath, clip.quaternionRotation, link.curves);
                    const auto timing = clip.timing(project.tempo());
                    link.clock = timing;
                    link.clip = true;
                    chain.links.push_back(std::move(link));
                    groupId = track.group;
                }
            }
            if (chain.links.empty()) {
                if (findGroup(project, id) == nullptr) { return chain; }
                groupId = id;
            }
            while (groupId != 0 && chain.links.size() <= maximumGroupDepth) {
                const auto* group = findGroup(project, groupId);
                if (group == nullptr) { break; }
                PreparedChain::Link link;
                load(link, group->properties, group->id);
                link.spatial = prepareSpatial(group->spatialPath, group->quaternionRotation, link.curves);
                chain.links.push_back(std::move(link));
                groupId = group->parent;
            }
            return chain;
        };
        for (const auto& camera : project.cameras) {
            PreparedCamera item { camera.id, {} };
            if (camera.target != 0) { item.target = chainFor(camera.target, false); }
            if (camera.parent != 0) { item.parent = chainFor(camera.parent, true); }
            for (std::size_t index = 0; index < cameraPropertySpecs.size(); ++index) {
                item.curves[index] = curveOrDefault(camera.properties, cameraPropertySpecs[index]);
                drivers.drive(item.curves[index], project, mainClock, camera.id, cameraPropertySpecs[index].id);
            }
            cameras.push_back(std::move(item));
        }
        for (std::size_t index = 0; index < beamPropertySpecs.size(); ++index) {
            beam.curves[index] = curveOrDefault(project.beam.properties, beamPropertySpecs[index]);
            drivers.driveBeam(beam.curves[index], project, mainClock, beamPropertySpecs[index].id);
        }
        for (const auto& cut : project.cameraCuts) {
            const auto camera = std::find_if(cameras.begin(), cameras.end(), [&](const auto& item) { return item.id == cut.camera; });
            if (cut.valid() && camera != cameras.end()) {
                cameraCuts.push_back({ cut.start, cut.end(), static_cast<std::size_t>(camera - cameras.begin()) });
            }
        }
        std::sort(cameraCuts.begin(), cameraCuts.end(), [](const auto& left, const auto& right) { return left.start < right.start; });
        const auto definitions = definitionsById(project);
        const auto prepareStage = [&](const CompositionStage& stage, bool root, const Composition& scope) {
            const auto& clip = *stage.clip;
            const auto& timing = stage.clipClock;
            PreparedClipStage item;
            item.id = clip.id; item.start = timing.start; item.end = timing.end();
            item.clock = timing;
            item.scopeClock = stage.scopeClock;
            for (std::size_t i = 0; i < objectPropertySpecs.size(); ++i) {
                item.curves[i] = curveOrDefault(clip.properties, objectPropertySpecs[i]);
                drivers.drive(item.curves[i], scope, stage.scopeClock, clip.id, objectPropertySpecs[i].id);
            }
            item.spatial = prepareSpatial(clip.spatialPath, clip.quaternionRotation, item.curves);
            item.effects = prepareEffects(clip.effects);
            drivers.driveEffects(item.effects, clip.effects, scope, stage.scopeClock);
            item.trackEffects = prepareEffects(stage.track->effects);
            drivers.driveEffects(item.trackEffects, stage.track->effects, scope, stage.scopeClock);
            if (!root) {
                item.compositionEffects = prepareEffects(*stage.effects);
                drivers.driveEffects(item.compositionEffects, *stage.effects, scope, stage.scopeClock);
            }
            auto groupId = stage.track->group;
            while (groupId != 0 && item.groups.size() < maximumGroupDepth) {
                const auto group = std::find_if(stage.groups->begin(), stage.groups->end(), [groupId](const auto& value) { return value.id == groupId; });
                if (group == stage.groups->end()) { break; }
                item.groups.emplace_back(*group);
                auto& prepared = item.groups.back();
                for (std::size_t i = 0; i < objectPropertySpecs.size(); ++i) { drivers.drive(prepared.curves[i], scope, stage.scopeClock, group->id, objectPropertySpecs[i].id); }
                drivers.driveEffects(prepared.effects, group->effects, scope, stage.scopeClock);
                groupId = group->parent;
            }
            return item;
        };
        // Stage k is authored in the root for k == 0, otherwise in the
        // definition instanced by stage k - 1.
        const auto scopeOf = [&](const auto& stages, std::size_t index) -> const Composition& {
            if (index == 0) { return project; }
            return *definitions.at(stages[index - 1].clip->composition);
        };
        const auto expanded = expandComposition(project, [&](const auto&, const auto& stages) {
            if (preparationError.isNotEmpty()) { return; }
            const auto& leaf = stages.back();
            const auto& clip = *leaf.clip;
            if (leaf.track->kind != TrackKind::visual) { return; }
            const auto asset = findAsset(project.assets, clip.asset);
            if (asset == nullptr || (asset->source == nullptr && asset->liveIdentity == nullptr)) { return; }
            PreparedClip item;
            static_cast<PreparedClipStage&>(item) = prepareStage(leaf, stages.size() == 1, scopeOf(stages, stages.size() - 1));
            item.rootTrack = stages.front().track->id;
            item.liveIdentity = asset->liveIdentity;
            item.source = clip.luaBake != nullptr && clip.luaBake->source != nullptr ? clip.luaBake->source : asset->source;
            // A generator is drawn from this clip's own values of its
            // parameters, keyed, modulated and linked like any property.
            const auto* generator = generatorOf(*asset);
            if (generator != nullptr) {
                const auto& scope = scopeOf(stages, stages.size() - 1);
                std::vector<Curve> parameters;
                parameters.reserve(generator->parameters.size());
                for (const auto& spec : generator->parameters) {
                    parameters.push_back(curveOrDefault(clip.properties, spec));
                    drivers.drive(parameters.back(), scope, leaf.scopeClock, clip.id, spec.id);
                }
                std::vector<double> values(parameters.size());
                item.source = generator::clipSource(*generator, leaf.clipClock.localTime(leaf.clipClock.end()), project.frameRate, [&](double seconds) {
                    for (std::size_t index = 0; index < parameters.size(); ++index) { values[index] = parameters[index].evaluate(seconds); }
                    return values;
                });
            }
            // An L-system is drawn at this clip's depth, keyed, modulated and
            // linked like any property; a depth between two grows the next.
            if (isFractal(asset->extension)) {
                const auto& scope = scopeOf(stages, stages.size() - 1);
                const auto stored = clip.properties.find("fractal.depth");
                auto depth = stored != clip.properties.end() ? stored->second : Curve(asset->fractalDepth);
                drivers.drive(depth, scope, leaf.scopeClock, clip.id, "fractal.depth");
                const auto drawn = fractal::clipSource(asset->data.toString(), leaf.clipClock.localTime(leaf.clipClock.end()), project.frameRate, [&](double seconds) { return depth.evaluate(seconds); });
                if (drawn != nullptr) { item.source = drawn; }
            }
            for (std::size_t index = stages.size() - 1; index > 0; --index) {
                item.ancestors.push_back(prepareStage(stages[index - 1], index == 1, scopeOf(stages, index - 1)));
            }
            // A light lights what the Scope draws and is never drawn there
            // itself; the Scene shows its picture.
            if (generator != nullptr && generator->category == GeneratorCategory::light && purpose != CompositionPurpose::editorGeometry) {
                PreparedLight light;
                light.kind = generator->kind;
                light.specs = generator->parameters;
                const auto& scope = scopeOf(stages, stages.size() - 1);
                for (const auto& spec : generator->parameters) {
                    light.parameters.push_back(curveOrDefault(clip.properties, spec));
                    drivers.drive(light.parameters.back(), scope, leaf.scopeClock, clip.id, spec.id);
                }
                item.source.reset();
                light.clip = std::move(item);
                lights.push_back(std::move(light));
                return;
            }
            item.light = generator != nullptr && generator->category == GeneratorCategory::light;
            clips.push_back(std::move(item));
        }, cancel);
        if (!expanded) { preparationError = expanded.error; }
        if (preparationError.isNotEmpty()) {
            clips.clear();
            lights.clear();
        }
        // Frames in which something solid hides lines, that lights light, in
        // which something reflects, a surface draws itself other than by its
        // edges, or a camera hatches, are baked off the render thread.
        const auto hides = std::any_of(clips.begin(), clips.end(), [](const auto& clip) { return !clip.light && clip.mayBeSolid(); });
        const auto reflective = std::any_of(clips.begin(), clips.end(), [](const auto& clip) { return !clip.light && clip.mayReflect(); });
        const auto restyled = std::any_of(clips.begin(), clips.end(), [](const auto& clip) { return !clip.light && clip.mayRestyle(); });
        const auto hatches = std::any_of(cameras.begin(), cameras.end(), [](const auto& camera) { return camera.mayHatch(); });
        if (purpose == CompositionPurpose::signal && preparationError.isEmpty() && (hides || !lights.empty() || reflective || restyled || hatches)) {
            hiddenLines = std::make_shared<hidden::Bake>(std::make_shared<const Project>(project), project.frameRate, project.duration, reach, sampleRate);
            hiddenLines->scope = project.scope;
            hiddenLines->hides = hides;
            hiddenLines->lights = !lights.empty();
            hiddenLines->reflects = reflective;
            hiddenLines->restyles = restyled;
            hiddenLines->hatches = hatches;
        }
    }

    // Geometry probe: the beam allocation at one phase of a static multiplexed
    // cycle. Output signals use BeamRenderer; tests use this direct time/phase
    // lookup to check the allocation.
    osci::Point sample(double time, double phase, double phaseSpan = 0, double timeSpan = 0, const LiveSourceFrames* liveFrames = nullptr) const {
        if (!std::isfinite(time) || !std::isfinite(phase) || !std::isfinite(phaseSpan) || phaseSpan < 0) { return {0, 0, 0, 0, 0, 0}; }
        const auto current = selectBeam(time, phase);
        if (current.clip == nullptr) { return {0, 0, 0, 0, 0, 0}; }
        return projectPoint(current.clip->sample(time, current.phase, phaseSpan * current.phaseScale, timeSpan, liveFrames), time);
    }

    struct BeamSelection {
        const PreparedClip* clip = nullptr;
        double phase = 0, phaseScale = 0;
    };

    BeamSelection selectBeam(double time, double phase) const {
        double allocation = 0.0;
        for (const auto& clip : clips) {
            if (!clip.light && clip.active(time) && clip.visible(time)) { allocation += clip.beam(time); }
        }
        if (!(allocation > 0.0)) { return {}; }
        auto cursor = phase * allocation;
        for (const auto& clip : clips) {
            if (clip.light || !clip.active(time) || !clip.visible(time)) { continue; }
            const auto share = clip.beam(time);
            if (cursor < share) { return {&clip, cursor / share, allocation / share}; }
            cursor -= share;
        }
        return {};
    }

    const PreparedCamera* activeCamera(double time) const {
        if (cameras.empty()) {
            return nullptr;
        }
        const auto next = std::upper_bound(cameraCuts.begin(), cameraCuts.end(), time,
            [](double value, const auto& cut) { return value < cut.start; });
        if (next != cameraCuts.begin()) {
            const auto& cut = *(next - 1);
            if (time >= cut.start && time < cut.end) {
                return &cameras[cut.cameraIndex];
            }
        }
        return &cameras.front();
    }

    bool hasCompositionEffects() const { return !effects.empty(); }
    const std::vector<PreparedEffect>& compositionEffects() const { return effects; }
    osci::Point applyCompositionEffects(osci::Point point, double time) const {
        return applyEffects(effects, point, time);
    }

    // Output space is the unit square. Geometry behind the camera has no
    // position; geometry far outside the frame is clamped and blanked so no
    // exported or live signal carries huge off-screen excursions.
    static constexpr float outputLimit = 8.0f;
    std::optional<osci::Point> projectVisible(osci::Point point, double time) const {
        if (!std::isfinite(time) || !point.hasFinitePosition()) {
            return std::nullopt;
        }
        return projectWorld(applyCompositionEffects(point, time), time);
    }
    // A point already in the scene, after every effect, as only the camera
    // places it: a reflection, found where the scene is seen in a mirror.
    std::optional<osci::Point> projectWorld(osci::Point point, double time) const {
        if (!std::isfinite(time) || !point.hasFinitePosition()) {
            return std::nullopt;
        }
        const auto& view = views.at(time, [&] {
            const auto* camera = activeCamera(time);
            return View {camera != nullptr, camera != nullptr ? camera->frame(time) : std::optional(PreparedCamera::fixedFrame())};
        });
        if (view.camera) {
            const auto projected = view.frame.has_value() ? PreparedCamera::project(*view.frame, point) : std::nullopt;
            if (!projected.has_value()) { return std::nullopt; }
            point = *projected;
        } else {
            // Empty camera collections retain the original fixed output framing.
            const auto depth = 4.0f - point.z;
            if (depth <= 0.05f) { return std::nullopt; }
            point.x *= 4.0f / depth;
            point.y *= 4.0f / depth;
            point.z = 1.0f;
        }
        if (!std::isfinite(point.x) || !std::isfinite(point.y)) { return std::nullopt; }
        if (std::abs(point.x) > outputLimit || std::abs(point.y) > outputLimit) {
            point.x = std::clamp(point.x, -outputLimit, outputLimit);
            point.y = std::clamp(point.y, -outputLimit, outputLimit);
            point.r = point.g = point.b = 0;
        }
        return point;
    }
    osci::Point projectPoint(osci::Point point, double time) const {
        return projectVisible(point, time).value_or(osci::Point(0, 0, 0, 0, 0, 0));
    }
    // A point of a clip in the scene, after everything that moves it but the
    // camera: what lights light.
    std::optional<Vec3> worldPoint(const PreparedClip& clip, osci::Point point, double time) const {
        point = applyCompositionEffects(clip.processPoint(point, time), time);
        if (!point.hasFinitePosition()) { return std::nullopt; }
        return Vec3 {point.x, point.y, point.z};
    }
    // A light while its clip plays: its anchor is where it is, and it shines
    // down its own -Z.
    std::optional<PreparedLight::State> lightAt(const PreparedLight& light, double time) const {
        if (!std::isfinite(time) || !light.clip.active(time)) { return std::nullopt; }
        const auto local = light.clip.localTime(time);
        const auto anchor = [&](std::size_t axis) { return static_cast<float>(light.clip.curves[anchorIndex + axis].evaluate(local)); };
        const osci::Point origin(anchor(0), anchor(1), anchor(2), -1, -1, -1);
        const auto at = applyCompositionEffects(light.clip.processPoint(origin, time), time);
        const auto ahead = applyCompositionEffects(light.clip.processPoint(osci::Point(anchor(0), anchor(1), anchor(2) - 1), time), time);
        if (!at.isFinite() || !ahead.hasFinitePosition()) { return std::nullopt; }
        PreparedLight::State state;
        state.kind = light.kind;
        state.position = {at.x, at.y, at.z};
        state.direction = (Vec3 {ahead.x, ahead.y, ahead.z} - state.position).normalized(1.0e-12).value_or(Vec3 {0, 0, -1});
        state.colour = {std::max(0.0f, at.r), std::max(0.0f, at.g), std::max(0.0f, at.b)};
        state.intensity = light.parameter("light.intensity", time);
        state.range = light.parameter("light.range", time);
        state.cone = light.parameter("light.cone", time);
        state.softness = light.parameter("light.softness", time) / 100;
        state.shadows = light.parameter("light.shadows", time) / 100;
        return state;
    }
    // Whether any light shines at a time: without one, everything is as bright as it is.
    bool lit(double time) const {
        return std::any_of(lights.begin(), lights.end(), [&](const PreparedLight& light) { return light.clip.active(time); });
    }
    // Whether anything shown at a time draws itself other than by its edges.
    bool restyles(double time) const {
        return std::any_of(clips.begin(), clips.end(), [&](const PreparedClip& clip) { return !clip.light && clip.active(time) && clip.visible(time) && !clip.lines(time).plain(); });
    }
    // Whether anything shown at a time is solid.
    bool solids(double time) const {
        return std::any_of(clips.begin(), clips.end(), [&](const PreparedClip& clip) { return !clip.light && clip.active(time) && clip.visible(time) && clip.solid(time) > 0; });
    }
    // Whether anything shown at a time reflects.
    bool reflects(double time) const {
        return std::any_of(clips.begin(), clips.end(), [&](const PreparedClip& clip) { return !clip.light && clip.active(time) && clip.visible(time) && clip.reflects(time) > 0; });
    }
    // The eye the frame is seen from, in the scene: the active camera's, or
    // without cameras the fixed framing's, four units in front of the
    // middle looking back at it.
    struct Viewpoint {
        Vec3 position, right, up, forward;
        double focalLength = 1;
    };
    std::optional<Viewpoint> viewpoint(double time) const {
        const auto* camera = activeCamera(time);
        if (camera == nullptr) {
            const auto fixed = PreparedCamera::fixedFrame();
            return Viewpoint {fixed.position, fixed.right, fixed.up, fixed.forward, fixed.focalLength};
        }
        const auto frame = camera->frame(time);
        if (!frame.has_value()) { return std::nullopt; }
        return Viewpoint {frame->position, frame->right, frame->up, frame->forward, frame->focalLength};
    }
    // A world point in the active camera's eye space (x right, y up, z along
    // its view), or the fixed framing's without cameras, as projectVisible
    // sees it before dividing by depth.
    std::optional<Vec3> eyePoint(osci::Point point, double time) const {
        if (!std::isfinite(time) || !point.hasFinitePosition()) { return std::nullopt; }
        point = applyCompositionEffects(point, time);
        const auto& view = views.at(time, [&] {
            const auto* camera = activeCamera(time);
            return View {camera != nullptr, camera != nullptr ? camera->frame(time) : std::optional(PreparedCamera::fixedFrame())};
        });
        if (!view.frame.has_value()) { return std::nullopt; }
        const auto offset = Vec3 {point.x, point.y, point.z} - view.frame->position;
        return Vec3 {offset.dot(view.frame->right), offset.dot(view.frame->up), offset.dot(view.frame->forward)};
    }

private:
    // The camera shown at a time and its frame (the fixed framing's without
    // cameras).
    struct View {
        bool camera = false;
        std::optional<PreparedCamera::Frame> frame;
    };
    TimeCache<View> views;

public:

    double duration;
    double sampleRate = 48000;
    double beamRate = 60;
    ScopeProfile scope;
    std::uint64_t publicationRevision = 0;
    juce::String preparationError;
    PreparedSoundtrack soundtrack;
    std::vector<PreparedClip> clips;
    std::vector<PreparedLight> lights;
    std::vector<PreparedCamera> cameras;
    // What the Scope shows (wider than the frame on a wide canvas), which
    // hidden lines are removed across, and the frames with them removed, when
    // something solid hides them.
    hidden::Reach hiddenLineReach;
    std::shared_ptr<hidden::Bake> hiddenLines;
    PreparedBeam beam;

private:
    // Loudness at 240 Hz: rectified peak per bin, then a fast-attack /
    // slow-release follower, normalised to the loudest moment of the piece.
    std::shared_ptr<const SoundtrackEnvelope> loudnessEnvelope(const std::atomic<bool>* cancel) {
        if (loudness != nullptr || loudnessBuilt) { return loudness; }
        loudnessBuilt = true;
        auto envelope = std::make_shared<SoundtrackEnvelope>();
        const auto bins = static_cast<std::size_t>(std::ceil(std::max(0.0, duration) * envelope->rate)) + 2;
        envelope->values.assign(bins, 0.0f);
        constexpr int probes = 24;
        float follower = 0, loudest = 0;
        for (std::size_t bin = 0; bin < bins; ++bin) {
            if ((bin & 1023) == 0 && cancelled(cancel)) { return nullptr; }
            float peak = 0;
            for (int probe = 0; probe < probes; ++probe) {
                const auto sample = soundtrack.sample((static_cast<double>(bin) + probe / static_cast<double>(probes)) / envelope->rate);
                peak = std::max({peak, std::abs(static_cast<float>(sample.left)), std::abs(static_cast<float>(sample.right))});
            }
            follower = peak > follower ? follower + (peak - follower) * .6f : follower * .93f;
            envelope->values[bin] = follower;
            loudest = std::max(loudest, follower);
        }
        if (loudest > 0) { for (auto& value : envelope->values) { value /= loudest; } }
        loudness = std::move(envelope);
        return loudness;
    }
    std::shared_ptr<const SoundtrackEnvelope> loudness;
    bool loudnessBuilt = false;
    struct PreparedCameraCut {
        double start, end;
        std::size_t cameraIndex;
    };
    std::vector<PreparedCameraCut> cameraCuts;
    std::vector<PreparedEffect> effects;
};
}

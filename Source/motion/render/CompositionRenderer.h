#pragma once

#include "../model/Cancellation.h"
#include "../model/Document.h"
#include "../model/CompositionGraph.h"
#include "PreparedEffects.h"
#include "PreparedSoundtrack.h"
#include "PreparedMidiPerformance.h"
#include "PreparedDrivers.h"
#include "../model/SpatialMotion.h"
#include "../model/Vec3.h"
#include "../live/LiveSourceFrames.h"
#include "../model/PropertySchema.h"
#include "../model/LuaClipBake.h"
#include "SampleClock.h"
#include "TimeCache.h"
#include <array>
#include <numbers>
#include <optional>

namespace motion {
inline std::shared_ptr<const PreparedSpatial> prepareSpatial(bool path, bool quaternion, const std::array<Curve, 13>& curves) {
    auto spatial = std::make_shared<PreparedSpatial>();
    if (path) { spatial->path = PreparedPath::prepare(curves[0], curves[1], curves[2]); }
    if (quaternion) { spatial->orientation = PreparedOrientation::prepare(curves[3], curves[4], curves[5]); }
    if (spatial->path == nullptr && spatial->orientation == nullptr) { return nullptr; }
    return spatial;
}

// A transform's curves evaluated at one time: scale, then rotation (Euler X,
// Y, Z, or a keyed orientation after modulation offsets), then translation
// (along a spatial path when there is one), and colour gains. Evaluated once,
// it applies to any number of points.
struct TransformPose {
    using Matrix = std::array<std::array<float, 3>, 3>;
    std::array<float, 3> scale {1, 1, 1}, translation {}, colour {1, 1, 1};
    Matrix rotation {{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};

    static TransformPose at(const std::array<Curve, 13>& curves, double time, const PreparedSpatial* spatial) {
        TransformPose pose;
        constexpr auto radians = std::numbers::pi / 180.0;
        for (std::size_t axis = 0; axis < 3; ++axis) { pose.scale[axis] = static_cast<float>(curves[6 + axis].evaluate(time)); }
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
        for (std::size_t channel = 0; channel < 3; ++channel) { pose.colour[channel] = static_cast<float>(curves[9 + channel].evaluate(time)); }
        return pose;
    }

    // The source's colour (white when it has none) times the colour gains.
    osci::Point colourOf(osci::Point point) const {
        const auto uncoloured = point.r < 0;
        point.r = std::clamp((uncoloured ? 1.0f : point.r) * colour[0], 0.0f, 1.0f);
        point.g = std::clamp((uncoloured ? 1.0f : point.g) * colour[1], 0.0f, 1.0f);
        point.b = std::clamp((uncoloured ? 1.0f : point.b) * colour[2], 0.0f, 1.0f);
        return point;
    }
    osci::Point apply(osci::Point point, bool applyColour) const {
        const auto x = point.x * scale[0], y = point.y * scale[1], z = point.z * scale[2];
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

inline osci::Point applyTransform(osci::Point point, const std::array<Curve, 13>& curves, double time, bool applyColour = true, const PreparedSpatial* spatial = nullptr) {
    return TransformPose::at(curves, time, spatial).apply(point, applyColour);
}

struct PreparedGroup {
    Id id;
    std::array<Curve, 13> curves;
    std::vector<PreparedEffect> effects;
    std::shared_ptr<const PreparedSpatial> spatial;

    explicit PreparedGroup(const Group& group) : id(group.id), effects(prepareEffects(group.effects)) {
        for (std::size_t index = 0; index < objectPropertySpecs.size(); ++index) {
            curves[index] = curveOrDefault(group.properties, objectPropertySpecs[index]);
        }
        spatial = prepareSpatial(group.spatialPath, group.quaternionRotation, curves);
    }
    double weight(double time) const {
        return clampWeight(curves[12].evaluate(time));
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
    std::array<Curve, 13> curves;
    std::vector<PreparedEffect> effects, trackEffects, compositionEffects;
    std::vector<PreparedGroup> groups;
    std::shared_ptr<const PreparedSpatial> spatial;
    std::optional<ClipTiming> scopeClock;
    TimeCache<TransformPose> poses;

    double scopeTime(double time) const { return scopeClock.has_value() ? scopeClock->localTime(time) : time; }
    // The nearest time inside the clip's own interval.
    double clampToContent(double time) const { return std::clamp(time, start, std::nextafter(end, start)); }
    double localTime(double time) const { return clock.localTime(time); }
    double localWeight(double time) const {
        return clampWeight(curves[12].evaluate(localTime(time)));
    }
    bool accumulateWeightLog(double time, double& logarithm) const {
        const auto value = localWeight(time);
        if (value <= 0) { return false; }
        logarithm += std::log(value);
        for (const auto& group : groups) {
            const auto factor = group.weight(scopeTime(time));
            if (factor <= 0) { return false; }
            logarithm += std::log(factor);
        }
        return true;
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
    std::shared_ptr<const PreparedMidiPerformance> midi;
    std::shared_ptr<const PreparedMidiInstrument> liveInstrument;
    std::vector<PreparedClipStage> ancestors; // inner-to-outer
    Id rootTrack = 0; // the main timeline track this clip plays on

    Id editorId() const { return ancestors.empty() ? id : ancestors.back().id; }
    bool active(double time) const { return time >= start && time < end; }
    double weight(double time) const {
        if (!ancestors.empty()) {
            // Nested group products can overflow before a later fade attenuates
            // them. Accumulate logs and saturate only after all attenuation.
            double logarithm = 0;
            if (!accumulateWeightLog(time, logarithm)) { return 0; }
            for (const auto& ancestor : ancestors) {
                if (!ancestor.accumulateWeightLog(time, logarithm)) { return 0; }
            }
            return std::min(maximumWeight, std::exp(std::min(logarithm, std::log(maximumWeight))));
        }
        auto value = localWeight(time);
        for (const auto& group : groups) { value *= group.weight(scopeTime(time)); }
        // A direct leaf has at most 32 ancestors bounded to maximumWeight each.
        return std::clamp(value, 0.0, maximumWeight);
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

// A transform chain from a clip or group up through its parent groups, used
// to place camera targets and parents. Groups run on the scope (project)
// clock; a clip link maps project time to its content time.
struct PreparedChain {
    struct Link {
        std::array<Curve, 13> curves;
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
};

struct PreparedCamera {
    Id id;
    std::array<Curve, 7> curves;
    PreparedChain target, parent;

    struct Frame {
        Vec3 position, right, up, forward;
        double focalLength = 1;
    };
    // The camera's world position and orthonormal basis at `time`: authored
    // position/rotation (X, then Y, then Z), carried by the parent group,
    // then aimed at the target's origin with Z rotation kept as roll.
    std::optional<Frame> frame(double time) const {
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
            const auto aim = (target.apply({0, 0, 0}, time) - result.position).normalized(minimumAxis);
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
    static constexpr double nearPlane = 0.05;
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

enum class CompositionPurpose { signal, editorGeometry };

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

struct PreparedComposition {
    explicit PreparedComposition(const Project& project, double destinationSampleRate = 48000, const std::atomic<bool>* cancel = nullptr, CompositionPurpose purpose = CompositionPurpose::signal) : duration(project.duration), sampleRate(destinationSampleRate), beamRate(beamCycleRate(project.frameRate)), scope(project.scope), soundtrack(project, cancel), effects(prepareEffects(project.effects)) {
        if (!soundtrack.preparationError.empty()) { preparationError = soundtrack.preparationError; return; }
        // Editor geometry is rebuilt during drags; loudness modulation only
        // matters to the signal, so the envelope is built there alone.
        PreparedDrivers drivers([this, cancel, purpose]() -> std::shared_ptr<const SoundtrackEnvelope> {
            return purpose == CompositionPurpose::signal ? loudnessEnvelope(cancel) : nullptr;
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
        std::map<std::array<double, 5>, std::shared_ptr<const PreparedMidiInstrument>> instruments;
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
            for (std::size_t index = stages.size() - 1; index > 0; --index) {
                item.ancestors.push_back(prepareStage(stages[index - 1], index == 1, scopeOf(stages, index - 1)));
            }
            if (purpose == CompositionPurpose::signal) {
                // One instrument serves live input and the clip's own notes.
                const auto key = clip.instrument.key();
                auto found = instruments.find(key);
                if (found == instruments.end()) {
                    const auto instrument = PreparedMidiInstrument::prepare(clip.instrument, sampleRate, cancel);
                    if (!instrument) { preparationError = "Could not prepare the MIDI instrument."; return; }
                    found = instruments.emplace(key, std::make_shared<const PreparedMidiInstrument>(*instrument)).first;
                }
                item.liveInstrument = found->second;
            }
            if (clip.midi != nullptr && purpose == CompositionPurpose::signal) {
                const auto performance = PreparedMidiPerformance::prepare(*clip.midi, clip, item.liveInstrument, leaf.tempo, cancel, &leaf.clipClock);
                if (!performance) {
                    preparationError = "MIDI clip \"" + juce::String(clip.name) + "\": " + juce::String(performance.error);
                    return;
                }
                item.midi = performance.performance;
                hasMidi = true;
            }
            clips.push_back(std::move(item));
        }, cancel);
        if (!expanded) { preparationError = expanded.error; }
        if (preparationError.isNotEmpty()) { clips.clear(); return; }
        for (const auto& track : project.tracks) {
            if (track.midiInput == 0 || track.kind != TrackKind::visual) { continue; }
            const auto clip = std::find_if(clips.begin(), clips.end(), [&](const auto& item) { return item.rootTrack == track.id && item.liveInstrument != nullptr; });
            if (clip == clips.end()) { continue; }
            liveTracks.push_back({track.id, track.midiInput == Track::anyMidiChannel ? 0 : track.midiInput, clip->liveInstrument});
        }
    }

    // Geometry probe: the beam allocation at one phase of a static multiplexed
    // cycle. Output signals use BeamRenderer; tests use this direct time/phase
    // lookup to check the allocation.
    osci::Point sample(double time, double phase, double phaseSpan = 0, double timeSpan = 0, double oscillatorTime = -1, const LiveSourceFrames* liveFrames = nullptr) const {
        if (oscillatorTime < 0) { oscillatorTime = time; }
        if (!std::isfinite(time) || !std::isfinite(phase) || !std::isfinite(phaseSpan) || phaseSpan < 0) { return {0, 0, 0, 0, 0, 0}; }
        const auto current = selectBeam(time, phase, oscillatorTime);
        if (current.clip == nullptr) { return {0, 0, 0, 0, 0, 0}; }
        const auto localSpan = current.note != 0 ? current.notePhaseSpan : phaseSpan * current.phaseScale;
        return projectPoint(current.clip->sample(time, current.phase, localSpan, timeSpan, liveFrames), time);
    }

    struct BeamSelection {
        const PreparedClip* clip = nullptr;
        double phase = 0, phaseScale = 0;
        Id note = 0;
        double notePhaseSpan = 0;
    };

    BeamSelection selectBeam(double time, double phase, double oscillatorTime = -1) const {
        if (oscillatorTime < 0) { oscillatorTime = time; }
        double allocation = 0.0;
        for (const auto& clip : clips) {
            if (clip.active(time)) { allocation += std::max(1.0, clip.weight(time)); }
        }
        if (allocation <= 0.0) { return {}; }
        auto cursor = phase * allocation;
        for (const auto& clip : clips) {
            if (!clip.active(time)) { continue; }
            const auto weight = clip.weight(time);
            if (cursor < weight) {
                if (clip.midi != nullptr) {
                    const auto note = clip.midi->select(time, cursor / weight, oscillatorTime);
                    return note.note == 0 ? BeamSelection{} : BeamSelection{&clip, note.phase, 0, note.note, note.phaseSpan};
                }
                return {&clip, cursor / weight, allocation / weight};
            }
            cursor -= weight;
        }
        // Unused allocation stays dark rather than normalizing away a fade.
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
        point = applyCompositionEffects(point, time);
        const auto& view = views.at(time, [&] {
            const auto* camera = activeCamera(time);
            return View {camera != nullptr, camera != nullptr ? camera->frame(time) : std::nullopt};
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

private:
    // The camera shown at a time and its frame (none without cameras).
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
    bool hasMidi = false;
    std::uint64_t publicationRevision = 0;
    juce::String preparationError;
    PreparedSoundtrack soundtrack;
    std::vector<PreparedClip> clips;
    std::vector<PreparedCamera> cameras;
    PreparedBeam beam;
    // Tracks armed for live MIDI (at most LiveMidiInputs::maximumRoutes are used).
    struct LiveTrack { Id track; int channel; std::shared_ptr<const PreparedMidiInstrument> instrument; };
    std::vector<LiveTrack> liveTracks;

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

#pragma once

#include "../model/Document.h"
#include "../model/CompositionGraph.h"
#include "PreparedEffects.h"
#include "PreparedSoundtrack.h"
#include "PreparedMidiPerformance.h"
#include "PreparedDrivers.h"
#include "../model/SpatialMotion.h"
#include "SampleClock.h"
#include <array>
#include <numbers>
#include <optional>

namespace motion {
inline osci::Point applySourceColour(osci::Point point, const std::array<Curve, 13>& curves, double time, double bpm) {
    const auto sourceRed = point.r < 0 ? 1.0f : point.r;
    const auto sourceGreen = point.r < 0 ? 1.0f : point.g;
    const auto sourceBlue = point.r < 0 ? 1.0f : point.b;
    point.r = std::clamp(static_cast<float>(sourceRed * curves[9].evaluate(time, bpm)), 0.0f, 1.0f);
    point.g = std::clamp(static_cast<float>(sourceGreen * curves[10].evaluate(time, bpm)), 0.0f, 1.0f);
    point.b = std::clamp(static_cast<float>(sourceBlue * curves[11].evaluate(time, bpm)), 0.0f, 1.0f);
    return point;
}

inline std::shared_ptr<const PreparedSpatial> prepareSpatial(bool path, bool quaternion, const std::array<Curve, 13>& curves) {
    auto spatial = std::make_shared<PreparedSpatial>();
    if (path) { spatial->path = PreparedPath::prepare(curves[0], curves[1], curves[2]); }
    if (quaternion) { spatial->orientation = PreparedOrientation::prepare(curves[3], curves[4], curves[5]); }
    if (spatial->path == nullptr && spatial->orientation == nullptr) { return nullptr; }
    return spatial;
}

inline osci::Point applyTransform(osci::Point point, const std::array<Curve, 13>& curves, double time, double bpm = 120, bool applyColour = true, const PreparedSpatial* spatial = nullptr) {
    point.scale(curves[6].evaluate(time, bpm), curves[7].evaluate(time, bpm), curves[8].evaluate(time, bpm));
    constexpr auto radians = std::numbers::pi / 180.0;
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
            extra[axis] = curve.evaluateWith(keyed, time, bpm) - keyed;
        }
        if (extra[0] != 0 || extra[1] != 0 || extra[2] != 0) { point.rotate(extra[0] * radians, extra[1] * radians, extra[2] * radians); }
        const auto rotated = spatial->orientation->at(time).rotate(point.x, point.y, point.z);
        point.x = static_cast<float>(rotated[0]);
        point.y = static_cast<float>(rotated[1]);
        point.z = static_cast<float>(rotated[2]);
    } else {
        point.rotate(curves[3].evaluate(time, bpm) * radians, curves[4].evaluate(time, bpm) * radians, curves[5].evaluate(time, bpm) * radians);
    }
    if (spatial != nullptr && spatial->path != nullptr) {
        const auto position = spatial->path->at(time);
        const auto axis = [&](std::size_t index) { return curves[index].linked() ? curves[index].evaluate(time, bpm) : curves[index].evaluateWith(position[index], time, bpm); };
        point.translate(axis(0), axis(1), axis(2));
    } else {
        point.translate(curves[0].evaluate(time, bpm), curves[1].evaluate(time, bpm), curves[2].evaluate(time, bpm));
    }
    if (applyColour) { point = applySourceColour(point, curves, time, bpm); }
    if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z)
        || !std::isfinite(point.r) || !std::isfinite(point.g) || !std::isfinite(point.b)) {
        return { 0, 0, 0, 0, 0, 0 };
    }
    return point;
}

struct PreparedGroup {
    Id id;
    std::array<Curve, 13> curves;
    std::vector<PreparedEffect> effects;
    std::shared_ptr<const PreparedSpatial> spatial;

    explicit PreparedGroup(const Group& group) : id(group.id), effects(prepareEffects(group.effects)) {
        for (std::size_t index = 0; index < propertyNames.size(); ++index) {
            const auto found = group.properties.find(propertyNames[index]);
            curves[index] = found == group.properties.end() ? Curve(index >= 6 ? 1 : 0) : found->second;
        }
        spatial = prepareSpatial(group.spatialPath, group.quaternionRotation, curves);
    }
    double weight(double time, double bpm = 120) const {
        const auto value = curves[12].evaluate(time, bpm);
        return std::isfinite(value) ? std::clamp(value, 0.0, 1000000.0) : 0.0;
    }
    osci::Point apply(osci::Point point, double time, double bpm = 120) const {
        return applyEffects(effects, applyTransform(point, curves, time, bpm, true, spatial.get()), time, bpm);
    }
};

// One authored clip's transform/effect scope. Runtime stages carry no document
// pointers; each clock maps main seconds directly to its authored coordinate.
struct PreparedClipStage {
    Id id = 0;
    double start = 0, end = 0, offset = 0, rate = 1;
    std::array<Curve, 13> curves;
    std::vector<PreparedEffect> effects, trackEffects, compositionEffects;
    std::vector<PreparedGroup> groups;
    std::shared_ptr<const PreparedSpatial> spatial;
    double bpm = 120, contentBpm = 120;
    std::optional<ClipTiming> scopeClock;

    double scopeTime(double time) const { return scopeClock.has_value() ? scopeClock->localTime(time) : time; }
    double localTime(double time) const { return offset + (time - start) * rate; }
    double localWeight(double time) const {
        const auto value = curves[12].evaluate(localTime(time), contentBpm);
        return std::isfinite(value) ? std::clamp(value, 0.0, 1000000.0) : 0;
    }
    bool accumulateWeightLog(double time, double& logarithm) const {
        const auto value = localWeight(time);
        if (value <= 0) { return false; }
        logarithm += std::log(value);
        for (const auto& group : groups) {
            const auto factor = group.weight(scopeTime(time), bpm);
            if (factor <= 0) { return false; }
            logarithm += std::log(factor);
        }
        return true;
    }
    osci::Point processStage(osci::Point point, double time) const {
        const auto local = localTime(time);
        point = applySourceColour(point, curves, local, contentBpm);
        point = applyEffects(effects, point, local, contentBpm);
        point = applyTransform(point, curves, local, contentBpm, false, spatial.get());
        point = applyEffects(trackEffects, point, scopeTime(time), bpm);
        for (const auto& group : groups) { point = group.apply(point, scopeTime(time), bpm); }
        return applyEffects(compositionEffects, point, scopeTime(time), bpm);
    }
};

struct PreparedClip : PreparedClipStage {
    std::shared_ptr<const LiveSourceIdentity> liveIdentity;
    std::shared_ptr<const PreparedSource> source;
    std::shared_ptr<const PreparedMidiPerformance> midi;
    std::shared_ptr<const PreparedMidiInstrument> liveInstrument;
    std::vector<PreparedClipStage> ancestors; // inner-to-outer

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
            return std::min(1000000.0, std::exp(std::min(logarithm, std::log(1000000.0))));
        }
        auto value = localWeight(time);
        for (const auto& group : groups) { value *= group.weight(scopeTime(time), bpm); }
        // A direct leaf has at most 32 ancestors bounded to 1e6 each.
        return std::clamp(value, 0.0, 1000000.0);
    }
    const PreparedSource* resolveSource(const LiveSourceFrames* liveFrames = nullptr) const noexcept {
        if (liveIdentity != nullptr) { return liveFrames != nullptr ? liveFrames->resolve(liveIdentity.get()) : nullptr; }
        return source.get();
    }
    osci::Point sample(double time, double phase, double phaseSpan = 0, double timeSpan = 0, const LiveSourceFrames* liveFrames = nullptr) const {
        const auto* resolved = resolveSource(liveFrames);
        if (resolved == nullptr) { return {0, 0, 0, 0, 0, 0}; }
        return processPoint(resolved->sample(localTime(time), phase, phaseSpan, std::abs(rate) * timeSpan), time);
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
        double start = 0, offset = 0, rate = 1, bpm = 120;
        bool clip = false;
    };
    std::vector<Link> links; // inner to outer
    bool empty() const { return links.empty(); }
    std::array<double, 3> apply(std::array<double, 3> position, double time) const {
        osci::Point point(static_cast<float>(position[0]), static_cast<float>(position[1]), static_cast<float>(position[2]));
        for (const auto& link : links) {
            const auto local = link.clip ? link.offset + (time - link.start) * link.rate : time;
            point = applyTransform(point, link.curves, local, link.bpm, false, link.spatial.get());
        }
        return {point.x, point.y, point.z};
    }
};

struct PreparedCamera {
    Id id;
    std::array<Curve, 7> curves;
    double bpm = 120;
    PreparedChain target, parent;

    struct Frame {
        std::array<double, 3> position, right, up, forward;
        double focalLength = 1;
    };
    // The camera's world position and orthonormal basis at `time`: authored
    // position/rotation (X, then Y, then Z), carried by the parent group,
    // then aimed at the target's origin with Z rotation kept as roll.
    std::optional<Frame> frame(double time) const {
        std::array<double, 7> values;
        for (std::size_t index = 0; index < values.size(); ++index) {
            values[index] = curves[index].evaluate(time, bpm);
            if (!std::isfinite(values[index])) { return std::nullopt; }
        }
        // Cubic interpolation can overshoot otherwise valid FOV key values.
        if (values[6] <= 0.0 || values[6] >= 180.0) { return std::nullopt; }
        constexpr auto radians = std::numbers::pi / 180.0;
        const auto axis = [&](float x, float y, float z) {
            osci::Point point(x, y, z);
            point.rotate(static_cast<float>(values[3] * radians), static_cast<float>(values[4] * radians), static_cast<float>(values[5] * radians));
            return std::array<double, 3> {point.x, point.y, point.z};
        };
        Frame result;
        result.position = {values[0], values[1], values[2]};
        result.right = axis(1, 0, 0);
        result.up = axis(0, 1, 0);
        result.forward = axis(0, 0, -1);
        result.focalLength = 1.0 / std::tan(values[6] * radians * 0.5);
        if (!parent.empty()) {
            const auto origin = parent.apply(result.position, time);
            const auto ahead = parent.apply(add(result.position, result.forward), time);
            const auto above = parent.apply(add(result.position, result.up), time);
            const auto forward = normalise(subtract(ahead, origin));
            if (!forward.has_value()) { return std::nullopt; }
            const auto right = normalise(cross(*forward, subtract(above, origin)));
            if (!right.has_value()) { return std::nullopt; }
            result.position = origin;
            result.forward = *forward;
            result.right = *right;
            result.up = cross(*right, *forward);
        }
        if (!target.empty()) {
            const auto aim = normalise(subtract(target.apply({0, 0, 0}, time), result.position));
            if (aim.has_value()) {
                // Level to world up unless looking straight up or down.
                auto right = normalise(cross(*aim, {0, 1, 0}));
                if (!right.has_value()) { right = normalise(cross(*aim, result.up)); }
                if (right.has_value()) {
                    const auto up = cross(*right, *aim);
                    const auto roll = values[5] * radians;
                    const auto c = std::cos(roll), s = std::sin(roll);
                    result.forward = *aim;
                    result.right = add(scale(*right, c), scale(up, s));
                    result.up = add(scale(up, c), scale(*right, -s));
                }
            }
        }
        return result;
    }
    bool visible(osci::Point point, double time) const {
        const auto current = frame(time);
        return current.has_value() && depthOf(*current, point) > nearPlane;
    }
    static constexpr float nearPlane = 0.05f;
    osci::Point projectPoint(osci::Point point, double time) const {
        const auto current = frame(time);
        return current.has_value() ? project(*current, point) : osci::Point(0, 0, 0, 0, 0, 0);
    }
    static double depthOf(const Frame& frame, const osci::Point& point) {
        return dot(subtract({point.x, point.y, point.z}, frame.position), frame.forward);
    }
    static osci::Point project(const Frame& frame, osci::Point point) {
        const auto offset = subtract({point.x, point.y, point.z}, frame.position);
        const auto depth = dot(offset, frame.forward);
        if (!std::isfinite(depth) || depth <= 0.05) { return { 0, 0, 0, 0, 0, 0 }; }
        point.x = static_cast<float>(dot(offset, frame.right) * frame.focalLength / depth);
        point.y = static_cast<float>(dot(offset, frame.up) * frame.focalLength / depth);
        point.z = 1.0f;
        if (!std::isfinite(point.x) || !std::isfinite(point.y)) { return { 0, 0, 0, 0, 0, 0 }; }
        return point;
    }

private:
    using Vector = std::array<double, 3>;
    static Vector add(const Vector& a, const Vector& b) { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }
    static Vector subtract(const Vector& a, const Vector& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
    static Vector scale(const Vector& a, double factor) { return {a[0] * factor, a[1] * factor, a[2] * factor}; }
    static double dot(const Vector& a, const Vector& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
    static Vector cross(const Vector& a, const Vector& b) { return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]}; }
    static std::optional<Vector> normalise(const Vector& a) {
        const auto length = std::sqrt(dot(a, a));
        if (!std::isfinite(length) || length < 1.0e-9) { return std::nullopt; }
        return scale(a, 1.0 / length);
    }
};

enum class CompositionPurpose { signal, editorGeometry };

// Whole beam cycles per video frame, at least 45 Hz so scopes do not flicker.
inline double beamCycleRate(double frameRate) {
    if (!std::isfinite(frameRate) || frameRate <= 0) { return 60; }
    return frameRate * std::max(1.0, std::ceil(45.0 / frameRate - 1.0e-9));
}

struct PreparedComposition {
    explicit PreparedComposition(const Project& project, double destinationSampleRate = 48000, const std::atomic<bool>* cancel = nullptr, CompositionPurpose purpose = CompositionPurpose::signal) : duration(project.duration), bpm(project.bpm), sampleRate(destinationSampleRate), beamRate(beamCycleRate(project.frameRate)), scope(project.scope), soundtrack(project, cancel), effects(prepareEffects(project.effects)) {
        if (!soundtrack.preparationError.empty()) { preparationError = soundtrack.preparationError; return; }
        PreparedDrivers drivers([this, cancel, purpose]() -> std::shared_ptr<const SoundtrackEnvelope> {
            return purpose == CompositionPurpose::signal ? loudnessEnvelope(cancel) : nullptr;
        });
        const ClipTiming mainClock(0, project.duration);
        drivers.driveEffects(effects, project.effects, project, mainClock);
        const auto chainFor = [&](Id id, bool groupsOnly) {
            PreparedChain chain;
            Id groupId = 0;
            const auto load = [&](PreparedChain::Link& link, const std::map<std::string, Curve>& properties, Id owner) {
                for (std::size_t index = 0; index < propertyNames.size(); ++index) {
                    const auto found = properties.find(propertyNames[index]);
                    link.curves[index] = found != properties.end() ? found->second : Curve(index >= 6 ? 1.0 : 0.0);
                    drivers.drive(link.curves[index], project, mainClock, owner, propertyNames[index]);
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
                    link.start = timing.start; link.offset = timing.offset; link.rate = timing.rate;
                    link.bpm = clip.curveBpm(project.tempo());
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
                link.bpm = project.bpm;
                chain.links.push_back(std::move(link));
                groupId = group->parent;
            }
            return chain;
        };
        for (const auto& camera : project.cameras) {
            PreparedCamera item { camera.id, {} };
            if (camera.target != 0) { item.target = chainFor(camera.target, false); }
            if (camera.parent != 0) { item.parent = chainFor(camera.parent, true); }
            const Camera defaults;
            for (std::size_t index = 0; index < cameraPropertyNames.size(); ++index) {
                const auto found = camera.properties.find(cameraPropertyNames[index]);
                item.curves[index] = found != camera.properties.end() ? found->second : defaults.properties.at(cameraPropertyNames[index]);
                drivers.drive(item.curves[index], project, mainClock, camera.id, cameraPropertyNames[index]);
            }
            item.bpm = project.bpm;
            cameras.push_back(std::move(item));
        }
        for (const auto& cut : project.cameraCuts) {
            const auto camera = std::find_if(cameras.begin(), cameras.end(), [&](const auto& item) { return item.id == cut.camera; });
            if (cut.valid() && camera != cameras.end()) {
                cameraCuts.push_back({ cut.start, cut.end(), static_cast<std::size_t>(camera - cameras.begin()) });
            }
        }
        std::sort(cameraCuts.begin(), cameraCuts.end(), [](const auto& left, const auto& right) { return left.start < right.start; });
        std::map<Id, const Composition*> definitions;
        for (const auto& definition : project.definitions) { definitions.emplace(definition->id, definition.get()); }
        const auto prepareStage = [&](const CompositionStage& stage, bool root, const Composition& scope) {
            const auto& clip = *stage.clip;
            const auto& timing = stage.clipClock;
            PreparedClipStage item;
            item.id = clip.id; item.start = timing.start; item.end = timing.end(); item.offset = timing.offset; item.rate = timing.rate;
            item.scopeClock = stage.scopeClock;
            item.bpm = stage.bpm;
            item.contentBpm = clip.curveBpm(stage.tempo);
            for (std::size_t i = 0; i < propertyNames.size(); ++i) {
                const auto curve = clip.properties.find(propertyNames[i]);
                item.curves[i] = curve != clip.properties.end() ? curve->second : Curve(i >= 6 ? 1.0 : 0.0);
                drivers.drive(item.curves[i], scope, stage.scopeClock, clip.id, propertyNames[i]);
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
                for (std::size_t i = 0; i < propertyNames.size(); ++i) { drivers.drive(prepared.curves[i], scope, stage.scopeClock, group->id, propertyNames[i]); }
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
        std::map<std::array<double, 4>, std::shared_ptr<const PreparedMidiInstrument>> instruments;
        const auto expanded = expandComposition(project, [&](const auto&, const auto& stages) {
            if (preparationError.isNotEmpty()) { return; }
            const auto& leaf = stages.back();
            const auto& clip = *leaf.clip;
            if (leaf.track->kind != TrackKind::visual) { return; }
            const auto asset = std::find_if(project.assets.begin(), project.assets.end(), [&](const auto& item) { return item != nullptr && item->id == clip.asset; });
            if (asset == project.assets.end() || ((*asset)->source == nullptr && (*asset)->drawing == nullptr && (*asset)->liveIdentity == nullptr)) { return; }
            PreparedClip item;
            static_cast<PreparedClipStage&>(item) = prepareStage(leaf, stages.size() == 1, scopeOf(stages, stages.size() - 1));
            item.liveIdentity = (*asset)->liveIdentity;
            item.source = (*asset)->source;
            if (item.source == nullptr) {
                item.source = std::make_shared<PreparedSource>(std::vector<std::shared_ptr<const osci::PreparedDrawing>> {(*asset)->drawing}, 30.0);
            }
            for (std::size_t index = stages.size() - 1; index > 0; --index) {
                item.ancestors.push_back(prepareStage(stages[index - 1], index == 1, scopeOf(stages, index - 1)));
            }
            if (purpose == CompositionPurpose::signal) {
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
                const auto performance = PreparedMidiPerformance::prepare(*clip.midi, clip, leaf.tempo, sampleRate, cancel, &leaf.clipClock);
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
        // Editor geometry is rebuilt during drags; loudness modulation only
        // matters to the signal, so the envelope is built there alone.
        if (purpose == CompositionPurpose::signal) { attachSoundtrack(cancel); }
    }

    // Geometry probe: the beam allocation at one phase of a static multiplexed
    // cycle. Output signals use BeamRenderer; this stays for editor and model
    // checks that need a direct time/phase lookup.
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
        return applyEffects(effects, point, time, bpm);
    }

    // Output space is the unit square. Geometry behind the camera has no
    // position; geometry far outside the frame is clamped and blanked so no
    // exported or live signal carries huge off-screen excursions.
    static constexpr float outputLimit = 8.0f;
    std::optional<osci::Point> projectVisible(osci::Point point, double time) const {
        if (!std::isfinite(time) || !std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z)) {
            return std::nullopt;
        }
        point = applyCompositionEffects(point, time);
        const auto* camera = activeCamera(time);
        if (camera != nullptr) {
            const auto frame = camera->frame(time);
            if (!frame.has_value() || PreparedCamera::depthOf(*frame, point) <= PreparedCamera::nearPlane) { return std::nullopt; }
            point = PreparedCamera::project(*frame, point);
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

    double duration;
    double bpm = 120;
    double sampleRate = 48000;
    double beamRate = 60;
    ScopeProfile scope;
    bool hasMidi = false;
    std::uint64_t publicationRevision = 0;
    juce::String preparationError;
    PreparedSoundtrack soundtrack;
    std::vector<PreparedClip> clips;
    std::vector<PreparedCamera> cameras;

private:
    // Loudness at 240 Hz: rectified peak per bin, then a fast-attack /
    // slow-release follower, normalised to the loudest moment of the piece.
    void attachSoundtrack(const std::atomic<bool>* cancel) {
        const auto uses = [](const Curve& curve) { return curve.modulation.enabled && curve.modulation.waveform == ModulationWaveform::soundtrack; };
        bool needed = false;
        const auto scanEffects = [&](const std::vector<PreparedEffect>& list) { for (const auto& effect : list) { for (const auto& curve : effect.curves) { needed = needed || uses(curve); } } };
        const auto scanStage = [&](const PreparedClipStage& stage) {
            for (const auto& curve : stage.curves) { needed = needed || uses(curve); }
            scanEffects(stage.effects); scanEffects(stage.trackEffects); scanEffects(stage.compositionEffects);
            for (const auto& group : stage.groups) { for (const auto& curve : group.curves) { needed = needed || uses(curve); } scanEffects(group.effects); }
        };
        for (const auto& clip : clips) { scanStage(clip); for (const auto& ancestor : clip.ancestors) { scanStage(ancestor); } }
        for (const auto& camera : cameras) { for (const auto& curve : camera.curves) { needed = needed || uses(curve); } }
        scanEffects(effects);
        if (!needed) { return; }
        const auto shared = loudnessEnvelope(cancel);
        if (shared == nullptr) { return; }
        const auto attach = [&](Curve& curve, double start, double offset, double rate) {
            if (uses(curve)) { curve.modulation.soundtrack = std::make_shared<const SoundtrackClock>(SoundtrackClock{shared, start, offset, rate}); }
        };
        const auto attachEffects = [&](std::vector<PreparedEffect>& list, double start, double offset, double rate) {
            for (auto& effect : list) { for (auto& curve : effect.curves) { attach(curve, start, offset, rate); } }
        };
        const auto attachStage = [&](PreparedClipStage& stage) {
            for (auto& curve : stage.curves) { attach(curve, stage.start, stage.offset, stage.rate); }
            attachEffects(stage.effects, stage.start, stage.offset, stage.rate);
            // Track, group and composition scopes run on the scope clock.
            const auto scopeStart = stage.scopeClock.has_value() ? stage.scopeClock->start : 0.0;
            const auto scopeOffset = stage.scopeClock.has_value() ? stage.scopeClock->offset : 0.0;
            const auto scopeRate = stage.scopeClock.has_value() ? stage.scopeClock->rate : 1.0;
            attachEffects(stage.trackEffects, scopeStart, scopeOffset, scopeRate);
            attachEffects(stage.compositionEffects, scopeStart, scopeOffset, scopeRate);
            for (auto& group : stage.groups) {
                for (auto& curve : group.curves) { attach(curve, scopeStart, scopeOffset, scopeRate); }
                attachEffects(group.effects, scopeStart, scopeOffset, scopeRate);
            }
        };
        for (auto& clip : clips) { attachStage(clip); for (auto& ancestor : clip.ancestors) { attachStage(ancestor); } }
        for (auto& camera : cameras) { for (auto& curve : camera.curves) { attach(curve, 0, 0, 1); } }
        attachEffects(effects, 0, 0, 1);
    }
    // Computed once, on first use, for curves and modulators that follow it.
    std::shared_ptr<const SoundtrackEnvelope> loudnessEnvelope(const std::atomic<bool>* cancel) {
        if (loudness != nullptr || loudnessBuilt) { return loudness; }
        loudnessBuilt = true;
        auto envelope = std::make_shared<SoundtrackEnvelope>();
        const auto bins = static_cast<std::size_t>(std::ceil(std::max(0.0, duration) * envelope->rate)) + 2;
        envelope->values.assign(bins, 0.0f);
        constexpr int probes = 24;
        float follower = 0, loudest = 0;
        for (std::size_t bin = 0; bin < bins; ++bin) {
            if (cancel != nullptr && (bin & 1023) == 0 && cancel->load()) { return nullptr; }
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

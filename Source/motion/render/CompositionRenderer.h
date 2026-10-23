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
#include "PreparedClip.h"
#include "PreparedCamera.h"
#include <array>
#include <span>
#include <numbers>
#include <optional>

namespace motion {
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

// signal: drawn to the Scope or exported, baking its hidden lines alongside.
// culling: prepared just as the signal is, by what bakes those hidden lines
// (a bake's workers, the Scene's preview), so it starts no bake of its own.
// editorGeometry: the editor's shapes, rebuilt during drags.
enum class CompositionPurpose { signal, culling, editorGeometry };

// The beam's base frequency: whole cycles per video frame, `cycles` of them,
// or (0) as few as reach 45 Hz so scopes do not flicker.
inline double automaticBeamCycles(double frameRate) {
    if (!std::isfinite(frameRate) || frameRate <= 0) { return 1; }
    return std::max(1.0, std::ceil(45.0 / frameRate - 1.0e-9));
}
inline double beamCycleRate(double frameRate, int cycles = 0) {
    if (!std::isfinite(frameRate) || frameRate <= 0) { return 60; }
    return frameRate * (cycles > 0 ? static_cast<double>(cycles) : automaticBeamCycles(frameRate));
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
    explicit PreparedComposition(const Project& project, double destinationSampleRate = 48000, const std::atomic<bool>* cancel = nullptr, CompositionPurpose purpose = CompositionPurpose::signal, hidden::Reach reach = squareScope()) : duration(project.duration), sampleRate(destinationSampleRate), beamRate(beamCycleRate(project.frameRate, project.beamCycles)), scope(project.scope), soundtrack(project, cancel), hiddenLineReach(reach), effects(prepareEffects(project.effects)) {
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
            for (const auto& track : project.tracks) {
                if (groupsOnly || track.kind != TrackKind::visual) { continue; }
                for (const auto& clip : track.clips) {
                    if (clip.id != id) { continue; }
                    PreparedChain::Link link;
                    drivers.driveAll(link.curves, clip.properties, objectPropertySpecs, project, mainClock, clip.id);
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
                drivers.driveAll(link.curves, group->properties, objectPropertySpecs, project, mainClock, group->id);
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
            drivers.driveAll(item.curves, camera.properties, cameraPropertySpecs, project, mainClock, camera.id);
            cameras.push_back(std::move(item));
        }
        for (std::size_t index = 0; index < beamPropertySpecs.size(); ++index) {
            beam.curves[index] = curveOrDefault(project.beam.properties, beamPropertySpecs[index]);
            drivers.driveBeam(beam.curves[index], project, beamPropertySpecs[index].id);
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
            drivers.driveAll(item.curves, clip.properties, objectPropertySpecs, scope, stage.scopeClock, clip.id);
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
            std::vector<Curve> parameters;
            if (generator != nullptr) {
                parameters.resize(generator->parameters.size());
                drivers.driveAll(parameters, clip.properties, generator->parameters, scopeOf(stages, stages.size() - 1), leaf.scopeClock, clip.id);
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
                light.parameters = std::move(parameters);
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
            // The beam's rate only sizes hatching: without it, any rate bakes the same.
            hiddenLines->settings = {.hides = hides, .lights = !lights.empty(), .reflects = reflective, .restyles = restyled, .hatches = hatches, .beamRate = hatches ? beamRate : 0.0, .scope = project.scope};
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
        if (!cameras.empty()) {
            const auto& frame = frameAt(time);
            const auto projected = frame.has_value() ? PreparedCamera::project(*frame, point) : std::nullopt;
            if (!projected.has_value()) { return std::nullopt; }
            point = *projected;
        } else {
            // Empty camera collections retain the original fixed output
            // framing, in the float arithmetic it has always had.
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
    using Viewpoint = PreparedCamera::Frame;
    std::optional<Viewpoint> viewpoint(double time) const { return frameAt(time); }
    // A world point in the active camera's eye space (x right, y up, z along
    // its view), or the fixed framing's without cameras, as projectVisible
    // sees it before dividing by depth.
    std::optional<Vec3> eyePoint(osci::Point point, double time) const {
        if (!std::isfinite(time) || !point.hasFinitePosition()) { return std::nullopt; }
        point = applyCompositionEffects(point, time);
        const auto& frame = frameAt(time);
        if (!frame.has_value()) { return std::nullopt; }
        const auto offset = Vec3 {point.x, point.y, point.z} - frame->position;
        return Vec3 {offset.dot(frame->right), offset.dot(frame->up), offset.dot(frame->forward)};
    }

private:
    // The frame of the camera shown at a time, or the fixed framing's
    // without cameras.
    const std::optional<PreparedCamera::Frame>& frameAt(double time) const {
        return views.at(time, [&] {
            const auto* camera = activeCamera(time);
            return camera != nullptr ? camera->frame(time) : std::optional(PreparedCamera::fixedFrame());
        });
    }
    TimeCache<std::optional<PreparedCamera::Frame>> views;

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

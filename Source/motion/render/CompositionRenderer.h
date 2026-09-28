#pragma once

#include "../model/Document.h"
#include "../model/CompositionGraph.h"
#include "PreparedEffects.h"
#include "PreparedSoundtrack.h"
#include "PreparedMidiPerformance.h"
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

inline osci::Point applyTransform(osci::Point point, const std::array<Curve, 13>& curves, double time, double bpm = 120, bool applyColour = true) {
    point.scale(curves[6].evaluate(time, bpm), curves[7].evaluate(time, bpm), curves[8].evaluate(time, bpm));
    constexpr auto radians = std::numbers::pi / 180.0;
    point.rotate(curves[3].evaluate(time, bpm) * radians, curves[4].evaluate(time, bpm) * radians, curves[5].evaluate(time, bpm) * radians);
    point.translate(curves[0].evaluate(time, bpm), curves[1].evaluate(time, bpm), curves[2].evaluate(time, bpm));
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

    explicit PreparedGroup(const Group& group) : id(group.id), effects(prepareEffects(group.effects)) {
        for (std::size_t index = 0; index < propertyNames.size(); ++index) {
            const auto found = group.properties.find(propertyNames[index]);
            curves[index] = found == group.properties.end() ? Curve(index >= 6 ? 1 : 0) : found->second;
        }
    }
    double weight(double time, double bpm = 120) const {
        const auto value = curves[12].evaluate(time, bpm);
        return std::isfinite(value) ? std::clamp(value, 0.0, 1000000.0) : 0.0;
    }
    osci::Point apply(osci::Point point, double time, double bpm = 120) const {
        return applyEffects(effects, applyTransform(point, curves, time, bpm), time, bpm);
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
        point = applyTransform(point, curves, local, contentBpm, false);
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

struct PreparedCamera {
    Id id;
    std::array<Curve, 7> curves;
    double bpm = 120;

    bool visible(osci::Point point, double time) const {
        std::array<double, 6> values;
        for (std::size_t index = 0; index < values.size(); ++index) {
            values[index] = curves[index].evaluate(time, bpm);
            if (!std::isfinite(values[index])) { return false; }
        }
        point.translate(-values[0], -values[1], -values[2]);
        constexpr auto radians = std::numbers::pi / 180.0;
        point.rotate(0.0f, 0.0f, -values[5] * radians);
        point.rotate(0.0f, -values[4] * radians, 0.0f);
        point.rotate(-values[3] * radians, 0.0f, 0.0f);
        return std::isfinite(point.z) && -point.z > nearPlane;
    }
    static constexpr float nearPlane = 0.05f;
    osci::Point projectPoint(osci::Point point, double time) const {
        std::array<double, 7> values;
        for (std::size_t index = 0; index < values.size(); ++index) {
            values[index] = curves[index].evaluate(time, bpm);
            if (!std::isfinite(values[index])) {
                return { 0, 0, 0, 0, 0, 0 };
            }
        }
        const auto fieldOfView = values[6];
        // Cubic interpolation can overshoot otherwise valid FOV key values.
        if (fieldOfView <= 0.0 || fieldOfView >= 180.0) {
            return { 0, 0, 0, 0, 0, 0 };
        }
        point.translate(-values[0], -values[1], -values[2]);
        constexpr auto radians = std::numbers::pi / 180.0;
        // Point::rotate applies X, then Y, then Z. Invert in reverse order;
        // negating all three angles in a single rotate call is not the inverse.
        point.rotate(0.0f, 0.0f, -values[5] * radians);
        point.rotate(0.0f, -values[4] * radians, 0.0f);
        point.rotate(-values[3] * radians, 0.0f, 0.0f);
        const auto depth = -point.z;
        if (!std::isfinite(depth) || depth <= 0.05f) {
            return { 0, 0, 0, 0, 0, 0 };
        }
        const auto focalLength = 1.0 / std::tan(fieldOfView * radians * 0.5);
        point.x *= focalLength / depth;
        point.y *= focalLength / depth;
        point.z = 1.0f;
        if (!std::isfinite(point.x) || !std::isfinite(point.y)) {
            return { 0, 0, 0, 0, 0, 0 };
        }
        return point;
    }
};

enum class CompositionPurpose { signal, editorGeometry };

// Whole beam cycles per video frame, at least 45 Hz so scopes do not flicker.
inline double beamCycleRate(double frameRate) {
    if (!std::isfinite(frameRate) || frameRate <= 0) { return 60; }
    return frameRate * std::max(1.0, std::ceil(45.0 / frameRate - 1.0e-9));
}

struct PreparedComposition {
    explicit PreparedComposition(const Project& project, double destinationSampleRate = 48000, const std::atomic<bool>* cancel = nullptr, CompositionPurpose purpose = CompositionPurpose::signal) : duration(project.duration), bpm(project.bpm), sampleRate(destinationSampleRate), beamRate(beamCycleRate(project.frameRate)), soundtrack(project, cancel), effects(prepareEffects(project.effects)) {
        if (!soundtrack.preparationError.empty()) { preparationError = soundtrack.preparationError; return; }
        for (const auto& camera : project.cameras) {
            PreparedCamera item { camera.id, {} };
            const Camera defaults;
            for (std::size_t index = 0; index < cameraPropertyNames.size(); ++index) {
                const auto found = camera.properties.find(cameraPropertyNames[index]);
                item.curves[index] = found != camera.properties.end() ? found->second : defaults.properties.at(cameraPropertyNames[index]);
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
        const auto prepareStage = [&](const CompositionStage& stage, bool root) {
            const auto& clip = *stage.clip;
            const auto& timing = stage.clipClock;
            PreparedClipStage item;
            item.id = clip.id; item.start = timing.start; item.end = timing.end(); item.offset = timing.offset; item.rate = timing.rate;
            item.scopeClock = stage.scopeClock;
            item.bpm = stage.bpm;
            item.contentBpm = clip.curveBpm(stage.bpm);
            for (std::size_t i = 0; i < propertyNames.size(); ++i) {
                const auto curve = clip.properties.find(propertyNames[i]);
                item.curves[i] = curve != clip.properties.end() ? curve->second : Curve(i >= 6 ? 1.0 : 0.0);
            }
            item.effects = prepareEffects(clip.effects);
            item.trackEffects = prepareEffects(stage.track->effects);
            if (!root) { item.compositionEffects = prepareEffects(*stage.effects); }
            auto groupId = stage.track->group;
            while (groupId != 0 && item.groups.size() < maximumGroupDepth) {
                const auto group = std::find_if(stage.groups->begin(), stage.groups->end(), [groupId](const auto& value) { return value.id == groupId; });
                if (group == stage.groups->end()) { break; }
                item.groups.emplace_back(*group);
                groupId = group->parent;
            }
            return item;
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
            static_cast<PreparedClipStage&>(item) = prepareStage(leaf, stages.size() == 1);
            item.liveIdentity = (*asset)->liveIdentity;
            item.source = (*asset)->source;
            if (item.source == nullptr) {
                item.source = std::make_shared<PreparedSource>(std::vector<std::shared_ptr<const osci::PreparedDrawing>> {(*asset)->drawing}, 30.0);
            }
            for (std::size_t index = stages.size() - 1; index > 0; --index) {
                item.ancestors.push_back(prepareStage(stages[index - 1], index == 1));
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
                const auto performance = PreparedMidiPerformance::prepare(*clip.midi, clip, leaf.bpm, sampleRate, cancel, &leaf.clipClock);
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
        attachSoundtrack(cancel);
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
            if (!camera->visible(point, time)) { return std::nullopt; }
            point = camera->projectPoint(point, time);
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
        auto envelope = std::make_shared<SoundtrackEnvelope>();
        const auto bins = static_cast<std::size_t>(std::ceil(std::max(0.0, duration) * envelope->rate)) + 2;
        envelope->values.assign(bins, 0.0f);
        constexpr int probes = 24;
        float follower = 0, loudest = 0;
        for (std::size_t bin = 0; bin < bins; ++bin) {
            if (cancel != nullptr && (bin & 1023) == 0 && cancel->load()) { return; }
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
        const std::shared_ptr<const SoundtrackEnvelope> shared = envelope;
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
    struct PreparedCameraCut {
        double start, end;
        std::size_t cameraIndex;
    };
    std::vector<PreparedCameraCut> cameraCuts;
    std::vector<PreparedEffect> effects;
};
}

#pragma once

#include "../model/Document.h"
#include "../model/CompositionGraph.h"
#include "PreparedEffects.h"
#include "PreparedSoundtrack.h"
#include "PreparedMidiPerformance.h"
#include "SampleClock.h"
#include "TraversalEligibility.h"
#include <array>
#include <numbers>

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

struct PreparedComposition {
    explicit PreparedComposition(const Project& project, double destinationSampleRate = 48000, const std::atomic<bool>* cancel = nullptr, CompositionPurpose purpose = CompositionPurpose::signal) : duration(project.duration), bpm(project.bpm), sampleRate(destinationSampleRate), soundtrack(project, cancel), effects(prepareEffects(project.effects)) {
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
        prepareTraversals(cancel);
    }

    // A signal sample is evaluated with both adjacent sample positions. This
    // stays stateless for seeking/export, while accounting for animated beam
    // allocation instead of assuming each clip's phase speed remains constant.
    struct SamplingNeighbours {
        double previousPhase, nextPhase, previousTime, nextTime, previousOscillatorTime, nextOscillatorTime;
    };

    struct TraversalPosition {
        const PreparedClip* clip = nullptr;
        const osci::PreparedDrawing* drawing = nullptr;
        std::int64_t index = 0, count = 0;
    };

    osci::Point sampleAtClock(double time, std::int64_t index, double clockRate, bool advancing = true, const LiveSourceFrames* liveFrames = nullptr) const {
        if (index < 0 || !std::isfinite(clockRate) || clockRate <= 0) { return {0, 0, 0, 0, 0, 0}; }
        const auto phaseAt = [clockRate](double frame) { return std::fmod(frame * 60.0 / clockRate, 1.0); };
        const auto frame = static_cast<double>(index);
        const auto timeFrame = std::round(time * clockRate);
        const SamplingNeighbours neighbours { phaseAt(frame - 1), phaseAt(frame + 1),
            advancing ? (timeFrame - 1) / clockRate : time, advancing ? (timeFrame + 1) / clockRate : time,
            (frame - 1) / clockRate, (frame + 1) / clockRate };
        const auto traversal = traversalAtClock(time, index, clockRate, advancing);
        auto point = sample(time, phaseAt(frame), 60.0 / clockRate, advancing ? 1.0 / clockRate : 0.0,
            frame / clockRate, &neighbours, &traversal, liveFrames);
        // Ownership guards do not detect a change of sampling strategy on the
        // same clip. Keep both sides dark when crossing an eligibility boundary.
        const auto previous = traversalAtClock(neighbours.previousTime, index - 1, clockRate, advancing);
        const auto next = index == std::numeric_limits<std::int64_t>::max() ? TraversalPosition{}
            : traversalAtClock(neighbours.nextTime, index + 1, clockRate, advancing);
        if ((traversal.drawing == nullptr) != (previous.drawing == nullptr)
            || (traversal.drawing == nullptr) != (next.drawing == nullptr)) {
            point.r = point.g = point.b = 0;
        }
        return point;
    }

    osci::Point sample(double time, double phase, double phaseSpan = 0, double timeSpan = 0, double oscillatorTime = -1, const SamplingNeighbours* clockNeighbours = nullptr, const TraversalPosition* traversal = nullptr, const LiveSourceFrames* liveFrames = nullptr) const {
        if (oscillatorTime < 0) { oscillatorTime = time; }
        if (!std::isfinite(time) || !std::isfinite(phase)) { return {0, 0, 0, 0, 0, 0}; }
        const auto current = selectBeam(time, phase, oscillatorTime);
        if (current.clip == nullptr) { return {0, 0, 0, 0, 0, 0}; }
        bool blank = !std::isfinite(phaseSpan) || phaseSpan < 0 || !std::isfinite(timeSpan) || timeSpan < 0;
        auto localSpan = phaseSpan == 0 ? 0.0 : (current.note != 0 ? current.notePhaseSpan : phaseSpan * current.phaseScale);
        if (!blank && (phaseSpan > 0 || timeSpan > 0)) {
            const auto wrap = [](double value) { return value - std::floor(value); };
            const auto oscillatorStep = 1 / sampleRate;
            // At exact allocation boundaries, subtracting a phase increment can
            // round to the other side of the sample actually emitted by the clock.
            // Live and exported signals compare the actual adjacent clock phases.
            const auto previousPhase = clockNeighbours != nullptr ? clockNeighbours->previousPhase : phase - phaseSpan;
            const auto nextPhase = clockNeighbours != nullptr ? clockNeighbours->nextPhase : phase + phaseSpan;
            const auto previousTime = clockNeighbours != nullptr ? clockNeighbours->previousTime : time - timeSpan;
            const auto nextTime = clockNeighbours != nullptr ? clockNeighbours->nextTime : time + timeSpan;
            const auto previousOscillator = clockNeighbours != nullptr ? clockNeighbours->previousOscillatorTime : oscillatorTime - oscillatorStep;
            const auto nextOscillator = clockNeighbours != nullptr ? clockNeighbours->nextOscillatorTime : oscillatorTime + oscillatorStep;
            const auto previous = selectBeam(previousTime, wrap(previousPhase), previousOscillator);
            const auto next = selectBeam(nextTime, wrap(nextPhase), nextOscillator);
            blank = previous.clip != current.clip || next.clip != current.clip
                || previous.note != current.note || next.note != current.note
                || activeCamera(previousTime) != activeCamera(time)
                || activeCamera(nextTime) != activeCamera(time);
            const auto* resolved = current.clip->resolveSource(liveFrames);
            if (!blank && clockNeighbours != nullptr && resolved != nullptr && resolved->frameCount() > 1) {
                const auto& source = *resolved;
                const auto frame = source.frameIndex(current.clip->localTime(time));
                blank = source.frameIndex(current.clip->localTime(previousTime)) != frame
                    || source.frameIndex(current.clip->localTime(nextTime)) != frame;
            }
            if (!blank) {
                localSpan = std::max({localSpan, std::abs(previous.phase - current.phase), std::abs(next.phase - current.phase)});
            }
        }
        const auto complete = traversal != nullptr && traversal->clip == current.clip && traversal->drawing != nullptr;
        const auto sourcePoint = complete ? current.clip->processPoint(traversal->drawing->sampleTraversal(traversal->index, traversal->count), time)
            : current.clip->sample(time, current.phase, localSpan, timeSpan, liveFrames);
        auto point = projectPoint(sourcePoint, time);
        if (blank) { point.r = point.g = point.b = 0; }
        return point;
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

    osci::Point projectPoint(osci::Point point, double time) const {
        if (!std::isfinite(time) || !std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z)) {
            return { 0, 0, 0, 0, 0, 0 };
        }
        point = applyCompositionEffects(point, time);
        const auto* camera = activeCamera(time);
        if (camera != nullptr) {
            return camera->projectPoint(point, time);
        }
        // Empty camera collections retain the original fixed output framing.
        const auto depth = 4.0f - point.z;
        if (depth <= 0.05f) {
            return { 0, 0, 0, 0, 0, 0 };
        }
        point.x *= 4.0f / depth;
        point.y *= 4.0f / depth;
        point.z = 1.0f;
        if (!std::isfinite(point.x) || !std::isfinite(point.y)) {
            return { 0, 0, 0, 0, 0, 0 };
        }
        return point;
    }

    double duration;
    double bpm = 120;
    double sampleRate = 48000;
    bool hasMidi = false;
    std::uint64_t publicationRevision = 0;
    juce::String preparationError;
    PreparedSoundtrack soundtrack;
    std::vector<PreparedClip> clips;
    std::vector<PreparedCamera> cameras;

private:
    struct TraversalSlot {
        std::size_t clip;
        double start, end;
        const osci::PreparedDrawing* drawing;
    };
    struct ConstantAllocation {
        std::size_t clip;
        double weight;
    };
    struct TraversalInterval {
        double start, end;
        double allocation;
        std::vector<TraversalSlot> slots;
        std::vector<ConstantAllocation> weights;

        std::size_t owner(double phase) const {
            auto cursor = phase * allocation;
            for (const auto& item : weights) {
                if (cursor < item.weight) { return item.clip; }
                cursor -= item.weight;
            }
            return std::numeric_limits<std::size_t>::max();
        }
    };

    void prepareTraversals(const std::atomic<bool>* cancel) {
        // Weight/key boundaries are prepared off-thread. Only structurally
        // constant allocation intervals qualify; matching endpoint values do
        // not prove constancy for modulation or cubic animation.
        std::vector<double> boundaries {0, duration};
        const auto add = [&](double time) {
            if (std::isfinite(time) && time > 0 && time < duration) { boundaries.push_back(time); }
        };
        for (const auto& clip : clips) {
            add(clip.start);
            add(clip.end);
            const auto addStage = [&](const PreparedClipStage& stage) {
                for (const auto& key : stage.curves[12].keyframes()) { add(stage.start + (key.time - stage.offset) / stage.rate); }
                for (const auto& group : stage.groups) {
                    for (const auto& key : group.curves[12].keyframes()) {
                        const auto& clock = stage.scopeClock;
                        add(clock.has_value() ? clock->start + (key.time - clock->offset) / clock->rate : key.time);
                    }
                }
            };
            addStage(clip);
            for (const auto& ancestor : clip.ancestors) { addStage(ancestor); }
        }
        std::sort(boundaries.begin(), boundaries.end());
        boundaries.erase(std::unique(boundaries.begin(), boundaries.end()), boundaries.end());
        for (std::size_t range = 1; range < boundaries.size(); ++range) {
            if (cancel != nullptr && cancel->load()) { return; }
            const auto start = boundaries[range - 1], end = boundaries[range];
            const auto first = std::nextafter(start, end), last = std::nextafter(end, start);
            if (first > last) { continue; }
            const auto middle = first + (last - first) * 0.5;
            double allocation = 0;
            bool constant = true;
            for (const auto& clip : clips) {
                if (!clip.active(middle)) { continue; }
                const auto stageConstant = [&](const PreparedClipStage& stage) {
                    if (!curveConstantOnInterval(stage.curves[12], stage.localTime(first), stage.localTime(last))) { return false; }
                    for (const auto& group : stage.groups) {
                        if (!curveConstantOnInterval(group.curves[12], stage.scopeTime(first), stage.scopeTime(last))) { return false; }
                    }
                    return true;
                };
                constant = constant && stageConstant(clip);
                for (const auto& ancestor : clip.ancestors) { constant = constant && stageConstant(ancestor); }
                allocation += std::max(1.0, clip.weight(middle));
            }
            if (!constant || allocation <= 0 || !std::isfinite(allocation)) { continue; }
            TraversalInterval interval {start, end, allocation, {}, {}};
            double consumed = 0;
            for (std::size_t index = 0; index < clips.size(); ++index) {
                const auto& clip = clips[index];
                if (!clip.active(middle)) { continue; }
                const auto weight = clip.weight(middle);
                interval.weights.push_back({index, weight});
                if (weight > 0 && clip.liveIdentity == nullptr && clip.midi == nullptr && clip.source->frameCount() == 1) {
                    const auto drawing = clip.source->firstFrame();
                    if (drawing != nullptr && drawing->minimumTraversalSamples() > 0) {
                        interval.slots.push_back({index, consumed / allocation, (consumed + weight) / allocation, drawing.get()});
                    }
                }
                consumed += weight;
            }
            if (!interval.slots.empty()) { traversalIntervals.push_back(std::move(interval)); }
        }
    }

    TraversalPosition traversalAtClock(double time, std::int64_t index, double rate, bool advancing) const {
        if (index < 0 || !std::isfinite(time) || traversalIntervals.empty()) { return {}; }
        const auto found = std::upper_bound(traversalIntervals.begin(), traversalIntervals.end(), time,
            [](double value, const auto& item) { return value < item.start; });
        if (found == traversalIntervals.begin()) { return {}; }
        const auto& interval = *(found - 1);
        if (time < interval.start || time >= interval.end) { return {}; }
        const auto cycle = std::floor(static_cast<double>(index) * 60.0 / rate);
        const auto cycleStart = std::ceil(cycle * rate / 60.0);
        const auto cycleEnd = std::ceil((cycle + 1) * rate / 60.0);
        // Limit conversion to exactly represented integer clocks. Real project
        // durations are many orders of magnitude below this bound.
        constexpr double largestExactClock = 9007199254740991.0;
        if (cycleStart < 0 || cycleEnd > largestExactClock || cycleEnd <= cycleStart) { return {}; }
        if (advancing && (time + (cycleStart - static_cast<double>(index) - 1) / rate <= interval.start
            || time + (cycleEnd - static_cast<double>(index)) / rate >= interval.end)) { return {}; }
        const auto phaseAt = [rate](std::int64_t frame) { return std::fmod(static_cast<double>(frame) * 60.0 / rate, 1.0); };
        const auto selected = interval.owner(phaseAt(index));
        for (const auto& slot : interval.slots) {
            const auto* clip = &clips[slot.clip];
            if (selected != slot.clip) { continue; }
            const auto* drawing = slot.drawing;
            // A fractional cycle may receive one fewer sample. Use the lower
            // bound so eligibility cannot alternate merely with clock rounding.
            if (std::floor((slot.end - slot.start) * rate / 60.0) < drawing->minimumTraversalSamples()) { return {}; }
            auto first = static_cast<std::int64_t>(std::ceil((cycle + slot.start) * rate / 60.0));
            auto end = static_cast<std::int64_t>(std::ceil((cycle + slot.end) * rate / 60.0));
            const auto owned = [&](std::int64_t frame) {
                return frame >= cycleStart && frame < cycleEnd
                    && interval.owner(phaseAt(frame)) == slot.clip;
            };
            // Floating ceil can disagree with the actual repeated-subtraction
            // selection predicate at an exact boundary. Correct and verify it.
            for (int correction = 0; correction < 2 && owned(first - 1); ++correction) { --first; }
            for (int correction = 0; correction < 2 && !owned(first) && first < end; ++correction) { ++first; }
            for (int correction = 0; correction < 2 && owned(end); ++correction) { ++end; }
            for (int correction = 0; correction < 2 && !owned(end - 1) && end > first; ++correction) { --end; }
            if (!owned(first) || owned(first - 1) || !owned(end - 1) || owned(end)
                || index < first || index >= end || end - first < drawing->minimumTraversalSamples()) { return {}; }
            return {clip, drawing, index - first, end - first};
        }
        return {};
    }

    std::vector<TraversalInterval> traversalIntervals;
    struct PreparedCameraCut {
        double start, end;
        std::size_t cameraIndex;
    };
    std::vector<PreparedCameraCut> cameraCuts;
    std::vector<PreparedEffect> effects;
};
}

#pragma once

#include "CompositionRenderer.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>

namespace motion {
// Beam cycles are latched plans. Each cycle is a pure function of its index,
// the sample rate, the composition snapshot and the latched timeline time, so
// random access, live playback and offline export produce identical samples.
//
// Within a cycle every visible layer is drawn once. Samples are apportioned by
// weight times projected path length, so equally weighted layers keep equal
// brightness per unit length. Faded layers reserve their full-weight share so a
// fade never brightens the rest of the scene. Layers are ordered to minimise
// dark travel, and every jump dwells dark at both ends so reconstruction
// filters and slow scopes settle before the next stroke lights.
class BeamRenderer {
public:
    static constexpr std::size_t maximumLayers = 256;
    static constexpr std::size_t maximumSegments = maximumLayers * 4 + 4;
    static constexpr int lengthProbes = 24, pointLengthProbes = 128;
    static constexpr double dwellSeconds = 12.0e-6;
    static constexpr double travelSecondsPerUnit = 30.0e-6;
    static constexpr double minimumLayerLength = 0.05;
    static constexpr std::int64_t maximumInterleave = 8;

    enum class Kind : std::uint8_t { draw, midi, move, hold };
    struct Segment {
        Kind kind = Kind::hold;
        std::int64_t first = 0, count = 0;
        const PreparedClip* clip = nullptr;
        const PreparedSource* source = nullptr;
        std::size_t frame = 0;
        bool reversed = false;
        osci::Point from, to;
    };

    // Cycles never straddle a frame of the project's frame rate, so exported
    // video frames integrate a whole number of cycles and do not pulse.
    static double cycleRateFor(double frameRate) { return beamCycleRate(frameRate); }
    static std::int64_t cycleStart(std::int64_t cycle, double rate, double cycleRate) {
        return static_cast<std::int64_t>(std::ceil(static_cast<double>(cycle) * rate / cycleRate - 1.0e-9));
    }
    static std::int64_t cycleOf(std::int64_t index, double rate, double cycleRate) {
        auto cycle = static_cast<std::int64_t>(std::floor(static_cast<double>(index) * cycleRate / rate));
        while (cycle > 0 && cycleStart(cycle, rate, cycleRate) > index) { --cycle; }
        while (cycleStart(cycle + 1, rate, cycleRate) <= index) { ++cycle; }
        return cycle;
    }

    // Allocates once; sampling and planning never allocate.
    BeamRenderer() : segments(std::make_unique<std::array<Segment, maximumSegments>>()), layers(std::make_unique<std::array<Layer, maximumLayers>>()) {}

    void reset() { planned = false; }

    // index is the oscillator sample. time is the timeline time of that sample;
    // when advancing, the cycle latches the timeline time of its first sample.
    // generation must change whenever the composition or live frames change.
    osci::Point sample(const PreparedComposition& composition, double time, std::int64_t index, double rate, bool advancing,
        std::uint64_t generation, const LiveSourceFrames* liveFrames = nullptr) {
        if (index < 0 || !std::isfinite(rate) || rate <= 0 || !std::isfinite(time)) { return {0, 0, 0, 0, 0, 0}; }
        const auto cycleRate = composition.beamRate;
        auto first = planFirst, end = planEnd;
        if (!planned || index < planFirst || index >= planEnd || rate != planRate) {
            const auto cycle = cycleOf(index, rate, cycleRate);
            first = cycleStart(cycle, rate, cycleRate);
            end = cycleStart(cycle + 1, rate, cycleRate);
        }
        // Per-sample transport time carries rounding; half a sample of drift is
        // the same cycle, anything more is a seek.
        const auto latched = advancing ? time - static_cast<double>(index - first) / rate : time;
        if (!planned || first != planFirst || generation != planGeneration || rate != planRate || advancing != planAdvancing
            || std::abs(latched - planTime) > 0.5 / rate) {
            plan(composition, latched, first, end, rate, liveFrames);
            planGeneration = generation;
            planAdvancing = advancing;
        }
        return evaluate(composition, index, time, rate, advancing);
    }

    // Diagnostic view of the current plan, for tests and the editor meter.
    std::span<const Segment> plannedSegments() const { return {segments->data(), segmentCount}; }
    std::size_t plannedLayers() const { return layerCount; }
    std::int64_t plannedInterleave() const { return interleaved; }

private:
    struct Layer {
        const PreparedClip* clip;
        const PreparedSource* source;
        std::size_t frame;
        double weight, length;
        osci::Point start, end;
        bool midi;
        std::int64_t minimum;
    };

    static double distance(const osci::Point& a, const osci::Point& b) {
        const auto dx = static_cast<double>(a.x) - b.x, dy = static_cast<double>(a.y) - b.y;
        const auto value = std::sqrt(dx * dx + dy * dy);
        return std::isfinite(value) ? value : 2.0;
    }
    static osci::Point dark(osci::Point point) { point.r = point.g = point.b = 0; return point; }
    static bool lit(const osci::Point& point) { return point.r > 0 || point.g > 0 || point.b > 0; }

    osci::Point output(const PreparedComposition& composition, const Layer& layer, osci::Point raw) const {
        return composition.projectPoint(layer.clip->processPoint(raw, planTime), planTime);
    }

    void plan(const PreparedComposition& composition, double time, std::int64_t first, std::int64_t end, double rate, const LiveSourceFrames* liveFrames) {
        planned = true; planFirst = first; planEnd = end; planRate = rate; planTime = time;
        segmentCount = 0; layerCount = 0; lastSegment = 0; interleaved = 1;
        auto& items = *layers;
        for (const auto& clip : composition.clips) {
            if (layerCount == maximumLayers) { break; }
            if (!clip.active(time)) { continue; }
            const auto weight = clip.weight(time);
            if (!(weight > 0)) { continue; }
            const auto* source = clip.resolveSource(liveFrames);
            if (source == nullptr || source->frameCount() == 0) { continue; }
            Layer layer{&clip, source, source->frameIndex(clip.localTime(time)), weight, 0, {}, {}, clip.midi != nullptr, 2};
            const auto* drawing = source->drawingAt(layer.frame);
            if (drawing != nullptr && !layer.midi) {
                layer.minimum = std::max<std::int64_t>(2, drawing->minimumTraversalSamples());
            }
            double voices = 1;
            if (layer.midi) {
                voices = static_cast<double>(clip.midi->activeCount(time));
                if (voices == 0) { continue; }
            }
            // Vector drawings know their exact length; probes only measure how
            // the clip's transforms, effects and camera scale it on screen. The
            // ratio of projected to source distance is uniform under affine
            // transforms, so pen-up jumps measure scale as well as strokes do.
            // Point frames have no stored length: sum their lit steps directly.
            const auto* vector = source->drawingAt(layer.frame);
            const auto probes = vector != nullptr ? lengthProbes : pointLengthProbes;
            osci::Point previousRaw, previousPoint;
            double sourceDistance = 0, projectedDistance = 0, litDistance = 0;
            for (int probe = 0; probe < probes; ++probe) {
                const auto phase = static_cast<double>(probe) / (probes - 1);
                const auto raw = source->sampleFrame(layer.frame, phase);
                const auto point = output(composition, layer, raw);
                if (probe == 0) {
                    layer.start = point;
                } else {
                    const auto dx = static_cast<double>(raw.x) - previousRaw.x, dy = static_cast<double>(raw.y) - previousRaw.y, dz = static_cast<double>(raw.z) - previousRaw.z;
                    const auto step = std::sqrt(dx * dx + dy * dy + dz * dz);
                    const auto projected = distance(previousPoint, point);
                    if (std::isfinite(step) && std::isfinite(projected)) { sourceDistance += step; projectedDistance += projected; }
                    if (lit(previousPoint) && lit(point)) { litDistance += projected; }
                }
                layer.end = point;
                previousRaw = raw; previousPoint = point;
            }
            if (vector != nullptr) {
                const auto scale = sourceDistance > 1.0e-9 ? projectedDistance / sourceDistance : 1.0;
                layer.length = vector->length() * (std::isfinite(scale) ? scale : 1.0);
            } else {
                layer.length = litDistance;
            }
            layer.length = std::max(minimumLayerLength, layer.length) * voices;
            items[layerCount++] = layer;
        }
        const auto total = end - first;
        if (layerCount == 0 || total <= 0) {
            push({Kind::hold, first, std::max<std::int64_t>(0, total), nullptr, nullptr, 0, false, {0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0}});
            return;
        }
        const auto dwell = std::max<std::int64_t>(1, static_cast<std::int64_t>(std::llround(dwellSeconds * rate)));
        const auto travel = [&](const osci::Point& a, const osci::Point& b) {
            return std::max<std::int64_t>(1, static_cast<std::int64_t>(std::ceil(distance(a, b) * travelSecondsPerUnit * rate)));
        };
        interleave(cycleOf(first, rate, composition.beamRate), total, dwell, travel(osci::Point(-1, 0, 0), osci::Point(1, 0, 0)));
        order();
        const auto entry = [&](std::size_t index) { return items[index].midi ? items[index].start : (reversedFlags[index] ? items[index].end : items[index].start); };
        const auto exit = [&](std::size_t index) { return items[index].midi ? items[index].start : (reversedFlags[index] ? items[index].start : items[index].end); };
        // Too many layers for this cycle drop from the end of the travel order.
        std::int64_t budget = 0;
        std::array<std::int64_t, maximumLayers> counts{};
        while (layerCount > 0) {
            std::int64_t overhead = dwell;
            for (std::size_t i = 1; i < layerCount; ++i) { overhead += 2 * dwell + travel(exit(i - 1), entry(i)); }
            overhead += dwell + travel(exit(layerCount - 1), entry(0));
            budget = total - overhead;
            std::int64_t minimum = 0;
            for (std::size_t i = 0; i < layerCount; ++i) { minimum += std::min(items[i].minimum, total); }
            if (budget >= minimum || (layerCount == 1 && budget >= 2)) { break; }
            --layerCount;
        }
        if (layerCount == 0) {
            push({Kind::hold, first, total, nullptr, nullptr, 0, false, {0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0}});
            return;
        }
        double reserved = 0;
        for (std::size_t i = 0; i < layerCount; ++i) { reserved += std::max(1.0, items[i].weight) * items[i].length; }
        const auto density = static_cast<double>(budget) / reserved;
        std::int64_t used = 0;
        for (std::size_t i = 0; i < layerCount; ++i) {
            const auto ideal = std::floor(items[i].weight * items[i].length * density);
            const auto floor = std::min(items[i].minimum, budget);
            counts[i] = std::max<std::int64_t>(floor, std::isfinite(ideal) ? static_cast<std::int64_t>(ideal) : floor);
            used += counts[i];
        }
        // Complete strokes first: trim the layers furthest above their minimum.
        while (used > budget) {
            std::size_t largest = layerCount;
            std::int64_t surplus = 0;
            for (std::size_t i = 0; i < layerCount; ++i) {
                const auto above = counts[i] - std::max<std::int64_t>(2, std::min(items[i].minimum, budget));
                if (above > surplus) { surplus = above; largest = i; }
            }
            if (largest == layerCount) { break; }
            const auto cut = std::min(surplus, used - budget);
            counts[largest] -= cut; used -= cut;
        }
        while (used > budget) {
            std::size_t largest = 0;
            for (std::size_t i = 1; i < layerCount; ++i) { if (counts[i] > counts[largest]) { largest = i; } }
            if (counts[largest] <= 2) { break; }
            --counts[largest]; --used;
        }
        auto cursor = first;
        const auto hold = [&](const osci::Point& at, std::int64_t count) {
            if (count > 0) { push({Kind::hold, cursor, count, nullptr, nullptr, 0, false, dark(at), dark(at)}); cursor += count; }
        };
        hold(entry(0), dwell);
        for (std::size_t i = 0; i < layerCount; ++i) {
            if (i > 0) {
                hold(exit(i - 1), dwell);
                const auto steps = travel(exit(i - 1), entry(i));
                push({Kind::move, cursor, steps, nullptr, nullptr, 0, false, dark(exit(i - 1)), dark(entry(i))});
                cursor += steps;
                hold(entry(i), dwell);
            }
            const auto& layer = items[i];
            push({layer.midi ? Kind::midi : Kind::draw, cursor, counts[i], layer.clip, layer.source, layer.frame, reversedFlags[i], layer.start, layer.end});
            cursor += counts[i];
        }
        hold(exit(layerCount - 1), dwell);
        const auto back = std::min(end - cursor, travel(exit(layerCount - 1), entry(0)));
        if (back > 0) {
            push({Kind::move, cursor, back, nullptr, nullptr, 0, false, dark(exit(layerCount - 1)), dark(entry(0))});
            cursor += back;
        }
        hold(entry(0), end - cursor);
    }

    // When complete strokes for every layer cannot fit in one cycle, layers
    // take turns across k consecutive cycles: each is drawn whole at a lower
    // refresh rate instead of every layer blanking out from undersampling.
    void interleave(std::int64_t cycle, std::int64_t total, std::int64_t dwell, std::int64_t travel) {
        auto& items = *layers;
        double demand = 0;
        for (std::size_t i = 0; i < layerCount; ++i) { demand += static_cast<double>(items[i].minimum + 2 * dwell + travel); }
        const auto groups = std::min<std::int64_t>(maximumInterleave, static_cast<std::int64_t>(std::ceil(demand / static_cast<double>(std::max<std::int64_t>(1, total)))));
        interleaved = std::max<std::int64_t>(1, groups);
        if (interleaved <= 1) { return; }
        const auto share = demand / static_cast<double>(interleaved);
        const auto turn = ((cycle % interleaved) + interleaved) % interleaved;
        std::size_t kept = 0;
        double before = 0;
        for (std::size_t i = 0; i < layerCount; ++i) {
            const auto cost = static_cast<double>(items[i].minimum + 2 * dwell + travel);
            const auto group = std::min<std::int64_t>(interleaved - 1, static_cast<std::int64_t>((before + cost * 0.5) / share));
            before += cost;
            if (group == turn) { items[kept++] = items[i]; }
        }
        layerCount = kept;
    }

    // Greedy nearest-neighbour order from the first layer, allowing either
    // traversal direction for vector layers. Deterministic for a given plan.
    void order() {
        auto& items = *layers;
        reversedFlags.fill(false);
        for (std::size_t placed = 1; placed < layerCount; ++placed) {
            const auto& last = items[placed - 1];
            const auto at = last.midi ? last.start : (reversedFlags[placed - 1] ? last.start : last.end);
            auto best = placed;
            bool bestReversed = false;
            auto bestDistance = std::numeric_limits<double>::infinity();
            for (std::size_t candidate = placed; candidate < layerCount; ++candidate) {
                const auto forward = distance(at, items[candidate].start);
                if (forward < bestDistance) { bestDistance = forward; best = candidate; bestReversed = false; }
                if (!items[candidate].midi) {
                    const auto backward = distance(at, items[candidate].end);
                    if (backward < bestDistance) { bestDistance = backward; best = candidate; bestReversed = true; }
                }
            }
            std::swap(items[placed], items[best]);
            reversedFlags[placed] = bestReversed;
        }
    }

    void push(const Segment& segment) {
        if (segmentCount < maximumSegments) { (*segments)[segmentCount++] = segment; }
    }

    const Segment* find(std::int64_t index) {
        const auto& all = *segments;
        if (lastSegment < segmentCount) {
            const auto& guess = all[lastSegment];
            if (index >= guess.first && index < guess.first + guess.count) { return &guess; }
            if (lastSegment + 1 < segmentCount) {
                const auto& next = all[lastSegment + 1];
                if (index >= next.first && index < next.first + next.count) { ++lastSegment; return &next; }
            }
        }
        std::size_t low = 0, high = segmentCount;
        while (low < high) {
            const auto middle = (low + high) / 2;
            if (all[middle].first + all[middle].count <= index) { low = middle + 1; } else { high = middle; }
        }
        if (low >= segmentCount || index < all[low].first) { return nullptr; }
        lastSegment = low;
        return &all[low];
    }

    osci::Point evaluate(const PreparedComposition& composition, std::int64_t index, double time, double rate, bool advancing) {
        const auto* segment = find(index);
        if (segment == nullptr) { return {0, 0, 0, 0, 0, 0}; }
        const auto j = index - segment->first;
        switch (segment->kind) {
            case Kind::hold: return segment->from;
            case Kind::move: {
                const auto t = static_cast<float>(static_cast<double>(j + 1) / static_cast<double>(segment->count + 1));
                auto point = segment->from;
                point.x += (segment->to.x - segment->from.x) * t;
                point.y += (segment->to.y - segment->from.y) * t;
                return dark(point);
            }
            case Kind::draw: {
                const auto n = segment->count;
                const auto step = segment->reversed ? n - 1 - j : j;
                const auto* drawing = segment->source->drawingAt(segment->frame);
                osci::Point raw;
                if (drawing != nullptr && drawing->minimumTraversalSamples() > 0 && n >= drawing->minimumTraversalSamples()) {
                    raw = drawing->sampleTraversal(step, n);
                } else {
                    const auto span = n > 1 ? 1.0 / static_cast<double>(n - 1) : 1.0;
                    raw = segment->source->sampleFrame(segment->frame, static_cast<double>(step) * span, span);
                }
                return composition.projectPoint(segment->clip->processPoint(raw, planTime), planTime);
            }
            case Kind::midi: {
                const auto n = segment->count;
                const auto at = [&](std::int64_t k) {
                    const auto sampleTime = advancing ? planTime + static_cast<double>(segment->first + k - planFirst) / rate : time;
                    return segment->clip->midi->select(sampleTime, (static_cast<double>(k) + 0.5) / static_cast<double>(n), static_cast<double>(segment->first + k) / rate);
                };
                const auto current = at(j);
                if (current.note == 0) { return dark(segment->from); }
                auto raw = segment->source->sampleFrame(segment->frame, current.phase, current.phaseSpan);
                const bool edge = j == 0 || j == n - 1 || at(j - 1).note != current.note || at(j + 1).note != current.note;
                auto point = composition.projectPoint(segment->clip->processPoint(raw, planTime), planTime);
                return edge ? dark(point) : point;
            }
        }
        return {0, 0, 0, 0, 0, 0};
    }

    std::unique_ptr<std::array<Segment, maximumSegments>> segments;
    std::unique_ptr<std::array<Layer, maximumLayers>> layers;
    std::array<bool, maximumLayers> reversedFlags{};
    std::size_t segmentCount = 0, layerCount = 0, lastSegment = 0;
    std::int64_t interleaved = 1;
    bool planned = false, planAdvancing = true;
    std::int64_t planFirst = 0, planEnd = 0;
    double planRate = 0, planTime = 0;
    std::uint64_t planGeneration = 0;
};
}

#pragma once

#include "CompositionRenderer.h"
#include "LiveMidiInputs.h"
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
// dark travel, and every jump dwells dark at both ends, then settles, so
// reconstruction filters and slow scopes arrive before the next stroke lights;
// the project's scope profile calibrates those times. When complete
// strokes do not fit one cycle, a plan spans several aligned cycles instead of
// undersampling, trading refresh rate for complete, unbroken drawings.
class BeamRenderer {
public:
    static constexpr std::size_t maximumLayers = 256;
    static constexpr std::size_t maximumSegments = maximumLayers * 4 + 4;
    // Vector lengths are exact; probes only measure the transform's scale.
    // Point frames sum lit probe steps. Both run once per cycle per layer.
    static constexpr int lengthProbes = 9, pointLengthProbes = 48;
    static constexpr double minimumLayerLength = 0.05;
    static constexpr std::int64_t maximumSpan = 8;
    // Tour improvement stops after this many candidate checks and moved
    // layers, bounding planning cost however many layers are visible.
    static constexpr std::int64_t maximumOrderingWork = 8192;

    enum class Kind : std::uint8_t { draw, midi, live, move, hold };
    struct Segment {
        Kind kind = Kind::hold;
        std::int64_t first = 0, count = 0;
        const PreparedClip* clip = nullptr;
        const PreparedSource* source = nullptr;
        std::size_t frame = 0;
        bool reversed = false;
        osci::Point from, to;
        const LiveMidiPerformance* live = nullptr;
    };

    // Cycles never straddle a frame of the project's frame rate, so exported
    // video frames integrate a whole number of cycles and do not pulse.
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
    // Diagnostic: plans keep the greedy travel order when disabled.
    void setOrderImprovement(bool enabled) { improving = enabled; planned = false; }

    // index is the oscillator sample. time is the timeline time of that sample;
    // when advancing, the cycle latches the timeline time of its first sample.
    // generation must change whenever the composition or live frames change.
    // live: armed tracks' live voices, drawn with their clips (never in export).
    osci::Point sample(const PreparedComposition& composition, double time, std::int64_t index, double rate, bool advancing,
        std::uint64_t generation, const LiveSourceFrames* liveFrames = nullptr, const LiveMidiInputs* live = nullptr) {
        if (index < 0 || !std::isfinite(rate) || rate <= 0 || !std::isfinite(time)) { return {0, 0, 0, 0, 0, 0}; }
        liveInputs = live;
        const auto cycleRate = composition.beamRate;
        auto first = planFirst, end = planEnd;
        if (!planned || index < planFirst || index >= planEnd || rate != planRate || cycleRate != planCycleRate || generation != planGeneration) {
            // Dense scenes span several cycles: the window is aligned to its
            // span k and k is the smallest span that fits the demand measured
            // at the window's own start, so windows are a pure function of the
            // cycle index for random access, playback and export alike.
            const auto cycle = cycleOf(index, rate, cycleRate);
            std::int64_t span = maximumSpan;
            for (std::int64_t k = 1; k <= maximumSpan; ++k) {
                const auto window = cycle - ((cycle % k) + k) % k;
                const auto windowFirst = cycleStart(window, rate, cycleRate);
                const auto windowTime = advancing ? time - static_cast<double>(index - windowFirst) / rate : time;
                if (spanFor(composition, windowTime, rate, cycleStart(window + 1, rate, cycleRate) - windowFirst, liveFrames) <= k) { span = k; break; }
            }
            const auto window = cycle - ((cycle % span) + span) % span;
            first = cycleStart(window, rate, cycleRate);
            end = cycleStart(window + span, rate, cycleRate);
            planSpan = span;
        }
        // Per-sample transport time carries rounding; half a sample of drift is
        // the same cycle, anything more is a seek.
        const auto latched = advancing ? time - static_cast<double>(index - first) / rate : time;
        if (!planned || first != planFirst || generation != planGeneration || rate != planRate || advancing != planAdvancing
            || std::abs(latched - planTime) > 0.5 / rate) {
            plan(composition, latched, first, end, rate, liveFrames);
            planGeneration = generation;
            planCycleRate = cycleRate;
            planAdvancing = advancing;
        }
        return evaluate(composition, index, time, rate, advancing);
    }

    // Diagnostic view of the current plan, for tests and the editor meter.
    std::span<const Segment> plannedSegments() const { return {segments->data(), segmentCount}; }
    std::size_t plannedLayers() const { return layerCount; }
    // Cycles the current plan spans: each layer refreshes once per span.
    std::int64_t plannedInterleave() const { return planSpan; }

private:
    // Scope profile times in samples at the plan's rate.
    struct Timing {
        std::int64_t dwell = 1, settle = 0;
        double travelSeconds = 0, rate = 0;
        std::int64_t travel(const osci::Point& a, const osci::Point& b) const { return travel(distance(a, b)); }
        std::int64_t travel(double length) const { return std::max<std::int64_t>(1, static_cast<std::int64_t>(std::ceil(length * travelSeconds * rate))); }
    };
    static Timing timingFor(const ScopeProfile& requested, double rate) {
        const auto profile = requested.valid() ? requested : ScopeProfile{};
        const auto samples = [rate](double micros) { return static_cast<std::int64_t>(std::llround(micros / 1.0e6 * rate)); };
        return {std::max<std::int64_t>(1, samples(profile.dwellMicros)), std::max<std::int64_t>(0, samples(profile.settleMicros)), profile.travelMicrosPerUnit / 1.0e6, rate};
    }

    struct Layer {
        const PreparedClip* clip;
        const PreparedSource* source;
        std::size_t frame;
        double weight, length;
        osci::Point start, end;
        bool midi;
        std::int64_t minimum;
        const LiveMidiPerformance* live = nullptr;
    };

    // The beam's travel between two points; a non-finite one counts as far.
    static double distance(double ax, double ay, double bx, double by) {
        const auto dx = ax - bx, dy = ay - by;
        const auto value = std::sqrt(dx * dx + dy * dy);
        return std::isfinite(value) ? value : 2.0;
    }
    static double distance(const osci::Point& a, const osci::Point& b) { return distance(a.x, a.y, b.x, b.y); }
    static osci::Point dark(osci::Point point) { point.r = point.g = point.b = 0; return point; }
    static bool lit(const osci::Point& point) { return point.r > 0 || point.g > 0 || point.b > 0; }

    osci::Point output(const PreparedComposition& composition, const Layer& layer, osci::Point raw) const {
        return composition.projectVisible(layer.clip->processPoint(raw, planTime), planTime).value_or(osci::Point(0, 0, 0, 0, 0, 0));
    }

    void plan(const PreparedComposition& composition, double time, std::int64_t first, std::int64_t end, double rate, const LiveSourceFrames* liveFrames) {
        planned = true; planFirst = first; planEnd = end; planRate = rate; planTime = time;
        segmentCount = 0; layerCount = 0; lastSegment = 0;
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
            measure(composition, layer, voices);
        }
        // Armed tracks draw their live voices with the track's clip under the
        // playhead (or its nearest clip while the playhead is between clips).
        if (liveInputs != nullptr) {
            const auto clock = static_cast<std::uint64_t>(std::max<std::int64_t>(0, first + liveInputs->clockOffset));
            for (std::size_t route = 0; route < liveInputs->count && layerCount < maximumLayers; ++route) {
                const auto& input = liveInputs->routes[route];
                const auto voices = static_cast<double>(input.performance.activeCount(clock));
                if (voices == 0) { continue; }
                const PreparedClip* chosen = nullptr;
                double nearest = std::numeric_limits<double>::infinity();
                for (const auto& clip : composition.clips) {
                    if (clip.rootTrack != input.track) { continue; }
                    const auto gap = clip.active(time) ? 0.0 : std::min(std::abs(clip.start - time), std::abs(clip.end - time));
                    if (gap < nearest) { nearest = gap; chosen = &clip; }
                }
                if (chosen == nullptr) { continue; }
                const auto* source = chosen->resolveSource(liveFrames);
                if (source == nullptr || source->frameCount() == 0) { continue; }
                const auto position = std::clamp(time, chosen->start, std::nextafter(chosen->end, chosen->start));
                Layer layer{chosen, source, source->frameIndex(chosen->localTime(position)), std::max(1.0, chosen->weight(position)), 0, {}, {}, true, 2, &input.performance};
                measure(composition, layer, voices);
            }
        }
        finishPlan(composition, first, end, rate);
    }

    // Lengths and endpoints from probes; appends the layer.
    void measure(const PreparedComposition& composition, Layer layer, double voices) {
        auto& items = *layers;
        const auto* source = layer.source;
        {
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
    }

    void finishPlan(const PreparedComposition& composition, std::int64_t first, std::int64_t end, double rate) {
        auto& items = *layers;
        const auto total = end - first;
        if (layerCount == 0 || total <= 0) {
            push({Kind::hold, first, std::max<std::int64_t>(0, total), nullptr, nullptr, 0, false, {0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0}});
            return;
        }
        const auto timing = timingFor(composition.scope, rate);
        const auto dwell = timing.dwell, settle = timing.settle;
        order();
        improveOrder();
        // Too many layers for this cycle drop from the end of the travel order.
        std::int64_t budget = 0;
        std::array<std::int64_t, maximumLayers> counts{};
        while (layerCount > 0) {
            std::int64_t overhead = dwell + settle;
            for (std::size_t i = 1; i < layerCount; ++i) { overhead += 2 * dwell + settle + timing.travel(exitOf(i - 1), entryOf(i)); }
            overhead += dwell + timing.travel(exitOf(layerCount - 1), entryOf(0));
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
            const auto least = std::min(items[i].minimum, budget);
            counts[i] = std::max<std::int64_t>(least, std::isfinite(ideal) ? static_cast<std::int64_t>(ideal) : least);
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
        // The beam arrives from the previous cycle's return jump.
        hold(entryOf(0), dwell + settle);
        for (std::size_t i = 0; i < layerCount; ++i) {
            if (i > 0) {
                hold(exitOf(i - 1), dwell);
                const auto steps = timing.travel(exitOf(i - 1), entryOf(i));
                push({Kind::move, cursor, steps, nullptr, nullptr, 0, false, dark(exitOf(i - 1)), dark(entryOf(i))});
                cursor += steps;
                hold(entryOf(i), dwell + settle);
            }
            const auto& layer = items[i];
            push({layer.live != nullptr ? Kind::live : layer.midi ? Kind::midi : Kind::draw, cursor, counts[i], layer.clip, layer.source, layer.frame, reversedFlags[i], layer.start, layer.end, layer.live});
            cursor += counts[i];
        }
        hold(exitOf(layerCount - 1), dwell);
        const auto back = std::min(end - cursor, timing.travel(exitOf(layerCount - 1), entryOf(0)));
        if (back > 0) {
            push({Kind::move, cursor, back, nullptr, nullptr, 0, false, dark(exitOf(layerCount - 1)), dark(entryOf(0))});
            cursor += back;
        }
        hold(entryOf(0), end - cursor);
    }

    // Cycles needed to draw every visible layer's complete strokes once,
    // from minimum traversal budgets plus dwell, settle and a unit of travel
    // per jump, capped at the maximum span. Cheap: no geometry is transformed.
    static std::int64_t spanFor(const PreparedComposition& composition, double time, double rate, std::int64_t cycleSamples, const LiveSourceFrames* liveFrames) {
        if (cycleSamples <= 0) { return 1; }
        const auto timing = timingFor(composition.scope, rate);
        const auto jump = 2 * timing.dwell + timing.settle + timing.travel(1.0);
        double demand = static_cast<double>(timing.dwell + timing.settle);
        std::size_t counted = 0;
        for (const auto& clip : composition.clips) {
            if (counted == maximumLayers) { break; }
            if (!clip.active(time) || !(clip.weight(time) > 0)) { continue; }
            const auto* source = clip.resolveSource(liveFrames);
            if (source == nullptr || source->frameCount() == 0) { continue; }
            const auto* drawing = clip.midi == nullptr ? source->drawingAt(source->frameIndex(clip.localTime(time))) : nullptr;
            demand += static_cast<double>(std::max<std::int64_t>(2, drawing != nullptr ? drawing->minimumTraversalSamples() : 2) + jump);
            ++counted;
        }
        return std::clamp<std::int64_t>(static_cast<std::int64_t>(std::ceil(demand / static_cast<double>(cycleSamples))), 1, maximumSpan);
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

    osci::Point entryOf(std::size_t index) const {
        const auto& layer = (*layers)[index];
        return layer.midi ? layer.start : (reversedFlags[index] ? layer.end : layer.start);
    }
    osci::Point exitOf(std::size_t index) const {
        const auto& layer = (*layers)[index];
        return layer.midi ? layer.start : (reversedFlags[index] ? layer.start : layer.end);
    }

    // Bounded 2-opt over the closed tour, including the return jump. Reversing
    // positions i..j also flips each layer's direction, so interior jumps keep
    // their lengths and only the two boundary jumps change; i == j flips one
    // layer. The first layer stays first, improving moves are taken in scan
    // order and work is capped, so the tour is a deterministic function of the
    // greedy order, never longer than it, at bounded cost. Allocates nothing.
    void improveOrder() {
        if (!improving || layerCount < 2) { return; }
        for (std::size_t i = 0; i < layerCount; ++i) {
            const auto entry = entryOf(i), exit = exitOf(i);
            ends[i] = {entry.x, entry.y, exit.x, exit.y};
        }
        std::int64_t work = 0;
        bool improved = true;
        while (improved && work < maximumOrderingWork) {
            improved = false;
            for (std::size_t i = 1; i < layerCount && work < maximumOrderingWork; ++i) {
                const auto& before = ends[i - 1];
                const auto& first = ends[i];
                auto removed = distance(before.exitX, before.exitY, first.entryX, first.entryY);
                for (std::size_t j = i; j < layerCount && work < maximumOrderingWork; ++j) {
                    ++work;
                    const auto& last = ends[j];
                    const auto& after = ends[j + 1 == layerCount ? 0 : j + 1];
                    const auto change = distance(before.exitX, before.exitY, last.exitX, last.exitY) + distance(first.entryX, first.entryY, after.entryX, after.entryY)
                        - removed - distance(last.exitX, last.exitY, after.entryX, after.entryY);
                    if (change < -1.0e-9) {
                        reverse(i, j);
                        work += static_cast<std::int64_t>(j - i + 1);
                        improved = true;
                        removed = distance(before.exitX, before.exitY, first.entryX, first.entryY);
                    }
                }
            }
        }
    }
    void reverse(std::size_t first, std::size_t last) {
        auto& items = *layers;
        for (auto i = first, j = last; i < j; ++i, --j) {
            std::swap(items[i], items[j]);
            std::swap(reversedFlags[i], reversedFlags[j]);
            std::swap(ends[i], ends[j]);
        }
        for (auto i = first; i <= last; ++i) {
            if (!items[i].midi) {
                reversedFlags[i] = !reversedFlags[i];
                std::swap(ends[i].entryX, ends[i].exitX);
                std::swap(ends[i].entryY, ends[i].exitY);
            }
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
                // Behind the camera there is no position: hold dark at the entry.
                return composition.projectVisible(segment->clip->processPoint(raw, planTime), planTime).value_or(dark(segment->from));
            }
            case Kind::live: {
                if (liveInputs == nullptr) { return dark(segment->from); }
                const auto offset = liveInputs->clockOffset;
                return voiced(composition, *segment, j, [&](std::int64_t k) {
                    const auto clock = static_cast<std::uint64_t>(std::max<std::int64_t>(0, segment->first + k + offset));
                    return segment->live->select(clock, (static_cast<double>(k) + 0.5) / static_cast<double>(segment->count));
                });
            }
            case Kind::midi: {
                return voiced(composition, *segment, j, [&](std::int64_t k) {
                    const auto sampleTime = advancing ? planTime + static_cast<double>(segment->first + k - planFirst) / rate : time;
                    return segment->clip->midi->select(sampleTime, (static_cast<double>(k) + 0.5) / static_cast<double>(segment->count), static_cast<double>(segment->first + k) / rate);
                });
            }
        }
        return {0, 0, 0, 0, 0, 0};
    }

    // Sample j of a segment shared among notes: `at(k)` is the note at sample
    // k. The beam is dark where no note sounds and at each note's ends, so it
    // blanks between voices.
    template <typename NoteAt>
    osci::Point voiced(const PreparedComposition& composition, const Segment& segment, std::int64_t j, const NoteAt& at) const {
        const auto current = at(j);
        if (current.note == 0) { return dark(segment.from); }
        const auto raw = segment.source->sampleFrame(segment.frame, current.phase, current.phaseSpan);
        const bool edge = j == 0 || j == segment.count - 1 || at(j - 1).note != current.note || at(j + 1).note != current.note;
        const auto projected = composition.projectVisible(segment.clip->processPoint(raw, planTime), planTime);
        if (!projected.has_value()) { return dark(segment.from); }
        return edge ? dark(*projected) : *projected;
    }

    std::unique_ptr<std::array<Segment, maximumSegments>> segments;
    std::unique_ptr<std::array<Layer, maximumLayers>> layers;
    const LiveMidiInputs* liveInputs = nullptr;
    std::array<bool, maximumLayers> reversedFlags{};
    // Entry and exit of each ordered layer while the tour is improved.
    struct Ends { double entryX, entryY, exitX, exitY; };
    std::array<Ends, maximumLayers> ends{};
    std::size_t segmentCount = 0, layerCount = 0, lastSegment = 0;
    std::int64_t planSpan = 1;
    bool planned = false, planAdvancing = true, improving = true;
    std::int64_t planFirst = 0, planEnd = 0;
    double planRate = 0, planTime = 0, planCycleRate = 0;
    std::uint64_t planGeneration = 0;
};
}

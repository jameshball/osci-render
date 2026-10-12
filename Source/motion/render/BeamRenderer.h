#pragma once

#include "CompositionRenderer.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>

namespace motion {
// Beam cycles are latched plans. Each cycle is a pure function of its index,
// the sample rate, the composition snapshot and the latched timeline time, so
// random access, live playback and offline export produce identical samples.
//
// Within a cycle every visible layer is drawn once. Samples are apportioned by
// projected path length, so every layer keeps the same brightness per unit
// length, then scaled by each layer's Beam: a layer at 2x takes twice its even
// share from the others, with no floor. Opacity dims a layer through its
// colour, without taking beam time from the others. Layers are ordered to minimise
// dark travel, and every jump dwells dark at both ends, then settles, so
// reconstruction filters and slow scopes arrive before the next stroke lights;
// the project's scope profile calibrates those times. Every layer is drawn
// every cycle whenever it can be: a drawing's complete strokes take a sample at
// each end and corner, and when those do not all fit, the layers needing the
// most make do with their strokes' ends. Only content with more separate
// strokes than one cycle can blank on and off spans several aligned cycles,
// trading refresh rate for drawings that show at all.
class BeamRenderer {
public:
    static constexpr std::size_t maximumLayers = 256;
    static constexpr std::size_t maximumSegments = maximumLayers * 4 + 4;
    // Vector lengths are exact; probes only measure the transform's scale.
    // Point frames sum lit probe steps. Both run once per cycle per layer.
    static constexpr int lengthProbes = 9, pointLengthProbes = 48;
    static constexpr double minimumLayerLength = 0.05;
    // Tour improvement stops after this many candidate checks and moved
    // layers, bounding planning cost however many layers are visible.
    static constexpr std::int64_t maximumOrderingWork = 8192;

    enum class Kind : std::uint8_t { draw, move, hold };
    struct Segment {
        Kind kind = Kind::hold;
        std::int64_t first = 0, count = 0;
        const PreparedClip* clip = nullptr;
        const PreparedSource* source = nullptr;
        std::size_t frame = 0;
        bool reversed = false;
        osci::Point from, to;
        // The vector drawing drawn (hidden lines removed, when baked); none
        // for point frames.
        const PreparedDrawing* drawing = nullptr;
        // Its points are in the scene already: a clip's reflections.
        bool world = false;
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
    osci::Point sample(const PreparedComposition& composition, double time, std::int64_t index, double rate, bool advancing,
        std::uint64_t generation, const LiveSourceFrames* liveFrames = nullptr) {
        if (index < 0 || !std::isfinite(rate) || rate <= 0 || !std::isfinite(time)) { return {0, 0, 0, 0, 0, 0}; }
        const auto cycleRate = composition.beamRate;
        auto first = planFirst, end = planEnd;
        if (!planned || index < planFirst || index >= planEnd || rate != planRate || cycleRate != planCycleRate || generation != planGeneration) {
            // Every cycle draws the whole frame: a dense scene is drawn faster
            // and simpler (see finishPlan), never shared across cycles, which
            // would show its parts by turns.
            const auto cycle = cycleOf(index, rate, cycleRate);
            first = cycleStart(cycle, rate, cycleRate);
            end = cycleStart(cycle + 1, rate, cycleRate);
        }
        // Per-sample transport time carries rounding; half a sample of drift is
        // the same cycle, anything more is a seek.
        const auto latched = advancing ? time - static_cast<double>(index - first) / rate : time;
        if (!planned || first != planFirst || end != planEnd || generation != planGeneration || rate != planRate || advancing != planAdvancing
            || std::abs(latched - planTime) > 0.5 / rate) {
            plan(composition, latched, first, end, rate, liveFrames);
            planGeneration = generation;
            planCycleRate = cycleRate;
            planAdvancing = advancing;
        }
        return evaluate(composition, index);
    }

    // Plans one cycle at `time` without sampling, so the editor can show how
    // the beam is shared; read it with plannedSegments().
    void planCycle(const PreparedComposition& composition, double time, double rate, const LiveSourceFrames* liveFrames = nullptr) {
        if (!std::isfinite(rate) || rate <= 0 || !std::isfinite(time)) { segmentCount = 0; layerCount = 0; return; }
        plan(composition, time, 0, cycleStart(1, rate, composition.beamRate), rate, liveFrames);
        planned = false;
    }

    // Diagnostic view of the current plan, for tests and the editor meter.
    std::span<const Segment> plannedSegments() const { return {segments->data(), segmentCount}; }
    std::size_t plannedLayers() const { return layerCount; }
    // Whether the current plan had to leave strokes out to fit the cycle (a
    // higher sample rate draws them all).
    bool plannedSimplified() const { return planSimplified; }

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
        double length;
        osci::Point start, end;
        std::int64_t minimum;
        double share = 1;
        const PreparedDrawing* drawing = nullptr;
        double shown = 1;
        // Fewer samples that still reach every stroke's ends.
        std::int64_t coarse = 2;
        // A clip's reflections: points in the scene, placed by the camera alone.
        bool world = false;
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

    // A clip's point on screen at the plan time; none behind the camera. A
    // reflection's point is in the scene already.
    std::optional<osci::Point> place(const PreparedComposition& composition, const PreparedClip& clip, const osci::Point& raw, bool world = false) const {
        if (world) { return composition.projectWorld(raw, planTime); }
        return composition.projectVisible(clip.processPoint(raw, planTime), planTime);
    }

    // A clip drawn at `time`: its source frame, the fewest samples that draw
    // its strokes completely, and the fewest that reach their ends.
    struct Visible {
        const PreparedSource* source;
        std::size_t frame;
        std::int64_t minimum;
        double share;
        const PreparedDrawing* drawing;
        double shown;
        std::int64_t coarse;
    };
    // A clip's vector drawing at a frame: with the lines its camera can't see
    // removed once that frame is baked, a point source's frame as the runs of
    // it that show.
    static std::pair<const PreparedDrawing*, double> shownDrawing(const PreparedComposition& composition, const PreparedClip& clip, const PreparedSource& source, std::size_t frame, double time) {
        const auto* whole = source.drawingAt(frame);
        if (composition.hiddenLines == nullptr || clip.liveIdentity != nullptr) { return {whole, 1.0}; }
        const auto* baked = composition.hiddenLines->at(time);
        if (baked == nullptr) { return {whole, 1.0}; }
        const auto* culled = baked->entry(static_cast<std::size_t>(&clip - composition.clips.data()));
        return culled != nullptr ? std::pair {culled->drawing.get(), culled->shown} : std::pair {whole, 1.0};
    }
    static std::optional<Visible> visible(const PreparedComposition& composition, const PreparedClip& clip, double time, const LiveSourceFrames* liveFrames) {
        if (clip.light || !clip.active(time) || !clip.visible(time)) { return std::nullopt; }
        const auto share = clip.beam(time);
        if (!(share > 0)) { return std::nullopt; }
        const auto* source = clip.resolveSource(liveFrames);
        if (source == nullptr || source->frameCount() == 0) { return std::nullopt; }
        const auto frame = source->frameIndex(clip.localTime(time));
        const auto [drawing, shown] = shownDrawing(composition, clip, *source, frame, time);
        // Wholly hidden behind meshes.
        if (drawing != nullptr && drawing->empty()) { return std::nullopt; }
        // A point source's runs keep its own budget: no floor per corner.
        const auto floored = drawing != nullptr && source->drawingAt(frame) != nullptr;
        return Visible {source, frame, std::max<std::int64_t>(2, floored ? drawing->minimumTraversalSamples() : 2), share, drawing, shown,
            std::max<std::int64_t>(2, floored ? drawing->coarseTraversalSamples() : 2)};
    }

    void plan(const PreparedComposition& composition, double time, std::int64_t first, std::int64_t end, double rate, const LiveSourceFrames* liveFrames) {
        planned = true; planFirst = first; planEnd = end; planRate = rate; planTime = time; planSimplified = false;
        segmentCount = 0; layerCount = 0; lastSegment = 0;
        for (const auto& clip : composition.clips) {
            if (layerCount == maximumLayers) { break; }
            const auto shown = visible(composition, clip, time, liveFrames);
            if (!shown.has_value()) { continue; }
            measure(composition, {&clip, shown->source, shown->frame, 0, {}, {}, shown->minimum, shown->share, shown->drawing, shown->shown, shown->coarse});
        }
        // What mirrors show, and the camera's hatching, each drawn as part
        // of its clip.
        const auto* baked = composition.hiddenLines != nullptr ? composition.hiddenLines->at(time) : nullptr;
        for (const auto* entries : {baked != nullptr ? &baked->reflections : nullptr, baked != nullptr ? &baked->hatching : nullptr}) {
            for (std::size_t index = 0; entries != nullptr && index < entries->size() && layerCount < maximumLayers; ++index) {
                const auto& entry = (*entries)[index];
                if (entry.clip >= composition.clips.size() || entry.drawing == nullptr || entry.drawing->empty()) { continue; }
                const auto& clip = composition.clips[entry.clip];
                if (clip.light || !clip.active(time) || !clip.visible(time) || !(clip.beam(time) > 0)) { continue; }
                const auto& drawing = *entry.drawing;
                measure(composition, {&clip, nullptr, 0, 0, {}, {}, std::max<std::int64_t>(2, drawing.minimumTraversalSamples()), clip.beam(time), &drawing, 1, std::max<std::int64_t>(2, drawing.coarseTraversalSamples()), true});
            }
        }
        finishPlan(composition, first, end, rate);
    }

    // Lengths and endpoints from probes; appends the layer.
    void measure(const PreparedComposition& composition, Layer layer) {
        // Vector drawings know their exact length; probes only measure how
        // the clip's transforms, effects and camera scale it on screen. The
        // ratio of projected to source distance is uniform under affine
        // transforms, so pen-up jumps measure scale as well as strokes do.
        // Point frames have no stored length: sum their lit steps directly.
        const auto* vector = layer.drawing;
        // A point source's frame, even as the runs of it that show, is measured
        // as point frames always are, so hiding lines doesn't change its share.
        const auto exact = vector != nullptr && (layer.world || layer.source->drawingAt(layer.frame) != nullptr);
        const auto probes = exact ? lengthProbes : pointLengthProbes;
        osci::Point previousRaw, previousPoint;
        double sourceDistance = 0, projectedDistance = 0, litDistance = 0;
        for (int probe = 0; probe < probes; ++probe) {
            const auto phase = static_cast<double>(probe) / (probes - 1);
            // A point source is measured on its own frame, as always.
            const auto raw = exact ? vector->sample(phase) : layer.source->sampleFrame(layer.frame, phase);
            const auto point = place(composition, *layer.clip, raw, layer.world).value_or(osci::Point(0, 0, 0, 0, 0, 0));
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
        if (exact) {
            const auto scale = sourceDistance > 1.0e-9 ? projectedDistance / sourceDistance : 1.0;
            layer.length = vector->beamLength() * (std::isfinite(scale) ? scale : 1.0);
        } else {
            layer.length = litDistance * layer.shown;
            // Its runs, though, start and end where the shown part does.
            if (vector != nullptr) {
                layer.start = place(composition, *layer.clip, vector->sample(0)).value_or(layer.start);
                layer.end = place(composition, *layer.clip, vector->sample(1)).value_or(layer.end);
            }
        }
        layer.length = std::max(minimumLayerLength, layer.length);
        (*layers)[layerCount++] = layer;
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
        // Each layer's floor: the samples its complete strokes need. When the
        // floors do not all fit, the layer needing the most makes do with its
        // strokes' ends alone, and so on until the rest fit; past that, the
        // heaviest are traced evenly along their length (two samples at
        // least), and only then does the last in the travel order drop.
        // Reflections go through all that before the rest does.
        std::int64_t budget = 0;
        std::array<std::int64_t, maximumLayers> counts{};
        std::array<std::int64_t, maximumLayers> floors{};
        for (std::size_t i = 0; i < layerCount; ++i) { floors[i] = items[i].minimum; }
        while (layerCount > 0) {
            std::int64_t overhead = dwell + settle;
            for (std::size_t i = 1; i < layerCount; ++i) { overhead += 2 * dwell + settle + timing.travel(exitOf(i - 1), entryOf(i)); }
            overhead += dwell + timing.travel(exitOf(layerCount - 1), entryOf(0));
            budget = total - overhead;
            std::int64_t minimum = 0;
            for (std::size_t i = 0; i < layerCount; ++i) { minimum += std::min(floors[i], total); }
            if (budget >= minimum || (layerCount == 1 && budget >= 2)) { break; }
            // What mirrors show gives way first: it is simplified as far as
            // it can be before anything it shows is.
            bool eased = false;
            for (const auto reflectionsOnly : {true, false}) {
                const auto yields = [&](std::size_t i) { return !reflectionsOnly || items[i].world; };
                // The heaviest that can make do with its strokes' ends.
                std::size_t heaviest = layerCount;
                for (std::size_t i = 0; i < layerCount; ++i) {
                    if (yields(i) && floors[i] > items[i].coarse && (heaviest == layerCount || floors[i] > floors[heaviest])) { heaviest = i; }
                }
                if (heaviest != layerCount) {
                    floors[heaviest] = items[heaviest].coarse;
                    eased = true;
                    break;
                }
                // Past that, every layer gives up the same share of what its
                // strokes' ends need, keeping its longest strokes whole (three
                // samples each at least), so none is starved for the rest and
                // the frame is never split across cycles.
                if (budget <= 0) { break; }
                std::int64_t fixed = 0, scalable = 0;
                for (std::size_t i = 0; i < layerCount; ++i) {
                    if (floors[i] <= 3 || !yields(i)) { fixed += floors[i]; } else { scalable += floors[i]; }
                }
                if (scalable == 0) { continue; }
                const auto room = static_cast<double>(std::max<std::int64_t>(0, budget - fixed));
                const auto scale = std::min(1.0, room / static_cast<double>(scalable));
                for (std::size_t i = 0; i < layerCount; ++i) {
                    if (floors[i] <= 3 || !yields(i)) { continue; }
                    const auto next = std::max<std::int64_t>(3, static_cast<std::int64_t>(std::floor(static_cast<double>(floors[i]) * scale)));
                    eased = eased || next != floors[i];
                    floors[i] = next;
                }
                if (eased) { break; }
            }
            if (eased) { continue; }
            --layerCount;
        }
        if (layerCount == 0) {
            push({Kind::hold, first, total, nullptr, nullptr, 0, false, {0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0}});
            return;
        }
        // Even brightness above the floors: every layer gets the same samples
        // per unit of length, or its complete-stroke floor if that is more,
        // at the density that fills the budget. A layer whose floor is large
        // (a detailed drawing) takes only that, never the others' fair share.
        double shortest = std::numeric_limits<double>::infinity();
        for (std::size_t i = 0; i < layerCount; ++i) { shortest = std::min(shortest, items[i].length); }
        const auto demand = [&](double density) {
            std::int64_t total = 0;
            for (std::size_t i = 0; i < layerCount; ++i) {
                const auto ideal = std::floor(items[i].length * density);
                total += std::max<std::int64_t>(std::min(floors[i], budget), std::isfinite(ideal) ? static_cast<std::int64_t>(ideal) : 0);
            }
            return total;
        };
        double low = 0, high = static_cast<double>(budget) / shortest;
        for (int step = 0; step < 48 && high - low > 1.0e-9 * high; ++step) {
            const auto middle = (low + high) * .5;
            if (demand(middle) <= budget) { low = middle; } else { high = middle; }
        }
        std::int64_t used = 0;
        for (std::size_t i = 0; i < layerCount; ++i) {
            const auto ideal = std::floor(items[i].length * low);
            const auto least = std::min(floors[i], budget);
            counts[i] = std::max<std::int64_t>(least, std::isfinite(ideal) ? static_cast<std::int64_t>(ideal) : least);
            used += counts[i];
        }
        // Floors that do not fit: trim the layers furthest above their minimum.
        while (used > budget) {
            std::size_t largest = layerCount;
            std::int64_t surplus = 0;
            for (std::size_t i = 0; i < layerCount; ++i) {
                const auto above = counts[i] - std::max<std::int64_t>(2, std::min(floors[i], budget));
                if (above > surplus) { surplus = above; largest = i; }
            }
            if (largest == layerCount) { break; }
            const auto cut = std::min(surplus, used - budget);
            counts[largest] -= cut; used -= cut;
        }
        // Beam scales each layer's even share with no floor: what one layer
        // gives up goes to the others, and their total stays the same. Below
        // its complete-stroke budget a layer is traced by phase instead.
        double scaled = 0;
        bool even = true;
        for (std::size_t i = 0; i < layerCount; ++i) {
            scaled += static_cast<double>(counts[i]) * items[i].share;
            even = even && items[i].share == 1;
        }
        if (!even && scaled > 0) {
            const auto shared = static_cast<double>(used);
            used = 0;
            for (std::size_t i = 0; i < layerCount; ++i) {
                const auto ideal = std::floor(static_cast<double>(counts[i]) * items[i].share * shared / scaled);
                counts[i] = std::max<std::int64_t>(2, std::isfinite(ideal) ? static_cast<std::int64_t>(ideal) : 2);
                used += counts[i];
            }
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
            if (layer.drawing != nullptr && layer.drawing->coarseTraversalSamples() > counts[i]) { planSimplified = true; }
            push({Kind::draw, cursor, counts[i], layer.clip, layer.source, layer.frame, reversedFlags[i], layer.start, layer.end, layer.drawing, layer.world});
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

    // Greedy nearest-neighbour order from the first layer, allowing either
    // traversal direction. Deterministic for a given plan.
    void order() {
        auto& items = *layers;
        reversedFlags.fill(false);
        for (std::size_t placed = 1; placed < layerCount; ++placed) {
            const auto at = exitOf(placed - 1);
            auto best = placed;
            bool bestReversed = false;
            auto bestDistance = std::numeric_limits<double>::infinity();
            for (std::size_t candidate = placed; candidate < layerCount; ++candidate) {
                const auto forward = distance(at, items[candidate].start);
                if (forward < bestDistance) { bestDistance = forward; best = candidate; bestReversed = false; }
                const auto backward = distance(at, items[candidate].end);
                if (backward < bestDistance) { bestDistance = backward; best = candidate; bestReversed = true; }
            }
            std::swap(items[placed], items[best]);
            reversedFlags[placed] = bestReversed;
        }
    }

    osci::Point entryOf(std::size_t index) const {
        const auto& layer = (*layers)[index];
        return reversedFlags[index] ? layer.end : layer.start;
    }
    osci::Point exitOf(std::size_t index) const {
        const auto& layer = (*layers)[index];
        return reversedFlags[index] ? layer.start : layer.end;
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
            reversedFlags[i] = !reversedFlags[i];
            std::swap(ends[i].entryX, ends[i].exitX);
            std::swap(ends[i].entryY, ends[i].exitY);
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

    osci::Point evaluate(const PreparedComposition& composition, std::int64_t index) {
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
                const auto* drawing = segment->drawing;
                osci::Point raw;
                if (drawing != nullptr && drawing->coarseTraversalSamples() > 0 && n >= 3) {
                    raw = drawing->sampleTraversal(step, n);
                } else {
                    const auto span = n > 1 ? 1.0 / static_cast<double>(n - 1) : 1.0;
                    raw = drawing != nullptr ? drawing->sample(static_cast<double>(step) * span, span) : segment->source->sampleFrame(segment->frame, static_cast<double>(step) * span, span);
                }
                // Behind the camera there is no position: hold dark at the entry.
                return place(composition, *segment->clip, raw, segment->world).value_or(dark(segment->from));
            }
        }
        return {0, 0, 0, 0, 0, 0};
    }

    std::unique_ptr<std::array<Segment, maximumSegments>> segments;
    std::unique_ptr<std::array<Layer, maximumLayers>> layers;
    std::array<bool, maximumLayers> reversedFlags{};
    // Entry and exit of each ordered layer while the tour is improved.
    struct Ends { double entryX, entryY, exitX, exitY; };
    std::array<Ends, maximumLayers> ends{};
    std::size_t segmentCount = 0, layerCount = 0, lastSegment = 0;
    bool planSimplified = false;
    bool planned = false, planAdvancing = true, improving = true;
    std::int64_t planFirst = 0, planEnd = 0;
    double planRate = 0, planTime = 0, planCycleRate = 0;
    std::uint64_t planGeneration = 0;
};
}

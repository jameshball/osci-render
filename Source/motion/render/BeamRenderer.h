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
// colour, without taking beam time from the others. Layers are ordered to
// minimise dark travel, and every jump dwells dark at both ends, then settles,
// so reconstruction filters and slow scopes arrive before the next stroke
// lights; the project's scope profile calibrates those times. Every layer is
// drawn every cycle: a drawing's complete strokes take a sample at each end
// and corner, and when those do not all fit, the layers needing the most make
// do with their strokes' ends, then with their longest strokes, and only then
// does the last layer in the tour drop.
//
// A display with no brightness input (the scope profile's XY only) shows
// every sample: the beam never waits, its samples are all drawn at full
// brightness, and opacity takes beam time instead of dimming.
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
    // How many times the cycle's even density a layer may take before it is
    // dimmed to match (see finishPlan).
    static constexpr double brightest = 1.5;

    enum class Kind : std::uint8_t { draw, move, hold };
    struct Segment {
        Kind kind = Kind::hold;
        std::int64_t first = 0, count = 0;
        const PreparedClip* clip = nullptr;
        const PreparedSource* source = nullptr;
        std::size_t frame = 0;
        bool reversed = false;
        // Dark unless set: a hold with nothing to draw leaves the beam off.
        osci::Point from {0, 0, 0, 0, 0, 0}, to {0, 0, 0, 0, 0, 0};
        // The vector drawing drawn (hidden lines removed, when baked); none
        // for point frames.
        const PreparedDrawing* drawing = nullptr;
        // Its points are in the scene already: what a mirror shows, or the
        // camera's hatching.
        bool world = false;
        // Whether its drawing is traced stroke by stroke (complete strokes
        // where they fit), or else by phase (a point source's runs).
        bool floored = true;
        // How bright it is drawn (see finishPlan).
        float gain = 1;
        // Where its drawing's traversal was last sampled (see
        // PreparedDrawing::TraversalCursor).
        mutable PreparedDrawing::TraversalCursor cursor;
        // On a display with no brightness input (XY only), every sample
        // shows: the colour it shows in, at full brightness. Dark where
        // nothing is drawn.
        std::array<float, 3> hue {0, 0, 0};
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
        auto profile = requested.valid() ? requested : ScopeProfile{};
        profile.xyOnly = requested.xyOnly;
        const auto samples = [rate](double micros) { return static_cast<std::int64_t>(std::llround(micros / 1.0e6 * rate)); };
        // A beam that can't go dark has nothing to wait for: holding still
        // would only draw a dot.
        if (profile.xyOnly) { return {0, 0, profile.travelMicrosPerUnit / 1.0e6, rate}; }
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
        // Points in the scene, placed by the camera alone: what a mirror
        // shows, or the camera's hatching.
        bool world = false;
        // Traced stroke by stroke (see Segment::floored).
        bool floored = true;
        // Its colour at full brightness, for an XY-only display.
        std::array<float, 3> hue {1, 1, 1};
    };

    // The beam's travel between two points; a non-finite one counts as far.
    static double distance(double ax, double ay, double bx, double by) {
        const auto dx = ax - bx, dy = ay - by;
        const auto value = std::sqrt(dx * dx + dy * dy);
        return std::isfinite(value) ? value : 2.0;
    }
    static double distance(const osci::Point& a, const osci::Point& b) { return distance(a.x, a.y, b.x, b.y); }
    static osci::Point dark(osci::Point point) { point.r = point.g = point.b = 0; return point; }
    // A colour at full brightness, its hue kept; none for a dark one.
    static std::optional<std::array<float, 3>> fullHue(const osci::Point& point) {
        const auto most = std::max({point.r, point.g, point.b});
        if (!(most > 0) || !std::isfinite(most)) { return std::nullopt; }
        return std::array<float, 3> {std::max(0.0f, point.r) / most, std::max(0.0f, point.g) / most, std::max(0.0f, point.b) / most};
    }
    // A clip's own colour at full brightness, for its samples on an XY-only display.
    std::array<float, 3> hueOf(const PreparedClip* clip) const {
        if (clip == nullptr) { return {1, 1, 1}; }
        return fullHue(clip->processPoint(osci::Point(0, 0, 0, -1, -1, -1), planTime)).value_or(std::array<float, 3> {1, 1, 1});
    }
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
        bool floored;
    };
    // A clip's vector drawing at a frame: with the lines its camera can't see
    // removed once that frame is baked, a point source's frame as the runs of
    // it that show.
    static std::pair<const PreparedDrawing*, double> shownDrawing(const PreparedComposition& composition, const hidden::Frame* baked, const PreparedClip& clip, const PreparedSource& source, std::size_t frame) {
        const auto* whole = source.drawingAt(frame);
        if (baked == nullptr || clip.liveIdentity != nullptr) { return {whole, 1.0}; }
        const auto* culled = baked->entry(static_cast<std::size_t>(&clip - composition.clips.data()));
        return culled != nullptr ? std::pair {culled->drawing.get(), culled->shown} : std::pair {whole, 1.0};
    }
    // A clip's share of the beam at `time`, none when it isn't drawn. Lights
    // are drawn only where the editor shows them (its geometry keeps them
    // among the clips); a signal's composition has moved them aside.
    static double shareOf(const PreparedComposition& composition, const PreparedClip& clip, double time) {
        if (clip.light || !clip.active(time) || !clip.visible(time)) { return 0; }
        // With no brightness to dim, opacity takes beam time instead.
        const auto share = clip.beam(time) * (composition.scope.xyOnly ? clip.opacity(time) : 1.0);
        return share > 0 ? share : 0.0;
    }
    static std::optional<Visible> visible(const PreparedComposition& composition, const hidden::Frame* baked, const PreparedClip& clip, double time, const LiveSourceFrames* liveFrames) {
        const auto share = shareOf(composition, clip, time);
        if (!(share > 0)) { return std::nullopt; }
        const auto* source = clip.resolveSource(liveFrames);
        if (source == nullptr || source->frameCount() == 0) { return std::nullopt; }
        const auto frame = source->frameIndex(clip.localTime(time));
        const auto [drawing, shown] = shownDrawing(composition, baked, clip, *source, frame);
        // Wholly hidden behind meshes.
        if (drawing != nullptr && drawing->empty()) { return std::nullopt; }
        // A point source's runs keep its own budget: no floor per corner.
        const auto floored = drawing != nullptr && source->drawingAt(frame) != nullptr;
        return Visible {source, frame, std::max<std::int64_t>(2, floored ? drawing->minimumTraversalSamples() : 2), share, drawing, shown,
            std::max<std::int64_t>(2, floored ? drawing->coarseTraversalSamples() : 2), floored};
    }

    void plan(const PreparedComposition& composition, double time, std::int64_t first, std::int64_t end, double rate, const LiveSourceFrames* liveFrames) {
        planned = true; planFirst = first; planEnd = end; planRate = rate; planTime = time; planSimplified = false;
        segmentCount = 0; layerCount = 0; lastSegment = 0;
        const auto* baked = composition.hiddenLines != nullptr ? composition.hiddenLines->at(time) : nullptr;
        for (const auto& clip : composition.clips) {
            if (layerCount == maximumLayers) { break; }
            const auto shown = visible(composition, baked, clip, time, liveFrames);
            if (!shown.has_value()) { continue; }
            measure(composition, {.clip = &clip, .source = shown->source, .frame = shown->frame, .length = 0, .start = {}, .end = {}, .minimum = shown->minimum, .share = shown->share,
                .drawing = shown->drawing, .shown = shown->shown, .coarse = shown->coarse, .floored = shown->floored});
        }
        // What mirrors show, and the camera's hatching, each drawn as part
        // of its clip.
        for (const auto* entries : {baked != nullptr ? &baked->reflections : nullptr, baked != nullptr ? &baked->hatching : nullptr}) {
            for (std::size_t index = 0; entries != nullptr && index < entries->size() && layerCount < maximumLayers; ++index) {
                const auto& entry = (*entries)[index];
                if (entry.clip >= composition.clips.size() || entry.drawing == nullptr || entry.drawing->empty()) { continue; }
                const auto share = shareOf(composition, composition.clips[entry.clip], time);
                if (!(share > 0)) { continue; }
                const auto& clip = composition.clips[entry.clip];
                const auto& drawing = *entry.drawing;
                measure(composition, {.clip = &clip, .source = nullptr, .frame = 0, .length = 0, .start = {}, .end = {}, .minimum = std::max<std::int64_t>(2, drawing.minimumTraversalSamples()), .share = share,
                    .drawing = &drawing, .shown = 1, .coarse = std::max<std::int64_t>(2, drawing.coarseTraversalSamples()), .world = true});
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
            push({.kind = Kind::hold, .first = first, .count = std::max<std::int64_t>(0, total)});
            return;
        }
        const auto timing = timingFor(composition.scope, rate);
        const auto dwell = timing.dwell, settle = timing.settle;
        order();
        improveOrder();
        // Each layer's floor: the samples its complete strokes need. When the
        // floors do not all fit, the layer needing the most makes do with its
        // strokes' ends alone, and so on until the rest fit; past that, the
        // heaviest keep their longest strokes (three samples each at least),
        // and only then does the last in the travel order drop.
        // Reflections go through all that before the rest does.
        std::int64_t budget = 0;
        std::array<std::int64_t, maximumLayers> counts{};
        std::array<std::int64_t, maximumLayers> floors{};
        for (std::size_t i = 0; i < layerCount; ++i) { floors[i] = items[i].minimum; }
        // A mirror's view (a reflection, in the scene) is traced by its
        // strokes' ends and then evenly by length however many samples it
        // gets: tiny and detailed, shrunk small in a mirror ball, its corners
        // would take far more of the cycle than its length's share, and as it
        // fitted the cycle or not it would switch between its corners and its
        // length and flash.
        for (std::size_t i = 0; i < layerCount; ++i) {
            if (items[i].world && items[i].coarse < floors[i]) { floors[i] = items[i].coarse; }
        }
        // The jumps between layers in travel order: the order stays as it
        // is while floors ease, and only the last layers drop, so each is
        // found once.
        std::array<std::int64_t, maximumLayers> jumps{};
        std::int64_t between = 0;
        for (std::size_t i = 1; i < layerCount; ++i) {
            jumps[i] = 2 * dwell + settle + timing.travel(exitOf(i - 1), entryOf(i));
            between += jumps[i];
        }
        while (layerCount > 0) {
            const auto overhead = dwell + settle + between + dwell + timing.travel(exitOf(layerCount - 1), entryOf(0));
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
            if (layerCount > 0) { between -= jumps[layerCount]; }
        }
        if (layerCount == 0) {
            push({.kind = Kind::hold, .first = first, .count = total});
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
        // A layer whose strokes' corners alone take far more samples than its
        // length's share (a mirror's view of a detailed scene, shrunk small:
        // many tiny strokes, little length) would draw that much brighter
        // than the rest: it is dimmed to match, by as much as it is over, so
        // its brightness follows its detail smoothly from frame to frame and
        // never flashes as it fits the cycle or not.
        std::array<float, maximumLayers> gains{};
        {
            // Matched to the dimmest: the even density, or with every layer
            // at its floor, the least of theirs.
            auto least = std::numeric_limits<double>::infinity();
            for (std::size_t i = 0; i < layerCount; ++i) {
                if (items[i].length > 0) { least = std::min(least, static_cast<double>(counts[i]) / items[i].length); }
            }
            const auto density = std::max(low, std::isfinite(least) ? least : 0.0);
            for (std::size_t i = 0; i < layerCount; ++i) {
                const auto fair = brightest * density * items[i].length;
                // With no brightness to dim (XY only), samples are brightness.
                gains[i] = !composition.scope.xyOnly && counts[i] > 0 && static_cast<double>(counts[i]) > fair ? static_cast<float>(fair / static_cast<double>(counts[i])) : 1.0f;
            }
        }
        // Beam scales each layer's even share with no floor: what one layer
        // gives up goes to the others, and their total stays the same. Below
        // its complete strokes' samples a drawing makes do with their ends,
        // then its longest strokes (see PreparedDrawing::sampleTraversal).
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
        // A beam that can't go dark (XY only) would draw a dot where it held
        // still at the end of the cycle: what is left goes to the drawings,
        // by their length's share, as the rest was.
        if (composition.scope.xyOnly && used < budget) {
            const auto spare = budget - used;
            double weighed = 0;
            for (std::size_t i = 0; i < layerCount; ++i) { weighed += items[i].length * items[i].share; }
            std::int64_t given = 0;
            std::array<double, maximumLayers> remainders{};
            for (std::size_t i = 0; i < layerCount && weighed > 0; ++i) {
                const auto quota = static_cast<double>(spare) * items[i].length * items[i].share / weighed;
                const auto whole = static_cast<std::int64_t>(std::floor(quota));
                counts[i] += whole;
                given += whole;
                remainders[i] = quota - static_cast<double>(whole);
            }
            // What rounding leaves, to the largest remainders.
            while (given < spare) {
                std::size_t largest = 0;
                for (std::size_t i = 1; i < layerCount; ++i) {
                    if (remainders[i] > remainders[largest]) { largest = i; }
                }
                ++counts[largest];
                remainders[largest] = -1;
                ++given;
            }
            used += given;
            // Each layer's colour, once: every sample on such a display shows.
            for (std::size_t i = 0; i < layerCount; ++i) { items[i].hue = hueOf(items[i].clip); }
        }
        auto cursor = first;
        const auto hold = [&](const osci::Point& at, std::int64_t count, std::size_t layer) {
            if (count <= 0) { return; }
            push({.kind = Kind::hold, .first = cursor, .count = count, .from = dark(at), .to = dark(at), .hue = items[layer].hue});
            cursor += count;
        };
        const auto move = [&](const osci::Point& from, const osci::Point& to, std::int64_t count, std::size_t layer) {
            if (count <= 0) { return; }
            push({.kind = Kind::move, .first = cursor, .count = count, .from = dark(from), .to = dark(to), .hue = items[layer].hue});
            cursor += count;
        };
        // The beam arrives from the previous cycle's return jump.
        hold(entryOf(0), dwell + settle, 0);
        for (std::size_t i = 0; i < layerCount; ++i) {
            if (i > 0) {
                hold(exitOf(i - 1), dwell, i - 1);
                move(exitOf(i - 1), entryOf(i), timing.travel(exitOf(i - 1), entryOf(i)), i);
                hold(entryOf(i), dwell + settle, i);
            }
            const auto& layer = items[i];
            if (layer.coarse > counts[i]) { planSimplified = true; }
            push({.kind = Kind::draw, .first = cursor, .count = counts[i], .clip = layer.clip, .source = layer.source, .frame = layer.frame, .reversed = reversedFlags[i],
                .from = layer.start, .to = layer.end, .drawing = layer.drawing, .world = layer.world, .floored = layer.floored, .gain = gains[i], .hue = layer.hue});
            cursor += counts[i];
        }
        hold(exitOf(layerCount - 1), dwell, layerCount - 1);
        move(exitOf(layerCount - 1), entryOf(0), std::min(end - cursor, timing.travel(exitOf(layerCount - 1), entryOf(0))), 0);
        hold(entryOf(0), end - cursor, 0);
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
        auto point = evaluateSegment(composition, *segment, index);
        if (!composition.scope.xyOnly) { return point; }
        // A display with no brightness input shows every sample, as brightly:
        // so does this, in the colour it is drawn in, or else its clip's.
        // A blank sample at a stroke's end would sit where the stroke's first
        // or last lit one does and draw a dot, so it goes on the way to the
        // next instead, between the samples either side that are drawn.
        if (segment->kind == Kind::draw && !lit(point)) { point = between(composition, index, point); }
        const auto hue = fullHue(point).value_or(segment->hue);
        point.r = hue[0];
        point.g = hue[1];
        point.b = hue[2];
        return point;
    }
    // Where a blank sample of a drawing goes on an XY-only display: as far
    // between the drawn samples either side (a lit one, or the beam's jump)
    // as it is between them in time.
    osci::Point between(const PreparedComposition& composition, std::int64_t index, osci::Point point) {
        constexpr std::int64_t reach = 4;
        // A cycle repeats: before its start is its end, after its end its start.
        const auto drawn = [&](std::int64_t at) -> std::optional<osci::Point> {
            const auto length = planEnd - planFirst;
            if (length <= 0) { return std::nullopt; }
            at = planFirst + ((at - planFirst) % length + length) % length;
            const auto* segment = find(at);
            if (segment == nullptr) { return std::nullopt; }
            const auto sample = evaluateSegment(composition, *segment, at);
            return segment->kind != Kind::draw || lit(sample) ? std::optional(sample) : std::nullopt;
        };
        std::optional<osci::Point> before, after;
        std::int64_t from = index, to = index;
        for (std::int64_t step = 1; step <= reach && !before.has_value(); ++step) { before = drawn(from = index - step); }
        for (std::int64_t step = 1; step <= reach && !after.has_value(); ++step) { after = drawn(to = index + step); }
        find(index);
        if (!before.has_value() || !after.has_value()) { return point; }
        const auto share = static_cast<float>(index - from) / static_cast<float>(to - from);
        point.x = before->x + (after->x - before->x) * share;
        point.y = before->y + (after->y - before->y) * share;
        return point;
    }
    osci::Point evaluateSegment(const PreparedComposition& composition, const Segment& segment, std::int64_t index) const {
        const auto j = index - segment.first;
        switch (segment.kind) {
            case Kind::hold: return segment.from;
            case Kind::move: {
                const auto t = static_cast<float>(static_cast<double>(j + 1) / static_cast<double>(segment.count + 1));
                auto point = segment.from;
                point.x += (segment.to.x - segment.from.x) * t;
                point.y += (segment.to.y - segment.from.y) * t;
                return dark(point);
            }
            case Kind::draw: {
                const auto n = segment.count;
                const auto step = segment.reversed ? n - 1 - j : j;
                const auto* drawing = segment.drawing;
                osci::Point raw;
                if (drawing != nullptr && segment.floored && drawing->coarseTraversalSamples() > 0 && n >= 3) {
                    raw = drawing->sampleTraversal(step, n, segment.world, &segment.cursor);
                } else {
                    const auto span = n > 1 ? 1.0 / static_cast<double>(n - 1) : 1.0;
                    raw = drawing != nullptr ? drawing->sample(static_cast<double>(step) * span, span) : segment.source->sampleFrame(segment.frame, static_cast<double>(step) * span, span);
                }
                // Behind the camera there is no position: hold dark where the
                // layer is entered.
                auto placed = place(composition, *segment.clip, raw, segment.world);
                if (!placed.has_value()) { return dark(segment.reversed ? segment.to : segment.from); }
                if (segment.gain < 1) {
                    // Uncoloured is white, as a clip's colour takes it.
                    const auto uncoloured = placed->r < 0;
                    placed->r = (uncoloured ? 1.0f : placed->r) * segment.gain;
                    placed->g = (uncoloured ? 1.0f : placed->g) * segment.gain;
                    placed->b = (uncoloured ? 1.0f : placed->b) * segment.gain;
                }
                return *placed;
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

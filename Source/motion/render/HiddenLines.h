#pragma once

#include "CompositionRenderer.h"
#include "HiddenLineBake.h"
#include "HiddenLineSolver.h"
#include <thread>

namespace motion::hidden {
// The part of a shape between two points of its progress. The shape itself
// is kept alive by its source.
class ShapePart final : public osci::Shape {
public:
    ShapePart(osci::Shape& whole, std::shared_ptr<const PreparedSource> owner, float from, float to) : shape(&whole), source(std::move(owner)), start(from), end(to) {}
    osci::Point nextVector(float progress) override { return shape->nextVector(start + (end - start) * progress); }
    void scale(float, float, float) override {}
    void translate(float, float, float) override {}
    float length() override {
        if (len < 0) { len = shape->length() * (end - start); }
        return len;
    }
    std::unique_ptr<osci::Shape> clone() override { return std::make_unique<ShapePart>(*this); }
    std::string type() override { return "Part"; }

private:
    osci::Shape* shape;
    std::shared_ptr<const PreparedSource> source;
    float start, end;
};

// Whether anything after a clip's transform can bend its straight lines.
inline bool bends(const PreparedClipStage& stage) {
    return !stage.effects.empty() || !stage.trackEffects.empty() || !stage.compositionEffects.empty()
        || std::any_of(stage.groups.begin(), stage.groups.end(), [](const auto& group) { return !group.effects.empty(); });
}

// One frame with hidden lines removed: every face of every visible mesh hides
// the lines behind it, in every visible clip, from the active camera. Lines
// are traced as chords (one for a straight line nothing bends), each found
// visible in exact intervals by the solver. Point-frame and live sources draw
// whole and hide nothing.
// The shortest visible piece kept, in output units (the frame spans -1 to 1).
inline constexpr double shortestOnScreen = .012;

inline std::unique_ptr<Frame> cull(const PreparedComposition& composition, double time, Solver& solver) {
    auto frame = std::make_unique<Frame>();
    const auto* camera = composition.activeCamera(time);
    if (camera == nullptr || !camera->hidesHiddenLines) { return frame; }
    const auto view = camera->frame(time);
    if (!view.has_value()) { return frame; }
    struct Visible {
        std::size_t index;
        const PreparedClip* clip;
        std::shared_ptr<const PreparedSource> source;
        const PreparedDrawing* drawing;
        bool bent;
    };
    std::vector<Visible> visible;
    for (std::size_t index = 0; index < composition.clips.size(); ++index) {
        const auto& clip = composition.clips[index];
        if (!clip.active(time) || !clip.visible(time) || clip.liveIdentity != nullptr || clip.source == nullptr) { continue; }
        const auto* drawing = clip.source->drawingAt(clip.source->frameIndex(clip.localTime(time)));
        if (drawing == nullptr) { continue; }
        auto bent = bends(clip) || std::any_of(clip.ancestors.begin(), clip.ancestors.end(), [](const auto& stage) { return bends(stage); });
        visible.push_back({index, &clip, clip.source, drawing, bent});
    }
    const auto eye = [&](const PreparedClip& clip, osci::Point point) { return composition.eyePoint(clip.processPoint(point, time), time); };
    solver.clear();
    std::vector<std::optional<Vec3>> corners;
    for (const auto& item : visible) {
        // Each vertex once, then its triangles.
        const auto& mesh = item.drawing->faces();
        if (mesh.empty()) { continue; }
        corners.clear();
        for (const auto& vertex : mesh.vertices) { corners.push_back(eye(*item.clip, vertex)); }
        for (const auto& triangle : mesh.triangles) {
            const auto& a = corners[triangle[0]];
            const auto& b = corners[triangle[1]];
            const auto& c = corners[triangle[2]];
            if (a.has_value() && b.has_value() && c.has_value()) { solver.addTriangle(*a, *b, *c); }
        }
    }
    if (solver.triangleCount() == 0) { return frame; }
    std::vector<Interval> pieces, chord;
    for (const auto& item : visible) {
        const auto count = item.drawing->shapeCount();
        std::vector<std::unique_ptr<osci::Shape>> kept;
        bool lost = false;
        // A line usually starts where the last ended: its end is reused.
        osci::Point lastEnd;
        std::optional<Vec3> lastEye;
        for (std::size_t index = 0; index < count; ++index) {
            auto* shape = item.drawing->shape(index);
            if (shape == nullptr || !(shape->length() > 0)) { continue; }
            const auto line = dynamic_cast<osci::Line*>(shape) != nullptr;
            const auto chords = line && !item.bent ? 1 : 12;
            pieces.clear();
            const auto start = shape->nextVector(0);
            auto from = lastEye.has_value() && start.x == lastEnd.x && start.y == lastEnd.y && start.z == lastEnd.z ? lastEye : eye(*item.clip, start);
            for (int step = 0; step < chords; ++step) {
                const auto to = eye(*item.clip, shape->nextVector(static_cast<float>(step + 1) / chords));
                if (from.has_value() && to.has_value()) {
                    chord.clear();
                    solver.visible(*from, *to, chord);
                    for (const auto& part : chord) {
                        const Interval piece {(step + part.from) / chords, (step + part.to) / chords};
                        // A piece running on from the last chord's joins it.
                        if (!pieces.empty() && piece.from - pieces.back().to < 1.0e-9) {
                            pieces.back().to = piece.to;
                        } else {
                            pieces.push_back(piece);
                        }
                    }
                }
                from = to;
            }
            lastEnd = shape->nextVector(1);
            lastEye = from;
            if (pieces.size() == 1 && pieces.front().from <= 1.0e-9 && pieces.front().to >= 1 - 1.0e-9) {
                kept.push_back(shape->clone());
                continue;
            }
            lost = true;
            // A sliver left by a corner peeking past an edge would only draw a
            // bright dot: pieces shorter than this on screen are dropped.
            const auto onScreen = [&](double progress) -> std::optional<std::pair<double, double>> {
                const auto point = eye(*item.clip, shape->nextVector(static_cast<float>(progress)));
                if (!point.has_value() || point->z <= Solver::nearPlane) { return std::nullopt; }
                return std::pair {point->x / point->z * view->focalLength, point->y / point->z * view->focalLength};
            };
            for (const auto& piece : pieces) {
                const auto a0 = onScreen(piece.from), a1 = onScreen((piece.from + piece.to) * .5), a2 = onScreen(piece.to);
                if (a0.has_value() && a1.has_value() && a2.has_value()
                    && std::hypot(a1->first - a0->first, a1->second - a0->second) + std::hypot(a2->first - a1->first, a2->second - a1->second) < shortestOnScreen) { continue; }
                const auto a = static_cast<float>(piece.from), b = static_cast<float>(piece.to);
                if (line) {
                    kept.push_back(std::make_unique<osci::Line>(shape->nextVector(a), shape->nextVector(b)));
                } else {
                    kept.push_back(std::make_unique<ShapePart>(*shape, item.source, a, b));
                }
            }
        }
        if (lost) { frame->drawings.emplace_back(item.index, std::make_shared<const PreparedDrawing>(std::move(kept))); }
    }
    return frame;
}

// Bakes a composition's hidden lines on worker threads, each with its own
// prepared copy of the project (a prepared composition is rendered by one
// thread at a time), starting at a frame and spreading out from it.
class Baker {
public:
    Baker() = default;
    ~Baker() { stop(); }

    // Replaces any bake in progress.
    void start(std::shared_ptr<Bake> bake, double sampleRate, std::size_t firstFrame) {
        stop();
        if (bake == nullptr || bake->project == nullptr || bake->complete()) { return; }
        cancel = std::make_shared<std::atomic<bool>>(false);
        const auto threads = std::clamp<unsigned>(std::thread::hardware_concurrency() / 2, 1u, 4u);
        for (unsigned index = 0; index < threads; ++index) {
            workers.emplace_back([bake, sampleRate, firstFrame, flag = cancel] { run(*bake, sampleRate, firstFrame, *flag); });
        }
    }
    void stop() {
        if (cancel != nullptr) { cancel->store(true); }
        for (auto& worker : workers) { worker.join(); }
        workers.clear();
    }

    // Every frame, on this thread and helpers, before an export renders.
    static void bakeAll(Bake& bake, double sampleRate, const std::atomic<bool>& cancel) {
        const auto threads = std::clamp<unsigned>(std::thread::hardware_concurrency(), 1u, 8u);
        std::vector<std::thread> helpers;
        for (unsigned index = 1; index < threads; ++index) { helpers.emplace_back([&] { run(bake, sampleRate, 0, cancel); }); }
        run(bake, sampleRate, 0, cancel);
        for (auto& helper : helpers) { helper.join(); }
    }

private:
    static void run(Bake& bake, double sampleRate, std::size_t firstFrame, const std::atomic<bool>& cancel) {
        if (cancel.load()) { return; }
        std::unique_ptr<PreparedComposition> composition;
        try {
            composition = std::make_unique<PreparedComposition>(*bake.project, sampleRate, &cancel);
        } catch (...) {
            return;
        }
        Solver solver;
        while (!cancel.load(std::memory_order_relaxed)) {
            const auto frame = bake.claim(firstFrame);
            if (!frame.has_value()) { return; }
            try {
                bake.publish(*frame, cull(*composition, bake.timeOf(*frame), solver));
            } catch (const std::bad_alloc&) {
                return;
            }
        }
    }

    std::shared_ptr<std::atomic<bool>> cancel;
    std::vector<std::thread> workers;
};
}

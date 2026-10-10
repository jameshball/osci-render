#pragma once

#include "CompositionRenderer.h"
#include "HiddenLineBake.h"
#include "HiddenLineCoverage.h"
#include "HiddenLineCrossings.h"
#include "HiddenLineDepth.h"
#include "HiddenLineFingerprint.h"
#include "HiddenLineDicing.h"
#include "../model/Vec3.h"
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

// A line whose ends keep their own colours: a run of a point source.
class ColouredLine final : public osci::Shape {
public:
    ColouredLine(osci::Point from, osci::Point to) : a(from), b(to) {}
    osci::Point nextVector(float progress) override {
        const auto mix = [progress](float x, float y) { return x + (y - x) * progress; };
        const auto coloured = a.r >= 0 && b.r >= 0;
        return {mix(a.x, b.x), mix(a.y, b.y), mix(a.z, b.z), coloured ? mix(a.r, b.r) : -1.0f, coloured ? mix(a.g, b.g) : -1.0f, coloured ? mix(a.b, b.b) : -1.0f};
    }
    void scale(float, float, float) override {}
    void translate(float, float, float) override {}
    float length() override {
        if (len < 0) { len = std::hypot(b.x - a.x, b.y - a.y, b.z - a.z); }
        return len;
    }
    std::unique_ptr<osci::Shape> clone() override { return std::make_unique<ColouredLine>(*this); }
    std::string type() override { return "Line"; }

private:
    osci::Point a, b;
};

// Whether an effect can bend straight lines: one that only colours points,
// or moves them all alike (turning, scaling, sliding or skewing), keeps a
// line straight and a face flat, so its faces need no dicing.
inline bool bends(const PreparedEffect& effect) {
    if (!effect.enabled) { return false; }
    switch (effect.kind) {
        case EffectKind::colour:
        case EffectKind::rotate:
        case EffectKind::scale:
        case EffectKind::translate:
        case EffectKind::skew:
            return false;
        default:
            return true;
    }
}
inline bool bends(const std::vector<PreparedEffect>& effects) {
    return std::any_of(effects.begin(), effects.end(), [](const PreparedEffect& effect) { return bends(effect); });
}

// Whether anything after a clip's transform can bend its straight lines.
inline bool bends(const PreparedClipStage& stage) {
    return bends(stage.effects) || bends(stage.trackEffects) || bends(stage.compositionEffects)
        || std::any_of(stage.groups.begin(), stage.groups.end(), [](const auto& group) { return bends(group.effects); });
}

// The shortest run that culling leaves kept, in output units (the frame
// spans -1 to 1): anything shorter would only draw a dot.
inline constexpr double shortestOnScreen = .012;
// Lines are tested every half pixel along their true path.
inline constexpr double testStep = DepthBuffer::pixel;
// A piece this short at the end of a cut run (two pixels) is too small to
// see but would cost the beam a sample of its own, so it is trimmed.
inline constexpr double sliver = 2 * DepthBuffer::pixel;
// Nothing nearer the eye than this is drawn or hides anything.
inline constexpr double nearPlane = .05;

// A crossing lies on both its surfaces, so one on a triangle nothing can be
// seen of never shows. Small triangles (every diced piece) none of whose
// corners, edge middles or middle shows are dropped before crossings are
// looked for; larger ones are kept, as a point could show between those.
inline void dropUnseen(std::vector<crossing::Triangle>& triangles, std::vector<std::array<osci::Point, 3>>& sources, const DepthBuffer& depth, double focalLength) {
    constexpr double small = 3 * DepthBuffer::pixel;
    std::size_t kept = 0;
    for (std::size_t index = 0; index < triangles.size(); ++index) {
        const auto& eye = triangles[index].eye;
        const auto at = [&](Vec3 point) { return Screen {point.x / point.z * focalLength, point.y / point.z * focalLength, 1 / point.z}; };
        const std::array<Screen, 3> corners {at(eye[0]), at(eye[1]), at(eye[2])};
        const auto reach = std::max({std::abs(corners[1].x - corners[0].x), std::abs(corners[2].x - corners[0].x), std::abs(corners[1].y - corners[0].y), std::abs(corners[2].y - corners[0].y)});
        bool seen = reach > small;
        const std::array<Vec3, 7> probes {eye[0], eye[1], eye[2], (eye[0] + eye[1]) * .5, (eye[1] + eye[2]) * .5, (eye[2] + eye[0]) * .5, (eye[0] + eye[1] + eye[2]) / 3};
        for (std::size_t probe = 0; probe < probes.size() && !seen; ++probe) { seen = depth.shows(at(probes[probe])); }
        if (!seen) { continue; }
        triangles[kept] = triangles[index];
        sources[kept] = sources[index];
        ++kept;
    }
    triangles.resize(kept);
    sources.resize(kept);
}

// What culling works in, kept from frame to frame so a worker's buffers are
// allocated once rather than every frame: the depth buffer, what has been
// drawn, the surfaces gathered for crossings, and the scratch lists of points
// and runs. One per thread.
struct Workspace {
    DepthBuffer depth;
    Coverage coverage;
    std::vector<std::optional<Screen>> samples;
    std::vector<Screen> drawn;
    std::vector<std::size_t> drawnStarts;
    std::vector<crossing::Triangle> triangles;
    std::vector<std::array<osci::Point, 3>> sources;
    std::vector<std::optional<Vec3>> corners, eyes;
    std::vector<std::pair<double, double>> runs;
    Dicer dicer;
};

// One frame with hidden lines removed, as the active camera sees it.
//
// Point by point: surfaces that hide (meshes' faces, and the fills of flat
// drawings: SVG, text, Lottie) are drawn into a depth buffer over the frame;
// lines are stepped along their true path every half pixel and kept where
// nothing is in front of them. Every point goes through its clip's whole chain
// of transforms and effects, so whatever bends the drawing, however sharply,
// bends what is tested: a face something bends is diced until its pieces are
// a couple of pixels across after the bend. A face or line nothing bends is
// exact through the camera, so a face is drawn whole and a line interpolated.
// Point sources hide nothing; their points are joined and tested like lines.
// Where two surfaces cut through each other, the line they cross along is an
// edge of what is seen that neither drawing has: it is added, and tested too.
// The drawing keeps its own shapes, cut where they hide; hidden runs are
// dropped, not darkened, so the beam skips straight to the next visible part.
// None when `cancel` is set partway.
inline std::unique_ptr<Frame> cull(const PreparedComposition& composition, double time, Workspace& workspace, const std::atomic<bool>* cancel = nullptr) {
    auto& depth = workspace.depth;
    const auto cancelled = [cancel] { return cancel != nullptr && cancel->load(std::memory_order_relaxed); };
    auto frame = std::make_unique<Frame>();
    const auto* camera = composition.activeCamera(time);
    if (camera == nullptr || !camera->hidesHiddenLines) { return frame; }
    const auto view = camera->frame(time);
    if (!view.has_value()) { return frame; }
    struct Item {
        std::size_t index;
        const PreparedClip* clip;
        std::shared_ptr<const PreparedSource> source;
        const PreparedDrawing* drawing;
        const PreparedPointFrames* points;
        std::size_t frame;
        bool bent;
    };
    std::vector<Item> items;
    for (std::size_t index = 0; index < composition.clips.size(); ++index) {
        const auto& clip = composition.clips[index];
        if (!clip.active(time) || !clip.visible(time) || clip.liveIdentity != nullptr || clip.source == nullptr) { continue; }
        const auto sourceFrame = clip.source->frameIndex(clip.localTime(time));
        const auto* drawing = clip.source->drawingAt(sourceFrame);
        const auto* points = clip.source->pointFrames();
        if (drawing == nullptr && points == nullptr) { continue; }
        const auto bent = bends(clip) || std::any_of(clip.ancestors.begin(), clip.ancestors.end(), [](const auto& stage) { return bends(stage); });
        items.push_back({index, &clip, clip.source, drawing, points, sourceFrame, bent});
    }
    const auto eye = [&](const PreparedClip& clip, osci::Point point) { return composition.eyePoint(clip.processPoint(point, time), time); };
    const auto screen = [&](const std::optional<Vec3>& point) -> std::optional<Screen> {
        if (!point.has_value() || !(point->z > nearPlane)) { return std::nullopt; }
        return Screen {point->x / point->z * view->focalLength, point->y / point->z * view->focalLength, 1 / point->z};
    };

    // What hides, over what the Scope shows. Its triangles are kept too when
    // two or more surfaces could cut through each other.
    const auto reach = composition.hiddenLineReach;
    depth.cover(reach);
    workspace.dicer.setReach(reach);
    const auto surfaces = std::count_if(items.begin(), items.end(), [](const Item& item) { return item.drawing != nullptr && !item.drawing->faces().empty(); });
    auto& surfaceTriangles = workspace.triangles;
    auto& surfaceSources = workspace.sources;
    surfaceTriangles.clear();
    surfaceSources.clear();
    const auto unproject = [&](const Screen& at) { return Vec3 {at.x / at.w / view->focalLength, at.y / at.w / view->focalLength, 1 / at.w}; };
    auto& corners = workspace.corners;
    for (std::uint32_t surface = 0; surface < items.size(); ++surface) {
        const auto& item = items[surface];
        if (item.drawing == nullptr || item.drawing->faces().empty()) { continue; }
        const auto& mesh = item.drawing->faces();
        corners.clear();
        for (const auto& vertex : mesh.vertices) { corners.push_back(eye(*item.clip, vertex)); }
        for (const auto& triangle : mesh.triangles) {
            if (cancelled()) { return nullptr; }
            const auto& a = corners[triangle[0]];
            const auto& b = corners[triangle[1]];
            const auto& c = corners[triangle[2]];
            if (!item.bent) {
                // Exact through the camera: whole, cut at the near plane.
                if (!a.has_value() || !b.has_value() || !c.has_value()) { continue; }
                // Cut a hair in front of the near plane, so every corner kept
                // (the cut's own, rounding included) is in front of it.
                constexpr auto cut = nearPlane * (1 + 1.0e-6);
                // What each corner kept was before the chain, to place what
                // crosses it: nothing bends the face, so a cut point is as far
                // along its edge before it as after.
                std::array<Vec3, 4> kept {};
                std::array<osci::Point, 4> keptSources {};
                std::size_t count = 0;
                const std::array<Vec3, 3> around {*a, *b, *c};
                const std::array<osci::Point, 3> sources {mesh.vertices[triangle[0]], mesh.vertices[triangle[1]], mesh.vertices[triangle[2]]};
                for (std::size_t index = 0; index < 3; ++index) {
                    const auto& current = around[index];
                    const auto& next = around[(index + 1) % 3];
                    if (current.z > cut) {
                        keptSources[count] = sources[index];
                        kept[count++] = current;
                    }
                    if ((current.z > cut) != (next.z > cut)) {
                        const auto along = (cut - current.z) / (next.z - current.z);
                        const auto& from = sources[index];
                        const auto& to = sources[(index + 1) % 3];
                        keptSources[count] = osci::Point(static_cast<float>(from.x + (to.x - from.x) * along), static_cast<float>(from.y + (to.y - from.y) * along), static_cast<float>(from.z + (to.z - from.z) * along));
                        kept[count++] = current + (next - current) * along;
                    }
                }
                std::array<Screen, 4> at {};
                bool seen = count >= 3;
                for (std::size_t index = 0; index < count && seen; ++index) {
                    const auto point = screen(kept[index]);
                    seen = point.has_value();
                    if (seen) { at[index] = *point; }
                }
                if (seen) { depth.triangle(at[0], at[1], at[2]); }
                if (seen && count == 4) { depth.triangle(at[0], at[2], at[3]); }
                // Crossings are found in front of the eye, so a face cut at
                // the near plane joins the search as the pieces kept: a floor
                // reaching back past the eye still meets what stands on it.
                if (surfaces > 1 && seen) {
                    for (std::size_t piece = 2; piece < count; ++piece) {
                        surfaceTriangles.push_back({{kept[0], kept[piece - 1], kept[piece]}, surface});
                        surfaceSources.push_back({keptSources[0], keptSources[piece - 1], keptSources[piece]});
                    }
                }
                continue;
            }
            // Diced point by point into pieces flat enough, once bent, to draw.
            workspace.dicer.dice({mesh.vertices[triangle[0]], screen(a)}, {mesh.vertices[triangle[1]], screen(b)}, {mesh.vertices[triangle[2]], screen(c)},
                [&](const osci::Point& point) { return screen(eye(*item.clip, point)); },
                [&](const DiceCorner& p, const DiceCorner& q, const DiceCorner& r, bool whole) {
                    depth.triangle(*p.at, *q.at, *r.at);
                    // Crossings are only found in front of the eye: a piece
                    // cut at the near plane reaches far off the screen.
                    if (surfaces > 1 && whole) {
                        surfaceTriangles.push_back({{unproject(*p.at), unproject(*q.at), unproject(*r.at)}, surface});
                        surfaceSources.push_back({p.source, q.source, r.source});
                    }
                });
        }
    }
    if (depth.isEmpty()) { return frame; }
    dropUnseen(surfaceTriangles, surfaceSources, depth, view->focalLength);
    const auto crossings = crossing::runs(surfaceTriangles, view->focalLength, reach, cancel);
    if (cancelled()) { return nullptr; }

    // What is hidden, and what would be drawn twice: every line, every pixel
    // along its true path. A point shows where nothing is in front of it and
    // no other clip's line already drawn runs along it. `place(t)` is a point
    // of a run at progress t; the parts that show come back as intervals of
    // t, and are kept as drawn until `commitDrawn`.
    auto& runs = workspace.runs;
    auto& coverage = workspace.coverage;
    auto& samples = workspace.samples;
    auto& drawn = workspace.drawn;
    auto& drawnStarts = workspace.drawnStarts;
    coverage.clear(reach);
    drawn.clear();
    drawnStarts.clear();
    // Only worth recording while a later clip, or a crossing, could retrace it.
    bool recording = false;
    const auto visibleRuns = [&](auto&& place, double length) {
        runs.clear();
        const auto steps = std::clamp(static_cast<int>(std::ceil(length / testStep)), 1, 65536);
        samples.clear();
        for (int step = 0; step <= steps; ++step) { samples.push_back(place(static_cast<double>(step) / steps)); }
        const auto showing = [&](int step) {
            const auto& point = samples[static_cast<std::size_t>(step)];
            if (!point.has_value() || !depth.shows(*point)) { return false; }
            const auto& before = samples[static_cast<std::size_t>(std::max(0, step - 1))];
            const auto& after = samples[static_cast<std::size_t>(std::min(steps, step + 1))];
            return !before.has_value() || !after.has_value() || !coverage.covers(*point, after->x - before->x, after->y - before->y);
        };
        bool open = false;
        double from = 0, previous = 0;
        for (int step = 0; step <= steps; ++step) {
            const auto t = static_cast<double>(step) / steps;
            const auto shows = showing(step);
            if (shows && !open) {
                from = step == 0 ? 0.0 : (previous + t) * .5;
                open = true;
            } else if (!shows && open) {
                runs.emplace_back(from, (previous + t) * .5);
                open = false;
            }
            previous = t;
        }
        if (open) { runs.emplace_back(from, 1.0); }
        if (!recording) { return; }
        for (const auto& [a, b] : runs) {
            drawnStarts.push_back(drawn.size());
            for (int step = 0; step <= steps; ++step) {
                const auto t = static_cast<double>(step) / steps;
                const auto& point = samples[static_cast<std::size_t>(step)];
                if (t >= a && t <= b && point.has_value()) { drawn.push_back(*point); }
            }
        }
    };
    const auto commitDrawn = [&] {
        drawnStarts.push_back(drawn.size());
        for (std::size_t index = 0; index + 1 < drawnStarts.size(); ++index) { coverage.add(std::span<const Screen>(drawn).subspan(drawnStarts[index], drawnStarts[index + 1] - drawnStarts[index])); }
        drawn.clear();
        drawnStarts.clear();
    };
    struct Kept {
        std::unique_ptr<osci::Shape> shape;
        bool cut;
    };
    // A run culling cut too short to see would only draw a dot: it is
    // dropped. Runs culling left whole stay as they are. The beam spends a
    // sample on every piece, so in a cut run slivers at its ends are trimmed
    // and straight pieces that bend by less than a pixel are joined:
    // otherwise it lingers on them and draws a bead.
    const auto onScreen = [&](osci::Shape& shape, const PreparedClip& clip) {
        double length = 0;
        std::optional<Screen> previous;
        for (int probe = 0; probe <= 4; ++probe) {
            const auto next = screen(eye(clip, shape.nextVector(probe / 4.0f)));
            if (previous.has_value() && next.has_value()) { length += std::hypot(next->x - previous->x, next->y - previous->y); }
            previous = next;
        }
        return length;
    };
    // Two straight pieces drawn as one when where they meet is within a pixel
    // of the line joining their far ends, on screen.
    const auto joinable = [&](osci::Shape& a, osci::Shape& b, const PreparedClip& clip) {
        if (dynamic_cast<osci::Line*>(&a) == nullptr || dynamic_cast<osci::Line*>(&b) == nullptr) { return false; }
        const auto p = screen(eye(clip, a.nextVector(0))), q = screen(eye(clip, a.nextVector(1))), r = screen(eye(clip, b.nextVector(1)));
        if (!p.has_value() || !q.has_value() || !r.has_value()) { return false; }
        const auto dx = r->x - p->x, dy = r->y - p->y;
        const auto span = dx * dx + dy * dy;
        const auto t = span > 0 ? std::clamp(((q->x - p->x) * dx + (q->y - p->y) * dy) / span, 0.0, 1.0) : 0.0;
        return std::hypot(q->x - (p->x + dx * t), q->y - (p->y + dy * t)) <= DepthBuffer::pixel;
    };
    const auto keep = [&](std::vector<Kept>& pieces, const PreparedClip& clip) {
        std::vector<std::unique_ptr<osci::Shape>> shapes;
        std::size_t first = 0;
        while (first < pieces.size()) {
            auto last = first + 1;
            while (last < pieces.size()) {
                const auto a = pieces[last - 1].shape->nextVector(1), b = pieces[last].shape->nextVector(0);
                if (std::abs(a.x - b.x) > 1.0e-6f || std::abs(a.y - b.y) > 1.0e-6f || std::abs(a.z - b.z) > 1.0e-6f) { break; }
                ++last;
            }
            std::vector<Kept> run;
            for (auto index = first; index < last; ++index) { run.push_back(std::move(pieces[index])); }
            first = last;
            const auto cut = std::any_of(run.begin(), run.end(), [](const auto& piece) { return piece.cut; });
            if (!cut) {
                for (auto& piece : run) { shapes.push_back(std::move(piece.shape)); }
                continue;
            }
            while (run.size() > 1 && onScreen(*run.front().shape, clip) < sliver) { run.erase(run.begin()); }
            while (run.size() > 1 && onScreen(*run.back().shape, clip) < sliver) { run.pop_back(); }
            std::vector<std::unique_ptr<osci::Shape>> joined;
            for (auto& piece : run) {
                if (!joined.empty() && joinable(*joined.back(), *piece.shape, clip)) {
                    joined.back() = std::make_unique<osci::Line>(joined.back()->nextVector(0), piece.shape->nextVector(1));
                } else {
                    joined.push_back(std::move(piece.shape));
                }
            }
            double length = 0;
            for (auto& shape : joined) { length += onScreen(*shape, clip); }
            if (length >= shortestOnScreen) {
                for (auto& shape : joined) { shapes.push_back(std::move(shape)); }
            }
        }
        return std::make_shared<const PreparedDrawing>(std::move(shapes), true);
    };
    const auto mix = [](const osci::Point& a, const osci::Point& b, double t) {
        const auto f = static_cast<float>(t);
        return osci::Point(a.x + (b.x - a.x) * f, a.y + (b.y - a.y) * f, a.z + (b.z - a.z) * f, a.r + (b.r - a.r) * f, a.g + (b.g - a.g) * f, a.b + (b.b - a.b) * f);
    };
    auto& eyes = workspace.eyes;
    // Every clip's own lines first, then where surfaces cut through each
    // other: a crossing along an edge already drawn is the one left out.
    std::vector<std::vector<Kept>> keptBy(items.size());
    std::vector<char> lostBy(items.size(), 0);
    for (std::uint32_t surface = 0; surface < items.size(); ++surface) {
        const auto& item = items[surface];
        if (cancelled()) { return nullptr; }
        auto& kept = keptBy[surface];
        bool lost = false;
        recording = surface + 1 < items.size() || !crossings.empty();
        if (item.drawing == nullptr) {
            // A point source: the line from each point to the next, round the
            // frame; a dark point lifts the pen.
            const auto& data = item.points->data();
            const auto count = item.points->pointsPerFrame();
            const auto offset = item.frame * count;
            if (offset + count > data.size()) { continue; }
            const auto pointAt = [&](std::size_t index) {
                const auto& sample = data[offset + index % count];
                return osci::Point(sample.x, sample.y, sample.z, sample.r, sample.g, sample.b);
            };
            const auto dark = [](const osci::Point& point) { return point.r == 0 && point.g == 0 && point.b == 0; };
            eyes.clear();
            for (std::size_t index = 0; index < count; ++index) { eyes.push_back(eye(*item.clip, pointAt(index))); }
            double whole = 0;
            for (std::size_t index = 0; index < count; ++index) {
                const auto a = pointAt(index), b = pointAt(index + 1);
                if (dark(a) || dark(b) || (a.x == b.x && a.y == b.y && a.z == b.z)) { continue; }
                whole += std::hypot(b.x - a.x, b.y - a.y, b.z - a.z);
                const auto& ea = eyes[index];
                const auto& eb = eyes[(index + 1) % count];
                const auto sa = screen(ea), sb = screen(eb);
                const auto length = sa.has_value() && sb.has_value() ? std::hypot(sb->x - sa->x, sb->y - sa->y) : 1.0;
                visibleRuns([&](double t) {
                    if (item.bent) { return screen(eye(*item.clip, mix(a, b, t))); }
                    return ea.has_value() && eb.has_value() ? screen(*ea + (*eb - *ea) * t) : std::optional<Screen>();
                }, length);
                if (runs.size() == 1 && runs.front().first <= 0 && runs.front().second >= 1) {
                    kept.push_back({std::make_unique<ColouredLine>(a, b), false});
                    continue;
                }
                lost = true;
                for (const auto& [from, to] : runs) { kept.push_back({std::make_unique<ColouredLine>(mix(a, b, from), mix(a, b, to)), true}); }
            }
            commitDrawn();
            if (lost) {
                auto shown = keep(kept, *item.clip);
                const auto fraction = whole > 0 ? shown->length() / whole : 0.0;
                frame->drawings.push_back({item.index, std::move(shown), fraction});
            }
            continue;
        }
        // A line usually starts where the last ended: its end is reused.
        osci::Point lastEnd;
        std::optional<Vec3> lastEye;
        for (std::size_t index = 0; index < item.drawing->shapeCount(); ++index) {
            auto* shape = item.drawing->shape(index);
            if (shape == nullptr || !(shape->length() > 0)) { continue; }
            const auto line = dynamic_cast<osci::Line*>(shape) != nullptr;
            const auto start = shape->nextVector(0), end = shape->nextVector(1);
            const auto from = lastEye.has_value() && start.x == lastEnd.x && start.y == lastEnd.y && start.z == lastEnd.z ? lastEye : eye(*item.clip, start);
            const auto to = eye(*item.clip, end);
            lastEnd = end;
            lastEye = to;
            if (line && !item.bent) {
                // Straight through the camera: interpolated exactly.
                const auto sa = screen(from), sb = screen(to);
                const auto length = sa.has_value() && sb.has_value() ? std::hypot(sb->x - sa->x, sb->y - sa->y) : 2 * (reach.x + reach.y);
                visibleRuns([&](double t) { return from.has_value() && to.has_value() ? screen(*from + (*to - *from) * t) : std::optional<Screen>(); }, length);
            } else {
                // Along its true path: its length on screen, from a few points
                // (generously: it only sets how finely the path is walked).
                double length = 0;
                auto previous = screen(from);
                for (int probe = 1; probe <= 8; ++probe) {
                    const auto next = screen(eye(*item.clip, shape->nextVector(probe / 8.0f)));
                    if (previous.has_value() && next.has_value()) { length += std::hypot(next->x - previous->x, next->y - previous->y); }
                    previous = next;
                }
                length *= 1.25;
                visibleRuns([&](double t) { return screen(eye(*item.clip, shape->nextVector(static_cast<float>(t)))); }, length);
            }
            if (runs.size() == 1 && runs.front().first <= 0 && runs.front().second >= 1) {
                kept.push_back({shape->clone(), false});
                continue;
            }
            lost = true;
            for (const auto& [a, b] : runs) {
                if (line) {
                    kept.push_back({std::make_unique<osci::Line>(shape->nextVector(static_cast<float>(a)), shape->nextVector(static_cast<float>(b))), true});
                } else {
                    kept.push_back({std::make_unique<ShapePart>(*shape, item.source, static_cast<float>(a), static_cast<float>(b)), true});
                }
            }
        }
        // A clip's path goes back over its own lines on purpose (the beam
        // retraces an edge rather than jump), so only other clips' lines can
        // cover it: what it draws counts once it is whole.
        commitDrawn();
        lostBy[surface] = lost;
    }
    // Where surfaces cut through each other: straight on the pieces they were
    // drawn from, so interpolated between their ends.
    recording = true;
    for (const auto& run : crossings) {
        if (cancelled()) { return nullptr; }
        const auto surface = surfaceTriangles[run.front().triangle].surface;
        auto& kept = keptBy[surface];
        for (const auto& piece : run) {
            const auto sa = screen(piece.eyeFrom), sb = screen(piece.eyeTo);
            if (!sa.has_value() || !sb.has_value()) { continue; }
            const auto& corners = surfaceSources[piece.triangle];
            const auto source = [&](const crossing::Weights& weights) {
                const auto mixed = [&](float osci::Point::* axis) { return static_cast<float>(corners[0].*axis * weights[0] + corners[1].*axis * weights[1] + corners[2].*axis * weights[2]); };
                return osci::Point(mixed(&osci::Point::x), mixed(&osci::Point::y), mixed(&osci::Point::z));
            };
            const auto from = source(piece.from), to = source(piece.to);
            visibleRuns([&](double t) { return screen(piece.eyeFrom + (piece.eyeTo - piece.eyeFrom) * t); }, std::hypot(sb->x - sa->x, sb->y - sa->y));
            commitDrawn();
            for (const auto& [a, b] : runs) {
                lostBy[surface] = 1;
                kept.push_back({std::make_unique<osci::Line>(mix(from, to, a), mix(from, to, b)), true});
            }
        }
    }
    for (std::uint32_t surface = 0; surface < items.size(); ++surface) {
        if (lostBy[surface] != 0) { frame->drawings.push_back({items[surface].index, keep(keptBy[surface], *items[surface].clip)}); }
    }
    return frame;
}

// Bakes a composition's hidden lines on worker threads, each with its own
// prepared copy of the project (a prepared composition is rendered by one
// thread at a time), starting at a frame and spreading out from it.
class Baker {
public:
    Baker() = default;
    ~Baker() {
        stop();
        for (auto& worker : retired) { worker.thread->waitForThreadToExit(-1); }
    }

    // Replaces any bake in progress. Its workers run at low priority, so they
    // can use every core but two without the editor, the Scope or the audio
    // waiting on them.
    void start(std::shared_ptr<Bake> bake, double sampleRate, std::size_t firstFrame) {
        stop();
        if (bake == nullptr || bake->project == nullptr || bake->complete()) { return; }
        cancel = std::make_shared<std::atomic<bool>>(false);
        const auto cores = std::thread::hardware_concurrency();
        const auto threads = std::clamp<unsigned>(cores > 2 ? cores - 2 : 1u, 1u, 8u);
        for (unsigned index = 0; index < threads; ++index) {
            auto done = std::make_shared<std::atomic<bool>>(false);
            auto thread = std::make_unique<WorkerThread>([bake, sampleRate, firstFrame, flag = cancel, done] {
                run(*bake, sampleRate, firstFrame, *flag);
                done->store(true);
            });
            thread->startThread(juce::Thread::Priority::low);
            workers.push_back({std::move(thread), done});
        }
    }
    // Stops the bake without waiting: its workers finish the step they are
    // on and are collected later, so an edit never waits for a frame.
    void stop() {
        if (cancel != nullptr) { cancel->store(true); }
        for (auto& worker : workers) { retired.push_back(std::move(worker)); }
        workers.clear();
        std::erase_if(retired, [](Worker& worker) {
            if (!worker.done->load()) { return false; }
            worker.thread->waitForThreadToExit(-1);
            return true;
        });
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
    class WorkerThread final : public juce::Thread {
    public:
        explicit WorkerThread(std::function<void()> work) : juce::Thread("Hidden line bake"), body(std::move(work)) {}
        void run() override { body(); }

    private:
        std::function<void()> body;
    };
    struct Worker {
        std::unique_ptr<WorkerThread> thread;
        std::shared_ptr<std::atomic<bool>> done;
    };

    static void run(Bake& bake, double sampleRate, std::size_t firstFrame, const std::atomic<bool>& cancel) {
        if (cancel.load()) { return; }
        std::unique_ptr<PreparedComposition> composition;
        try {
            composition = std::make_unique<PreparedComposition>(*bake.project, sampleRate, &cancel, CompositionPurpose::signal, bake.reach);
        } catch (...) {
            return;
        }
        auto workspace = std::make_unique<Workspace>();
        // The last frame this worker culled and what it depended on: a frame
        // that depends on just the same is the same, and is copied.
        Fingerprint lastPrint, print;
        std::unique_ptr<Frame> last;
        while (!cancel.load(std::memory_order_relaxed)) {
            const auto frame = bake.claim(firstFrame);
            if (!frame.has_value()) { return; }
            try {
                const auto time = bake.timeOf(*frame);
                print.take(*composition, time);
                if (last != nullptr && print == lastPrint) {
                    bake.publish(*frame, std::make_unique<Frame>(*last));
                    continue;
                }
                auto result = cull(*composition, time, *workspace, &cancel);
                if (result == nullptr) { return; }
                last = std::make_unique<Frame>(*result);
                std::swap(lastPrint, print);
                bake.publish(*frame, std::move(result));
            } catch (const std::bad_alloc&) {
                return;
            }
        }
    }

    std::shared_ptr<std::atomic<bool>> cancel;
    std::vector<Worker> workers, retired;
};
}

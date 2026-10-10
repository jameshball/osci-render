#pragma once

#include "CompositionRenderer.h"
#include "HiddenLineBake.h"
#include "HiddenLineDepth.h"
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

// Whether anything after a clip's transform can bend its straight lines.
inline bool bends(const PreparedClipStage& stage) {
    return !stage.effects.empty() || !stage.trackEffects.empty() || !stage.compositionEffects.empty()
        || std::any_of(stage.groups.begin(), stage.groups.end(), [](const auto& group) { return !group.effects.empty(); });
}

// The shortest run that culling leaves kept, in output units (the frame
// spans -1 to 1): anything shorter would only draw a dot.
inline constexpr double shortestOnScreen = .012;
// Bent surfaces are diced until each piece is about this many pixels across
// on screen, judged after every effect, point by point.
inline constexpr double diceCell = 4;
// Lines are tested every half pixel along their true path.
inline constexpr double testStep = DepthBuffer::pixel;
// Nothing nearer the eye than this is drawn or hides anything.
inline constexpr double nearPlane = .05;

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
// The drawing keeps its own shapes, cut where they hide; hidden runs are
// dropped, not darkened, so the beam skips straight to the next visible part.
// None when `cancel` is set partway.
inline std::unique_ptr<Frame> cull(const PreparedComposition& composition, double time, DepthBuffer& depth, const std::atomic<bool>* cancel = nullptr) {
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
    const auto pixels = [](const Screen& a, const Screen& b) { return std::hypot(b.x - a.x, b.y - a.y) / DepthBuffer::pixel; };

    // What hides.
    depth.clear();
    std::vector<std::optional<Vec3>> corners;
    for (const auto& item : items) {
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
                std::array<Vec3, 4> kept {};
                std::size_t count = 0;
                const std::array<Vec3, 3> around {*a, *b, *c};
                for (std::size_t index = 0; index < 3; ++index) {
                    const auto& current = around[index];
                    const auto& next = around[(index + 1) % 3];
                    if (current.z > nearPlane) { kept[count++] = current; }
                    if ((current.z > nearPlane) != (next.z > nearPlane)) { kept[count++] = current + (next - current) * ((nearPlane - current.z) / (next.z - current.z)); }
                }
                const auto at = [&](std::size_t index) { return *screen(kept[index]); };
                if (count >= 3) { depth.triangle(at(0), at(1), at(2)); }
                if (count == 4) { depth.triangle(at(0), at(2), at(3)); }
                continue;
            }
            // Diced, point by point: halved across its longest edge, through
            // a new point bent like any other, until no edge is longer than
            // diceCell on screen. Pieces wholly off the frame hide nothing on
            // it and are dropped.
            struct Corner { osci::Point source; std::optional<Screen> at; };
            const auto reach = DepthBuffer::extent;
            const auto off = [reach](const Corner& p, const Corner& q, const Corner& r) {
                if (!p.at.has_value() && !q.at.has_value() && !r.at.has_value()) { return true; }
                if (!p.at.has_value() || !q.at.has_value() || !r.at.has_value()) { return false; }
                const auto past = [&](auto&& test) { return test(*p.at) && test(*q.at) && test(*r.at); };
                return past([reach](const Screen& s) { return s.x < -reach; }) || past([reach](const Screen& s) { return s.x > reach; })
                    || past([reach](const Screen& s) { return s.y < -reach; }) || past([reach](const Screen& s) { return s.y > reach; });
            };
            const auto split = [&](auto&& self, const Corner& p, const Corner& q, const Corner& r, int depthLeft) -> void {
                if (off(p, q, r)) { return; }
                const auto length = [&](const Corner& u, const Corner& v) {
                    return u.at.has_value() && v.at.has_value() ? pixels(*u.at, *v.at) : std::numeric_limits<double>::infinity();
                };
                const auto pq = length(p, q), qr = length(q, r), rp = length(r, p);
                // Behind the eye, a piece is split only so far.
                if (!p.at.has_value() || !q.at.has_value() || !r.at.has_value()) { depthLeft = std::min(depthLeft, 12); }
                if (std::max({pq, qr, rp}) <= diceCell || depthLeft == 0) {
                    if (p.at.has_value() && q.at.has_value() && r.at.has_value()) { depth.triangle(*p.at, *q.at, *r.at); }
                    return;
                }
                const auto halve = [&](const Corner& u, const Corner& v) {
                    const auto middle = osci::Point((u.source.x + v.source.x) * .5f, (u.source.y + v.source.y) * .5f, (u.source.z + v.source.z) * .5f);
                    return Corner {middle, screen(eye(*item.clip, middle))};
                };
                if (pq >= qr && pq >= rp) {
                    const auto m = halve(p, q);
                    self(self, p, m, r, depthLeft - 1);
                    self(self, m, q, r, depthLeft - 1);
                } else if (qr >= rp) {
                    const auto m = halve(q, r);
                    self(self, p, q, m, depthLeft - 1);
                    self(self, p, m, r, depthLeft - 1);
                } else {
                    const auto m = halve(r, p);
                    self(self, p, q, m, depthLeft - 1);
                    self(self, m, q, r, depthLeft - 1);
                }
            };
            split(split, {mesh.vertices[triangle[0]], screen(a)}, {mesh.vertices[triangle[1]], screen(b)}, {mesh.vertices[triangle[2]], screen(c)}, 40);
        }
    }
    if (depth.isEmpty()) { return frame; }

    // What is hidden: every line, every half pixel along its true path.
    // `place(t)` is a point of a run at progress t; the parts that show come
    // back as intervals of t.
    std::vector<std::pair<double, double>> runs;
    const auto visibleRuns = [&](auto&& place, double length) {
        runs.clear();
        const auto steps = std::clamp(static_cast<int>(std::ceil(length / testStep)), 1, 65536);
        bool open = false;
        double from = 0, previous = 0;
        for (int step = 0; step <= steps; ++step) {
            const auto t = static_cast<double>(step) / steps;
            const auto point = place(t);
            const auto shows = point.has_value() && depth.shows(*point);
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
    };
    struct Kept {
        std::unique_ptr<osci::Shape> shape;
        bool cut;
    };
    // A run culling cut too short to see would only draw a dot: it is
    // dropped. Runs culling left whole stay.
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
            auto shown = !std::any_of(pieces.begin() + static_cast<std::ptrdiff_t>(first), pieces.begin() + static_cast<std::ptrdiff_t>(last), [](const auto& piece) { return piece.cut; });
            double length = 0;
            for (auto index = first; index < last && !shown; ++index) {
                auto& shape = *pieces[index].shape;
                std::optional<Screen> previous;
                for (int probe = 0; probe <= 4; ++probe) {
                    const auto next = screen(eye(clip, shape.nextVector(probe / 4.0f)));
                    if (previous.has_value() && next.has_value()) { length += std::hypot(next->x - previous->x, next->y - previous->y); }
                    previous = next;
                }
                shown = length >= shortestOnScreen;
            }
            if (shown) {
                for (auto index = first; index < last; ++index) { shapes.push_back(std::move(pieces[index].shape)); }
            }
            first = last;
        }
        return std::make_shared<const PreparedDrawing>(std::move(shapes), true);
    };
    const auto mix = [](const osci::Point& a, const osci::Point& b, double t) {
        const auto f = static_cast<float>(t);
        return osci::Point(a.x + (b.x - a.x) * f, a.y + (b.y - a.y) * f, a.z + (b.z - a.z) * f, a.r + (b.r - a.r) * f, a.g + (b.g - a.g) * f, a.b + (b.b - a.b) * f);
    };
    std::vector<std::optional<Vec3>> eyes;
    for (const auto& item : items) {
        if (cancelled()) { return nullptr; }
        std::vector<Kept> kept;
        bool lost = false;
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
                const auto length = sa.has_value() && sb.has_value() ? std::hypot(sb->x - sa->x, sb->y - sa->y) : 2 * DepthBuffer::extent;
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
        if (lost) { frame->drawings.push_back({item.index, keep(kept, *item.clip)}); }
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
        for (auto& worker : retired) { worker.thread.join(); }
    }

    // Replaces any bake in progress.
    void start(std::shared_ptr<Bake> bake, double sampleRate, std::size_t firstFrame) {
        stop();
        if (bake == nullptr || bake->project == nullptr || bake->complete()) { return; }
        cancel = std::make_shared<std::atomic<bool>>(false);
        const auto threads = std::clamp<unsigned>(std::thread::hardware_concurrency() / 2, 1u, 4u);
        for (unsigned index = 0; index < threads; ++index) {
            auto done = std::make_shared<std::atomic<bool>>(false);
            workers.push_back({std::thread([bake, sampleRate, firstFrame, flag = cancel, done] {
                run(*bake, sampleRate, firstFrame, *flag);
                done->store(true);
            }), done});
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
            worker.thread.join();
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
    struct Worker {
        std::thread thread;
        std::shared_ptr<std::atomic<bool>> done;
    };

    static void run(Bake& bake, double sampleRate, std::size_t firstFrame, const std::atomic<bool>& cancel) {
        if (cancel.load()) { return; }
        std::unique_ptr<PreparedComposition> composition;
        try {
            composition = std::make_unique<PreparedComposition>(*bake.project, sampleRate, &cancel);
        } catch (...) {
            return;
        }
        auto depth = std::make_unique<DepthBuffer>();
        while (!cancel.load(std::memory_order_relaxed)) {
            const auto frame = bake.claim(firstFrame);
            if (!frame.has_value()) { return; }
            try {
                auto result = cull(*composition, bake.timeOf(*frame), *depth, &cancel);
                if (result == nullptr) { return; }
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

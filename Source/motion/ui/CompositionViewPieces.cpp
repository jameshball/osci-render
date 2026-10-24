#include "CompositionView.h"
#include "../render/HiddenLineDepth.h"

// What the Scene draws, piece by piece: each clip's shapes on screen, the
// lighting asked of the scene preview, and the depth that hides lines.

bool MotionCompositionView::askForLighting(double time) const {
    const auto view = camera.view();
    if (scenePreview == nullptr || shownProject == nullptr || prepared == nullptr || !view.has_value() || getWidth() <= 0 || getHeight() <= 0) { return false; }
    // Only a scene with a light, or something that may reflect or draw
    // itself other than by its edges, looks any different worked out.
    if (std::none_of(prepared->clips.begin(), prepared->clips.end(), [](const motion::PreparedClip& clip) { return clip.light || clip.mayReflect() || clip.mayRestyle(); })) { return false; }
    const auto asked = std::tuple {preparedSerial, time, camera.position.x, camera.position.y, camera.position.z, camera.yaw, camera.pitch, camera.fovDegrees, getWidth(), getHeight()};
    if (lightingAsked == asked) { return true; }
    lightingAsked = asked;
    // The view's lens as a camera's, reaching across the whole Scene.
    const motion::hidden::OtherEye eye {{view->position, view->right, view->up, view->forward, 1 / view->tangent}, sceneReach()};
    scenePreview->request(shownProject, preparedSerial, time, eye);
    return true;
}

const std::vector<MotionCompositionView::ScreenPiece>& MotionCompositionView::screenPieces() const {
    auto live = processor.liveSourcePreview();
    const auto time = editingTime();
    // The latest lighting for what is shown, at whatever time, from wherever
    // and for whichever edit it was worked out: it catches up as the view
    // moves and as edits are made.
    auto latest = askForLighting(time) && scenePreview != nullptr ? scenePreview->latest() : nullptr;
    if (latest != nullptr && latest->revision < lightingSince) { latest.reset(); }
    const PiecesKey key {preparedSerial, live.get(), latest.get(), time, camera.position.x, camera.position.y, camera.position.z, camera.yaw, camera.pitch, camera.fovDegrees, selected, getWidth(), getHeight(), solidView, partMode};
    if (pieceKey == key) { return pieceCache; }
    pieceKey = key;
    // Held so a later set of live frames cannot reuse this one's address.
    pieceLive = std::move(live);
    lighting = std::move(latest);
    pieceCache.clear();
    if (prepared == nullptr) { return pieceCache; }
    const auto view = camera.view();
    // A clip's index in the lighting's preparation, by who it is.
    const auto litIndex = [&](const motion::PreparedClip& clip) -> std::optional<std::size_t> {
        if (lighting == nullptr) { return std::nullopt; }
        const auto identity = motion::ScenePreview::identityOf(clip);
        const auto found = std::find(lighting->clips.begin(), lighting->clips.end(), identity);
        return found == lighting->clips.end() ? std::nullopt : std::optional(static_cast<std::size_t>(found - lighting->clips.begin()));
    };
    for (const auto& clip : prepared->clips) {
        const auto sampleTime = clipTime(clip, time);
        if (!clip.active(sampleTime)) { continue; }
        // Lit as the Scope draws it, except while picking parts (picked by
        // the source's own shapes). Its own lines stay, faintly, so what is
        // dark (or a mirror that shows nothing of itself) can still be seen
        // and picked.
        const auto index = partMode ? std::nullopt : litIndex(clip);
        const auto* entry = index.has_value() ? lighting->frame.entry(*index) : nullptr;
        const auto add = [&](const motion::PreparedDrawing* drawing, float fade) {
            forEachPiece(clip, sampleTime, view, pieceLive.get(), [&](const Piece& piece) {
                const auto line = screenLine(view, piece.a, piece.b);
                if (line.has_value()) { pieceCache.push_back({*line, piece.a, piece.b, piece.colour.withMultipliedAlpha(fade), &clip, piece.shape, static_cast<float>(view->toEye((piece.a + piece.b) * .5).z)}); }
            }, drawing);
        };
        add(nullptr, entry != nullptr ? .2f : 1.0f);
        if (entry != nullptr && !entry->drawing->empty()) { add(entry->drawing.get(), 1.0f); }
    }
    // What mirrors show from this view, as part of the mirror (not pickable
    // shape by shape); in the scene already.
    static const std::vector<motion::hidden::Frame::Entry> noReflections;
    const auto& reflections = lighting != nullptr && !partMode ? lighting->frame.reflections : noReflections;
    for (const auto& reflection : reflections) {
        if (reflection.clip >= lighting->clips.size() || reflection.drawing == nullptr) { continue; }
        const auto mirror = std::find_if(prepared->clips.begin(), prepared->clips.end(), [&](const motion::PreparedClip& clip) { return motion::ScenePreview::identityOf(clip) == lighting->clips[reflection.clip]; });
        if (mirror == prepared->clips.end()) { continue; }
        auto& drawing = *reflection.drawing;
        for (std::size_t index = 0; index < drawing.shapeCount(); ++index) {
            auto* shape = drawing.shape(index);
            constexpr int steps = 8;
            auto previous = shape->nextVector(0);
            for (int step = 1; step <= steps; ++step) {
                const auto next = shape->nextVector(static_cast<float>(step) / steps);
                const motion::Vec3 a {previous.x, previous.y, previous.z}, b {next.x, next.y, next.z};
                const auto line = screenLine(view, a, b);
                if (line.has_value() && (next.r > 0 || next.g > 0 || next.b > 0)) {
                    pieceCache.push_back({*line, a, b, juce::Colour::fromFloatRGBA(next.r, next.g, next.b, 1.0f), &*mirror, -1, static_cast<float>(view->toEye((a + b) * .5).z)});
                }
                previous = next;
            }
        }
    }
    if (solidView) { keepUnhidden(view, time); }
    return pieceCache;
}

void MotionCompositionView::keepUnhidden(const CameraView& view, double time) const {
    if (!view.has_value() || prepared == nullptr || getHeight() <= 0) { return; }
    // Where a point is for the depth buffer: as the Scene projects it, -1 to
    // 1 across the square output frame, and its inverse depth.
    const auto tangent = std::max(1.0e-9, view->tangent);
    // Faces this near the eye are cut there: nearer, inverse depth runs away.
    constexpr double nearest = .01;
    const auto onScreen = [&](motion::Vec3 eye) { return motion::hidden::Screen {eye.x / (eye.z * tangent), eye.y / (eye.z * tangent), 1 / eye.z}; };
    // The buffer covers the whole Scene, past the frame on every side, at
    // about a cell to every pixel and a half: fine enough to cut lines where
    // they pass behind, cheap enough to draw every frame of a drag.
    const auto frame = outputFrame();
    const auto across = std::clamp(static_cast<int>(frame.getWidth() / 1.5f), 64, 768);
    motion::hidden::DepthBuffer depth(sceneReach(), across);
    // A face in eye space, cut where it crosses the near plane.
    const auto fill = [&](const std::array<motion::Vec3, 3>& corners) {
        std::array<motion::Vec3, 4> kept {};
        std::size_t count = 0;
        for (std::size_t index = 0; index < 3; ++index) {
            const auto& current = corners[index];
            const auto& next = corners[(index + 1) % 3];
            if (current.z > nearest) { kept[count++] = current; }
            if ((current.z > nearest) != (next.z > nearest)) { kept[count++] = current + (next - current) * ((nearest - current.z) / (next.z - current.z)); }
        }
        for (std::size_t piece = 2; piece < count; ++piece) { depth.triangle(onScreen(kept[0]), onScreen(kept[piece - 1]), onScreen(kept[piece])); }
    };
    for (const auto& clip : prepared->clips) {
        const auto sampleTime = clipTime(clip, time);
        // Only what is solid hides anything, as the beam draws it: a wireframe hides nothing, itself included.
        if (clip.light || !clip.active(sampleTime) || !clip.visible(sampleTime) || clip.source == nullptr || !(clip.solid(sampleTime) > 0)) { continue; }
        const auto* drawing = clip.source->drawingAt(clip.source->frameIndex(clip.localTime(sampleTime)));
        if (drawing == nullptr || drawing->faces().empty()) { continue; }
        const auto& mesh = drawing->faces();
        const auto eyeOf = [&](const osci::Point& point) -> std::optional<motion::Vec3> {
            const auto world = prepared->worldPoint(clip, point, sampleTime);
            return world.has_value() ? std::optional(view->toEye(*world)) : std::nullopt;
        };
        std::vector<std::optional<motion::Vec3>> corners;
        corners.reserve(mesh.vertices.size());
        for (const auto& vertex : mesh.vertices) { corners.push_back(eyeOf(vertex)); }
        // What an effect bends is no longer flat between its corners: its
        // faces are cut finer, about every 16 pixels, to follow the lines on it.
        const auto bent = warps(clip);
        for (const auto& triangle : mesh.triangles) {
            const auto& a = corners[triangle[0]], & b = corners[triangle[1]], & c = corners[triangle[2]];
            if (!a.has_value() || !b.has_value() || !c.has_value()) { continue; }
            if (!bent) {
                fill({*a, *b, *c});
                continue;
            }
            // How long an edge is on screen; one reaching behind the eye is
            // cut as finely as any.
            const auto pixels = [&](const motion::Vec3& from, const motion::Vec3& to) {
                if (!(from.z > nearest) || !(to.z > nearest)) { return 256.0; }
                const auto p = onScreen(from), q = onScreen(to);
                return std::hypot(p.x - q.x, p.y - q.y) * frame.getWidth() * .5;
            };
            const auto steps = std::clamp(static_cast<int>(std::ceil(std::max({pixels(*a, *b), pixels(*b, *c), pixels(*c, *a)}) / 16)), 1, 16);
            const auto& pa = mesh.vertices[triangle[0]], & pb = mesh.vertices[triangle[1]], & pc = mesh.vertices[triangle[2]];
            const auto at = [&](int i, int j) {
                const auto u = static_cast<float>(i) / static_cast<float>(steps), v = static_cast<float>(j) / static_cast<float>(steps), w = 1 - u - v;
                return eyeOf(osci::Point(pa.x * w + pb.x * u + pc.x * v, pa.y * w + pb.y * u + pc.y * v, pa.z * w + pb.z * u + pc.z * v));
            };
            // The face's points on a triangular lattice, row by row.
            std::vector<std::optional<motion::Vec3>> lattice;
            for (int i = 0; i <= steps; ++i) {
                for (int j = 0; j <= steps - i; ++j) { lattice.push_back(at(i, j)); }
            }
            const auto point = [&](int i, int j) -> const std::optional<motion::Vec3>& {
                // Row i starts after rows 0..i-1, of steps + 1 - row points each.
                return lattice[static_cast<std::size_t>(i * (steps + 1) - i * (i - 1) / 2 + j)];
            };
            for (int i = 0; i < steps; ++i) {
                for (int j = 0; j < steps - i; ++j) {
                    const auto& p = point(i, j), & q = point(i + 1, j), & r = point(i, j + 1);
                    if (p.has_value() && q.has_value() && r.has_value()) { fill({*p, *q, *r}); }
                    if (j + 1 < steps - i) {
                        const auto& t = point(i + 1, j + 1);
                        if (q.has_value() && t.has_value() && r.has_value()) { fill({*q, *t, *r}); }
                    }
                }
            }
        }
    }
    if (depth.isEmpty()) { return; }
    std::vector<ScreenPiece> kept;
    kept.reserve(pieceCache.size());
    for (const auto& piece : pieceCache) {
        // Tested every few pixels along it on screen.
        const auto steps = std::clamp(static_cast<int>(piece.line.getLength() / 3.0f), 1, 256);
        const auto shows = [&](int step) {
            const auto eye = view->toEye(piece.a + (piece.b - piece.a) * (static_cast<double>(step) / steps));
            return !(eye.z > nearest) || depth.shows(onScreen(eye));
        };
        int from = -1;
        const auto keep = [&](int start, int end) {
            if (start == 0 && end == steps) {
                kept.push_back(piece);
                return;
            }
            // Cut where it passes behind, half a step either side.
            const auto t0 = start == 0 ? 0.0 : (start - .5) / steps, t1 = end == steps ? 1.0 : (end + .5) / steps;
            const auto a = piece.a + (piece.b - piece.a) * t0, b = piece.a + (piece.b - piece.a) * t1;
            const auto line = screenLine(view, a, b);
            if (line.has_value()) { kept.push_back({*line, a, b, piece.colour, piece.clip, piece.shape, piece.depth}); }
        };
        for (int step = 0; step <= steps; ++step) {
            const auto seen = shows(step);
            if (seen && from < 0) { from = step; }
            if (!seen && from >= 0) {
                keep(from, step - 1);
                from = -1;
            }
        }
        if (from >= 0) { keep(from, steps); }
    }
    pieceCache = std::move(kept);
}

float MotionCompositionView::depthFade(double depth) const {
    const auto pivot = std::max(1.0e-6, camera.distance());
    return static_cast<float>(std::clamp(1.0 - (depth - pivot) / pivot, .4, 1.0));
}

double MotionCompositionView::clipTime(const motion::PreparedClip& clip, double time) const {
    return clip.editorId() == selected && atSelectedPathEnd(time) ? std::nextafter(time, 0.0) : time;
}

bool MotionCompositionView::warps(const motion::PreparedClip& clip) const {
    const auto stage = [](const motion::PreparedClipStage& item) {
        return !item.effects.empty() || !item.trackEffects.empty() || !item.compositionEffects.empty()
            || std::any_of(item.groups.begin(), item.groups.end(), [](const auto& group) { return !group.effects.empty(); });
    };
    return (prepared != nullptr && prepared->hasCompositionEffects()) || stage(clip) || std::any_of(clip.ancestors.begin(), clip.ancestors.end(), stage);
}

void MotionCompositionView::forEachPiece(const motion::PreparedClip& clip, double time, const CameraView& view, const motion::LiveSourceFrames* live, const std::function<void(const Piece&)>& visit, const motion::PreparedDrawing* litDrawing) const {
    const auto* source = clip.resolveSource(live);
    if (source == nullptr || !view.has_value()) { return; }
    const auto lit = [](const osci::Point& point) { return point.r != 0 || point.g != 0 || point.b != 0; };
    const auto colourOf = [](const osci::Point& point) { return juce::Colour::fromFloatRGBA(point.r, point.g, point.b, 1.0f); };
    const auto* drawing = litDrawing != nullptr ? litDrawing : source->drawingAt(source->frameIndex(clip.localTime(time)));
    if (drawing == nullptr) {
        // Traced sources walk their stored points at native density, so
        // short lit runs stay visible. Output uses its audio rate.
        const auto count = source->previewSampleCount();
        const auto span = source->previewPhaseSpan();
        auto previous = clip.sample(time, 0, span, 0, live);
        for (std::size_t index = 1; index <= count; ++index) {
            const auto next = clip.sample(time, static_cast<double>(index) / count, span, 0, live);
            if (lit(previous) && lit(next)) { visit({worldPoint(previous, time), worldPoint(next, time), colourOf(next), -1}); }
            previous = next;
        }
        return;
    }
    const auto count = drawing->shapeCount();
    const auto bent = warps(clip);
    // Big sources take fewer steps per curve, so the Scene stays quick.
    // A shape an effect bends may wind a long way (a swirl far from the
    // centre), so it may take more.
    const auto mostSteps = static_cast<int>(std::clamp<std::size_t>(60000 / std::max<std::size_t>(1, count), 1, bent ? 512 : 48));
    const auto pixels = outputFrame().getHeight() / 2.0;
    const auto place = [&](osci::Shape& shape, float progress) {
        const auto point = clip.processPoint(shape.nextVector(progress), time);
        return std::pair {point, worldPoint(point, time)};
    };
    for (std::size_t index = 0; index < count; ++index) {
        auto* shape = drawing->shape(index);
        // Shapes of no length never reach the output, so they are not drawn.
        if (shape == nullptr || !(shape->length() > 0)) { continue; }
        const auto straight = dynamic_cast<osci::Line*>(shape) != nullptr;
        const auto start = place(*shape, 0), end = place(*shape, 1);
        int steps = 1;
        // About one step per 4 px of curve on screen; effects can bend
        // straight lines too.
        if (bent || !straight) {
            const auto least = straight ? 1 : std::min(4, mostSteps);
            steps = mostSteps;
            if (mostSteps > least) {
                // Measured at several points: a long line an effect winds
                // round (a swirl) is far longer than its ends and middle say.
                constexpr int probes = 8;
                double span = 0;
                bool measured = true;
                auto before = view->project(start.second);
                for (int probe = 1; probe <= probes && measured; ++probe) {
                    const auto at = probe == probes ? view->project(end.second) : view->project(place(*shape, static_cast<float>(probe) / probes).second);
                    measured = before.has_value() && at.has_value();
                    if (measured) { span += std::hypot(at->x - before->x, at->y - before->y); }
                    before = at;
                }
                span *= pixels;
                if (measured && std::isfinite(span)) { steps = static_cast<int>(std::clamp(std::ceil(span / 4), static_cast<double>(least), static_cast<double>(mostSteps))); }
            }
        }
        auto previous = start;
        for (int step = 1; step <= steps; ++step) {
            const auto next = step == steps ? end : place(*shape, static_cast<float>(step) / steps);
            if (lit(previous.first) && lit(next.first)) { visit({previous.second, next.second, colourOf(next.first), static_cast<int>(index)}); }
            previous = next;
        }
    }
}

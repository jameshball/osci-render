#include "LottieParser.h"
#include <thorvg.h>
#include <unordered_set>
#include <functional>
#include <clipper2/clipper.h>

namespace {
    // Global refcount for thorvg Initializer across all LottieParser instances.
    juce::SpinLock& initializerLock() {
        static juce::SpinLock l;
        return l;
    }
    int& initializerCount() {
        static int c = 0;
        return c;
    }
    void acquireThorVG() {
        juce::SpinLock::ScopedLockType sl(initializerLock());
        if (initializerCount() == 0) {
            tvg::Initializer::init(0);
        }
        ++initializerCount();
    }
    void releaseThorVG() {
        juce::SpinLock::ScopedLockType sl(initializerLock());
        if (--initializerCount() == 0) {
            tvg::Initializer::term();
        }
    }

    // Walk from `leaf` up to (but not including) `root`, multiplying each
    // ancestor's local transform to produce the world-space transform.
    tvg::Matrix accumulatedTransform(const tvg::Paint* leaf, const tvg::Paint* root) {
        tvg::Matrix m{1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f};
        std::vector<const tvg::Paint*> chain;
        for (auto p = leaf; p != nullptr && p != root; p = p->parent()) {
            chain.push_back(p);
        }
        // Multiply root->leaf so we accumulate correctly.
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            tvg::Matrix t = const_cast<tvg::Paint*>(*it)->transform();
            tvg::Matrix r;
            r.e11 = m.e11 * t.e11 + m.e12 * t.e21 + m.e13 * t.e31;
            r.e12 = m.e11 * t.e12 + m.e12 * t.e22 + m.e13 * t.e32;
            r.e13 = m.e11 * t.e13 + m.e12 * t.e23 + m.e13 * t.e33;
            r.e21 = m.e21 * t.e11 + m.e22 * t.e21 + m.e23 * t.e31;
            r.e22 = m.e21 * t.e12 + m.e22 * t.e22 + m.e23 * t.e32;
            r.e23 = m.e21 * t.e13 + m.e22 * t.e23 + m.e23 * t.e33;
            r.e31 = m.e31 * t.e11 + m.e32 * t.e21 + m.e33 * t.e31;
            r.e32 = m.e31 * t.e12 + m.e32 * t.e22 + m.e33 * t.e32;
            r.e33 = m.e31 * t.e13 + m.e32 * t.e23 + m.e33 * t.e33;
            m = r;
        }
        return m;
    }

    inline tvg::Point applyMatrix(const tvg::Matrix& m, tvg::Point p) {
        return {m.e11 * p.x + m.e12 * p.y + m.e13,
                m.e21 * p.x + m.e22 * p.y + m.e23};
    }

    float accumulatedOpacity(const tvg::Paint* leaf, const tvg::Paint* root) {
        float o = 1.0f;
        for (auto p = leaf; p != nullptr && p != root; p = p->parent()) {
            o *= static_cast<float>(p->opacity()) / 255.0f;
        }
        return o;
    }

    bool isVisibleShape(const tvg::Shape* shape, float inheritedOpacity) {
        if (inheritedOpacity <= 0.0f) return false;

        uint8_t fr = 0, fg = 0, fb = 0, fa = 0;
        uint8_t sr = 0, sg = 0, sb = 0, sa = 0;

        const bool hasFillGradient = shape->fill() != nullptr;
        const bool hasSolidFill = (shape->fill(&fr, &fg, &fb, &fa) == tvg::Result::Success) && fa > 0;

        const float strokeWidth = shape->strokeWidth();
        const bool hasStrokeGradient = strokeWidth > 0.0f && shape->strokeFill() != nullptr;
        const bool hasSolidStroke = strokeWidth > 0.0f &&
                                    (shape->strokeFill(&sr, &sg, &sb, &sa) == tvg::Result::Success) && sa > 0;

        if (!(hasFillGradient || hasSolidFill || hasStrokeGradient || hasSolidStroke)) return false;

        // Lottie animates path visibility via stroke trim. thorvg stores the
        // trim parameters separately - if begin == end the entire path is
        // trimmed away and should not be rendered.
        float trimBegin = 0.0f, trimEnd = 1.0f;
        shape->trimpath(&trimBegin, &trimEnd);
        if (std::abs(trimEnd - trimBegin) < 1.0e-4f) return false;

        return true;
    }

    // ---- trim-path helpers ----------------------------------------------

    struct PathPrim {
        bool cubic = false;         // true = cubic bezier, false = straight line
        tvg::Point p0{}, p1{}, p2{}, p3{}; // p1,p2 unused for lines
        float len = 0.0f;
    };

    inline tvg::Point lerp(tvg::Point a, tvg::Point b, float t) {
        return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
    }

    float cubicLength(tvg::Point p0, tvg::Point p1, tvg::Point p2, tvg::Point p3, int depth = 5) {
        auto chord = std::hypot(p3.x - p0.x, p3.y - p0.y);
        auto control = std::hypot(p1.x - p0.x, p1.y - p0.y)
                     + std::hypot(p2.x - p1.x, p2.y - p1.y)
                     + std::hypot(p3.x - p2.x, p3.y - p2.y);
        if (depth <= 0 || std::abs(control - chord) < 0.5f) {
            return 0.5f * (chord + control);
        }
        // subdivide at t=0.5
        auto m01 = lerp(p0, p1, 0.5f);
        auto m12 = lerp(p1, p2, 0.5f);
        auto m23 = lerp(p2, p3, 0.5f);
        auto m012 = lerp(m01, m12, 0.5f);
        auto m123 = lerp(m12, m23, 0.5f);
        auto m0123 = lerp(m012, m123, 0.5f);
        return cubicLength(p0, m01, m012, m0123, depth - 1)
             + cubicLength(m0123, m123, m23, p3, depth - 1);
    }

    // Extract the cubic bezier segment between parameters t0..t1 using de Casteljau.
    void cubicSubsegment(tvg::Point p0, tvg::Point p1, tvg::Point p2, tvg::Point p3,
                         float t0, float t1,
                         tvg::Point& q0, tvg::Point& q1, tvg::Point& q2, tvg::Point& q3) {
        // First split at t1 to get [0, t1]
        auto a1 = lerp(p0, p1, t1);
        auto b1 = lerp(p1, p2, t1);
        auto c1 = lerp(p2, p3, t1);
        auto a2 = lerp(a1, b1, t1);
        auto b2 = lerp(b1, c1, t1);
        auto a3 = lerp(a2, b2, t1);
        // Segment [0, t1] is: p0, a1, a2, a3
        // Now split [0, t1] at t0/t1 to get [t0, t1]
        float u = (t1 > 0.0f) ? (t0 / t1) : 0.0f;
        auto d1 = lerp(p0, a1, u);
        auto e1 = lerp(a1, a2, u);
        auto f1 = lerp(a2, a3, u);
        auto d2 = lerp(d1, e1, u);
        auto e2 = lerp(e1, f1, u);
        auto d3 = lerp(d2, e2, u);
        // Segment [t0, t1] as cubic bezier: d3, e2, f1, a3
        q0 = d3; q1 = e2; q2 = f1; q3 = a3;
    }

    // Given a subpath's primitives, emit the portion in arclength range [b*L, e*L].
    template <typename Emit>
    void emitTrimmed(const std::vector<PathPrim>& prims, float begin, float end, Emit emit) {
        if (prims.empty()) return;
        float totalLen = 0.0f;
        for (auto& p : prims) totalLen += p.len;
        if (totalLen <= 0.0f) return;

        // Lottie/thorvg trim can wrap: if begin > end when loop, thorvg swaps.
        // We keep the simple case: if begin > end, emit two ranges: [begin, 1] and [0, end].
        auto emitRange = [&](float b, float e) {
            if (e <= b) return;
            float bLen = b * totalLen;
            float eLen = e * totalLen;
            float acc = 0.0f;
            for (auto& p : prims) {
                float segStart = acc;
                float segEnd = acc + p.len;
                acc = segEnd;
                if (segEnd <= bLen) continue;
                if (segStart >= eLen) break;
                float t0 = (p.len > 0.0f) ? std::max(0.0f, (bLen - segStart) / p.len) : 0.0f;
                float t1 = (p.len > 0.0f) ? std::min(1.0f, (eLen - segStart) / p.len) : 1.0f;
                if (t1 <= t0) continue;
                if (p.cubic) {
                    tvg::Point q0, q1, q2, q3;
                    cubicSubsegment(p.p0, p.p1, p.p2, p.p3, t0, t1, q0, q1, q2, q3);
                    emit(true, q0, q1, q2, q3);
                } else {
                    auto a = lerp(p.p0, p.p3, t0);
                    auto b = lerp(p.p0, p.p3, t1);
                    emit(false, a, tvg::Point{}, tvg::Point{}, b);
                }
            }
        };

        if (begin < end) {
            emitRange(begin, end);
        } else {
            emitRange(begin, 1.0f);
            emitRange(0.0f, end);
        }
    }

    // Adaptive cubic flattening. Appends points to `out` (including the
    // endpoint p3; the caller is responsible for first pushing p0).
    void flattenCubic(tvg::Point p0, tvg::Point p1, tvg::Point p2, tvg::Point p3,
                      std::vector<tvg::Point>& out, float tolSq = 0.25f, int depth = 10) {
        // Distance from control points to the chord p0->p3.
        auto dx = p3.x - p0.x;
        auto dy = p3.y - p0.y;
        auto len2 = dx * dx + dy * dy;
        auto perp = [&](tvg::Point c) -> float {
            if (len2 <= 1.0e-12f) {
                auto ex = c.x - p0.x, ey = c.y - p0.y;
                return ex * ex + ey * ey;
            }
            float t = ((c.x - p0.x) * dx + (c.y - p0.y) * dy) / len2;
            float px = p0.x + t * dx, py = p0.y + t * dy;
            float ex = c.x - px, ey = c.y - py;
            return ex * ex + ey * ey;
        };
        if (depth <= 0 || (perp(p1) < tolSq && perp(p2) < tolSq)) {
            out.push_back(p3);
            return;
        }
        auto m01 = lerp(p0, p1, 0.5f);
        auto m12 = lerp(p1, p2, 0.5f);
        auto m23 = lerp(p2, p3, 0.5f);
        auto m012 = lerp(m01, m12, 0.5f);
        auto m123 = lerp(m12, m23, 0.5f);
        auto m0123 = lerp(m012, m123, 0.5f);
        flattenCubic(p0, m01, m012, m0123, out, tolSq, depth - 1);
        flattenCubic(m0123, m123, m23, p3, out, tolSq, depth - 1);
    }
}

void OsciLottieParser::AnimationDeleter::operator()(tvg::Animation* a) const noexcept {
    delete a;
}

OsciLottieParser::OsciLottieParser(juce::String jsonContent) {
    acquireThorVG();

    // thorvg loads with copy=false, so we must retain the data.
    auto utf8 = jsonContent.toRawUTF8();
    auto size = static_cast<uint32_t>(jsonContent.getNumBytesAsUTF8());
    jsonBuffer.append(utf8, size);
    // ThorVG's Lottie header scan walks until '\0'. Keep an explicit terminator.
    const char nullTerminator = '\0';
    jsonBuffer.append(&nullTerminator, 1);

    animation.reset(tvg::Animation::gen());
    if (animation == nullptr) {
        showError("Failed to create Lottie animation.");
        fallbackShapes();
        return;
    }

    auto picture = animation->picture();
    if (picture == nullptr) {
        showError("Failed to create Lottie picture.");
        fallbackShapes();
        return;
    }

    auto result = picture->load(static_cast<const char*>(jsonBuffer.getData()),
                                static_cast<uint32_t>(jsonBuffer.getSize() - 1),
                                "lottie+json",
                                nullptr,
                                true);
    if (result != tvg::Result::Success) {
        showError("The Lottie file could not be loaded.");
        fallbackShapes();
        return;
    }

    float w = 0.0f, h = 0.0f;
    picture->size(&w, &h);
    pictureWidth = (w > 0.0f) ? w : 1.0f;
    pictureHeight = (h > 0.0f) ? h : 1.0f;

    totalFrames = std::max(1, (int) std::floor(animation->totalFrame() + 0.5f));
    if (totalFrames < 1) totalFrames = 1;

    // thorvg reports total animation duration in seconds. Derive fps.
    float duration = animation->duration();
    if (duration > 0.0f && totalFrames > 0) {
        frameRate = static_cast<double>(totalFrames) / static_cast<double>(duration);
    }

    // Pre-render every frame up front so setFrame/draw are O(1) and
    // realtime-safe (the audio thread calls setFrame from processBlock).
    framesCache.resize(totalFrames);
    for (int i = 0; i < totalFrames; ++i) {
        animation->frame(static_cast<float>(i));
        // Force scene-tree update at the current frame by requesting bounds.
        float bx = 0.0f, by = 0.0f, bw = 0.0f, bh = 0.0f;
        picture->bounds(&bx, &by, &bw, &bh);
        extractShapesAtCurrentFrame(framesCache[i]);
    }
    currentFrame = 0;
}

OsciLottieParser::~OsciLottieParser() {
    // unique_ptr destroys the animation via AnimationDeleter before releaseThorVG.
    animation.reset();
    releaseThorVG();
}

int OsciLottieParser::getNumFrames() const { return totalFrames; }
int OsciLottieParser::getCurrentFrame() const { return juce::jmax(0, currentFrame); }

void OsciLottieParser::setFrame(int index) {
    if (totalFrames <= 0) return;
    index = juce::jlimit(0, totalFrames - 1, index);
    currentFrame = index;
}

std::vector<std::unique_ptr<osci::Shape>> OsciLottieParser::draw() {
    std::vector<std::unique_ptr<osci::Shape>> out;
    if (currentFrame < 0 || currentFrame >= (int) framesCache.size()) return out;
    auto& src = framesCache[currentFrame];
    out.reserve(src.size());
    for (auto& s : src) {
        out.push_back(s->clone());
    }
    return out;
}

void OsciLottieParser::extractShapesAtCurrentFrame(std::vector<std::unique_ptr<osci::Shape>>& out) {
    out.clear();
    if (animation == nullptr) return;
    auto picture = animation->picture();
    if (picture == nullptr) return;

    // Force scene-tree update at the current frame by requesting bounds.
    float bx = 0.0f, by = 0.0f, bw = 0.0f, bh = 0.0f;
    picture->bounds(&bx, &by, &bw, &bh);

    // First pass: discover matte/clip relationships.
    // - `maskTargets`: paints that are used purely as mask/clip sources for
    //   another paint; their outlines should not be drawn (they are
    //   invisible "matte" sources in Lottie terms).
    // - `clipScopes`: per scope-paint, the matte target subtree and whether
    //   it's inverted. Any descendant shape of the scope paint has its
    //   visible region intersected with (or subtracted from) the target's
    //   fill polygon.
    std::unordered_set<const tvg::Paint*> maskTargets;
    struct ClipScope { const tvg::Paint* target; bool invert; };
    std::unordered_map<const tvg::Paint*, ClipScope> clipScopes;
    {
        auto addSubtree = [&](const tvg::Paint* target) {
            if (target == nullptr) return;
            std::unique_ptr<tvg::Accessor> sub(tvg::Accessor::gen());
            sub->set(const_cast<tvg::Paint*>(target), [](const tvg::Paint* p, void* d) -> bool {
                static_cast<std::unordered_set<const tvg::Paint*>*>(d)->insert(p);
                return true;
            }, &maskTargets);
        };

        struct CollectCtx {
            std::function<void(const tvg::Paint*)>* addSubtree;
            std::unordered_map<const tvg::Paint*, ClipScope>* clipScopes;
        };
        std::function<void(const tvg::Paint*)> addSubtreeFn = addSubtree;
        CollectCtx collectCtx { &addSubtreeFn, &clipScopes };

        std::unique_ptr<tvg::Accessor> scan(tvg::Accessor::gen());
        scan->set(picture, [](const tvg::Paint* paint, void* data) -> bool {
            auto cctx = static_cast<CollectCtx*>(data);
            const tvg::Paint* maskTarget = nullptr;
            auto method = paint->mask(&maskTarget);
            if (method != tvg::MaskMethod::None && maskTarget != nullptr) {
                (*cctx->addSubtree)(maskTarget);
                // Only treat scene-to-scene mattes as clip scopes. thorvg's
                // Lottie loader uses `mask(shape, Alpha)` for internal layer
                // bounding / layer masks where the shape coordinates are in
                // a coordinate space we can't reliably resolve via parent()
                // walking, leading to mis-clipping. Scene-to-scene mattes
                // (Lottie's track-matte feature) are the common case we do
                // want to clip.
                const bool sceneTarget = maskTarget->type() == tvg::Type::Scene;
                if (sceneTarget) {
                    if (method == tvg::MaskMethod::Alpha) {
                        (*cctx->clipScopes)[paint] = { maskTarget, false };
                    } else if (method == tvg::MaskMethod::InvAlpha) {
                        (*cctx->clipScopes)[paint] = { maskTarget, true };
                    }
                }
            }
            if (auto* clipper = paint->clip()) {
                (*cctx->addSubtree)(clipper);
                // Same caveat: only trust scene-level clips.
                if (clipper->type() == tvg::Type::Scene) {
                    (*cctx->clipScopes)[paint] = { clipper, false };
                }
            }
            return true;
        }, &collectCtx);
    }

    struct CollectedShape {
        std::vector<std::vector<PathPrim>> subpaths;
        tvg::Matrix xform;
        float trimBegin = 0.0f, trimEnd = 1.0f;
        bool simultaneous = true;
        bool hasTrim = false;
        bool opaqueFill = false;
        tvg::FillRule fillRule = tvg::FillRule::NonZero;
        const tvg::Paint* paint = nullptr; // used to resolve ancestor clips
    };

    auto toClipperFillRule = [](tvg::FillRule r) {
        return r == tvg::FillRule::EvenOdd ? Clipper2Lib::FillRule::EvenOdd
                                           : Clipper2Lib::FillRule::NonZero;
    };

    struct Ctx {
        OsciLottieParser* self;
        const tvg::Paint* root;
        const std::unordered_set<const tvg::Paint*>* maskTargets;
        std::vector<CollectedShape>* collected;
    };
    std::vector<CollectedShape> collected;
    Ctx ctx { this, picture, &maskTargets, &collected };

    std::unique_ptr<tvg::Accessor> accessor(tvg::Accessor::gen());
    accessor->set(picture, [](const tvg::Paint* paint, void* data) -> bool {
        auto ctx = static_cast<Ctx*>(data);
        if (paint->type() != tvg::Type::Shape) return true;

        // Skip shapes that are only used as mask/clip sources for other paints.
        if (ctx->maskTargets->count(paint) != 0) return true;

        auto shape = static_cast<const tvg::Shape*>(paint);

        const float inheritedOpacity = accumulatedOpacity(paint, ctx->root);
        if (!isVisibleShape(shape, inheritedOpacity)) return true;

        const tvg::PathCommand* cmds = nullptr;
        const tvg::Point* pts = nullptr;
        uint32_t cmdCnt = 0, ptsCnt = 0;
        if (shape->path(&cmds, &cmdCnt, &pts, &ptsCnt) != tvg::Result::Success) return true;
        if (cmdCnt == 0 || ptsCnt == 0) return true;

        CollectedShape cs;
        cs.paint = paint;
        cs.xform = accumulatedTransform(paint, ctx->root);

        cs.simultaneous = shape->trimpath(&cs.trimBegin, &cs.trimEnd);
        cs.hasTrim = std::abs(cs.trimEnd - cs.trimBegin - 1.0f) > 1.0e-4f
                     || std::abs(cs.trimBegin) > 1.0e-4f;

        // Determine whether this shape qualifies as an opaque occluder.
        // Must have a solid (non-gradient) fill at full alpha, full inherited
        // opacity, and no trim path (trim trims both fill and stroke in
        // Lottie so a trimmed fill isn't a reliable solid occluder).
        uint8_t fr = 0, fg = 0, fb = 0, fa = 0;
        const bool solidFill = (shape->fill(&fr, &fg, &fb, &fa) == tvg::Result::Success) && fa == 255;
        const bool noGradient = shape->fill() == nullptr;
        cs.opaqueFill = solidFill && noGradient && !cs.hasTrim && inheritedOpacity >= 0.999f;
        cs.fillRule = shape->fillRule();

        // Parse commands into subpaths.
        std::vector<PathPrim> current;
        tvg::Point cursor{0.0f, 0.0f};
        tvg::Point start{0.0f, 0.0f};
        uint32_t pi = 0;
        auto finishSubpath = [&]() {
            if (!current.empty()) {
                cs.subpaths.push_back(std::move(current));
                current.clear();
            }
        };
        for (uint32_t i = 0; i < cmdCnt; ++i) {
            switch (cmds[i]) {
                case tvg::PathCommand::MoveTo: {
                    if (pi >= ptsCnt) break;
                    finishSubpath();
                    cursor = pts[pi++];
                    start = cursor;
                    break;
                }
                case tvg::PathCommand::LineTo: {
                    if (pi >= ptsCnt) break;
                    auto next = pts[pi++];
                    PathPrim p;
                    p.cubic = false;
                    p.p0 = cursor;
                    p.p3 = next;
                    p.len = std::hypot(next.x - cursor.x, next.y - cursor.y);
                    current.push_back(p);
                    cursor = next;
                    break;
                }
                case tvg::PathCommand::CubicTo: {
                    if (pi + 2 >= ptsCnt) break;
                    auto c1 = pts[pi++];
                    auto c2 = pts[pi++];
                    auto end = pts[pi++];
                    PathPrim p;
                    p.cubic = true;
                    p.p0 = cursor;
                    p.p1 = c1;
                    p.p2 = c2;
                    p.p3 = end;
                    p.len = cubicLength(cursor, c1, c2, end);
                    current.push_back(p);
                    cursor = end;
                    break;
                }
                case tvg::PathCommand::Close: {
                    if (std::hypot(start.x - cursor.x, start.y - cursor.y) > 1.0e-5f) {
                        PathPrim p;
                        p.cubic = false;
                        p.p0 = cursor;
                        p.p3 = start;
                        p.len = std::hypot(start.x - cursor.x, start.y - cursor.y);
                        current.push_back(p);
                    }
                    cursor = start;
                    finishSubpath();
                    break;
                }
            }
        }
        finishSubpath();

        if (!cs.subpaths.empty()) {
            ctx->collected->push_back(std::move(cs));
        }
        return true;
    }, &ctx);

    // --- Pass 2: emit shapes top-to-bottom, subtracting each subsequent
    //     shape's outline against the union of opaque fills painted on top.

    const float w = pictureWidth;
    const float h = pictureHeight;
    const float normScale = 2.0f / std::max(w, h);
    const float cx = w * 0.5f;
    const float cy = h * 0.5f;

    auto worldToOsci = [&](tvg::Point p) -> osci::Point {
        return osci::Point((p.x - cx) * normScale, -(p.y - cy) * normScale, 0.0);
    };

    // Flatten a cubic into polyline points in world (post-xform) space.
    auto flattenPrim = [&](const tvg::Matrix& xf, bool cubic,
                           tvg::Point a, tvg::Point c1, tvg::Point c2, tvg::Point b,
                           std::vector<tvg::Point>& out, bool appendStart) {
        auto wa = applyMatrix(xf, a);
        auto wb = applyMatrix(xf, b);
        if (appendStart) out.push_back(wa);
        if (cubic) {
            auto wc1 = applyMatrix(xf, c1);
            auto wc2 = applyMatrix(xf, c2);
            flattenCubic(wa, wc1, wc2, wb, out);
        } else {
            out.push_back(wb);
        }
    };

    Clipper2Lib::PathsD occluder;
    constexpr double kClipperPrecision = 4.0; // decimal places

    // --- Compute per-paint clip polygon cache. For each `ClipScope` target
    //     (matte or clip source) we flatten its subtree's fills into a
    //     world-space closed polygon set.
    std::unordered_map<const tvg::Paint*, Clipper2Lib::PathsD> targetPolyCache;
    auto computeTargetPolygon = [&](const tvg::Paint* target) -> const Clipper2Lib::PathsD& {
        auto it2 = targetPolyCache.find(target);
        if (it2 != targetPolyCache.end()) return it2->second;
        Clipper2Lib::PathsD rings;
        struct WalkCtx {
            const tvg::Paint* worldRoot;
            Clipper2Lib::PathsD* rings;
            std::function<Clipper2Lib::FillRule(tvg::FillRule)>* toFR;
        };
        std::function<Clipper2Lib::FillRule(tvg::FillRule)> toFR = toClipperFillRule;
        WalkCtx wctx { picture, &rings, &toFR };
        std::unique_ptr<tvg::Accessor> acc(tvg::Accessor::gen());
        acc->set(const_cast<tvg::Paint*>(target), [](const tvg::Paint* p, void* d) -> bool {
            auto* wc = static_cast<WalkCtx*>(d);
            if (p->type() != tvg::Type::Shape) return true;
            auto shape = static_cast<const tvg::Shape*>(p);
            const tvg::PathCommand* cmds = nullptr;
            const tvg::Point* pts = nullptr;
            uint32_t cc = 0, pc = 0;
            if (shape->path(&cmds, &cc, &pts, &pc) != tvg::Result::Success) return true;
            if (cc == 0 || pc == 0) return true;
            uint8_t fr = 0, fg = 0, fb = 0, fa = 0;
            const bool solid = (shape->fill(&fr, &fg, &fb, &fa) == tvg::Result::Success && fa > 0);
            const bool grad = shape->fill() != nullptr;
            Clipper2Lib::PathsD shapeRings;
            auto flushRing = [&]() {
                if (ring.size() >= 3) {
                    Clipper2Lib::PathD pd;
                    pd.reserve(ring.size());
                    for (auto& pt : ring) {
                        auto wp = applyMatrix(xform, pt);
                        pd.push_back({wp.x, wp.y});
                    }
                    shapeRings. = applyMatrix(xform, pt);
                        pd.push_back({wp.x, wp.y});
                    }
                    wc->rings->push_back(std::move(pd));
                }
                ring.clear();
            };
            tvg::Point cursor{0, 0}, start{0, 0};
            uint32_t pi = 0;
            for (uint32_t i = 0; i < cc; ++i) {
                switch (cmds[i]) {
                    case tvg::PathCommand::MoveTo:
                        flushRing();
                        if (pi < pc) {
                            cursor = pts[pi++];
                            start = cursor;
                            ring.push_back(cursor);
                        }
                        break;
                    case tvg::PathCommand::LineTo:
                        if (pi < pc) {
                            cursor = pts[pi++];
                            ring.push_back(cursor);
                        }
                        break;
                    case tvg::PathCommand::CubicTo:
                        if (pi + 2 < pc) {
                            auto c1 = pts[pi++];
                            auto c2 = pts[pi++];
                            auto e = pts[pi++];
                            flattenCubic(cursor, c1, c2, e, ring);
                            cursor = e;
                        }
                        break;
                    case tvg::PathCommand::Close:
                        flushRing();
                        cursor = start;
            // Resolve this shape's own fill under its declared fill rule so
            // the emitted rings are non-overlapping, then append to the
            // matte-target accumulator. The outer union can then safely use
            // NonZero regardless of how individual shapes were specified.
            if (!shapeRings.empty()) {
                Clipper2Lib::ClipperD c(kClipperPrecision);
                c.AddSubject(shapeRings);
                Clipper2Lib::PathsD sClosed, sOpen;
                c.Execute(Clipper2Lib::ClipType::Union,
                          (*wc->toFR)(shape->fillRule()), sClosed, sOpen);
                for (auto& r : sClosed) wc->rings->push_back(std::move(r));
            }
                        break;
                }
            }
            flushRing();
            return true;
        }, &wctx);
        if (!rings.empty()) {
            Clipper2Lib::ClipperD c(kClipperPrecision);
            c.AddSubject(rings);
            Clipper2Lib::PathsD closed, open;
            c.Execute(Clipper2Lib::ClipType::Union,
                      Clipper2Lib::FillRule::NonZero, closed, open);
            rings = std::move(closed);
        }
        return targetPolyCache.emplace(target, std::move(rings)).first->second;
    };

    // Resolve the effective clip polygon for a paint by walking up its
    // ancestor chain and accumulating all clip-scope contributions.
    // Returns {hasClip, polygon}: if hasClip is false, no clipping is applied.
    auto resolveClip = [&](const tvg::Paint* paint) -> std::pair<bool, Clipper2Lib::PathsD> {
        bool has = false;
        Clipper2Lib::PathsD clipRegion;
        for (auto p = paint; p != nullptr && p != picture; p = p->parent()) {
            auto itc = clipScopes.find(p);
            if (itc == clipScopes.end()) continue;
            const auto& scope = itc->second;
            auto& poly = computeTargetPolygon(scope.target);
            if (poly.empty()) {
                // If the target polygon can't be resolved (e.g. stroke-only
                // mask, degenerate geometry), treat this scope as a no-op
                // rather than clipping everything to nothing.
                continue;
            }
            if (!has) {
                has = true;
                if (scope.invert) {
                    // First clip is inverse: need complement. Use a huge
                    // bounding rectangle and subtract the target polygon.
                    // (Alternative: accumulate all clips then subtract.)
                    Clipper2Lib::PathD huge = {
                        {-1e7, -1e7}, {1e7, -1e7}, {1e7, 1e7}, {-1e7, 1e7}
                    };
                    Clipper2Lib::ClipperD c(kClipperPrecision);
                    c.AddSubject(Clipper2Lib::PathsD{huge});
                    c.AddClip(poly);
                    Clipper2Lib::PathsD closed, open;
                    c.Execute(Clipper2Lib::ClipType::Difference,
                              Clipper2Lib::FillRule::NonZero, closed, open);
                    clipRegion = std::move(closed);
                } else {
                    clipRegion = poly;
                }
            } else {
                Clipper2Lib::ClipperD c(kClipperPrecision);
                c.AddSubject(clipRegion);
                c.AddClip(poly);
                Clipper2Lib::PathsD closed, open;
                c.Execute(scope.invert ? Clipper2Lib::ClipType::Difference
                                       : Clipper2Lib::ClipType::Intersection,
                          Clipper2Lib::FillRule::NonZero, closed, open);
                clipRegion = std::move(closed);
            }
        }
        return { has, std::move(clipRegion) };
    };

    // Iterate back-to-front (last visited = top-most in render order).
    for (auto it = collected.rbegin(); it != collected.rend(); ++it) {
        auto& cs = *it;

        // Build the set of trimmed primitives this shape actually draws.
        // We collect them into "pieces" — each piece is one contiguous stroke
        // (may span multiple primitives). For untrimmed shapes, each subpath
        // is one piece. For trimmed shapes, `emitTrimmed` may produce one or
        // two contiguous segments per subpath.
        struct Prim { bool cubic; tvg::Point p0, p1, p2, p3; };
        std::vector<std::vector<Prim>> pieces;
        auto pieceEmitter = [&](bool cubic, tvg::Point a, tvg::Point c1, tvg::Point c2, tvg::Point b) {
            if (pieces.empty() || pieces.back().empty()) {
                pieces.emplace_back();
            } else {
                // Start a new piece if this primitive doesn't continue the previous.
                auto& last = pieces.back().back();
                if (std::hypot(last.p3.x - a.x, last.p3.y - a.y) > 1.0e-5f) {
                    pieces.emplace_back();
                }
            }
            pieces.back().push_back({cubic, a, c1, c2, b});
        };

        if (!cs.hasTrim) {
            for (auto& sp : cs.subpaths) {
                pieces.emplace_back();
                for (auto& p : sp) pieceEmitter(p.cubic, p.p0, p.p1, p.p2, p.p3);
            }
        } else if (cs.simultaneous) {
            for (auto& sp : cs.subpaths) {
                pieces.emplace_back();
                emitTrimmed(sp, cs.trimBegin, cs.trimEnd, pieceEmitter);
            }
        } else {
            std::vector<PathPrim> joined;
            for (auto& sp : cs.subpaths) for (auto& p : sp) joined.push_back(p);
            pieces.emplace_back();
            emitTrimmed(joined, cs.trimBegin, cs.trimEnd, pieceEmitter);
        }

        const bool hasOccluder = !occluder.empty();
        auto [hasClip, clipPoly] = resolveClip(cs.paint);
        if (hasClip && clipPoly.empty()) {
            // Fully clipped out.
        } else if (!hasOccluder && !hasClip) {
            // Emit native primitives (including cubics) to preserve fidelity.
            for (auto& piece : pieces) {
                for (auto& pr : piece) {
                    if (pr.cubic) {
                        auto q0 = worldToOsci(applyMatrix(cs.xform, pr.p0));
                        auto q1 = worldToOsci(applyMatrix(cs.xform, pr.p1));
                        auto q2 = worldToOsci(applyMatrix(cs.xform, pr.p2));
                        auto q3 = worldToOsci(applyMatrix(cs.xform, pr.p3));
                        out.push_back(std::make_unique<osci::CubicBezierCurve>(
                            q0.x, q0.y, q1.x, q1.y, q2.x, q2.y, q3.x, q3.y));
                    } else {
                        out.push_back(std::make_unique<osci::Line>(
                            worldToOsci(applyMatrix(cs.xform, pr.p0)),
                            worldToOsci(applyMatrix(cs.xform, pr.p3))));
                    }
                }
            }
        } else {
            // Flatten pieces into polylines and clip them.
            Clipper2Lib::PathsD subjects;
            for (auto& piece : pieces) {
                if (piece.empty()) continue;
                std::vector<tvg::Point> world;
                world.reserve(piece.size() * 8);
                bool first = true;
                for (auto& pr : piece) {
                    flattenPrim(cs.xform, pr.cubic, pr.p0, pr.p1, pr.p2, pr.p3, world, first);
                    first = false;
                }
                if (world.size() < 2) continue;
                Clipper2Lib::PathD pd;
                pd.reserve(world.size());
                for (auto& p : world) pd.push_back({p.x, p.y});
                subjects.push_back(std::move(pd));
            }

            if (!subjects.empty()) {
                // If we have a clip, first intersect the open paths with it.
                if (hasClip) {
                    Clipper2Lib::ClipperD clipper(kClipperPrecision);
                    clipper.AddOpenSubject(subjects);
                    clipper.AddClip(clipPoly);
                    Clipper2Lib::PathsD closed, open;
                    clipper.Execute(Clipper2Lib::ClipType::Intersection,
                                    Clipper2Lib::FillRule::NonZero,
                                    closed, open);
                    subjects = std::move(open);
                }

                // Then subtract the occluder.
                if (hasOccluder && !subjects.empty()) {
                    Clipper2Lib::ClipperD clipper(kClipperPrecision);
                    clipper.AddOpenSubject(subjects);
                    clipper.AddClip(occluder);
                    Clipper2Lib::PathsD closed, open;
                    clipper.Execute(Clipper2Lib::ClipType::Difference,
                                    Clipper2Lib::FillRule::NonZero,
                                    closed, open);
                    subjects = std::move(open);
                }

                for (auto& pl : subjects) {
                    for (size_t k = 0; k + 1 < pl.size(); ++k) {
                        auto a = worldToOsci({(float)pl[k].x, (float)pl[k].y});
                        auto b = worldToOsci({(float)pl[k + 1].x, (float)pl[k + 1].y});
                        out.push_back(std::make_unique<osci::Line>(a, b));
                    }
                }
            }
        }

        // If this shape has an opaque fill, union its closed polygon(s) into
        // the running occluder so that subsequent (lower-z-order) shapes are
        // clipped against it.
        if (cs.opaqueFill) {
            Clipper2Lib::PathsD fillPaths;
            for (auto& sp : cs.subpaths) {
                if (sp.empty()) continue;
                std::vector<tvg::Point> world;
                world.reserve(sp.size() * 8);
                // Start at first primitive's p0.
                world.push_back(applyMatrix(cs.xform, sp.front().p0));
                for (auto& pr : sp) {
                    flattenPrim(cs.xform, pr.cubic, pr.p0, pr.p1, pr.p2, pr.p3, world, false);
                }
                if (world.size() < 3) continue;
                Clipper2Lib::PathD pd;
                pd.reserve(world.size());
                for (auto& p : world) pd.push_back({p.x, p.y});
                fillPaths.push_back(std::move(pd));
            }
            if (!fillPaths.empty()) {
                // Resolve the shape's own fill rule first so the resulting
                // polygon set is non-overlapping, then union with the
                // running occluder (which is already NonZero-simple).
                Clipper2Lib::PathsD selfClosed, selfOpen;
                {
                    Clipper2Lib::ClipperD self(kClipperPrecision);
                    self.AddSubject(fillPaths);
                    self.Execute(Clipper2Lib::ClipType::Union,
                                 toClipperFillRule(cs.fillRule),
                                 selfClosed, selfOpen);
                }
                Clipper2Lib::ClipperD clipper(kClipperPrecision);
                if (!occluder.empty()) clipper.AddSubject(occluder);
                clipper.AddSubject(selfClosed);
                Clipper2Lib::PathsD closed, open;
                clipper.Execute(Clipper2Lib::ClipType::Union,
                                Clipper2Lib::FillRule::NonZero,
                                closed, open);
                occluder = std::move(closed);
            }
        }
    }

    osci::Shape::removeOutOfBounds(out);
}

void OsciLottieParser::showError(juce::String message) {
    juce::MessageManager::callAsync([message] {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::AlertIconType::WarningIcon, "Error", message);
    });
}

void OsciLottieParser::fallbackShapes() {
    framesCache.clear();
    framesCache.resize(1);
    framesCache[0].push_back(std::make_unique<osci::Line>(-0.5, -0.5, 0.5, 0.5));
    framesCache[0].push_back(std::make_unique<osci::Line>(-0.5, 0.5, 0.5, -0.5));
    totalFrames = 1;
    currentFrame = 0;
}

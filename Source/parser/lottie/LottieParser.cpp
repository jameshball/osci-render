#include "LottieParser.h"
#include <thorvg.h>
#include <unordered_set>
#include <functional>

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

    setFrame(0);
}

OsciLottieParser::~OsciLottieParser() {
    // unique_ptr destroys the animation via AnimationDeleter before releaseThorVG.
    animation.reset();
    releaseThorVG();
}

int OsciLottieParser::getNumFrames() const { return totalFrames; }
int OsciLottieParser::getCurrentFrame() const { return juce::jmax(0, currentFrame); }

void OsciLottieParser::setFrame(int index) {
    if (animation == nullptr) return;

    index = juce::jlimit(0, totalFrames - 1, index);
    if (index == currentFrame && !currentShapes.empty()) return;

    animation->frame(static_cast<float>(index));
    currentFrame = index;

    extractShapesAtCurrentFrame();
}

std::vector<std::unique_ptr<osci::Shape>> OsciLottieParser::draw() {
    std::vector<std::unique_ptr<osci::Shape>> out;
    out.reserve(currentShapes.size());
    for (auto& s : currentShapes) {
        out.push_back(s->clone());
    }
    return out;
}

void OsciLottieParser::extractShapesAtCurrentFrame() {
    currentShapes.clear();
    if (animation == nullptr) return;
    auto picture = animation->picture();
    if (picture == nullptr) return;

    // Force scene-tree update at the current frame by requesting bounds.
    float bx = 0.0f, by = 0.0f, bw = 0.0f, bh = 0.0f;
    picture->bounds(&bx, &by, &bw, &bh);

    // First pass: collect paints that are used as masks or clippers for OTHER
    // paints. Lottie matte/track-matte layers are lowered by thorvg into
    // `Paint::mask()` / `Paint::clip()` relationships, where the target is
    // often a Scene (whose descendant shapes are the actual matte source).
    // Without this filter we get ghost rectangles / duplicate shapes in
    // animations that use mattes.
    std::unordered_set<const tvg::Paint*> maskTargets;
    {
        // Helper: add `paint` and all its descendants to maskTargets.
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
        };
        std::function<void(const tvg::Paint*)> addSubtreeFn = addSubtree;
        CollectCtx collectCtx { &addSubtreeFn };

        std::unique_ptr<tvg::Accessor> scan(tvg::Accessor::gen());
        scan->set(picture, [](const tvg::Paint* paint, void* data) -> bool {
            auto cctx = static_cast<CollectCtx*>(data);
            const tvg::Paint* maskTarget = nullptr;
            if (paint->mask(&maskTarget) != tvg::MaskMethod::None && maskTarget != nullptr) {
                (*cctx->addSubtree)(maskTarget);
            }
            if (auto* clipper = paint->clip()) {
                (*cctx->addSubtree)(clipper);
            }
            return true;
        }, &collectCtx);
    }

    struct Ctx {
        OsciLottieParser* self;
        const tvg::Paint* root;
        const std::unordered_set<const tvg::Paint*>* maskTargets;
    } ctx { this, picture, &maskTargets };

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

        const auto xform = accumulatedTransform(paint, ctx->root);

        const float w = ctx->self->pictureWidth;
        const float h = ctx->self->pictureHeight;
        const float scale = 2.0f / std::max(w, h);
        const float cx = w * 0.5f;
        const float cy = h * 0.5f;

        auto toOsci = [&](tvg::Point p) -> osci::Point {
            auto q = applyMatrix(xform, p);
            return osci::Point((q.x - cx) * scale, -(q.y - cy) * scale, 0.0);
        };

        // Query trim and simultaneous mode.
        float trimBegin = 0.0f, trimEnd = 1.0f;
        bool simultaneous = shape->trimpath(&trimBegin, &trimEnd);
        const bool hasTrim = std::abs(trimEnd - trimBegin - 1.0f) > 1.0e-4f
                             || std::abs(trimBegin) > 1.0e-4f;

        // Emitter that converts a (cubic or line) primitive into osci shapes,
        // applying world transform.
        auto emitPrim = [&](bool cubic, tvg::Point a, tvg::Point c1, tvg::Point c2, tvg::Point b) {
            if (cubic) {
                auto p0 = toOsci(a);
                auto p1 = toOsci(c1);
                auto p2 = toOsci(c2);
                auto p3 = toOsci(b);
                ctx->self->currentShapes.push_back(std::make_unique<osci::CubicBezierCurve>(
                    p0.x, p0.y, p1.x, p1.y, p2.x, p2.y, p3.x, p3.y));
            } else {
                ctx->self->currentShapes.push_back(std::make_unique<osci::Line>(toOsci(a), toOsci(b)));
            }
        };

        // Collect subpaths as lists of primitives.
        std::vector<std::vector<PathPrim>> subpaths;
        std::vector<PathPrim> current;
        tvg::Point cursor{0.0f, 0.0f};
        tvg::Point start{0.0f, 0.0f};
        uint32_t pi = 0;
        auto finishSubpath = [&]() {
            if (!current.empty()) {
                subpaths.push_back(std::move(current));
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

        if (!hasTrim) {
            // Fast path: emit all primitives untouched.
            for (auto& sp : subpaths) {
                for (auto& p : sp) {
                    emitPrim(p.cubic, p.p0, p.p1, p.p2, p.p3);
                }
            }
        } else if (simultaneous) {
            for (auto& sp : subpaths) {
                emitTrimmed(sp, trimBegin, trimEnd, emitPrim);
            }
        } else {
            // Non-simultaneous: treat all subpaths as a single concatenated path.
            std::vector<PathPrim> joined;
            for (auto& sp : subpaths) for (auto& p : sp) joined.push_back(p);
            emitTrimmed(joined, trimBegin, trimEnd, emitPrim);
        }
        return true;
    }, &ctx);

    osci::Shape::removeOutOfBounds(currentShapes);
}

void OsciLottieParser::showError(juce::String message) {
    juce::MessageManager::callAsync([message] {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::AlertIconType::WarningIcon, "Error", message);
    });
}

void OsciLottieParser::fallbackShapes() {
    currentShapes.clear();
    currentShapes.push_back(std::make_unique<osci::Line>(-0.5, -0.5, 0.5, 0.5));
    currentShapes.push_back(std::make_unique<osci::Line>(-0.5, 0.5, 0.5, -0.5));
}

#pragma once

#include "CompositionRenderer.h"
#include "HiddenLineBake.h"
#include "HiddenLineCoverage.h"
#include "HiddenLineCrossings.h"
#include "HiddenLineDepth.h"
#include "HiddenLineFingerprint.h"
#include "HiddenLineHatching.h"
#include "HiddenLineReflections.h"
#include "HiddenLineScene.h"
#include "HiddenLineStyles.h"
#include "HiddenLineDicing.h"
#include "Lighting.h"
#include "../model/Vec3.h"
#include <map>
#include <thread>
#include <unordered_map>

namespace motion::hidden {
// The part of a shape between two points of its progress. The shape itself
// is kept alive by its source.
class ShapePart final : public osci::Shape {
public:
    // `owner` keeps `whole` alive: its source, or the drawing it is of.
    ShapePart(osci::Shape& whole, std::shared_ptr<const void> owner, float from, float to) : shape(&whole), owner(std::move(owner)), start(from), end(to) {}
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
    std::shared_ptr<const void> owner;
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

// Which of a drawing's faces each of its lines runs along, by triangle: two
// for an edge between faces, one for an edge of a surface, none for a line on
// no face. Every line of a flat drawing lies on its one plane.
inline std::vector<std::array<std::int32_t, 2>> facesAlong(const PreparedDrawing& drawing) {
    const auto& mesh = drawing.faces();
    std::vector<std::array<std::int32_t, 2>> along(drawing.shapeCount(), {-1, -1});
    if (mesh.empty()) { return along; }
    if (mesh.planar) {
        for (auto& faces : along) { faces[0] = 0; }
        return along;
    }
    using Key = std::array<std::int64_t, 3>;
    const auto key = [](const osci::Point& point) {
        const auto rounded = [](float value) { return static_cast<std::int64_t>(std::llround(static_cast<double>(value) * 1.0e5)); };
        return Key {rounded(point.x), rounded(point.y), rounded(point.z)};
    };
    const auto edgeKey = [](Key a, Key b) { return a < b ? std::pair {a, b} : std::pair {b, a}; };
    std::map<std::pair<Key, Key>, std::array<std::int32_t, 2>> edges;
    for (std::size_t triangle = 0; triangle < mesh.triangles.size(); ++triangle) {
        const auto index = static_cast<std::int32_t>(triangle);
        for (std::size_t corner = 0; corner < 3; ++corner) {
            const auto& from = mesh.vertices[mesh.triangles[triangle][corner]];
            const auto& to = mesh.vertices[mesh.triangles[triangle][(corner + 1) % 3]];
            auto& faces = edges.try_emplace(edgeKey(key(from), key(to)), std::array<std::int32_t, 2> {-1, -1}).first->second;
            if (faces[0] < 0) {
                faces[0] = index;
            } else if (faces[1] < 0 && faces[0] != index) {
                faces[1] = index;
            }
        }
    }
    for (std::size_t index = 0; index < drawing.shapeCount(); ++index) {
        auto* shape = drawing.shape(index);
        if (shape == nullptr) { continue; }
        const auto found = edges.find(edgeKey(key(shape->nextVector(0)), key(shape->nextVector(1))));
        if (found != edges.end()) { along[index] = found->second; }
    }
    return along;
}

// A shape, or part of one, lit: it draws its own colours (white where it has
// none) times the light reaching it, blended between the light at stops along
// it. One shape for a whole run of light, however its light changes, so the
// beam walks it in even steps: every shape's end takes a sample of its own,
// and many short pieces of one line would bunch samples into beads.
class LitPart final : public osci::Shape {
public:
    struct Stop {
        float at;
        std::array<float, 3> light;
    };
    LitPart(std::shared_ptr<osci::Shape> whole, float from, float to, std::vector<Stop> stops) : shape(std::move(whole)), start(from), end(to), lights(std::move(stops)) {}
    osci::Point nextVector(float progress) override {
        const auto next = std::upper_bound(lights.begin(), lights.end(), progress, [](float value, const Stop& stop) { return value < stop.at; });
        const auto& after = next == lights.end() ? lights.back() : *next;
        const auto& before = next == lights.begin() ? lights.front() : *(next - 1);
        const auto span = after.at - before.at;
        const auto blend = span > 0 ? std::clamp((progress - before.at) / span, 0.0f, 1.0f) : 0.0f;
        std::array<float, 3> light {};
        for (std::size_t channel = 0; channel < 3; ++channel) { light[channel] = before.light[channel] + (after.light[channel] - before.light[channel]) * blend; }
        return litColour(shape->nextVector(start + (end - start) * progress), light);
    }
    void scale(float, float, float) override {}
    void translate(float, float, float) override {}
    float length() override {
        if (len < 0) { len = shape->length() * (end - start); }
        return len;
    }
    std::unique_ptr<osci::Shape> clone() override { return std::make_unique<LitPart>(*this); }
    std::string type() override { return "Lit"; }

private:
    std::shared_ptr<osci::Shape> shape;
    float start, end;
    std::vector<Stop> lights;
};

// Every surface's faces (meshes', and flat drawings' fills) drawn into a
// depth buffer as an eye sees them: a camera, to hide lines, or a light, to
// find its shadows. `world` holds each item's mesh corners in the scene;
// `place(item, point)` puts any point of an item there, for the faces
// something bends, which are diced so they follow it. `whole(surface, eye,
// sources)` is told of every face, or part of one cut where it comes into
// view, drawn flat: its corners in the eye's space and before the chain.
// False when cancelled partway.
template <typename Place, typename Whole, typename Cancelled>
bool drawSurfaces(const std::vector<SceneItem>& items, const std::vector<std::vector<std::optional<Vec3>>>& world, Place&& place, const Eye& eye, const Projection& projection,
    Reach reach, DepthBuffer& depth, Dicer& dicer, Whole&& whole, Cancelled&& cancelled) {
    depth.cover(reach);
    dicer.setReach(reach);
    const auto seen = [&](const std::optional<Vec3>& point) -> std::optional<Vec3> {
        if (!point.has_value()) { return std::nullopt; }
        return eye.see(*point);
    };
    for (std::uint32_t surface = 0; surface < items.size(); ++surface) {
        const auto& item = items[surface];
        if (item.drawing == nullptr || item.drawing->faces().empty()) { continue; }
        const auto& mesh = item.drawing->faces();
        const auto& corners = world[surface];
        for (std::uint32_t face = 0; face < mesh.triangles.size(); ++face) {
            const auto& triangle = mesh.triangles[face];
            if (cancelled()) { return false; }
            depth.tagNext(std::uint64_t {surface} << 32 | face);
            const auto a = seen(corners[triangle[0]]), b = seen(corners[triangle[1]]), c = seen(corners[triangle[2]]);
            if (!item.bent) {
                // Exact through the eye: whole, cut (in perspective) a hair in
                // front of the near plane, so every corner kept, the cut's own
                // included, is in front of it.
                if (!a.has_value() || !b.has_value() || !c.has_value()) { continue; }
                // What each corner kept was before the chain, to place what
                // crosses it: nothing bends the face, so a cut point is as far
                // along its edge before it as after.
                std::array<Vec3, 4> kept {};
                std::array<osci::Point, 4> keptSources {};
                std::size_t count = 0;
                const std::array<Vec3, 3> around {*a, *b, *c};
                const std::array<osci::Point, 3> sources {mesh.vertices[triangle[0]], mesh.vertices[triangle[1]], mesh.vertices[triangle[2]]};
                constexpr auto cut = Projection::nearPlane * (1 + 1.0e-6);
                for (std::size_t index = 0; index < 3; ++index) {
                    const auto& current = around[index];
                    const auto& next = around[(index + 1) % 3];
                    if (projection.keeps(current.z)) {
                        keptSources[count] = sources[index];
                        kept[count++] = current;
                    }
                    if (projection.keeps(current.z) != projection.keeps(next.z)) {
                        const auto along = (cut - current.z) / (next.z - current.z);
                        const auto& from = sources[index];
                        const auto& to = sources[(index + 1) % 3];
                        keptSources[count] = osci::Point(static_cast<float>(from.x + (to.x - from.x) * along), static_cast<float>(from.y + (to.y - from.y) * along), static_cast<float>(from.z + (to.z - from.z) * along));
                        kept[count++] = current + (next - current) * along;
                    }
                }
                std::array<Screen, 4> at {};
                bool drawn = count >= 3;
                for (std::size_t index = 0; index < count && drawn; ++index) {
                    const auto point = projection(kept[index]);
                    drawn = point.has_value();
                    if (drawn) { at[index] = *point; }
                }
                if (!drawn) { continue; }
                depth.triangle(at[0], at[1], at[2]);
                if (count == 4) { depth.triangle(at[0], at[2], at[3]); }
                for (std::size_t piece = 2; piece < count; ++piece) { whole(surface, std::array<Vec3, 3> {kept[0], kept[piece - 1], kept[piece]}, std::array<osci::Point, 3> {keptSources[0], keptSources[piece - 1], keptSources[piece]}); }
                continue;
            }
            // Diced point by point into pieces flat enough, once bent, to draw.
            dicer.dice({mesh.vertices[triangle[0]], projection(a)}, {mesh.vertices[triangle[1]], projection(b)}, {mesh.vertices[triangle[2]], projection(c)},
                [&](const osci::Point& point) { return projection(seen(place(item, point))); },
                [&](const DiceCorner& p, const DiceCorner& q, const DiceCorner& r, bool flat) {
                    depth.triangle(*p.at, *q.at, *r.at);
                    // A piece cut at the near plane reaches far off the
                    // screen: only whole pieces are told of.
                    if (flat) { whole(surface, std::array<Vec3, 3> {projection.unproject(*p.at), projection.unproject(*q.at), projection.unproject(*r.at)}, std::array<osci::Point, 3> {p.source, q.source, r.source}); }
                });
        }
    }
    return true;
}

// What culling works in, kept from frame to frame so a worker's buffers are
// allocated once rather than every frame: the depth buffer, what has been
// drawn, the surfaces gathered for crossings, the lights' shadows, and the
// scratch lists of points and runs. One per thread.
struct Workspace {
    DepthBuffer depth;
    Coverage coverage;
    std::vector<std::optional<Screen>> samples;
    std::vector<Screen> drawn;
    std::vector<std::size_t> drawnStarts;
    std::vector<crossing::Triangle> triangles;
    std::vector<std::array<osci::Point, 3>> sources;
    std::vector<std::optional<Vec3>> eyes;
    std::vector<std::vector<std::optional<Vec3>>> world;
    std::vector<std::pair<double, double>> runs;
    Dicer dicer;
    ShadowBuffers shadows;
    // What stands between mirrors and what they show.
    SceneRays rays;
    // Patterns laid on surfaces, and the frame's drawings of surfaces that
    // draw themselves other than by their edges.
    LaidSurfaces laid;
    std::vector<Styled> styled;
    std::vector<LightView> lights;
    // Which faces each line of a drawing runs along, found once a drawing
    // (whose source is kept alive here, so its address is never another's).
    struct Along {
        std::shared_ptr<const PreparedSource> source;
        std::vector<std::array<std::int32_t, 2>> faces;
    };
    std::unordered_map<const PreparedDrawing*, Along> along;
    const std::vector<std::array<std::int32_t, 2>>& facesAlongOf(const PreparedDrawing& drawing, const std::shared_ptr<const PreparedSource>& source) {
        if (!drawing.shapeFaces().empty()) { return drawing.shapeFaces(); }
        auto found = along.find(&drawing);
        if (found == along.end()) {
            if (along.size() > 1024) { along.clear(); }
            found = along.emplace(&drawing, Along {source, facesAlong(drawing)}).first;
        }
        return found->second.faces;
    }
    // What the lights' shadows were last drawn from: each light's place, and
    // every surface's drawing (its source kept alive, so its address is
    // never another's) and corners. While none changes, nor bends, the
    // shadows are the same and are kept.
    struct ShadowsDrawnFrom {
        bool valid = false;
        std::vector<LightState> lights;
        std::vector<std::shared_ptr<const PreparedSource>> sources;
        std::vector<const PreparedDrawing*> drawings;
        std::vector<std::vector<std::optional<Vec3>>> world;
    } shadowsDrawnFrom;
};

// One frame as the active camera sees it, with hidden lines removed when it
// hides them, and lit when lights shine.
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
//
// Lights see the same surfaces, each from where it is, so whatever something
// blocks from a light is in its shadow. What is kept is then lit, a couple of
// pixels at a time: each part draws its own colour times the light reaching
// it, and parts no light reaches are dropped as hidden ones are.
// None when `cancel` is set partway.
// An eye to see a frame from other than its camera: an editor's view. From
// it nothing is hidden (it shows every line, as an editor does); lights
// light as ever, and mirrors show what they would to it, over `reach`.
struct OtherEye {
    PreparedComposition::Viewpoint eye;
    Reach reach;
};
// The camera's hatching (see HiddenLineHatching.h) on each surface it sees:
// the light at each point of the curve found on the surface the depth buffer
// has there, by its smooth normal; drawn in its clip's colour in the light's,
// as bright as `amount`, its points in the scene.
inline std::vector<Frame::Entry> hatch(const PreparedComposition& composition, double time, const std::vector<SceneItem>& items, const std::vector<std::vector<std::optional<Vec3>>>& world,
    const std::vector<LightView>& lights, bool lit, const Eye& eye, const Projection& projection, Reach reach, const DepthBuffer& depth, double amount, int detail, Workspace& workspace) {
    // Each surface's normals in the scene, as smooth as it is.
    std::vector<std::optional<surface::Normals>> normals(items.size());
    std::vector<std::vector<Vec3>> corners(items.size());
    for (std::size_t index = 0; index < items.size(); ++index) {
        const auto& item = items[index];
        if (item.drawing == nullptr || item.drawing->faces().empty()) { continue; }
        for (const auto& corner : world[index]) { corners[index].push_back(corner.value_or(Vec3 {})); }
        if (item.styled != nullptr) {
            normals[index] = item.styled->normals;
            continue;
        }
        const auto& laid = workspace.laid.of(*item.drawing, item.source);
        normals[index] = laid.smoothing.normals(corners[index], laid.faces);
    }
    const auto look = [&](double x, double y) -> std::optional<hatching::Seen> {
        if (std::abs(x) > reach.x || std::abs(y) > reach.y) { return std::nullopt; }
        const auto nearest = depth.nearestAt(x, y);
        if (!nearest.has_value() || !(nearest->w > 0)) { return std::nullopt; }
        const auto surface = static_cast<std::uint32_t>(nearest->tag >> 32), face = static_cast<std::uint32_t>(nearest->tag & 0xffffffffu);
        if (surface >= items.size() || !normals[surface].has_value()) { return std::nullopt; }
        // A mirror shows the scene, not hatching of its own: as much as it
        // reflects, it has none.
        if (!(items[surface].clip->reflects(time) < 1)) { return std::nullopt; }
        // Where the line from the eye meets the face: exactly on it (the
        // buffer's depth is pushed back, most for a face seen edge on).
        const auto& triangle = items[surface].drawing->faces().triangles[face];
        const auto& a = corners[surface][triangle[0]], & b = corners[surface][triangle[1]], & c = corners[surface][triangle[2]];
        const auto seen = projection.unproject({x, y, nearest->w});
        auto at = eye.position + eye.right * seen.x + eye.up * seen.y + eye.forward * seen.z;
        const auto plane = (b - a).cross(c - a);
        const auto toward = at - eye.position;
        const auto across = plane.dot(toward);
        if (std::abs(across) > 1.0e-12 * plane.length() * toward.length()) { at = eye.position + toward * (plane.dot(a - eye.position) / across); }
        if (!lit) { return hatching::Seen {surface, at, 1}; }
        // Where on its face it is, for the normal there.
        const auto ab = b - a, ac = c - a, ap = at - a;
        const auto d00 = ab.dot(ab), d01 = ab.dot(ac), d11 = ac.dot(ac), d20 = ap.dot(ab), d21 = ap.dot(ac);
        const auto denominator = d00 * d11 - d01 * d01;
        surface::Weights weights {1.0 / 3, 1.0 / 3, 1.0 / 3};
        if (std::abs(denominator) > 1.0e-300) {
            const auto v = std::clamp((d11 * d20 - d01 * d21) / denominator, 0.0, 1.0), w = std::clamp((d00 * d21 - d01 * d20) / denominator, 0.0, 1.0 - v);
            weights = {1 - v - w, v, w};
        }
        Normals facing;
        facing.add(normals[surface]->at(face, weights));
        const auto light = lightAt(lights, at, facing, static_cast<float>(items[surface].clip->lit(time)));
        const auto brightest = std::max({light[0], light[1], light[2]});
        if (!(brightest > 0)) { return hatching::Seen {surface, at, 0}; }
        return hatching::Seen {surface, at, static_cast<double>(brightest), {light[0] / brightest, light[1] / brightest, light[2] / brightest}};
    };
    // The beam draws a cycle in so many samples, and the curve needs one at
    // each of its points: it takes no more than half of them, leaving the rest
    // for everything else.
    const auto beamSamples = composition.sampleRate / std::max(1.0, composition.beamRate);
    const auto strokes = hatching::strokes(std::max(reach.x, reach.y), detail, look, static_cast<std::size_t>(std::max(64.0, beamSamples * .5)));
    // Each surface's strokes, in its clip's colour in the light's, dimmed by
    // the hatching's amount and as much as the surface reflects: on a
    // display with no brightness input, drawn as much more quickly.
    std::vector<std::vector<std::unique_ptr<osci::Shape>>> shapes(items.size());
    std::vector<std::vector<double>> weights(items.size());
    for (const auto& stroke : strokes) {
        const auto& clip = *items[stroke.surface].clip;
        auto colour = composition.applyCompositionEffects(clip.processPoint(osci::Point(0, 0, 0, 1, 1, 1), time), time);
        const auto itself = static_cast<float>(1 - clip.reflects(time));
        const auto weight = composition.scope.xyOnly ? std::max(leastLitWeight, itself * amount) : 1.0;
        colour.r *= itself;
        colour.g *= itself;
        colour.b *= itself;
        const auto point = [&](const hatching::Seen& seen) {
            return osci::Point(static_cast<float>(seen.at.x), static_cast<float>(seen.at.y), static_cast<float>(seen.at.z),
                std::max(0.0f, colour.r) * seen.hue[0] * static_cast<float>(amount), std::max(0.0f, colour.g) * seen.hue[1] * static_cast<float>(amount), std::max(0.0f, colour.b) * seen.hue[2] * static_cast<float>(amount));
        };
        for (std::size_t index = 1; index < stroke.points.size(); ++index) {
            shapes[stroke.surface].push_back(std::make_unique<ColouredLine>(point(stroke.points[index - 1]), point(stroke.points[index])));
            weights[stroke.surface].push_back(weight);
        }
    }
    std::vector<Frame::Entry> entries;
    for (std::size_t surface = 0; surface < items.size(); ++surface) {
        if (shapes[surface].empty()) { continue; }
        entries.push_back({items[surface].index, std::make_shared<PreparedDrawing>(std::move(shapes[surface]), true, weights[surface]), 1});
    }
    return entries;
}

inline std::unique_ptr<Frame> cull(const PreparedComposition& composition, double time, Workspace& workspace, const std::atomic<bool>* cancel = nullptr, const OtherEye* from = nullptr) {
    auto& depth = workspace.depth;
    const auto cancelled = [cancel] { return cancel != nullptr && cancel->load(std::memory_order_relaxed); };
    auto frame = std::make_unique<Frame>();
    const auto* camera = composition.activeCamera(time);
    const auto view = camera != nullptr ? camera->frame(time) : std::nullopt;
    const auto hides = from == nullptr && camera != nullptr && camera->hidesHiddenLines && view.has_value();
    const auto lit = composition.lit(time);
    // A display with no brightness input shows how bright a line is only by
    // how long the beam takes over it (see litWeight).
    const auto xyOnly = composition.scope.xyOnly;
    const auto reflecting = composition.reflects(time);
    const auto restyling = composition.restyles(time);
    // How much the camera shades by hatching over its frame rather than by
    // brightness.
    const auto hatching = camera != nullptr && view.has_value() ? camera->hatching(time) : 0.0;
    const auto frameHatching = from == nullptr ? hatching : 0.0;
    if (!hides && !lit && !reflecting && !restyling && !(hatching > 0)) { return frame; }
    std::vector<SceneItem> items;
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
    const Eye cameraEye = view.has_value() ? Eye {view->position, view->right, view->up, view->forward} : Eye {};
    const Projection cameraProjection {view.has_value() ? view->focalLength : 1.0};
    const auto eye = [&](const PreparedClip& clip, osci::Point point) { return composition.eyePoint(clip.processPoint(point, time), time); };
    const auto screen = [&](const std::optional<Vec3>& point) { return cameraProjection(point); };
    const auto place = [&](const SceneItem& item, const osci::Point& point) { return composition.worldPoint(*item.clip, point, time); };
    // Where a clip's point lands in the frame, with or without a camera.
    const auto onFrame = [&](const PreparedClip& clip, const osci::Point& point) -> std::optional<Screen> {
        if (view.has_value()) { return screen(eye(clip, point)); }
        const auto projected = composition.projectVisible(clip.processPoint(point, time), time);
        if (!projected.has_value()) { return std::nullopt; }
        return Screen {projected->x, projected->y, 1};
    };
    // Every mesh corner in the scene, once, for every eye that sees it.
    auto& world = workspace.world;
    world.resize(items.size());
    for (std::size_t surface = 0; surface < items.size(); ++surface) {
        world[surface].clear();
        const auto& item = items[surface];
        if (item.drawing == nullptr || item.drawing->faces().empty()) { continue; }
        for (const auto& vertex : item.drawing->faces().vertices) { world[surface].push_back(place(item, vertex)); }
    }
    const auto surfaces = std::count_if(items.begin(), items.end(), [](const SceneItem& item) { return item.drawing != nullptr && !item.drawing->faces().empty(); });

    // The lights shining, and what each sees, to find its shadows.
    auto& lights = workspace.lights;
    auto& drawnFrom = workspace.shadowsDrawnFrom;
    // Kept from the last frame while every light casting shadows is where it
    // was and they fall on just the same surfaces.
    const auto sameShadows = [&] {
        if (!drawnFrom.valid || drawnFrom.drawings.size() != items.size() || drawnFrom.world != world) { return false; }
        for (std::size_t surface = 0; surface < items.size(); ++surface) {
            const auto& item = items[surface];
            if (drawnFrom.drawings[surface] != item.drawing || (item.bent && item.drawing != nullptr && !item.drawing->faces().empty())) { return false; }
        }
        std::size_t index = 0;
        for (const auto& light : composition.lights) {
            const auto state = composition.lightAt(light, time);
            if (!state.has_value()) { continue; }
            if (index >= drawnFrom.lights.size()) { return false; }
            const auto& was = drawnFrom.lights[index++];
            if (was.kind != state->kind || was.casts() != state->casts() || was.position != state->position || was.direction != state->direction || was.cone != state->cone) { return false; }
        }
        return index == drawnFrom.lights.size();
    };
    const auto keepShadows = lit && sameShadows();
    if (keepShadows) {
        std::size_t index = 0;
        for (const auto& light : composition.lights) {
            const auto state = composition.lightAt(light, time);
            if (state.has_value()) { lights[index++].state = *state; }
        }
    } else {
        lights.clear();
        workspace.shadows.reset();
        drawnFrom.valid = false;
    }
    if (lit && !keepShadows) {
        for (const auto& light : composition.lights) {
            const auto state = composition.lightAt(light, time);
            if (!state.has_value()) { continue; }
            lights.push_back({*state, {}});
            auto& shining = lights.back();
            if (!state->casts() || surfaces == 0) { continue; }
            const auto noCrossings = [](std::uint32_t, const std::array<Vec3, 3>&, const std::array<osci::Point, 3>&) {};
            const auto see = [&](const Eye& from, const Projection& projection, bool cube) {
                auto& buffer = workspace.shadows.next(cube);
                shining.views.push_back({from, projection, &buffer});
                return drawSurfaces(items, world, place, from, projection, Reach {}, buffer, workspace.dicer, noCrossings, cancelled);
            };
            bool done = true;
            if (state->kind == GeneratorKind::pointLight) {
                for (const auto& face : cubeEyes(state->position)) { done = done && see(face, Projection {1}, true); }
            } else if (state->spot()) {
                // A little past the cone, so its soft edge is all in view.
                constexpr double radians = 3.14159265358979323846 / 180;
                const auto half = std::clamp(state->cone / 2 + 2, 1.0, 87.0);
                done = see(Eye::looking(state->position, state->direction), Projection {1 / std::tan(half * radians)}, false);
            } else {
                // Parallel, framed on every surface's corners as the light sees them.
                auto from = Eye::looking({}, state->direction);
                double left = 1.0e300, right = -1.0e300, bottom = 1.0e300, top = -1.0e300, nearest = 1.0e300, furthest = -1.0e300;
                for (const auto& corners : world) {
                    for (const auto& corner : corners) {
                        if (!corner.has_value()) { continue; }
                        const auto at = from.see(*corner);
                        left = std::min(left, at.x);
                        right = std::max(right, at.x);
                        bottom = std::min(bottom, at.y);
                        top = std::max(top, at.y);
                        nearest = std::min(nearest, at.z);
                        furthest = std::max(furthest, at.z);
                    }
                }
                if (!(right >= left) || !(top >= bottom)) { continue; }
                from.position = from.right * ((left + right) / 2) + from.up * ((bottom + top) / 2) + from.forward * nearest;
                const auto half = std::max({(right - left) / 2, (top - bottom) / 2, 1.0e-9});
                Projection projection {1 / half, true};
                projection.far = furthest - nearest;
                projection.span = std::max(projection.far, 1.0e-6);
                done = see(from, projection, false);
            }
            if (!done) { return nullptr; }
        }
        drawnFrom.lights.clear();
        for (const auto& light : lights) { drawnFrom.lights.push_back(light.state); }
        drawnFrom.sources.clear();
        drawnFrom.drawings.clear();
        for (const auto& item : items) {
            drawnFrom.sources.push_back(item.source);
            drawnFrom.drawings.push_back(item.drawing);
        }
        drawnFrom.world = world;
        drawnFrom.valid = true;
    }

    // Surfaces that draw themselves other than by their edges (see
    // LineStyle), or carry the camera's hatching, drawn again for the frame:
    // their outlines as the eye sees them, from the camera or the other eye.
    auto& styled = workspace.styled;
    styled.resize(std::max(styled.size(), items.size()));
    const auto viewpoint = from != nullptr ? std::optional(from->eye) : composition.viewpoint(time);
    const auto eyeAt = viewpoint.has_value() ? std::optional(viewpoint->position) : std::nullopt;
    bool outlining = false;
    // Another eye hides nothing; when the camera would, it is shown only the
    // near side of what surfaces lay on themselves.
    const auto nearSideOnly = from != nullptr && camera != nullptr && camera->hidesHiddenLines;
    for (std::size_t surface = 0; restyling && surface < items.size(); ++surface) {
        auto& item = items[surface];
        if (item.drawing == nullptr || item.drawing->faces().empty()) { continue; }
        const auto style = item.clip->lines(time);
        if (style.plain()) { continue; }
        auto& into = styled[surface];
        into.place(*item.drawing, item.source, world[surface], workspace.laid);
        restyle(into, item.source, workspace.facesAlongOf(*item.drawing, item.source), style, eyeAt, workspace.laid, nearSideOnly);
        item.drawing = into.drawing.get();
        item.styled = &into;
        outlining = outlining || into.eyeOnlyFrom < into.drawing->shapeCount();
    }
    // What stands between two points, for mirrors, and for outlines: their
    // own faces graze the line to the eye, so are found along it, not in the
    // depth buffer.
    const auto rayed = reflecting || (hides && outlining);
    if (rayed) { buildRays(items, world, workspace.rays); }

    // What hides, over what the Scope shows. Its triangles are kept too when
    // two or more surfaces could cut through each other.
    const auto reach = composition.hiddenLineReach;
    auto& surfaceTriangles = workspace.triangles;
    auto& surfaceSources = workspace.sources;
    surfaceTriangles.clear();
    surfaceSources.clear();
    std::vector<std::vector<crossing::Piece>> crossings;
    if (hides || frameHatching > 0) {
        const auto gather = [&](std::uint32_t surface, const std::array<Vec3, 3>& corners, const std::array<osci::Point, 3>& sources) {
            if (surfaces < 2 || !hides) { return; }
            surfaceTriangles.push_back({corners, surface});
            surfaceSources.push_back(sources);
        };
        if (!drawSurfaces(items, world, place, cameraEye, cameraProjection, reach, depth, workspace.dicer, gather, cancelled)) { return nullptr; }
        if (hides && !depth.isEmpty()) {
            dropUnseen(surfaceTriangles, surfaceSources, depth, view->focalLength);
            crossings = crossing::runs(surfaceTriangles, view->focalLength, reach, cancel);
            if (cancelled()) { return nullptr; }
        }
    }
    if (frameHatching > 0 && !depth.isEmpty()) {
        frame->hatching = hatch(composition, time, items, world, lights, lit, cameraEye, cameraProjection, reach, depth, frameHatching, camera->hatchDetail(time), workspace);
        if (cancelled()) { return nullptr; }
    }
    // Hiding lines, a camera that sees no surface hides nothing.
    const auto hiding = hides && !depth.isEmpty();
    if (!hiding && !lit && !reflecting && !restyling) { return frame; }

    // What is hidden, and what would be drawn twice: every line, every pixel
    // along its true path. A point shows where nothing is in front of it and
    // no other clip's line already drawn runs along it. `place(t)` is a point
    // of a run at progress t; the parts that show come back as intervals of
    // t, and are kept as drawn until `commitDrawn`. Not hiding lines, all of
    // every line shows.
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
    // `seen(t, point)` says whether nothing hides the point at t.
    const auto runsSeen = [&](auto&& at, double length, auto&& seen) {
        runs.clear();
        if (!hiding) {
            runs.emplace_back(0.0, 1.0);
            return;
        }
        const auto steps = std::clamp(static_cast<int>(std::ceil(length / testStep)), 1, 65536);
        samples.clear();
        for (int step = 0; step <= steps; ++step) { samples.push_back(at(static_cast<double>(step) / steps)); }
        // What the Scope doesn't show is dropped too: drawn, it would only
        // take the beam's time from what it does.
        const auto showing = [&](int step) {
            const auto& point = samples[static_cast<std::size_t>(step)];
            if (!point.has_value() || std::abs(point->x) > reach.x * DepthBuffer::overscan || std::abs(point->y) > reach.y * DepthBuffer::overscan || !seen(static_cast<double>(step) / steps, *point)) { return false; }
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
    const auto visibleRuns = [&](auto&& at, double length) {
        runsSeen(at, length, [&](double, const Screen& point) { return depth.shows(point); });
    };
    const auto commitDrawn = [&] {
        if (!hiding) { return; }
        drawnStarts.push_back(drawn.size());
        for (std::size_t index = 0; index + 1 < drawnStarts.size(); ++index) { coverage.add(std::span<const Screen>(drawn).subspan(drawnStarts[index], drawnStarts[index + 1] - drawnStarts[index])); }
        drawn.clear();
        drawnStarts.clear();
    };
    // A piece kept, whether culling cut it, and the faces it runs along, in
    // the scene, to light it by.
    struct Kept {
        std::unique_ptr<osci::Shape> shape;
        bool cut;
        Normals normals;
        double weight = 1;
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
            const auto next = onFrame(clip, shape.nextVector(probe / 4.0f));
            if (previous.has_value() && next.has_value()) { length += std::hypot(next->x - previous->x, next->y - previous->y); }
            previous = next;
        }
        return length;
    };
    // Two straight pieces drawn as one when where they meet is within a pixel
    // of the line joining their far ends, on screen.
    const auto joinable = [&](osci::Shape& a, osci::Shape& b, const PreparedClip& clip) {
        if (dynamic_cast<osci::Line*>(&a) == nullptr || dynamic_cast<osci::Line*>(&b) == nullptr) { return false; }
        const auto p = onFrame(clip, a.nextVector(0)), q = onFrame(clip, a.nextVector(1)), r = onFrame(clip, b.nextVector(1));
        if (!p.has_value() || !q.has_value() || !r.has_value()) { return false; }
        const auto dx = r->x - p->x, dy = r->y - p->y;
        const auto span = dx * dx + dy * dy;
        const auto t = span > 0 ? std::clamp(((q->x - p->x) * dx + (q->y - p->y) * dy) / span, 0.0, 1.0) : 0.0;
        return std::hypot(q->x - (p->x + dx * t), q->y - (p->y + dy * t)) <= DepthBuffer::pixel;
    };
    const auto keep = [&](std::vector<Kept>& pieces, const PreparedClip& clip) {
        std::vector<std::unique_ptr<osci::Shape>> shapes;
        std::vector<double> weights;
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
                for (auto& piece : run) {
                    shapes.push_back(std::move(piece.shape));
                    weights.push_back(piece.weight);
                }
                continue;
            }
            while (run.size() > 1 && onScreen(*run.front().shape, clip) < sliver) { run.erase(run.begin()); }
            while (run.size() > 1 && onScreen(*run.back().shape, clip) < sliver) { run.pop_back(); }
            std::vector<std::unique_ptr<osci::Shape>> joined;
            std::vector<double> joinedWeights;
            for (auto& piece : run) {
                if (!joined.empty() && joinable(*joined.back(), *piece.shape, clip)) {
                    joined.back() = std::make_unique<osci::Line>(joined.back()->nextVector(0), piece.shape->nextVector(1));
                } else {
                    joined.push_back(std::move(piece.shape));
                    joinedWeights.push_back(piece.weight);
                }
            }
            double length = 0;
            for (auto& shape : joined) { length += onScreen(*shape, clip); }
            if (length >= shortestOnScreen) {
                for (auto& shape : joined) { shapes.push_back(std::move(shape)); }
                weights.insert(weights.end(), joinedWeights.begin(), joinedWeights.end());
            }
        }
        return std::make_shared<const PreparedDrawing>(std::move(shapes), true, weights);
    };
    const auto mix = [](const osci::Point& a, const osci::Point& b, double t) {
        const auto f = static_cast<float>(t);
        return osci::Point(a.x + (b.x - a.x) * f, a.y + (b.y - a.y) * f, a.z + (b.z - a.z) * f, a.r + (b.r - a.r) * f, a.g + (b.g - a.g) * f, a.b + (b.b - a.b) * f);
    };
    // Each surface's faces' normals in the scene, to light the lines on them.
    std::vector<std::vector<std::optional<Vec3>>> faceNormals(items.size());
    if (lit) {
        for (std::size_t surface = 0; surface < items.size(); ++surface) {
            const auto& item = items[surface];
            if (item.drawing == nullptr || item.drawing->faces().empty()) { continue; }
            const auto& corners = world[surface];
            for (const auto& triangle : item.drawing->faces().triangles) {
                const auto& a = corners[triangle[0]];
                const auto& b = corners[triangle[1]];
                const auto& c = corners[triangle[2]];
                faceNormals[surface].push_back(a.has_value() && b.has_value() && c.has_value() ? normalOf(*a, *b, *c) : std::nullopt);
            }
        }
    }
    const auto normalsOf = [&](std::size_t surface, std::size_t shape) {
        Normals normals;
        if (!lit || items[surface].drawing == nullptr || faceNormals[surface].empty()) { return normals; }
        if (items[surface].styled != nullptr) {
            const auto smooth = items[surface].styled->normalsOf(shape);
            if (smooth.has_value()) { return *smooth; }
        }
        const auto& along = workspace.facesAlongOf(*items[surface].drawing, items[surface].source);
        if (shape >= along.size()) { return normals; }
        for (const auto face : along[shape]) {
            if (face >= 0 && static_cast<std::size_t>(face) < faceNormals[surface].size()) { normals.add(faceNormals[surface][static_cast<std::size_t>(face)]); }
        }
        return normals;
    };
    auto& eyes = workspace.eyes;
    // Every clip's own lines first, then where surfaces cut through each
    // other: a crossing along an edge already drawn is the one left out.
    std::vector<std::vector<Kept>> keptBy(items.size());
    std::vector<char> lostBy(items.size(), 0);
    // A point source's whole length, to give it the share of the beam its
    // runs that show are of it.
    std::vector<double> wholeBy(items.size(), -1);
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
            if (hiding) {
                for (std::size_t index = 0; index < count; ++index) { eyes.push_back(eye(*item.clip, pointAt(index))); }
            }
            double whole = 0;
            for (std::size_t index = 0; index < count; ++index) {
                const auto a = pointAt(index), b = pointAt(index + 1);
                if (dark(a) || dark(b) || (a.x == b.x && a.y == b.y && a.z == b.z)) { continue; }
                whole += std::hypot(b.x - a.x, b.y - a.y, b.z - a.z);
                if (!hiding) {
                    kept.push_back({std::make_unique<ColouredLine>(a, b), false, {}});
                    continue;
                }
                const auto& ea = eyes[index];
                const auto& eb = eyes[(index + 1) % count];
                const auto sa = screen(ea), sb = screen(eb);
                const auto length = sa.has_value() && sb.has_value() ? std::hypot(sb->x - sa->x, sb->y - sa->y) : 1.0;
                visibleRuns([&](double t) {
                    if (item.bent) { return screen(eye(*item.clip, mix(a, b, t))); }
                    return ea.has_value() && eb.has_value() ? screen(*ea + (*eb - *ea) * t) : std::optional<Screen>();
                }, length);
                if (runs.size() == 1 && runs.front().first <= 0 && runs.front().second >= 1) {
                    kept.push_back({std::make_unique<ColouredLine>(a, b), false, {}});
                    continue;
                }
                lost = true;
                for (const auto& [from, to] : runs) { kept.push_back({std::make_unique<ColouredLine>(mix(a, b, from), mix(a, b, to)), true, {}}); }
            }
            commitDrawn();
            wholeBy[surface] = whole;
            lostBy[surface] = lost;
            continue;
        }
        // How bright the clip's style draws a line (its Edges, Outline or
        // pattern), as beam time where there is no brightness to dim.
        const auto amountOf = [&](osci::Shape* shape) -> double {
            const auto* dimmed = xyOnly ? dynamic_cast<const DimmedShape*>(shape) : nullptr;
            return dimmed != nullptr ? std::max(static_cast<double>(dimmed->gain()), leastLitWeight) : 1.0;
        };
        // A line usually starts where the last ended: its end is reused.
        osci::Point lastEnd;
        std::optional<Vec3> lastEye;
        for (std::size_t index = 0; index < item.drawing->shapeCount(); ++index) {
            auto* shape = item.drawing->shape(index);
            if (shape == nullptr || !(shape->length() > 0)) { continue; }
            if (!hiding) {
                kept.push_back({shape->clone(), false, normalsOf(surface, index), amountOf(shape)});
                continue;
            }
            const auto line = dynamic_cast<osci::Line*>(shape) != nullptr;
            const auto start = shape->nextVector(0), end = shape->nextVector(1);
            const auto from = lastEye.has_value() && start.x == lastEnd.x && start.y == lastEnd.y && start.z == lastEnd.z ? lastEye : eye(*item.clip, start);
            const auto to = eye(*item.clip, end);
            lastEnd = end;
            lastEye = to;
            if (item.styled != nullptr && index >= item.styled->eyeOnlyFrom && rayed && eyeAt.has_value()) {
                // An outline: hidden by what the line to the eye meets.
                const SceneRays::Near grazed {surface, item.styled->grazing(static_cast<std::uint32_t>(item.drawing->shapeFaces()[index][0]))};
                const auto sa = screen(from), sb = screen(to);
                const auto length = sa.has_value() && sb.has_value() ? std::hypot(sb->x - sa->x, sb->y - sa->y) : 2 * (reach.x + reach.y);
                runsSeen([&](double t) { return from.has_value() && to.has_value() ? screen(*from + (*to - *from) * t) : std::optional<Screen>(); }, length, [&](double t, const Screen&) {
                    const auto at = place(item, shape->nextVector(static_cast<float>(t)));
                    return at.has_value() && !workspace.rays.blocked(*at, *eyeAt, 1.0e-6, 0, SceneRays::none, grazed);
                });
            } else if (line && !item.bent) {
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
                kept.push_back({shape->clone(), false, normalsOf(surface, index), amountOf(shape)});
                continue;
            }
            lost = true;
            for (const auto& [a, b] : runs) {
                if (line) {
                    kept.push_back({std::make_unique<osci::Line>(shape->nextVector(static_cast<float>(a)), shape->nextVector(static_cast<float>(b))), true, normalsOf(surface, index).part(a, b), amountOf(shape)});
                } else {
                    kept.push_back({std::make_unique<ShapePart>(*shape, item.styled != nullptr ? std::shared_ptr<const void>(item.styled->drawing) : std::shared_ptr<const void>(item.source), static_cast<float>(a), static_cast<float>(b)), true, normalsOf(surface, index).part(a, b), amountOf(shape)});
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
            // It lies on the face it was found on.
            Normals normals;
            if (lit) {
                const auto a = place(items[surface], corners[0]), b = place(items[surface], corners[1]), c = place(items[surface], corners[2]);
                if (a.has_value() && b.has_value() && c.has_value()) { normals.add(normalOf(*a, *b, *c)); }
            }
            for (const auto& [a, b] : runs) {
                lostBy[surface] = 1;
                kept.push_back({std::make_unique<osci::Line>(mix(from, to, a), mix(from, to, b)), true, normals});
            }
        }
    }

    // Lit: each piece kept, a couple of pixels at a time, the light reaching
    // it found; pieces of it no light reaches dropped, and each run of the
    // rest drawn as one shape, its light blended between as few stops as keep
    // it within a little of what it is all along.
    if (lit) {
        constexpr double litStep = 2 * DepthBuffer::pixel;
        constexpr float dark = 1.0f / 512;
        constexpr double tolerance = 1.0 / 64;
        std::vector<double> ts;
        std::vector<std::array<float, 3>> light;
        std::vector<double> shades;
        std::vector<std::size_t> breaks;
        for (std::uint32_t surface = 0; surface < items.size(); ++surface) {
            if (cancelled()) { return nullptr; }
            const auto& item = items[surface];
            std::vector<Kept> litPieces;
            // What lights do to it: at 0 it keeps its own colour, lit or not.
            const auto response = static_cast<float>(item.clip->lit(time));
            for (auto& piece : keptBy[surface]) {
                std::shared_ptr<osci::Shape> shape = std::move(piece.shape);
                // Its length in the frame, from a few points of it.
                double length = 0;
                std::optional<Screen> previous;
                for (int probe = 0; probe <= 8; ++probe) {
                    const auto next = onFrame(*item.clip, shape->nextVector(probe / 8.0f));
                    if (previous.has_value() && next.has_value()) { length += std::hypot(next->x - previous->x, next->y - previous->y); }
                    previous = next;
                }
                const auto steps = std::clamp(static_cast<int>(std::ceil(length * 1.25 / litStep)), 1, 4096);
                ts.resize(static_cast<std::size_t>(steps) + 1);
                light.resize(static_cast<std::size_t>(steps) + 1);
                shades.resize(static_cast<std::size_t>(steps) + 1);
                // A straight piece nothing bends is straight in the scene too:
                // its points there are blended between its ends'.
                std::optional<Vec3> startsAt, endsAt;
                if (!item.bent && dynamic_cast<osci::Line*>(shape.get()) != nullptr) {
                    startsAt = place(item, shape->nextVector(0));
                    endsAt = place(item, shape->nextVector(1));
                }
                const auto straight = startsAt.has_value() && endsAt.has_value();
                const auto lightStep = [&](int step) {
                    const auto t = static_cast<double>(step) / steps;
                    const auto at = straight ? std::optional<Vec3>(*startsAt + (*endsAt - *startsAt) * t) : place(item, shape->nextVector(static_cast<float>(t)));
                    double shade = 0;
                    ts[static_cast<std::size_t>(step)] = t;
                    light[static_cast<std::size_t>(step)] = at.has_value() ? lightAt(lights, *at, piece.normals.at(t), response, &shade) : std::array<float, 3> {};
                    shades[static_cast<std::size_t>(step)] = at.has_value() ? shade : -1;
                };
                // Found finely only where their ends' shadows differ, either
                // is dark or their light differs by much: away from a
                // shadow's edge, light changes smoothly along a line.
                findSparsely(steps, lightStep, [&](int from, int to) {
                    const auto& a = light[static_cast<std::size_t>(from)];
                    const auto& b = light[static_cast<std::size_t>(to)];
                    constexpr float alike = 1.0f / 64;
                    const auto darkEnd = std::max({a[0], a[1], a[2]}) < dark || std::max({b[0], b[1], b[2]}) < dark;
                    return darkEnd || std::abs(shades[static_cast<std::size_t>(from)] - shades[static_cast<std::size_t>(to)]) > 1.0e-6
                        || std::abs(a[0] - b[0]) > alike || std::abs(a[1] - b[1]) > alike || std::abs(a[2] - b[2]) > alike;
                }, [&](int step, int from, int to, float share) {
                    ts[static_cast<std::size_t>(step)] = static_cast<double>(step) / steps;
                    for (std::size_t channel = 0; channel < 3; ++channel) {
                        light[static_cast<std::size_t>(step)][channel] = light[static_cast<std::size_t>(from)][channel] + (light[static_cast<std::size_t>(to)][channel] - light[static_cast<std::size_t>(from)][channel]) * share;
                    }
                });
                const auto isDark = [&](std::size_t index) { return std::max({light[index][0], light[index][1], light[index][2]}) < dark; };
                // The parts any light reaches: from halfway to the dark either side.
                const auto firstMade = litPieces.size();
                bool partial = false;
                std::size_t index = 0;
                while (index < ts.size()) {
                    if (isDark(index)) {
                        partial = true;
                        ++index;
                        continue;
                    }
                    auto last = index;
                    while (last + 1 < ts.size() && !isDark(last + 1)) { ++last; }
                    // Breaks where its light strays from a straight blend.
                    breaks.clear();
                    breaks.push_back(index);
                    std::vector<std::pair<std::size_t, std::size_t>> pending {{index, last}};
                    while (!pending.empty()) {
                        const auto [low, high] = pending.back();
                        pending.pop_back();
                        double worst = 0;
                        std::size_t at = low;
                        for (auto middle = low + 1; middle < high; ++middle) {
                            const auto blend = (ts[middle] - ts[low]) / (ts[high] - ts[low]);
                            for (std::size_t channel = 0; channel < 3; ++channel) {
                                const auto straight = light[low][channel] + (light[high][channel] - light[low][channel]) * blend;
                                const auto stray = std::abs(light[middle][channel] - straight);
                                if (stray > worst) {
                                    worst = stray;
                                    at = middle;
                                }
                            }
                        }
                        if (worst > tolerance) {
                            breaks.push_back(at);
                            pending.push_back({low, at});
                            pending.push_back({at, high});
                        }
                    }
                    breaks.push_back(last);
                    std::sort(breaks.begin(), breaks.end());
                    breaks.erase(std::unique(breaks.begin(), breaks.end()), breaks.end());
                    const auto from = index == 0 ? 0.0 : (ts[index - 1] + ts[index]) * .5;
                    const auto to = last + 1 == ts.size() ? 1.0 : (ts[last] + ts[last + 1]) * .5;
                    const auto positionOf = [&](std::size_t sample) { return sample == index ? from : sample == last ? to : ts[sample]; };
                    std::vector<LitPart::Stop> stops;
                    for (const auto at : breaks) { stops.push_back({static_cast<float>((positionOf(at) - from) / std::max(to - from, 1.0e-12)), light[at]}); }
                    if (xyOnly && breaks.size() >= 2) {
                        // Its light shown by how fast the beam goes: a piece
                        // between each pair of breaks, each as slow as it is
                        // lit there, so a line brightens and fades along it.
                        const auto lightOf = [&](std::size_t sample) { return std::max({light[sample][0], light[sample][1], light[sample][2]}); };
                        for (std::size_t stop = 0; stop + 1 < breaks.size(); ++stop) {
                            const auto a = breaks[stop], b = breaks[stop + 1];
                            const auto weight = litWeight((lightOf(a) + lightOf(b)) * .5, true) * piece.weight;
                            std::vector<LitPart::Stop> ends {{0.0f, light[a]}, {1.0f, light[b]}};
                            litPieces.push_back({std::make_unique<LitPart>(shape, static_cast<float>(positionOf(a)), static_cast<float>(positionOf(b)), std::move(ends)), piece.cut, {}, weight});
                        }
                        index = last + 1;
                        continue;
                    }
                    float brightest = 0;
                    for (auto sample = index; sample <= last; ++sample) { brightest = std::max({brightest, light[sample][0], light[sample][1], light[sample][2]}); }
                    const auto weight = litWeight(brightest, xyOnly) * (xyOnly ? piece.weight : 1.0);
                    litPieces.push_back({std::make_unique<LitPart>(shape, static_cast<float>(from), static_cast<float>(to), std::move(stops)), piece.cut, {}, weight});
                    index = last + 1;
                }
                if (partial) {
                    for (auto made = firstMade; made < litPieces.size(); ++made) { litPieces[made].cut = true; }
                }
            }
            keptBy[surface] = std::move(litPieces);
            lostBy[surface] = 1;
        }
    }
    for (std::uint32_t surface = 0; surface < items.size(); ++surface) {
        // A mirror shows the scene in place of itself: its own lines are
        // as much dimmer as it reflects, and gone when it reflects wholly.
        // A surface drawn again for the frame is listed as drawn.
        if (items[surface].styled != nullptr) { lostBy[surface] = 1; }
        const auto itself = 1 - items[surface].clip->reflects(time);
        if (itself < 1) {
            auto& pieces = keptBy[surface];
            if (itself > 0) {
                for (auto& piece : pieces) {
                    piece.shape = std::make_unique<DimmedShape>(std::move(piece.shape), static_cast<float>(itself));
                    piece.weight *= litWeight(itself, xyOnly);
                }
            } else {
                pieces.clear();
            }
            lostBy[surface] = 1;
        }
        if (lostBy[surface] == 0) { continue; }
        auto shown = keep(keptBy[surface], *items[surface].clip);
        const auto whole = wholeBy[surface];
        const auto fraction = whole < 0 ? 1.0 : whole > 0 ? shown->length() / whole : 0.0;
        frame->drawings.push_back({items[surface].index, std::move(shown), fraction});
    }
    // What mirrors show.
    if (reflecting && viewpoint.has_value()) {
        auto shown = reflect(composition, time, items, world, place, normalsOf, lights, lit, *viewpoint, from != nullptr ? from->reach : composition.hiddenLineReach, hiding ? &depth : nullptr, workspace.rays, workspace.laid, cancelled);
        if (!shown.has_value()) { return nullptr; }
        frame->reflections = std::move(*shown);
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

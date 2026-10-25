#pragma once

#include "CompositionRenderer.h"
#include "HiddenLineBake.h"
#include "HiddenLineCoverage.h"
#include "HiddenLineCrossings.h"
#include "HiddenLineDepth.h"
#include "HiddenLineHatching.h"
#include "HiddenLineReflections.h"
#include "HiddenLineScene.h"
#include "HiddenLineShapes.h"
#include "HiddenLineStyles.h"
#include "HiddenLineDicing.h"
#include "HiddenLineTour.h"
#include "Lighting.h"
#include "../model/Vec3.h"
#include <map>
#include <numbers>
#include <span>
#include <unordered_map>

namespace motion::hidden {
// Whether an effect can bend straight lines: one that only colours points,
// or moves them all alike (turning, scaling, sliding or skewing), keeps a
// line straight and a face flat, so its faces need no dicing.
inline bool bends(const PreparedEffect& effect) {
    if (!effect.enabled) { return false; }
    switch (effect.kind) {
        case EffectKind::colour:
        case EffectKind::skew:
        // Each copy is the shape made smaller and moved.
        case EffectKind::duplicator:
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
    constexpr double slight = 3 * DepthBuffer::pixel;
    std::size_t kept = 0;
    for (std::size_t index = 0; index < triangles.size(); ++index) {
        const auto& eye = triangles[index].eye;
        const auto at = [&](Vec3 point) { return Screen {point.x / point.z * focalLength, point.y / point.z * focalLength, 1 / point.z}; };
        const std::array<Screen, 3> corners {at(eye[0]), at(eye[1]), at(eye[2])};
        const auto reach = std::max({std::abs(corners[1].x - corners[0].x), std::abs(corners[2].x - corners[0].x), std::abs(corners[1].y - corners[0].y), std::abs(corners[2].y - corners[0].y)});
        bool seen = reach > slight;
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

// A frame's bent surfaces, diced once in the scene for every view that
// draws them (see Dicer): each surface's pieces, face by face.
struct BentMesh {
    struct Piece {
        std::array<Vec3, 3> at;
        std::array<osci::Point, 3> source;
    };
    struct Surface {
        std::vector<Piece> pieces;
        // Where each face's pieces start, and where the last ends.
        std::vector<std::uint32_t> starts;
    };
    std::vector<Surface> surfaces;

    std::span<const Piece> of(std::size_t surface, std::size_t face) const {
        if (surface >= surfaces.size() || face + 1 >= surfaces[surface].starts.size()) { return {}; }
        const auto& starts = surfaces[surface].starts;
        return std::span<const Piece>(surfaces[surface].pieces).subspan(starts[face], starts[face + 1] - starts[face]);
    }
};

// Dices every bent surface's faces once, for `views`: `world` holds each
// item's mesh corners in the scene, and `place(item, point)` puts any point
// of an item there. False when cancelled partway.
template <typename Place, typename Cancelled>
bool diceBent(const std::vector<SceneItem>& items, const std::vector<std::vector<std::optional<Vec3>>>& world, Place&& place, std::span<const DiceView> views, Dicer& dicer, BentMesh& bent, Cancelled&& cancelled) {
    bent.surfaces.resize(std::max(bent.surfaces.size(), items.size()));
    for (auto& surface : bent.surfaces) {
        surface.pieces.clear();
        surface.starts.clear();
    }
    dicer.begin(views);
    for (std::size_t surface = 0; surface < items.size(); ++surface) {
        const auto& item = items[surface];
        if (!item.bent || item.drawing == nullptr || item.drawing->faces().empty() || views.empty()) { continue; }
        const auto& mesh = item.drawing->faces();
        const auto& corners = world[surface];
        auto& into = bent.surfaces[surface];
        dicer.nextSurface();
        for (const auto& triangle : mesh.triangles) {
            if (cancelled()) { return false; }
            into.starts.push_back(static_cast<std::uint32_t>(into.pieces.size()));
            const auto& a = corners[triangle[0]], & b = corners[triangle[1]], & c = corners[triangle[2]];
            if (!a.has_value() || !b.has_value() || !c.has_value()) { continue; }
            const auto diced = dicer.dice({mesh.vertices[triangle[0]], *a}, {mesh.vertices[triangle[1]], *b}, {mesh.vertices[triangle[2]], *c},
                [&](const osci::Point& point) { return place(item, point); },
                [&](const DiceCorner& p, const DiceCorner& q, const DiceCorner& r) { into.pieces.push_back({{p.at, q.at, r.at}, {p.source, q.source, r.source}}); }, cancelled);
            if (!diced) { return false; }
        }
        into.starts.push_back(static_cast<std::uint32_t>(into.pieces.size()));
    }
    return true;
}

// A flat triangle in an eye's space into a depth buffer: whole, cut (in
// perspective) a hair in front of the near plane, so every corner kept, the
// cut's own included, is in front of it. A cut point is as far along its
// edge before the chain as after: the triangle is flat. `whole(eye, sources)`
// is told of each piece drawn. `dropOffFrame`: left out when wholly past one
// side of the view (a bent surface's many small pieces).
template <typename Whole>
void drawFlat(const Projection& projection, Reach reach, DepthBuffer& depth, const std::array<Vec3, 3>& around, const std::array<osci::Point, 3>& sources, bool dropOffFrame, Whole&& whole) {
    std::array<Vec3, 4> kept {};
    std::array<osci::Point, 4> keptSources {};
    std::size_t count = 0;
    constexpr auto cut = nearPlane * (1 + 1.0e-6);
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
    if (count < 3) { return; }
    std::array<Screen, 4> at {};
    for (std::size_t index = 0; index < count; ++index) {
        const auto point = projection(kept[index]);
        if (!point.has_value()) { return; }
        at[index] = *point;
    }
    if (dropOffFrame) {
        const auto x = reach.x * DepthBuffer::overscan, y = reach.y * DepthBuffer::overscan;
        const auto past = [&](auto&& test) { return std::all_of(at.begin(), at.begin() + static_cast<std::ptrdiff_t>(count), test); };
        if (past([x](const Screen& s) { return s.x < -x; }) || past([x](const Screen& s) { return s.x > x; }) || past([y](const Screen& s) { return s.y < -y; }) || past([y](const Screen& s) { return s.y > y; })) { return; }
    }
    depth.triangle(at[0], at[1], at[2]);
    if (count == 4) { depth.triangle(at[0], at[2], at[3]); }
    for (std::size_t piece = 2; piece < count; ++piece) { whole(std::array<Vec3, 3> {kept[0], kept[piece - 1], kept[piece]}, std::array<osci::Point, 3> {keptSources[0], keptSources[piece - 1], keptSources[piece]}); }
}

// Every surface's faces (meshes', and flat drawings' fills) drawn into a
// depth buffer as an eye sees them: a camera, to hide lines, or a light, to
// find its shadows. `world` holds each item's mesh corners in the scene;
// the faces something bends are drawn as `bent` diced them, so they follow
// it. `whole(surface, eye, sources)` is told of every face, or part of one
// cut where it comes into view, drawn flat: its corners in the eye's space
// and before the chain. `solidOnly`: only the faces of what is solid (see
// SceneItem::solid), for what hides lines. False when cancelled partway.
template <typename Whole, typename Cancelled>
bool drawSurfaces(const std::vector<SceneItem>& items, const std::vector<std::vector<std::optional<Vec3>>>& world, const BentMesh& bent, const Eye& eye, const Projection& projection,
    Reach reach, DepthBuffer& depth, Whole&& whole, Cancelled&& cancelled, bool solidOnly = false) {
    depth.cover(reach);
    const auto draw = [&](std::uint32_t surface, const std::array<Vec3, 3>& around, const std::array<osci::Point, 3>& sources, bool dropOffFrame) {
        drawFlat(projection, reach, depth, around, sources, dropOffFrame, [&](const std::array<Vec3, 3>& corners, const std::array<osci::Point, 3>& from) { whole(surface, corners, from); });
    };
    for (std::uint32_t surface = 0; surface < items.size(); ++surface) {
        const auto& item = items[surface];
        if (item.drawing == nullptr || item.drawing->faces().empty() || (solidOnly && !(item.solid > 0))) { continue; }
        const auto& mesh = item.drawing->faces();
        const auto& corners = world[surface];
        for (std::uint32_t face = 0; face < mesh.triangles.size(); ++face) {
            if (cancelled()) { return false; }
            depth.tagNext(std::uint64_t {surface} << 32 | face);
            if (item.bent) {
                for (const auto& piece : bent.of(surface, face)) { draw(surface, {eye.see(piece.at[0]), eye.see(piece.at[1]), eye.see(piece.at[2])}, piece.source, true); }
                continue;
            }
            const auto& triangle = mesh.triangles[face];
            const auto& a = corners[triangle[0]], & b = corners[triangle[1]], & c = corners[triangle[2]];
            if (!a.has_value() || !b.has_value() || !c.has_value()) { continue; }
            draw(surface, {eye.see(*a), eye.see(*b), eye.see(*c)}, {mesh.vertices[triangle[0]], mesh.vertices[triangle[1]], mesh.vertices[triangle[2]]}, false);
        }
    }
    return true;
}

// What culling works in, kept from frame to frame so a worker's buffers are
// allocated once rather than every frame: the depth buffer, what has been
// drawn, the surfaces gathered for crossings, the lights' shadows, and the
// scratch lists of points and runs. One per thread.
struct Workspace {
    // Whether culling may light lines on the GPU, when there is one (see
    // gpu::Device); off, it lights them on the CPU, as with none.
    bool gpu = true;
    DepthBuffer depth;
    // Every surface the camera sees, for its hatching, when that is more
    // than what is solid (which alone is in `depth`).
    DepthBuffer hatchDepth;
    Coverage coverage;
    std::vector<std::optional<Screen>> samples;
    std::vector<Screen> drawn;
    std::vector<std::size_t> drawnStarts;
    std::vector<crossing::Triangle> triangles;
    std::vector<std::array<osci::Point, 3>> sources;
    std::vector<std::optional<Vec3>> eyes;
    std::vector<std::vector<std::optional<Vec3>>> world;
    std::vector<std::pair<double, double>> runs;
    // A part something only part solid hides, drawn as bright as it lets show.
    struct HiddenRun {
        double from, to, brightness;
    };
    std::vector<HiddenRun> hiddenRuns;
    Dicer dicer;
    BentMesh bent;
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

// An eye to see a frame from other than its camera: an editor's view. From
// it nothing is hidden (it shows every line, as an editor does); lights
// light as ever, and mirrors show what they would to it, over `reach`.
struct OtherEye {
    PreparedCamera::Frame eye;
    Reach reach;
};

// One frame as the active camera sees it, with hidden lines removed when it
// hides them, and lit when lights shine.
//
// Point by point: surfaces that hide (meshes' faces, and the fills of flat
// drawings: SVG, text, Lottie) are drawn into a depth buffer over the frame;
// lines are stepped along their true path, no more than a pixel of it apart
// (see testStep), and kept where nothing is in front of them. Every point goes
// through its clip's whole chain of transforms and effects, so whatever bends
// the drawing, however sharply, bends what is tested: a face something bends
// is diced until its pieces are a couple of pixels across after the bend. A
// face or line nothing bends is exact through the camera, so a face is drawn
// whole and a line interpolated. Point sources hide nothing; their points
// are joined and tested like lines. Where two surfaces cut through each
// other, the line they cross along is an edge of what is seen that neither
// drawing has: it is added, and tested too. The drawing keeps its own shapes,
// cut where they hide; hidden runs are dropped, not darkened, so the beam
// skips straight to the next visible part (a camera hiding lines only
// part-way draws them dimmed, as much as it shows them).
//
// Lights see the same surfaces, each from where it is, so whatever something
// blocks from a light is in its shadow. What is kept is then lit, a couple of
// pixels at a time: each part draws its own colour times the light reaching
// it, and parts no light reaches are dropped as hidden ones are.
//
// Seen from `other`, when given, rather than the camera. None when `cancel`
// is set partway.
std::unique_ptr<Frame> cull(const PreparedComposition& composition, double time, Workspace& workspace, const std::atomic<bool>* cancel = nullptr, const OtherEye* other = nullptr);
}

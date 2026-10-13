#pragma once

#include "HiddenLineScene.h"
#include "SurfaceLines.h"
#include <map>
#include <memory>
#include <tuple>
#include <vector>

namespace motion::hidden {
// What is worked out once for each drawing's surface, kept from frame to
// frame: its corners and faces as surface lines take them, its normals, and
// the patterns laid on it. Fixed to the drawing (its source kept alive, so
// its address is never another's).
class LaidSurfaces {
public:
    struct Surface {
        std::shared_ptr<const PreparedSource> source;
        std::vector<Vec3> corners;
        std::vector<surface::Face> faces;
        surface::Smoothing smoothing;
        surface::Normals normals;
        // Whether it is closed (every edge has a face either side), so the
        // eye sees only its near side.
        bool closed = false;
        std::map<std::tuple<int, int, double>, std::vector<surface::Piece>> patterns;

        // A pattern laid on it; noise at `turn` round its loop. Of noise that
        // morphs, only the latest is kept.
        const std::vector<surface::Piece>& pattern(surface::Pattern kind, int detail, double turn) {
            const auto noise = kind == surface::Pattern::noise;
            const std::tuple key {static_cast<int>(kind), detail, noise ? turn : 0.0};
            auto found = patterns.find(key);
            if (found == patterns.end()) {
                if (noise) { std::erase_if(patterns, [](const auto& laid) { return std::get<0>(laid.first) == static_cast<int>(surface::Pattern::noise); }); }
                found = patterns.emplace(key, surface::pattern(corners, faces, kind, detail, noise ? turn : 0.0)).first;
            }
            return found->second;
        }
    };

    // A new frame: surfaces laid before it may be let go of to make room.
    void nextFrame() { ++frame; }

    // Its surface, laid once and kept while it is used (holding its source,
    // so the drawing is not freed and its address reused). Past `kept`, the
    // longest unused go first; none used this frame do, so references
    // handed out within a frame stay good.
    Surface& of(const PreparedDrawing& drawing, const std::shared_ptr<const PreparedSource>& source) {
        auto found = laid.find(&drawing);
        if (found != laid.end()) {
            found->second.used = frame;
            return found->second.surface;
        }
        while (laid.size() >= kept) {
            auto oldest = laid.end();
            for (auto entry = laid.begin(); entry != laid.end(); ++entry) {
                if (entry->second.used != frame && (oldest == laid.end() || entry->second.used < oldest->second.used)) { oldest = entry; }
            }
            if (oldest == laid.end()) { break; }
            laid.erase(oldest);
        }
        {
            Surface made;
            made.source = source;
            const auto& mesh = drawing.faces();
            for (const auto& vertex : mesh.vertices) { made.corners.push_back({vertex.x, vertex.y, vertex.z}); }
            made.faces.assign(mesh.triangles.begin(), mesh.triangles.end());
            made.smoothing = surface::Smoothing::of(made.corners, made.faces);
            made.normals = made.smoothing.normals(made.corners, made.faces);
            std::map<std::pair<std::tuple<std::int64_t, std::int64_t, std::int64_t>, std::tuple<std::int64_t, std::int64_t, std::int64_t>>, int> sides;
            for (const auto& face : made.faces) {
                // A face with no area (two corners at one place) has no sides.
                const std::array places {surface::detail::placeOf(made.corners[face[0]]), surface::detail::placeOf(made.corners[face[1]]), surface::detail::placeOf(made.corners[face[2]])};
                if (places[0] == places[1] || places[1] == places[2] || places[2] == places[0]) { continue; }
                for (std::size_t side = 0; side < 3; ++side) {
                    auto a = places[side], b = places[(side + 1) % 3];
                    if (b < a) { std::swap(a, b); }
                    ++sides[{a, b}];
                }
            }
            made.closed = !sides.empty() && std::all_of(sides.begin(), sides.end(), [](const auto& side) { return side.second == 2; });
            found = laid.emplace(&drawing, Entry {std::move(made), frame}).first;
        }
        return found->second.surface;
    }

private:
    struct Entry {
        Surface surface;
        std::uint64_t used = 0;
    };
    static constexpr std::size_t kept = 64;
    std::map<const PreparedDrawing*, Entry> laid;
    std::uint64_t frame = 0;
};

// How far a line on a surface follows its curve before it is cut (the turn
// of the surface's normal along a straight stretch, radians): an outline,
// all of whose curve is seen, more finely than a pattern.
inline constexpr double outlineTurn = .1, patternTurn = .2;

// Pieces of a surface's lines as straight lines in its source's own space,
// where its corners are, in as many stretches as follow its curve:
// `line(from, to, face, starts, ends)` for each, `starts` and `ends` being
// where on its face the stretch's ends are.
template <typename Line>
void smoothLines(const LaidSurfaces::Surface& surface, const std::vector<surface::Piece>& pieces, double turn, Line&& line) {
    // Where the last piece ended, flat and lifted: a piece going on from
    // there starts exactly there, though its face's corners lift the place
    // a little differently (at a cone's tip, each face takes its normal from
    // the faces near it alone), so a line stays one stroke.
    std::optional<std::pair<Vec3, osci::Point>> ended;
    for (const auto& piece : pieces) {
        const auto stretches = surface::stretchesOf(surface.normals, piece, turn);
        const auto weightsAt = [&](double share) {
            surface::Weights weights;
            for (std::size_t corner = 0; corner < 3; ++corner) { weights[corner] = piece.from[corner] + (piece.to[corner] - piece.from[corner]) * share; }
            return weights;
        };
        const auto point = [&](int stretch) {
            const auto p = surface::smoothAt(surface.corners, surface.faces, surface.normals, piece.face, weightsAt(static_cast<double>(stretch) / stretches));
            return osci::Point(static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z));
        };
        const auto& face = surface.faces[piece.face];
        const auto flatStart = surface::at(surface.corners, face, piece.from);
        auto previous = ended.has_value() && surface::detail::placeOf(ended->first) == surface::detail::placeOf(flatStart) ? ended->second : point(0);
        for (int stretch = 1; stretch <= stretches; ++stretch) {
            const auto next = point(stretch);
            line(previous, next, piece.face, weightsAt(static_cast<double>(stretch - 1) / stretches), weightsAt(static_cast<double>(stretch) / stretches));
            previous = next;
        }
        ended = std::pair {surface::at(surface.corners, face, piece.to), previous};
    }
}

// A surface drawn as it draws itself for a frame (see LineStyle): its own
// lines as bright as its Edges, then the lines of each pattern over it as
// bright as that pattern, then its outline as the eye sees it as bright as
// its Outline, these last two following the smooth surface its faces stand
// for. Every line says the face it lies on; those of patterns and outlines
// are lit as the smooth surface is where they are. The outline's lines are
// the eye's alone, so no mirror shows them (it shows its own view's).
struct Styled {
    // The drawing it was given, and the one it draws instead.
    const PreparedDrawing* unstyled = nullptr;
    std::shared_ptr<const PreparedDrawing> drawing;
    // Where the lines laid on it begin, and where its outline's begin.
    std::size_t laidFrom = 0, eyeOnlyFrom = 0;
    // For each line laid on it, where on its face the line's ends are.
    std::vector<std::array<surface::Weights, 2>> ends;
    // Its corners and normals in the scene, this frame, and which way its
    // faces wind: the sign of what they enclose (outward when positive).
    std::vector<Vec3> corners;
    surface::Normals normals;
    double enclosed = 0;

    // Puts it in the scene for the frame: `drawing` with its mesh's corners
    // at `world`.
    void place(const PreparedDrawing& drawing, const std::shared_ptr<const PreparedSource>& owner, const std::vector<std::optional<Vec3>>& world, LaidSurfaces& laid) {
        unstyled = &drawing;
        auto& surface = laid.of(drawing, owner);
        corners.clear();
        for (const auto& corner : world) { corners.push_back(corner.value_or(Vec3 {})); }
        normals = surface.smoothing.normals(corners, surface.faces);
        enclosed = 0;
        for (const auto& f : surface.faces) { enclosed += corners[f[0]].dot(corners[f[1]].cross(corners[f[2]])); }
    }
    // Whether a point of a closed surface is on its far side from `eye`.
    bool farFrom(Vec3 eye, std::uint32_t face, const surface::Weights& at, const std::vector<surface::Face>& faces) const {
        const auto facing = normals.at(face, at).dot(eye - surface::at(corners, faces[face], at));
        return enclosed < 0 ? facing > 0 : facing < 0;
    }

    // The normals to light a line by, turning along it as the surface does;
    // none for one of its own (lit by the faces it runs along, as ever).
    std::optional<Normals> normalsOf(std::size_t shape) const {
        if (shape < laidFrom || shape - laidFrom >= ends.size()) { return std::nullopt; }
        const auto face = static_cast<std::uint32_t>(drawing->shapeFaces()[shape][0]);
        const auto& [starts, finishes] = ends[shape - laidFrom];
        return Normals::turning(normals.at(face, starts), normals.at(face, finishes));
    }
    // How near a line of its outline its own faces may come, along the line
    // from it to the eye that sees it, without hiding it: it is where the
    // surface turns away, so the faces about it graze that line.
    double grazing(std::uint32_t face) const {
        const auto& f = unstyled->faces().triangles[face];
        const auto& a = corners[f[0]], & b = corners[f[1]], & c = corners[f[2]];
        return 2 * std::max({(b - a).length(), (c - b).length(), (a - c).length()});
    }
};

// Draws a surface in its style, placed in the scene for the frame (see
// Styled::place), as `eye` sees it. `nearSideOnly`: what is laid on a closed
// surface's far side from the eye is left out, as lines hidden there would
// be (for a view that hides none).
inline void restyle(Styled& styled, const std::shared_ptr<const PreparedSource>& owner, const std::vector<std::array<std::int32_t, 2>>& along, const LineStyle& style,
    std::optional<Vec3> eye, LaidSurfaces& laid, bool nearSideOnly = false) {
    const auto& source = *styled.unstyled;
    std::vector<std::unique_ptr<osci::Shape>> shapes;
    std::vector<std::array<std::int32_t, 2>> faces;
    const auto dimmed = [](std::unique_ptr<osci::Shape> shape, double amount) -> std::unique_ptr<osci::Shape> {
        if (amount >= 1) { return shape; }
        return std::make_unique<DimmedShape>(std::move(shape), static_cast<float>(amount));
    };
    if (style.edges > 0) {
        for (std::size_t index = 0; index < source.shapeCount(); ++index) {
            auto* shape = source.shape(index);
            if (shape == nullptr) { continue; }
            shapes.push_back(dimmed(shape->clone(), style.edges));
            faces.push_back(index < along.size() ? along[index] : std::array<std::int32_t, 2> {-1, -1});
        }
    }
    auto& surface = laid.of(source, owner);
    styled.laidFrom = shapes.size();
    styled.ends.clear();
    const auto add = [&](const std::vector<surface::Piece>& pieces, double amount, double turn, bool laidOn) {
        const auto culling = laidOn && nearSideOnly && surface.closed && eye.has_value();
        smoothLines(surface, pieces, turn, [&](osci::Point from, osci::Point to, std::uint32_t face, const surface::Weights& starts, const surface::Weights& ends) {
            if (culling && styled.farFrom(*eye, face, starts, surface.faces) && styled.farFrom(*eye, face, ends, surface.faces)) { return; }
            shapes.push_back(dimmed(std::make_unique<osci::Line>(from, to), amount));
            faces.push_back({static_cast<std::int32_t>(face), -1});
            styled.ends.push_back({starts, ends});
        });
    };
    for (std::size_t pattern = 0; pattern < style.patterns.size(); ++pattern) {
        if (!(style.patterns[pattern] > 0)) { continue; }
        add(surface.pattern(static_cast<surface::Pattern>(pattern), static_cast<int>(style.detail), style.noiseTurn), style.patterns[pattern], patternTurn, true);
    }
    styled.eyeOnlyFrom = shapes.size();
    if (style.outline > 0 && eye.has_value()) { add(surface::outline(styled.corners, surface.faces, styled.normals, *eye), style.outline, outlineTurn, false); }
    auto drawing = std::make_shared<PreparedDrawing>(std::move(shapes), true);
    drawing->setFaces(source.faces());
    drawing->setShapeFaces(std::move(faces));
    styled.drawing = std::move(drawing);
}
}

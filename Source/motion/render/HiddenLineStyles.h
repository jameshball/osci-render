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
        std::map<std::pair<int, int>, std::vector<surface::Piece>> patterns;

        const std::vector<surface::Piece>& pattern(surface::Pattern kind, int detail) {
            const std::pair key {static_cast<int>(kind), detail};
            auto found = patterns.find(key);
            if (found == patterns.end()) { found = patterns.emplace(key, surface::pattern(corners, faces, kind, detail)).first; }
            return found->second;
        }
    };

    Surface& of(const PreparedDrawing& drawing, const std::shared_ptr<const PreparedSource>& source) {
        auto found = laid.find(&drawing);
        if (found == laid.end()) {
            if (laid.size() > 64) { laid.clear(); }
            Surface made;
            made.source = source;
            const auto& mesh = drawing.faces();
            for (const auto& vertex : mesh.vertices) { made.corners.push_back({vertex.x, vertex.y, vertex.z}); }
            made.faces.assign(mesh.triangles.begin(), mesh.triangles.end());
            made.smoothing = surface::Smoothing::of(made.corners, made.faces);
            made.normals = made.smoothing.normals(made.corners, made.faces);
            found = laid.emplace(&drawing, std::move(made)).first;
        }
        return found->second;
    }

private:
    std::map<const PreparedDrawing*, Surface> laid;
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
        auto previous = point(0);
        for (int stretch = 1; stretch <= stretches; ++stretch) {
            const auto next = point(stretch);
            line(previous, next, piece.face, weightsAt(static_cast<double>(stretch - 1) / stretches), weightsAt(static_cast<double>(stretch) / stretches));
            previous = next;
        }
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
    // Where the lines laid on it begin, and where its outline's do.
    std::size_t laidFrom = 0, eyeOnlyFrom = 0;
    // For each line laid on it, where on its face the line's ends are.
    std::vector<std::array<surface::Weights, 2>> ends;
    // Its corners and normals in the scene, this frame.
    std::vector<Vec3> corners;
    surface::Normals normals;

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

// Draws a surface in its style (`styled.unstyled` its drawing), `world`
// holding its mesh's corners in the scene, as `eye` sees it.
inline void restyle(Styled& styled, const std::shared_ptr<const PreparedSource>& owner, const std::vector<std::array<std::int32_t, 2>>& along, const LineStyle& style,
    const std::vector<std::optional<Vec3>>& world, std::optional<Vec3> eye, LaidSurfaces& laid) {
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
    styled.corners.clear();
    for (const auto& corner : world) { styled.corners.push_back(corner.value_or(Vec3 {})); }
    styled.normals = surface.smoothing.normals(styled.corners, surface.faces);
    styled.laidFrom = shapes.size();
    styled.ends.clear();
    const auto add = [&](const std::vector<surface::Piece>& pieces, double amount, double turn) {
        smoothLines(surface, pieces, turn, [&](osci::Point from, osci::Point to, std::uint32_t face, const surface::Weights& starts, const surface::Weights& ends) {
            shapes.push_back(dimmed(std::make_unique<osci::Line>(from, to), amount));
            faces.push_back({static_cast<std::int32_t>(face), -1});
            styled.ends.push_back({starts, ends});
        });
    };
    for (std::size_t pattern = 0; pattern < style.patterns.size(); ++pattern) {
        if (style.patterns[pattern] > 0) { add(surface.pattern(static_cast<surface::Pattern>(pattern), static_cast<int>(style.detail)), style.patterns[pattern], patternTurn); }
    }
    styled.eyeOnlyFrom = shapes.size();
    if (style.outline > 0 && eye.has_value()) { add(surface::outline(styled.corners, surface.faces, styled.normals, *eye), style.outline, outlineTurn); }
    auto drawing = std::make_shared<PreparedDrawing>(std::move(shapes), true);
    drawing->setFaces(source.faces());
    drawing->setShapeFaces(std::move(faces));
    styled.drawing = std::move(drawing);
}
}

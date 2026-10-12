#include "../../Source/motion/render/SierpinskiHatching.h"
#include <cstdlib>
#include <iostream>
#include <set>
#include <string>

using namespace motion;
using namespace motion::hidden;
using namespace motion::hidden::hatching::sierpinski;

namespace {
void check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

// A ball as the sphere generator makes one: parallels and meridians.
void ball(double radius, int rings, int segments, std::vector<Vec3>& corners, std::vector<surface::Face>& faces) {
    const auto pi = 3.14159265358979323846;
    for (int ring = 0; ring <= rings; ++ring) {
        for (int segment = 0; segment < segments; ++segment) {
            const auto latitude = pi * ring / rings - pi / 2, longitude = 2 * pi * segment / segments;
            corners.push_back(Vec3 {std::cos(latitude) * std::cos(longitude), std::sin(latitude), std::cos(latitude) * std::sin(longitude)} * radius);
        }
    }
    for (std::uint32_t ring = 0; ring < static_cast<std::uint32_t>(rings); ++ring) {
        for (std::uint32_t segment = 0; segment < static_cast<std::uint32_t>(segments); ++segment) {
            const auto a = ring * segments + segment, b = ring * segments + (segment + 1) % segments;
            faces.push_back({a, b, b + segments});
            faces.push_back({a, b + segments, a + segments});
        }
    }
}

Vec3 middleOf(const Curve& curve, const std::vector<Vec3>& corners, const std::vector<surface::Face>& faces, std::uint32_t node) {
    const auto [face, weights] = curve.middleOf(node);
    return surface::at(corners, faces[face], weights);
}

double areaOf(const Curve& curve, const std::vector<Vec3>& corners, const std::vector<surface::Face>& faces, std::uint32_t node) {
    const auto& n = curve.nodes[node];
    const auto a = surface::at(corners, faces[n.face], n.corners[0]), b = surface::at(corners, faces[n.face], n.corners[1]), c = surface::at(corners, faces[n.face], n.corners[2]);
    return (b - a).cross(c - a).length() / 2;
}
}

int main() {
    std::vector<Vec3> corners;
    std::vector<surface::Face> faces;
    ball(1, 8, 16, corners, faces);
    // The leaves cover the ball once: their areas add up to its faces'.
    {
        const auto curve = Curve::of(corners, faces, .1);
        check(curve.roots.size() == 1, "one loop over a ball: " + std::to_string(curve.roots.size()));
        double faceArea = 0;
        for (const auto& f : faces) { faceArea += (corners[f[1]] - corners[f[0]]).cross(corners[f[2]] - corners[f[0]]).length() / 2; }
        double leafArea = 0;
        double widest = 0;
        for (const auto leaf : curve.leaves) {
            leafArea += areaOf(curve, corners, faces, leaf);
            const auto& n = curve.nodes[leaf];
            std::array<Vec3, 3> at;
            for (std::size_t corner = 0; corner < 3; ++corner) { at[corner] = surface::at(corners, faces[n.face], n.corners[corner]); }
            widest = std::max({widest, (at[1] - at[0]).length(), (at[2] - at[1]).length(), (at[0] - at[2]).length()});
        }
        check(std::abs(leafArea - faceArea) < 1.0e-9 * faceArea + 1.0e-12, "the leaves cover every face once: " + std::to_string(leafArea) + " of " + std::to_string(faceArea));
        check(widest <= .1 + 1.0e-9, "none wider than the finest: " + std::to_string(widest));
        // Unbroken: each leaf shares a corner (and so a side, or a point) with the next.
        double longestStep = 0;
        for (std::size_t index = 0; index < curve.leaves.size(); ++index) {
            const auto here = middleOf(curve, corners, faces, curve.leaves[index]), next = middleOf(curve, corners, faces, curve.leaves[(index + 1) % curve.leaves.size()]);
            longestStep = std::max(longestStep, (next - here).length());
        }
        check(longestStep < .15, "each leaf beside the next, round the loop: " + std::to_string(longestStep));
        // A big face carries as much of the curve as its size, a small one
        // as little: in full light, what the curve passes through is alike
        // in size wherever it is.
        const auto lit = Lit::of(curve, [](std::uint32_t) { return 1.0; }, [&](std::uint32_t leaf) { return areaOf(curve, corners, faces, leaf); }, [&](std::uint32_t leaf) { return middleOf(curve, corners, faces, leaf); });
        double smallest = 1.0e300, largest = 0;
        lit.curveAt(.1, 1, 1, [&](std::optional<std::uint32_t> node, double) {
            if (!node.has_value()) { return; }
            smallest = std::min(smallest, lit.width[*node]);
            largest = std::max(largest, lit.width[*node]);
        });
        check(largest < 4 * smallest, "drawn alike in size, however big their faces: " + std::to_string(largest / smallest));
    }
    // However coarse, what the curve passes through is a patch of the
    // surface, about as long as it is wide, not a strip across it.
    {
        std::vector<Vec3> fine;
        std::vector<surface::Face> fineFaces;
        ball(1, 24, 48, fine, fineFaces);
        const auto curve = Curve::of(fine, fineFaces, .05);
        const auto lit = Lit::of(curve, [](std::uint32_t) { return 1.0; }, [&](std::uint32_t leaf) { return areaOf(curve, fine, fineFaces, leaf); }, [&](std::uint32_t leaf) { return middleOf(curve, fine, fineFaces, leaf); });
        double worst = 0, total = 0;
        int count = 0;
        lit.curveAt(.4, 1, 1, [&](std::optional<std::uint32_t> node, double) {
            if (!node.has_value()) { return; }
            const auto& n = curve.nodes[*node];
            double reach = 0;
            const auto centre = middleOf(curve, fine, fineFaces, *node);
            for (auto leaf = n.lo; leaf < n.hi; ++leaf) { reach = std::max(reach, (middleOf(curve, fine, fineFaces, curve.leaves[leaf]) - centre).length()); }
            const auto ratio = 2 * reach / lit.width[*node];
            worst = std::max(worst, ratio);
            total += ratio;
            ++count;
        });
        std::cerr << "patches " << count << " mean " << total / count << " worst " << worst << "\n";
        check(total / count < 2.5, "patches, not strips, on the whole: " + std::to_string(total / count));
    }
    // A cone's long thin faces are halved into pieces no thinner than they
    // need be: no streaks from its tip. (Where a face is narrower than the
    // finest, near the tip, its pieces can only be as narrow; by area,
    // they are few.)
    {
        std::vector<Vec3> cone {{0, 2, 0}};
        std::vector<surface::Face> fan;
        for (int segment = 0; segment < 16; ++segment) {
            const auto angle = 2 * 3.14159265358979323846 * segment / 16;
            cone.push_back({std::cos(angle) * .3, 0, std::sin(angle) * .3});
        }
        for (std::uint32_t segment = 0; segment < 16; ++segment) { fan.push_back({0, 1 + (segment + 1) % 16, 1 + segment}); }
        const auto curve = Curve::of(cone, fan, .05);
        double thinness = 0, total = 0;
        for (const auto leaf : curve.leaves) {
            const auto& n = curve.nodes[leaf];
            std::array<Vec3, 3> at;
            for (std::size_t corner = 0; corner < 3; ++corner) { at[corner] = surface::at(cone, fan[n.face], n.corners[corner]); }
            const auto longest = std::max({(at[1] - at[0]).length(), (at[2] - at[1]).length(), (at[0] - at[2]).length()});
            const auto area = (at[1] - at[0]).cross(at[2] - at[0]).length() / 2;
            thinness += longest * longest / 2;
            total += area;
        }
        check(thinness / total < 4, "pieces well shaped, by area: " + std::to_string(thinness / total));
    }
    // Finer asks more of it.
    {
        const auto coarse = Curve::of(corners, faces, .2), fine = Curve::of(corners, faces, .05);
        check(fine.leaves.size() > 8 * coarse.leaves.size(), "finer, more: " + std::to_string(coarse.leaves.size()) + " " + std::to_string(fine.leaves.size()));
    }
    // Lit, as coarse as the light: dark half sparse, lit half dense.
    {
        const auto curve = Curve::of(corners, faces, .05);
        const auto lit = Lit::of(curve, [&](std::uint32_t leaf) { return middleOf(curve, corners, faces, leaf).x > 0 ? 1.0 : .2; }, [&](std::uint32_t leaf) { return areaOf(curve, corners, faces, leaf); }, [&](std::uint32_t leaf) { return middleOf(curve, corners, faces, leaf); });
        std::size_t bright = 0, dim = 0;
        lit.curveAt(.05, 1, 1, [&](std::optional<std::uint32_t> node, double) {
            if (!node.has_value()) { return; }
            (middleOf(curve, corners, faces, *node).x > 0 ? bright : dim) += 1;
        });
        check(bright > 3 * dim, "densest where lit: " + std::to_string(bright) + " " + std::to_string(dim));
        std::size_t visited = 0;
        lit.curveAt(.05, 1, .3, [&](std::optional<std::uint32_t> node, double) { visited += node.has_value() ? 1 : 0; });
        check(visited < bright + dim, "less gain, coarser");
    }
    // A flat square of two faces, and a box of twelve.
    {
        const std::vector<Vec3> square {{-1, -1, 0}, {1, -1, 0}, {1, 1, 0}, {-1, 1, 0}};
        const auto curve = Curve::of(square, {{0, 1, 2}, {0, 2, 3}}, .1);
        check(curve.roots.size() == 1 && curve.leaves.size() > 300, "a square, all of it: " + std::to_string(curve.leaves.size()));
        std::vector<Vec3> box {{-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1}, {-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1}};
        std::vector<surface::Face> sides;
        for (const auto& face : std::array<std::array<std::uint32_t, 4>, 6> {{{0, 3, 2, 1}, {4, 5, 6, 7}, {0, 1, 5, 4}, {1, 2, 6, 5}, {2, 3, 7, 6}, {3, 0, 4, 7}}}) {
            sides.push_back({face[0], face[1], face[2]});
            sides.push_back({face[0], face[2], face[3]});
        }
        const auto boxCurve = Curve::of(box, sides, .25);
        check(boxCurve.roots.size() == 1, "a box, one loop");
        double longestStep = 0;
        for (std::size_t index = 0; index < boxCurve.leaves.size(); ++index) {
            const auto here = middleOf(boxCurve, box, sides, boxCurve.leaves[index]), next = middleOf(boxCurve, box, sides, boxCurve.leaves[(index + 1) % boxCurve.leaves.size()]);
            longestStep = std::max(longestStep, (next - here).length());
        }
        check(longestStep < .4, "round its edges unbroken: " + std::to_string(longestStep));
    }
    std::cout << "SierpinskiTests passed\n";
    return 0;
}

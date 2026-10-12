#include "../../Source/motion/render/SurfaceLines.h"
#include <cstdlib>
#include <iostream>
#include <string>

using namespace motion;
using namespace motion::hidden::surface;

namespace {
void check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

// A ball as the sphere generator makes one: parallels and meridians.
void ball(double radius, int rings, int segments, std::vector<Vec3>& corners, std::vector<Face>& faces) {
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

// How many separate lines the pieces make, end to end.
int linesIn(const std::vector<Vec3>& corners, const std::vector<Face>& faces, const std::vector<Piece>& pieces) {
    int lines = pieces.empty() ? 0 : 1;
    for (std::size_t index = 1; index < pieces.size(); ++index) {
        const auto end = at(corners, faces[pieces[index - 1].face], pieces[index - 1].to);
        const auto start = at(corners, faces[pieces[index].face], pieces[index].from);
        lines += (end - start).length() > 1.0e-6 ? 1 : 0;
    }
    return lines;
}
}

int main() {
    std::vector<Vec3> corners;
    std::vector<Face> faces;
    ball(1, 24, 48, corners, faces);

    // Slices: level lines, end to end round the ball, one loop a level.
    {
        const auto pieces = pattern(corners, faces, Pattern::slices, 8);
        check(!pieces.empty(), "a ball sliced has slices");
        for (const auto& piece : pieces) {
            const auto from = at(corners, faces[piece.face], piece.from), to = at(corners, faces[piece.face], piece.to);
            check(std::abs(from.y - to.y) < 1.0e-6, "each slice is level");
            check(std::abs((from.y + 1) * 4 - .5 - std::round((from.y + 1) * 4 - .5)) < 1.0e-5, "evenly, half a spacing in from the ends");
        }
        check(linesIn(corners, faces, pieces) == 8, "one loop a level, eight across: " + std::to_string(linesIn(corners, faces, pieces)));
    }
    // A spiral: one line round and round, from bottom to top.
    {
        const auto pieces = pattern(corners, faces, Pattern::spiral, 8);
        check(!pieces.empty(), "a ball has a spiral");
        check(linesIn(corners, faces, pieces) <= 3, "it runs as one line (poles aside): " + std::to_string(linesIn(corners, faces, pieces)));
        double turned = 0;
        for (const auto& piece : pieces) {
            const auto from = at(corners, faces[piece.face], piece.from), to = at(corners, faces[piece.face], piece.to);
            turned += std::remainder(std::atan2(to.z, to.x) - std::atan2(from.z, from.x), 2 * 3.14159265358979323846);
        }
        check(std::abs(turned) > 2 * 3.14159265358979323846 * 6, "turning round many times as it climbs");
    }
    // Rings and noise lie on the surface too.
    for (const auto kind : {Pattern::rings, Pattern::noise, Pattern::hilbert}) {
        const auto pieces = pattern(corners, faces, kind, 16);
        check(!pieces.empty(), "every pattern draws something on a ball");
        for (const auto& piece : pieces) {
            for (const auto& weights : {piece.from, piece.to}) {
                check(weights[0] >= -1.0e-9 && weights[1] >= -1.0e-9 && weights[2] >= -1.0e-9 && std::abs(weights[0] + weights[1] + weights[2] - 1) < 1.0e-9, "within its face");
            }
        }
    }
    // A Hilbert curve laid on runs on, piece after piece, across faces.
    {
        const auto pieces = pattern(corners, faces, Pattern::hilbert, 16);
        check(linesIn(corners, faces, pieces) < static_cast<int>(pieces.size()) / 4, "its pieces mostly meet end to end");
    }

    // A ball's outline from far away is a circle round it, square to the eye.
    {
        const Vec3 eye {0, 0, 50};
        const auto pieces = outline(corners, faces, eye);
        check(!pieces.empty(), "a ball has an outline");
        check(linesIn(corners, faces, pieces) == 1, "one loop round it: " + std::to_string(linesIn(corners, faces, pieces)));
        for (const auto& piece : pieces) {
            const auto point = at(corners, faces[piece.face], piece.from);
            check(std::abs(point.z - .02) < .01, "where the ball turns from the eye (r * r / d in front of its middle)");
        }
    }
    // A box's outline is the edges between faces toward and away from the eye.
    {
        std::vector<Vec3> box {{-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1}, {-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1}};
        std::vector<Face> sides;
        for (const auto& face : std::array<std::array<std::uint32_t, 4>, 6> {{{0, 3, 2, 1}, {4, 5, 6, 7}, {0, 1, 5, 4}, {1, 2, 6, 5}, {2, 3, 7, 6}, {3, 0, 4, 7}}}) {
            sides.push_back({face[0], face[1], face[2]});
            sides.push_back({face[0], face[2], face[3]});
        }
        const auto pieces = outline(box, sides, {3, 4, 5});
        check(pieces.size() == 6, "a box seen from a corner has six outline edges: " + std::to_string(pieces.size()));
        check(linesIn(box, sides, pieces) == 1, "joined in one loop");
        // A flat square's outline is its border.
        const std::vector<Vec3> square {{-1, -1, 0}, {1, -1, 0}, {1, 1, 0}, {-1, 1, 0}};
        check(outline(square, {{0, 1, 2}, {0, 2, 3}}, {0, 0, 5}).size() == 4, "a flat square's outline is its four sides");
    }
    std::cout << "SurfaceLinesTests passed\n";
    return 0;
}

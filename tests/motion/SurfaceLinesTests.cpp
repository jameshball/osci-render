#include "../../Source/motion/render/SurfaceLines.h"
#include <cstdlib>
#include <iostream>
#include <limits>
#include <optional>
#include <set>
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
    // A spiral: lines round and round, side by side as a screw's thread,
    // climbing steeply enough to be seen to, eight a height apart.
    {
        const auto pieces = pattern(corners, faces, Pattern::spiral, 8);
        check(!pieces.empty(), "a ball has a spiral");
        // How steeply it climbs about the middle of the ball.
        std::vector<double> slopes;
        double turned = 0, line = 0;
        std::optional<Vec3> last;
        for (const auto& piece : pieces) {
            const auto from = at(corners, faces[piece.face], piece.from), to = at(corners, faces[piece.face], piece.to);
            if (std::abs(from.y) < .3) {
                const auto across = std::hypot(to.x - from.x, to.z - from.z);
                if (across > 1.0e-6) { slopes.push_back(std::abs(to.y - from.y) / across); }
            }
            if (last.has_value() && (from - *last).length() > 1.0e-5) {
                turned += std::abs(line);
                line = 0;
            }
            line += std::remainder(std::atan2(to.z, to.x) - std::atan2(from.z, from.x), 2 * 3.14159265358979323846);
            last = to;
        }
        turned += std::abs(line);
        std::sort(slopes.begin(), slopes.end());
        const auto slope = slopes[slopes.size() / 2];
        check(slope > .2 && slope < .45, "climbing at about 18 degrees: " + std::to_string(std::atan(slope) * 180 / 3.14159265358979323846));
        check(std::abs(turned) > 2 * 3.14159265358979323846 * 6, "its lines together going round many times");
        // Level slices a height apart, a spiral's lines too: as many up a
        // flat ball (squashed to a fifth) as up a round one.
        std::vector<Vec3> flat = corners;
        for (auto& corner : flat) { corner.y *= .2; }
        const auto levels = pattern(flat, faces, Pattern::slices, 8);
        std::set<long> heights;
        for (const auto& piece : levels) { heights.insert(std::lround(at(flat, faces[piece.face], piece.from).y * 1.0e4)); }
        check(heights.size() == 8, "eight slices up a flat ball: " + std::to_string(heights.size()));
    }
    // Two balls apart, one higher: each is climbed from its own lowest
    // points and wound about its own middle, the higher one as the lower.
    {
        std::vector<Vec3> two = corners;
        std::vector<Face> both = faces;
        const auto offset = static_cast<std::uint32_t>(two.size());
        for (const auto& corner : corners) { two.push_back(corner + Vec3 {3, 2, 0}); }
        for (const auto& face : faces) { both.push_back({face[0] + offset, face[1] + offset, face[2] + offset}); }
        const auto pieces = pattern(two, both, Pattern::spiral, 8);
        std::vector<double> slopes;
        for (const auto& piece : pieces) {
            if (piece.face < faces.size()) { continue; }
            const auto from = at(two, both[piece.face], piece.from), to = at(two, both[piece.face], piece.to);
            if (std::abs(from.y - 2) < .3) {
                const auto across = std::hypot(to.x - from.x, to.z - from.z);
                if (across > 1.0e-6) { slopes.push_back(std::abs(to.y - from.y) / across); }
            }
        }
        check(!slopes.empty(), "the higher ball has a spiral");
        std::sort(slopes.begin(), slopes.end());
        const auto slope = slopes[slopes.size() / 2];
        check(slope > .1 && slope < .45, "climbing round the higher ball as round the lower: " + std::to_string(std::atan(slope) * 180 / 3.14159265358979323846));
    }
    // A flat square, lying down or standing up: a spiral across it is one
    // line, winding in from its edges as a record's groove does, not many
    // starts crowded together.
    for (int lying = 0; lying < 3; ++lying) {
        std::vector<Vec3> square;
        std::vector<Face> cells;
        constexpr int n = 8;
        for (int i = 0; i <= n; ++i) {
            for (int j = 0; j <= n; ++j) {
                const auto u = -1 + 2.0 * i / n, v = -1 + 2.0 * j / n;
                square.push_back(lying == 0 ? Vec3 {u, 0, v} : lying == 1 ? Vec3 {u, v, 0} : Vec3 {0, u, v});
            }
        }
        for (std::uint32_t i = 0; i < n; ++i) {
            for (std::uint32_t j = 0; j < n; ++j) {
                const auto k = i * (n + 1) + j;
                cells.push_back({k, k + 1, k + n + 2});
                cells.push_back({k, k + n + 2, k + n + 1});
            }
        }
        const auto pieces = pattern(square, cells, Pattern::spiral, 8);
        check(!pieces.empty() && pieces.size() < 2000, "a flat square's spiral is a few hundred pieces: " + std::to_string(pieces.size()));
        check(linesIn(square, cells, pieces) == 1, "one line: " + std::to_string(linesIn(square, cells, pieces)));
    }
    // A mesh an effect broke (a corner not finite) still draws its patterns
    // on the faces that are whole, and none on the rest.
    {
        std::vector<Vec3> broken {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {std::numeric_limits<double>::quiet_NaN(), 0, 0}, {1, 1, std::numeric_limits<double>::infinity()}};
        std::vector<Face> parts {{0, 1, 2}, {1, 3, 2}, {1, 4, 2}};
        for (const auto kind : {Pattern::spiral, Pattern::noise, Pattern::slices, Pattern::rings}) {
            const auto pieces = pattern(broken, parts, kind, 8);
            check(std::all_of(pieces.begin(), pieces.end(), [](const Piece& piece) { return piece.face == 0; }), "patterns only on the whole face");
        }
    }
    // Rings and noise lie on the surface too.
    for (const auto kind : {Pattern::rings, Pattern::noise}) {
        const auto pieces = pattern(corners, faces, kind, 16);
        check(!pieces.empty(), "every pattern draws something on a ball");
        for (const auto& piece : pieces) {
            for (const auto& weights : {piece.from, piece.to}) {
                check(weights[0] >= -1.0e-9 && weights[1] >= -1.0e-9 && weights[2] >= -1.0e-9 && std::abs(weights[0] + weights[1] + weights[2] - 1) < 1.0e-9, "within its face");
            }
        }
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
    // A pattern is as curved across a big face as it is: rings on a square
    // of two faces are round.
    {
        const std::vector<Vec3> square {{-1, -1, 0}, {1, -1, 0}, {1, 1, 0}, {-1, 1, 0}};
        const std::vector<Face> halves {{0, 1, 2}, {0, 2, 3}};
        const auto pieces = pattern(square, halves, Pattern::rings, 8);
        check(pieces.size() > 100, "rings across two faces, in many pieces: " + std::to_string(pieces.size()));
        for (const auto& piece : pieces) {
            const auto half = Weights {(piece.from[0] + piece.to[0]) / 2, (piece.from[1] + piece.to[1]) / 2, (piece.from[2] + piece.to[2]) / 2};
            for (const auto& weights : {piece.from, piece.to, half}) {
                const auto radius = at(square, halves[piece.face], weights).length() / .25 + .5;
                check(std::abs(radius - std::round(radius)) < .03, "each ring round its middle, a spacing from the next: " + std::to_string(radius));
            }
        }
        check(linesIn(square, halves, pieces) == 12, "four whole rings, and two more cut in four by the corners: " + std::to_string(linesIn(square, halves, pieces)));
    }
    // Noise morphs as it turns round its loop, and is back as it was after a
    // whole turn, never sooner.
    {
        const auto at = [&](double turn) { return pattern(corners, faces, Pattern::noise, 8, turn); };
        const auto same = [&](const std::vector<Piece>& one, const std::vector<Piece>& other) {
            if (one.size() != other.size()) { return false; }
            for (std::size_t index = 0; index < one.size(); ++index) {
                if (one[index].face != other[index].face) { return false; }
                for (std::size_t corner = 0; corner < 3; ++corner) {
                    if (std::abs(one[index].from[corner] - other[index].from[corner]) > 1.0e-6 || std::abs(one[index].to[corner] - other[index].to[corner]) > 1.0e-6) { return false; }
                }
            }
            return true;
        };
        const auto start = at(0);
        check(same(start, at(1)) && same(start, at(-2)), "a whole turn brings it back");
        for (const auto turn : {.01, .25, .5, .75}) { check(!same(start, at(turn)), "it morphs on the way round: " + std::to_string(turn)); }
        check(same(at(.3), at(1.3)), "wherever it starts");
    }
    std::cout << "SurfaceLinesTests passed\n";
    return 0;
}

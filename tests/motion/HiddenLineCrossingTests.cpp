#include "../../Source/motion/render/HiddenLineCrossings.h"
#include <cstdlib>
#include <iostream>

namespace {
using motion::Vec3;
using namespace motion::hidden::crossing;

void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

bool near(double a, double b, double tolerance = 1.0e-9) { return std::abs(a - b) <= tolerance; }

// The twelve triangles of a box's faces.
void addBox(std::vector<Triangle>& triangles, Vec3 low, Vec3 high, std::uint32_t surface) {
    const auto corner = [&](int x, int y, int z) { return Vec3 {x != 0 ? high.x : low.x, y != 0 ? high.y : low.y, z != 0 ? high.z : low.z}; };
    const int faces[6][4][3] {
        {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}, {{0, 0, 1}, {0, 1, 1}, {1, 1, 1}, {1, 0, 1}},
        {{0, 0, 0}, {0, 1, 0}, {0, 1, 1}, {0, 0, 1}}, {{1, 0, 0}, {1, 0, 1}, {1, 1, 1}, {1, 1, 0}},
        {{0, 0, 0}, {0, 0, 1}, {1, 0, 1}, {1, 0, 0}}, {{0, 1, 0}, {1, 1, 0}, {1, 1, 1}, {0, 1, 1}},
    };
    for (const auto& face : faces) {
        const auto at = [&](int index) { return corner(face[index][0], face[index][1], face[index][2]); };
        triangles.push_back({{at(0), at(1), at(2)}, surface});
        triangles.push_back({{at(0), at(2), at(3)}, surface});
    }
}

void throughAPlane() {
    check(!through({1, 2, 3}, 1.0e-9).has_value(), "a triangle on one side does not cross");
    check(!through({0, 0, 1}, 1.0e-9).has_value(), "an edge on the plane only touches");
    check(!through({0, 1, 2}, 1.0e-9).has_value(), "a corner on the plane, the rest on one side, only touches");
    check(!through({0, 0, 0}, 1.0e-9).has_value(), "a triangle in the plane does not cross it");
    const auto cut = through({-1, 1, 1}, 1.0e-9);
    check(cut.has_value(), "one corner alone on its side crosses");
    check(near((*cut)[0][0], .5) && near((*cut)[0][1], .5) && near((*cut)[1][0], .5) && near((*cut)[1][2], .5), "it crosses halfway along both its edges");
    const auto corner = through({0, -1, 1}, 1.0e-9);
    check(corner.has_value() && near((*corner)[0][0], 1) && near((*corner)[1][1], .5) && near((*corner)[1][2], .5), "through a corner it crosses the far edge");
}

void twoTriangles() {
    std::vector<Triangle> triangles {
        {{Vec3 {-1, -1, 5}, Vec3 {1, -1, 5}, Vec3 {0, 1, 5}}, 0},
        {{Vec3 {0, -.5, 4}, Vec3 {0, .5, 4}, Vec3 {0, 0, 6}}, 1},
    };
    const auto piece = between(triangles, 0, 1);
    check(piece.has_value(), "a triangle standing through another crosses it");
    check(piece->triangle == 0, "the crossing belongs to the earlier surface");
    check(near(piece->eyeFrom.x, 0) && near(piece->eyeTo.x, 0) && near(piece->eyeFrom.z, 5) && near(piece->eyeTo.z, 5), "it lies where the two planes meet");
    check(near(std::abs(piece->eyeTo.y - piece->eyeFrom.y), .5), "it spans only where both triangles reach");
    const auto again = Vec3 {piece->eyeFrom.x, piece->eyeFrom.y, piece->eyeFrom.z};
    check(near((at(triangles[0].eye, piece->from) - again).length(), 0), "its weights find it on its own triangle");

    std::vector<Triangle> resting {
        {{Vec3 {-1, -1, 5}, Vec3 {1, -1, 5}, Vec3 {0, 1, 5}}, 0},
        {{Vec3 {0, -.5, 5}, Vec3 {0, .5, 5}, Vec3 {0, 0, 6}}, 1},
    };
    check(!between(resting, 0, 1).has_value(), "a triangle resting its edge on another only touches it");
    std::vector<Triangle> flat {
        {{Vec3 {-1, -1, 5}, Vec3 {1, -1, 5}, Vec3 {0, 1, 5}}, 0},
        {{Vec3 {-.5, -.5, 5}, Vec3 {.5, -.5, 5}, Vec3 {0, .5, 5}}, 1},
    };
    check(!between(flat, 0, 1).has_value(), "triangles in one plane do not cross");
    std::vector<Triangle> apart {
        {{Vec3 {-1, -1, 5}, Vec3 {1, -1, 5}, Vec3 {0, 1, 5}}, 0},
        {{Vec3 {3, -.5, 4}, Vec3 {3, .5, 4}, Vec3 {3, 0, 6}}, 1},
    };
    check(!between(apart, 0, 1).has_value(), "triangles apart do not cross");
}

void sunkBoxes() {
    // A small box sunk through the side of a big one: they cross in a square
    // on the big box's side, which joins into one closed run.
    std::vector<Triangle> triangles;
    addBox(triangles, {-1, -1, 4}, {1, 1, 6}, 0);
    addBox(triangles, {.5, -.25, 4.75}, {1.5, .25, 5.25}, 1);
    const auto found = runs(triangles, 1.0);
    check(found.size() == 1, "the crossing of two boxes is one run");
    double length = 0;
    for (const auto& piece : found.front()) {
        check(near(piece.eyeFrom.x, 1, 1.0e-9) && near(piece.eyeTo.x, 1, 1.0e-9), "it lies on the big box's side");
        check(triangles[piece.triangle].surface == 0, "it belongs to the big box");
        length += (piece.eyeTo - piece.eyeFrom).length();
    }
    check(near(length, 2 * (.5 + .5), 1.0e-9), "it goes all the way round the small box");
    for (std::size_t index = 1; index < found.front().size(); ++index) {
        check((found.front()[index].eyeFrom - found.front()[index - 1].eyeTo).length() < 1.0e-6, "each piece starts where the last ended");
    }

    std::vector<Triangle> stacked;
    addBox(stacked, {-1, -1, 4}, {1, 1, 6}, 0);
    addBox(stacked, {1, -.25, 4.75}, {2, .25, 5.25}, 1);
    check(runs(stacked, 1.0).empty(), "a box resting against another does not cross it");
}
}

int main() {
    throughAPlane();
    twoTriangles();
    sunkBoxes();
    std::cout << "Hidden line crossing tests passed\n";
    return 0;
}

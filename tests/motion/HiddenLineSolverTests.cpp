#include "../../Source/motion/render/HiddenLineSolver.h"
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <random>

namespace {
using motion::Vec3;
using motion::hidden::Interval;
using motion::hidden::Solver;

void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

double visibleLength(const std::vector<Interval>& pieces) {
    double total = 0;
    for (const auto& piece : pieces) { total += piece.to - piece.from; }
    return total;
}

// Whether the ray from the eye to P meets the triangle before P (Moller-Trumbore).
bool hiddenBy(Vec3 point, Vec3 a, Vec3 b, Vec3 c) {
    const auto e1 = b - a, e2 = c - a;
    const auto p = point.cross(e2);
    const auto determinant = e1.dot(p);
    if (std::abs(determinant) < 1.0e-14) { return false; }
    const auto inverse = 1.0 / determinant;
    const auto s = (Vec3 {} - a);
    const auto u = s.dot(p) * inverse;
    if (u < 0 || u > 1) { return false; }
    const auto q = s.cross(e1);
    const auto v = point.dot(q) * inverse;
    if (v < 0 || u + v > 1) { return false; }
    const auto t = e2.dot(q) * inverse;
    return t > 0 && t < 1 - 1.0e-6;
}

struct Mesh {
    std::vector<std::array<Vec3, 3>> triangles;
    std::vector<std::array<Vec3, 2>> edges;
};

// A UV sphere: quads as two triangles, its parallels and meridians as edges.
Mesh sphere(Vec3 centre, double radius, int around, int down) {
    Mesh mesh;
    const auto pi = 3.14159265358979323846;
    const auto at = [&](int i, int j) {
        const auto theta = 2 * pi * i / around, phi = pi * j / down;
        return centre + Vec3 {std::sin(phi) * std::cos(theta), std::cos(phi), std::sin(phi) * std::sin(theta)} * radius;
    };
    for (int j = 0; j < down; ++j) {
        for (int i = 0; i < around; ++i) {
            const auto a = at(i, j), b = at(i + 1, j), c = at(i + 1, j + 1), d = at(i, j + 1);
            mesh.triangles.push_back({a, b, c});
            mesh.triangles.push_back({a, c, d});
            mesh.edges.push_back({a, b});
            mesh.edges.push_back({a, d});
        }
    }
    return mesh;
}

void cube() {
    // Seen face on from the eye: only the near face's four edges show.
    const Vec3 corners[8] {{-1, -1, 4}, {1, -1, 4}, {1, 1, 4}, {-1, 1, 4}, {-1, -1, 6}, {1, -1, 6}, {1, 1, 6}, {-1, 1, 6}};
    const int faces[6][4] {{0, 1, 2, 3}, {4, 5, 6, 7}, {0, 1, 5, 4}, {1, 2, 6, 5}, {2, 3, 7, 6}, {3, 0, 4, 7}};
    Solver solver;
    for (const auto& face : faces) {
        solver.addTriangle(corners[face[0]], corners[face[1]], corners[face[2]]);
        solver.addTriangle(corners[face[0]], corners[face[2]], corners[face[3]]);
    }
    const int edges[12][2] {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    for (std::size_t edge = 0; edge < 12; ++edge) {
        std::vector<Interval> pieces;
        solver.visible(corners[edges[edge][0]], corners[edges[edge][1]], pieces);
        const auto shown = visibleLength(pieces);
        if (edge < 4) {
            check(std::abs(shown - 1) < 1.0e-9, "the near face's edges are wholly visible");
        } else {
            check(shown < 1.0e-6, "edges behind the near face are hidden");
        }
    }
    // A line passing behind the cube is hidden exactly where it crosses it.
    std::vector<Interval> pieces;
    solver.visible({-4, 0, 8}, {4, 0, 8}, pieces);
    check(pieces.size() == 2, "a line behind the cube shows either side of it");
    // The cube's silhouette is x = +-1 at depth 4, so +-2 at depth 8: s = 0.25 and 0.75.
    check(std::abs(pieces[0].to - .25) < 1.0e-6 && std::abs(pieces[1].from - .75) < 1.0e-6, "the hidden part ends exactly at the silhouette");
    // In front of the cube, nothing hides it; behind the eye, nothing is drawn.
    pieces.clear();
    solver.visible({-4, 0, 2}, {4, 0, 2}, pieces);
    check(pieces.size() == 1 && std::abs(visibleLength(pieces) - 1) < 1.0e-9, "a line in front is wholly visible");
    pieces.clear();
    solver.visible({0, 0, -1}, {0, 0, 1}, pieces);
    check(pieces.size() == 1 && std::abs(pieces[0].from - (1 + Solver::nearPlane) / 2) < 1.0e-9, "only the part beyond the near plane is visible");
}

void matchesRayCasting() {
    // Random triangles and lines: every sampled point agrees with a ray cast,
    // away from the boundaries the solver finds.
    std::mt19937 random(7);
    std::uniform_real_distribution<double> spread(-2, 2), depth(2, 9);
    const auto point = [&] { return Vec3 {spread(random), spread(random), depth(random)}; };
    for (int scene = 0; scene < 30; ++scene) {
        Solver solver;
        std::vector<std::array<Vec3, 3>> triangles;
        for (int index = 0; index < 25; ++index) {
            const auto centre = point();
            triangles.push_back({centre + Vec3 {spread(random), spread(random), spread(random)} * .4, centre + Vec3 {spread(random), spread(random), spread(random)} * .4, centre + Vec3 {spread(random), spread(random), spread(random)} * .4});
            solver.addTriangle(triangles.back()[0], triangles.back()[1], triangles.back()[2]);
        }
        for (int line = 0; line < 40; ++line) {
            const auto a = point(), b = point();
            std::vector<Interval> pieces;
            solver.visible(a, b, pieces);
            for (int sample = 0; sample <= 400; ++sample) {
                const auto s = sample / 400.0;
                bool shown = false, nearBoundary = false;
                for (const auto& piece : pieces) {
                    shown = shown || (s >= piece.from && s <= piece.to);
                    nearBoundary = nearBoundary || std::abs(s - piece.from) < 1.0e-4 || std::abs(s - piece.to) < 1.0e-4;
                }
                if (nearBoundary) { continue; }
                const auto p = a + (b - a) * s;
                bool hidden = false;
                for (const auto& triangle : triangles) { hidden = hidden || hiddenBy(p, triangle[0], triangle[1], triangle[2]); }
                // Grazing hits within the solver's tolerances may differ.
                if (hidden == shown) {
                    bool grazing = false;
                    for (const auto& triangle : triangles) {
                        for (const auto offset : {-1.0e-4, 1.0e-4}) {
                            grazing = grazing || hiddenBy(a + (b - a) * (s + offset), triangle[0], triangle[1], triangle[2]) != hidden;
                        }
                    }
                    check(grazing, "the solver agrees with ray casting");
                }
            }
        }
    }
}

void benchmark() {
    // A dense mesh behind another: the cost of one frame, as a bake pays it.
    for (const auto& [around, down] : {std::pair {48, 24}, std::pair {128, 64}}) {
        auto front = sphere({0, 0, 4}, 1, around, down);
        const auto back = sphere({.6, .2, 6.5}, 1.4, around, down);
        front.triangles.insert(front.triangles.end(), back.triangles.begin(), back.triangles.end());
        front.edges.insert(front.edges.end(), back.edges.begin(), back.edges.end());
        Solver solver;
        std::vector<Interval> pieces;
        pieces.reserve(front.edges.size() * 2);
        constexpr int frames = 10;
        double shown = 0;
        const auto started = std::chrono::steady_clock::now();
        for (int frame = 0; frame < frames; ++frame) {
            solver.clear();
            for (const auto& triangle : front.triangles) { solver.addTriangle(triangle[0], triangle[1], triangle[2]); }
            pieces.clear();
            for (const auto& edge : front.edges) { solver.visible(edge[0], edge[1], pieces); }
            shown = visibleLength(pieces);
        }
        const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count() / frames;
        std::cout << "Hidden lines: " << front.triangles.size() << " triangles, " << front.edges.size() << " edges: "
                  << seconds * 1000 << " ms per frame, " << shown / static_cast<double>(front.edges.size()) * 100 << "% of edge length shown\n";
        // About half of each sphere faces away, and the near one hides more of the far one.
        check(shown < front.edges.size() * .6 && shown > front.edges.size() * .2, "a sphere's far side is hidden");
        check(seconds < .25, "a frame of tens of thousands of triangles takes well under a quarter of a second");
    }
}
}

int main() {
    cube();
    matchesRayCasting();
    benchmark();
    std::cout << "Hidden line solver tests passed\n";
}

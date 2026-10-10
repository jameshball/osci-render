#include "../../Source/motion/render/HiddenLineDepth.h"
#include <array>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <random>

namespace {
using motion::hidden::DepthBuffer;
using motion::hidden::Screen;

void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

struct Point3 { double x, y, z; };
Screen project(Point3 p) { return {p.x / p.z, p.y / p.z, 1 / p.z}; }
Point3 lerp(Point3 a, Point3 b, double t) { return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t}; }

// The fraction of a segment that shows, point by point.
double shown(const DepthBuffer& depth, Point3 a, Point3 b) {
    int count = 0;
    constexpr int samples = 2000;
    for (int index = 0; index <= samples; ++index) { count += depth.shows(project(lerp(a, b, index / double(samples)))) ? 1 : 0; }
    return count / double(samples + 1);
}

// Whether the ray from the eye to p meets the triangle before it.
bool hiddenBy(Point3 p, Point3 a, Point3 b, Point3 c) {
    const auto sub = [](Point3 u, Point3 v) { return Point3 {u.x - v.x, u.y - v.y, u.z - v.z}; };
    const auto cross = [](Point3 u, Point3 v) { return Point3 {u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x}; };
    const auto dot = [](Point3 u, Point3 v) { return u.x * v.x + u.y * v.y + u.z * v.z; };
    const auto e1 = sub(b, a), e2 = sub(c, a), q = cross(p, e2);
    const auto determinant = dot(e1, q);
    if (std::abs(determinant) < 1.0e-14) { return false; }
    const auto s = sub({0, 0, 0}, a);
    const auto u = dot(s, q) / determinant;
    if (u < 0 || u > 1) { return false; }
    const auto r = cross(s, e1);
    const auto v = dot(p, r) / determinant;
    if (v < 0 || u + v > 1) { return false; }
    const auto t = dot(e2, r) / determinant;
    return t > 0 && t < 1 - 1.0e-3;
}

void cube() {
    // Seen face on: the near face's edges show; everything behind it doesn't.
    const Point3 corners[8] {{-1, -1, 4}, {1, -1, 4}, {1, 1, 4}, {-1, 1, 4}, {-1, -1, 6}, {1, -1, 6}, {1, 1, 6}, {-1, 1, 6}};
    const int faces[6][4] {{0, 1, 2, 3}, {4, 5, 6, 7}, {0, 1, 5, 4}, {1, 2, 6, 5}, {2, 3, 7, 6}, {3, 0, 4, 7}};
    DepthBuffer depth;
    for (const auto& face : faces) {
        depth.triangle(project(corners[face[0]]), project(corners[face[1]]), project(corners[face[2]]));
        depth.triangle(project(corners[face[0]]), project(corners[face[2]]), project(corners[face[3]]));
    }
    const int edges[12][2] {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    for (std::size_t edge = 0; edge < 12; ++edge) {
        const auto fraction = shown(depth, corners[edges[edge][0]], corners[edges[edge][1]]);
        if (edge < 4) {
            check(fraction == 1, "the near face's edges show whole");
        } else {
            check(fraction < .01, "edges behind the near face are hidden");
        }
    }
    // A line behind the cube shows either side of it, cut at the silhouette
    // (x = +-1 at depth 4 is +-2 at depth 8: a quarter of the way along).
    const Point3 a {-4, 0, 8}, b {4, 0, 8};
    check(depth.shows(project(lerp(a, b, .25 - .005))) && !depth.shows(project(lerp(a, b, .25 + .005))), "it is cut within a pixel of the silhouette");
    check(!depth.shows(project(lerp(a, b, .75 - .005))) && depth.shows(project(lerp(a, b, .75 + .005))), "on both sides");
    check(shown(depth, {-4, 0, 2}, {4, 0, 2}) == 1, "a line in front shows whole");
}

void edgeOn() {
    // A face seen nearly edge on, its depth changing steeply across its dozen
    // or so pixels: a line on it shows, and one just behind it, 2% further
    // away, is hidden (as an arm's far edge behind the body it touches).
    const Point3 corners[4] {{1, -1, 4}, {1.3, -1, 6}, {1.3, 1, 6}, {1, 1, 4}};
    DepthBuffer depth;
    depth.triangle(project(corners[0]), project(corners[1]), project(corners[2]));
    depth.triangle(project(corners[0]), project(corners[2]), project(corners[3]));
    const Point3 a {1.15, -.8, 5}, b {1.15, .8, 5};
    const auto behind = [](Point3 p) { return Point3 {p.x * 1.02, p.y * 1.02, p.z * 1.02}; };
    check(shown(depth, a, b) == 1, "a line on a face seen edge on shows whole");
    check(shown(depth, corners[0], corners[1]) == 1 && shown(depth, corners[1], corners[2]) == 1, "its edges show whole");
    check(shown(depth, behind(a), behind(b)) < .01, "a line just behind it is hidden");
}

void wideCanvas() {
    // A face past the side of the square frame, where a wide canvas still
    // shows it: what is behind it there is hidden, and the buffer, covered
    // anew for a square canvas, has nothing there to hide anything.
    const Point3 corners[4] {{6, -1, 4}, {7, -1, 4}, {7, 1, 4}, {6, 1, 4}};
    const Point3 behind {13, 0, 8}, before {4.875, 0, 3};
    DepthBuffer depth(motion::hidden::Reach::ofCanvas(1920, 1080));
    depth.triangle(project(corners[0]), project(corners[1]), project(corners[2]));
    depth.triangle(project(corners[0]), project(corners[2]), project(corners[3]));
    check(!depth.shows(project(behind)), "a point behind a face past a square frame's side is hidden on a wide canvas");
    check(depth.shows(project(before)), "and one in front of it shows");
    depth.cover({});
    depth.triangle(project(corners[0]), project(corners[1]), project(corners[2]));
    depth.triangle(project(corners[0]), project(corners[2]), project(corners[3]));
    check(depth.shows(project(behind)), "a square canvas shows nothing there, so nothing there is hidden");
}

void edgeOnNeighbour() {
    // A face seen nearly edge on, nearest at a pixel's centre, its edge
    // passing between that centre and a point just past it; the point lies
    // just behind a face-on square. The square hides the point though the
    // edge-on face, not the square, has the pixel.
    DepthBuffer depth;
    const auto square = [](double x, double y) { return Screen {x, y, .25}; };
    depth.triangle(square(-.5, -.5), square(.5, -.5), square(.5, .5));
    depth.triangle(square(-.5, -.5), square(.5, .5), square(-.5, .5));
    const auto pixel = DepthBuffer::pixel;
    const auto centre = (512 + .5) * pixel - DepthBuffer::overscan;
    const auto edge = centre + .15 * pixel;
    // Nearest at its edge, rising steeply toward the eye leftward.
    depth.triangle({edge, -.3, .3}, {edge, .3, .3}, {edge - 5 * pixel, 0, 3});
    const Screen behind {centre + .3 * pixel, 0, .24};
    check(!depth.shows(behind), "a point past an edge-on face's edge is still hidden by the face that covers it");
    check(depth.shows({centre + .3 * pixel, 0, .26}), "and one in front of that face shows");
}

void matchesRayCasting() {
    // Random triangles and points: the buffer agrees with ray casting except
    // within a pixel or two of an edge.
    std::mt19937 random(7);
    std::uniform_real_distribution<double> spread(-.8, .8), depthRange(2, 9);
    int disagreements = 0, total = 0;
    for (int scene = 0; scene < 20; ++scene) {
        DepthBuffer depth;
        std::vector<std::array<Point3, 3>> triangles;
        for (int index = 0; index < 20; ++index) {
            const auto z = depthRange(random);
            const Point3 centre {spread(random) * z, spread(random) * z, z};
            std::array<Point3, 3> triangle {};
            for (auto& corner : triangle) { corner = {centre.x + spread(random), centre.y + spread(random), centre.z + spread(random)}; }
            triangles.push_back(triangle);
            depth.triangle(project(triangle[0]), project(triangle[1]), project(triangle[2]));
        }
        for (int sample = 0; sample < 4000; ++sample) {
            const auto z = depthRange(random);
            const Point3 p {spread(random) * z, spread(random) * z, z};
            bool hidden = false;
            for (const auto& triangle : triangles) { hidden = hidden || hiddenBy(p, triangle[0], triangle[1], triangle[2]); }
            disagreements += depth.shows(project(p)) == hidden ? 1 : 0;
            ++total;
        }
    }
    std::cout << "Depth buffer disagrees with ray casting at " << 100.0 * disagreements / total << "% of points\n";
    check(disagreements < total / 100, "the depth buffer agrees with ray casting but at edges");
}

void benchmark() {
    // A dense sphere: drawing it, then testing a half-pixel walk round it.
    const auto pi = 3.14159265358979323846;
    constexpr int around = 128, down = 64;
    const auto at = [&](int i, int j) {
        const auto theta = 2 * pi * i / around, phi = pi * j / down;
        return Point3 {std::sin(phi) * std::cos(theta), std::cos(phi), 4 + std::sin(phi) * std::sin(theta)};
    };
    DepthBuffer depth;
    const auto started = std::chrono::steady_clock::now();
    depth.clear();
    for (int j = 0; j < down; ++j) {
        for (int i = 0; i < around; ++i) {
            depth.triangle(project(at(i, j)), project(at(i + 1, j)), project(at(i + 1, j + 1)));
            depth.triangle(project(at(i, j)), project(at(i + 1, j + 1)), project(at(i, j + 1)));
        }
    }
    const auto drawn = std::chrono::steady_clock::now();
    int showing = 0;
    for (int j = 0; j < down; ++j) {
        for (int i = 0; i < around; ++i) { showing += shown(depth, at(i, j), at(i + 1, j)) > .5 ? 1 : 0; }
    }
    const auto tested = std::chrono::steady_clock::now();
    std::cout << "Depth buffer: " << 2 * around * down << " triangles drawn in " << std::chrono::duration<double, std::milli>(drawn - started).count() << " ms; "
              << around * down << " edges tested at 2000 points each in " << std::chrono::duration<double, std::milli>(tested - drawn).count() << " ms\n";
    check(showing > around * down / 3 && showing < around * down * 2 / 3, "about half a sphere's edges face the eye");
}
}

int main() {
    cube();
    edgeOn();
    wideCanvas();
    edgeOnNeighbour();
    matchesRayCasting();
    benchmark();
    std::cout << "Hidden line depth tests passed\n";
}

#include "../../Source/motion/render/HiddenLineCoverage.h"
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {
using motion::hidden::Coverage;
using motion::hidden::DepthBuffer;
using motion::hidden::Screen;

void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

constexpr double pixel = DepthBuffer::pixel;

// Points a pixel apart from (x0, y0) to (x1, y1).
std::vector<Screen> line(double x0, double y0, double x1, double y1) {
    const auto steps = std::max(1, static_cast<int>(std::ceil(std::hypot(x1 - x0, y1 - y0) / pixel)));
    std::vector<Screen> points;
    for (int step = 0; step <= steps; ++step) {
        const auto t = static_cast<double>(step) / steps;
        points.push_back({x0 + (x1 - x0) * t, y0 + (y1 - y0) * t, 1});
    }
    return points;
}

void retracing() {
    Coverage coverage;
    coverage.clear({});
    coverage.add(line(-.5, 0, .5, 0));
    check(coverage.covers({0, .7 * pixel, 1}, 1, 0), "a line most of a pixel beside one drawn, running along it, is covered");
    check(coverage.covers({.2, 0, 1}, -1, .05), "and running back along it");
    check(!coverage.covers({0, 3 * pixel, 1}, 1, 0), "a line a few pixels beside it is not");
    check(!coverage.covers({0, 0, 1}, 0, 1), "nor one crossing it");
    check(!coverage.covers({0, 0, 1}, 1, .5), "nor one meeting it at a shallow angle");
    check(!coverage.covers({.5, 0, 1}, 1, 0) && !coverage.covers({.5 + pixel, 0, 1}, 1, 0), "a line going on from its end is not");
    check(!coverage.covers({-.5 + .2 * pixel, 0, 1}, 1, 0), "nor is its very end");
}

void curves() {
    // A circle drawn as many points: covered all round, its joins included.
    Coverage coverage;
    coverage.clear({});
    std::vector<Screen> circle;
    constexpr int steps = 2000;
    for (int step = 0; step <= steps; ++step) {
        const auto angle = 2 * 3.14159265358979323846 * step / steps;
        circle.push_back({.4 * std::cos(angle), .4 * std::sin(angle), 1});
    }
    coverage.add(circle);
    int covered = 0;
    for (int step = 1; step < 400; ++step) {
        const auto angle = 2 * 3.14159265358979323846 * step / 400;
        covered += coverage.covers({(.4 + .5 * pixel) * std::cos(angle), (.4 + .5 * pixel) * std::sin(angle), 1}, -std::sin(angle), std::cos(angle)) ? 1 : 0;
    }
    check(covered == 399, "a circle drawn again just beside itself is covered all round");
}

void wide() {
    // Past a square frame's side, on a wide canvas.
    Coverage coverage;
    coverage.clear(motion::hidden::Reach::ofCanvas(1920, 1080));
    coverage.add(line(1.4, -.2, 1.4, .2));
    check(coverage.covers({1.4, 0, 1}, 0, 1), "a wide canvas's sides are covered too");
    coverage.clear({});
    check(!coverage.covers({1.4, 0, 1}, 0, 1), "and clearing forgets what was drawn");
}
}

int main() {
    retracing();
    curves();
    wide();
    std::cout << "Hidden line coverage tests passed\n";
}

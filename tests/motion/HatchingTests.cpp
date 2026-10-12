#include "../../Source/motion/render/HiddenLineHatching.h"
#include <cstdlib>
#include <iostream>
#include <string>

using namespace motion;
using namespace motion::hidden::hatching;

namespace {
void check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

// The cells' middles in order, and how wide each step between them is.
std::vector<std::pair<double, double>> middles(double half, int across, double light) {
    std::vector<std::pair<double, double>> found;
    curve(half, across, [&](double x, double y) { return std::optional<Seen>(Seen {0, {x, y, 0}, light}); }, [&](const std::optional<Seen>& seen, double) {
        if (seen.has_value()) { found.emplace_back(seen->at.x, seen->at.y); }
    });
    return found;
}
}

int main() {
    // In full light the curve is Hilbert's at its finest: every cell, each a
    // step from the last.
    {
        const auto points = middles(1, 16, 1);
        check(points.size() >= 16 * 16, "every finest cell: " + std::to_string(points.size()));
        double longest = 0;
        for (std::size_t index = 1; index < points.size(); ++index) {
            longest = std::max(longest, std::hypot(points[index].first - points[index - 1].first, points[index].second - points[index - 1].second));
        }
        check(longest <= 2.0 / 16 + 1.0e-9, "each a cell from the last, unbroken: " + std::to_string(longest));
    }
    // Less light, fewer cells: as dense as the light, as a share of the
    // brightest seen.
    {
        const auto lengths = [&](double dim) {
            double left = 0, right = 0;
            std::optional<std::pair<double, double>> last;
            curve(1, 32, [&](double x, double y) { return std::optional<Seen>(Seen {0, {x, y, 0}, x < 0 ? dim : 1.0}); }, [&](const std::optional<Seen>& seen, double) {
                if (!seen.has_value()) { return; }
                if (last.has_value() && (last->first < 0) == (seen->at.x < 0)) { ((seen->at.x < 0) ? left : right) += std::hypot(seen->at.x - last->first, seen->at.y - last->second); }
                last = std::pair {seen->at.x, seen->at.y};
            });
            return left / right;
        };
        const auto half = lengths(.5), quarter = lengths(.25);
        check(half > .3 && half < .75, "half the light, about half the curve: " + std::to_string(half));
        check(quarter < half, "a quarter, less still: " + std::to_string(quarter));
        check(std::abs(lengths(1) - 1) < .05, "alike, alike");
        // Only the share counts: twice as bright everywhere is the same curve.
        const auto middles = [](double scale) {
            std::vector<std::pair<double, double>> found;
            curve(1, 32, [&](double x, double y) { return std::optional<Seen>(Seen {0, {x, y, 0}, scale * (x + 1)}); }, [&](const std::optional<Seen>& seen, double) {
                if (seen.has_value()) { found.emplace_back(seen->at.x, seen->at.y); }
            });
            return found;
        };
        check(middles(1) == middles(3), "exposed to the brightest");
        check(middles(.37) == middles(.37), "the same light, the same curve");
    }
    // A gradient of light is a gradient of density, not steps: from dark to
    // full across the frame, each band of it has more cells than the last.
    {
        std::vector<int> bands(8, 0);
        curve(1, 64, [](double x, double y) { return std::optional<Seen>(Seen {0, {x, y, 0}, (x + 1) / 2}); }, [&](const std::optional<Seen>& seen, double) {
            if (seen.has_value()) { ++bands[static_cast<std::size_t>(std::clamp(static_cast<int>((seen->at.x + 1) / 2 * 8), 0, 7))]; }
        });
        for (std::size_t band = 2; band < bands.size(); ++band) { check(bands[band] >= bands[band - 1], "denser band by band: " + std::to_string(band) + " " + std::to_string(bands[band - 1]) + " " + std::to_string(bands[band])); }
        check(bands[7] > 3 * bands[2], "far denser in full light than dim");
    }
    // Strokes: runs of cells on one surface; where nothing is, the curve breaks.
    {
        const auto found = strokes(1, 16, [](double x, double y) -> std::optional<Seen> {
            if (std::hypot(x, y) > .8) { return std::nullopt; }
            return Seen {x < 0 ? 0u : 1u, {x, y, 0}, 1};
        });
        bool both = false, left = false, right = false;
        for (const auto& stroke : found) {
            for (const auto& point : stroke.points) { check((point.at.x < 0) == (stroke.surface == 0), "each stroke on its own surface"); }
            left = left || stroke.surface == 0;
            right = right || stroke.surface == 1;
        }
        both = left && right;
        check(both, "both surfaces hatched");
    }
    // Where no light falls, nothing is drawn.
    {
        const auto found = strokes(1, 16, [](double x, double y) -> std::optional<Seen> { return Seen {0, {x, y, 0}, x < 0 ? 0.0 : 1.0}; });
        for (const auto& stroke : found) {
            for (const auto& point : stroke.points) { check(point.at.x > 0, "none in the dark"); }
        }
        check(!found.empty(), "but the lit half is");
    }
    // However little of a cell a surface fills, it is found and hatched.
    {
        const auto found = strokes(1, 64, [](double x, double y) -> std::optional<Seen> {
            if (std::hypot(x - .61, y - .58) > .08) { return std::nullopt; }
            return Seen {0, {x, y, 0}, 1};
        });
        std::size_t points = 0;
        for (const auto& stroke : found) {
            for (const auto& point : stroke.points) { check(std::hypot(point.at.x - .61, point.at.y - .58) <= .08, "on the small surface"); }
            points += stroke.points.size();
        }
        check(points > 12, "a small surface is hatched as finely as a large one: " + std::to_string(points));
    }
    // Held to what the beam can draw: no more points than allowed, but still
    // densest where the light is.
    {
        const auto look = [](double x, double y) { return std::optional<Seen>(Seen {0, {x, y, 0}, (x + 1) / 2}); };
        std::size_t whole = 0;
        for (const auto& stroke : strokes(1, 128, look)) { whole += stroke.points.size(); }
        const auto held = strokes(1, 128, look, whole / 4);
        std::size_t points = 0, left = 0, right = 0;
        for (const auto& stroke : held) {
            for (const auto& point : stroke.points) {
                ++points;
                (point.at.x < 0 ? left : right) += 1;
            }
        }
        check(points <= whole / 4 && points > whole / 8, "about as many as allowed: " + std::to_string(points) + " of " + std::to_string(whole / 4));
        check(right > 2 * left, "densest in the light still");
    }
    std::cout << "HatchingTests passed\n";
    return 0;
}

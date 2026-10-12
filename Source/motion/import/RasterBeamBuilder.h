#pragma once

#include "../model/Cancellation.h"
#include "../model/RasterSettings.h"
#include "../render/HilbertCurve.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <numeric>

namespace motion {

// Worker-only raster preparation. Pixel edges form closed, four-connected
// contours; diagonally touching pixels remain separate paths. All vertices are
// retained, so a small point budget fails rather than cutting across corners.
// Or horizontal passes over what is lit; or a Hilbert curve over it (see
// hilbert::curve, as the camera's hatching draws), as finely as the point
// budget allows: one line wherever the picture is lit. By density, its cells
// divided finer where the picture is brighter, drawn in each pixel's hue, so
// it shows its shading by X and Y alone; or by colour, as fine all over, in
// each pixel's colour, for a scope with brightness.
class RasterBeamBuilder {
public:
    struct Result {
        std::vector<PointSample> points;
        std::string error;
        explicit operator bool() const { return error.empty() && !points.empty(); }
    };

    // Tightly packed, straight-alpha RGBA8. Inversion applies to RGB before
    // thresholding and emission; alpha is never inverted. Transparent pixels
    // are always blank. Output uses linear interpolation safely, including the
    // last-to-first seam: travel endpoints have RGB=0 and unchanged geometry.
    static Result build(const std::uint8_t* rgba, std::size_t bytes, int width, int height, const RasterSettings& settings,
            const std::atomic<bool>* cancel = nullptr) {
        if (width < 1 || height < 1 || width > 512 || height > 512 || rgba == nullptr
            || bytes != static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4) {
            return {{}, "Raster must be tightly packed RGBA8, with dimensions between 1 and 512."};
        }
        const auto invalid = settings.validate();
        if (!invalid.empty()) { return {{}, invalid}; }
        if (cancelled(cancel)) { return {{}, "Raster preparation cancelled."}; }
        try {
            const auto colour = [&](int x, int y) {
                x = std::clamp(x, 0, width - 1);
                y = std::clamp(y, 0, height - 1);
                const auto* pixel = rgba + (static_cast<std::size_t>(y) * width + x) * 4;
                const auto alpha = static_cast<float>(pixel[3]) / 255.0f;
                std::array<float, 3> rgb;
                for (int c = 0; c < 3; ++c) {
                    const auto value = static_cast<float>(pixel[c]) / 255.0f;
                    rgb[static_cast<std::size_t>(c)] = (settings.invert ? 1.0f - value : value) * alpha;
                }
                return rgb;
            };
            std::vector<std::uint8_t> mask(static_cast<std::size_t>(width) * height);
            for (int y = 0; y < height; ++y) {
                if (cancelled(cancel)) { return {{}, "Raster preparation cancelled."}; }
                for (int x = 0; x < width; ++x) {
                    const auto rgb = colour(x, y);
                    const auto luminance = 0.2126 * rgb[0] + 0.7152 * rgb[1] + 0.0722 * rgb[2];
                    mask[static_cast<std::size_t>(y) * width + x] = luminance > 0 && luminance >= settings.threshold;
                }
            }
            const auto lit = [&](int x, int y) {
                return x >= 0 && y >= 0 && x < width && y < height && mask[static_cast<std::size_t>(y) * width + x] != 0;
            };
            std::vector<Path> paths;
            std::size_t minimum = 0;
            const auto addPath = [&](Path path) {
                // One dark sample at either end; bright vertices include the
                // repeated first vertex for closed paths.
                minimum += path.vertices.size() + 2;
                if (minimum > settings.pointsPerFrame) { return false; }
                paths.push_back(std::move(path));
                return true;
            };
            if (settings.mode == RasterSettings::Mode::hilbert) {
                // A grid round the picture, single pixels at its finest;
                // dark outside it.
                int deepest = 0;
                while ((1 << deepest) < std::max(width, height)) { ++deepest; }
                const auto cells = static_cast<std::size_t>(1) << deepest;
                std::vector<float> brightness(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
                for (int y = 0; y < height; ++y) {
                    for (int x = 0; x < width; ++x) {
                        const auto rgb = colour(x, y);
                        brightness[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)] = static_cast<float>(0.2126 * rgb[0] + 0.7152 * rgb[1] + 0.0722 * rgb[2]);
                    }
                }
                const auto density = settings.shading == RasterSettings::Shading::density;
                // By density: how bright each cell is, stretched between the
                // picture's own darkest and brightest (but the odd pixel), so
                // the curve's density spans its range, then to the power of
                // 1.5, as a line on a scope with brightness would show it. By
                // colour: how much of each cell is lit, the curve as fine
                // over all of it, its colour showing how bright.
                auto sorted = brightness;
                const auto at = [&](double share) {
                    const auto index = static_cast<std::size_t>(share * static_cast<double>(sorted.size() - 1));
                    std::nth_element(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(index), sorted.end());
                    return sorted[index];
                };
                const auto darkest = at(.01), brightest = at(.99);
                const auto stretched = brightest - darkest > .05f;
                std::vector<float> finest(cells * cells, 0.0f);
                for (int y = 0; y < height; ++y) {
                    for (int x = 0; x < width; ++x) {
                        const auto value = brightness[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)];
                        const auto lit = value > 0 && value >= settings.threshold;
                        const auto spread = stretched ? std::clamp((value - darkest) / (brightest - darkest), 0.0f, 1.0f) : value;
                        finest[static_cast<std::size_t>(y) * cells + static_cast<std::size_t>(x)] = density ? std::pow(spread, 1.5f) : (lit ? 1.0f : 0.0f);
                    }
                }
                const auto pyramid = hilbert::Pyramid::of(std::move(finest), deepest, hilbert::Pyramid::Combine::mean);
                // Whether a cell of the curve is drawn: by density, bright
                // enough; by colour, mostly lit.
                const auto shows = [&](const hilbert::Cell& cell) {
                    const auto value = pyramid.at(cell.level, cell.row, cell.column);
                    return density ? value > 0 && value >= settings.threshold : value >= .5f;
                };
                const auto curve = [&](double gain, auto&& visit) {
                    hilbert::curve(deepest, [&](const hilbert::Cell& cell) {
                        const auto value = pyramid.at(cell.level, cell.row, cell.column);
                        return value > 0 && cell.size * value * gain > hilbert::threshold(cell.column, cell.row, cell.level);
                    }, visit);
                };
                // As fine as the picture asks, or less, evenly, to fit the
                // budget: each cell drawn a sample, each run two more.
                const auto gain = hilbert::fitting(settings.pointsPerFrame, [&](double trying) {
                    std::size_t count = 0;
                    bool open = false;
                    curve(trying, [&](const hilbert::Cell& cell) {
                        const auto drawn = shows(cell);
                        count += drawn ? (open ? 1 : 3) : 0;
                        open = drawn;
                    });
                    return count;
                });
                if (cancelled(cancel)) { return {{}, "Raster preparation cancelled."}; }
                Path path;
                path.fullHue = density;
                bool fits = true;
                curve(gain, [&](const hilbert::Cell& cell) {
                    if (shows(cell)) {
                        path.vertices.push_back({cell.x, cell.y});
                        return;
                    }
                    if (!path.vertices.empty() && fits) { fits = addPath(std::move(path)); }
                    path = {};
                    path.fullHue = density;
                });
                if (!path.vertices.empty() && fits) { fits = addPath(std::move(path)); }
                if (!fits) { return tooComplex(); }
            } else if (settings.mode == RasterSettings::Mode::scanlines) {
                for (int y = 0; y < height; ++y) {
                    if (cancelled(cancel)) { return {{}, "Raster preparation cancelled."}; }
                    const int direction = (y & 1) == 0 ? 1 : -1;
                    int x = direction == 1 ? 0 : width - 1;
                    while (x >= 0 && x < width) {
                        if (!lit(x, y)) { x += direction; continue; }
                        const int first = x;
                        while (x + direction >= 0 && x + direction < width && lit(x + direction, y)) { x += direction; }
                        Path path;
                        path.vertices.push_back({first + 0.5, y + 0.5});
                        if (x != first) { path.vertices.push_back({x + 0.5, y + 0.5}); }
                        if (!addPath(std::move(path))) { return tooComplex(); }
                        x += direction;
                    }
                }
            } else {
                // Four outgoing-edge bits per grid vertex, not a graph of
                // heap-allocated pixel nodes. At 512x512 this is 263169 bytes.
                const int stride = width + 1;
                std::vector<std::uint8_t> edges(static_cast<std::size_t>(stride) * (height + 1));
                const auto edge = [&](int x, int y, int direction) { edges[static_cast<std::size_t>(y) * stride + x] |= static_cast<std::uint8_t>(1u << direction); };
                for (int y = 0; y < height; ++y) {
                    if (cancelled(cancel)) { return {{}, "Raster preparation cancelled."}; }
                    for (int x = 0; x < width; ++x) {
                        if (!lit(x, y)) { continue; }
                        if (!lit(x, y - 1)) { edge(x, y, 0); }
                        if (!lit(x + 1, y)) { edge(x + 1, y, 1); }
                        if (!lit(x, y + 1)) { edge(x + 1, y + 1, 2); }
                        if (!lit(x - 1, y)) { edge(x, y + 1, 3); }
                    }
                }
                constexpr std::array<int, 4> dx {1, 0, -1, 0}, dy {0, 1, 0, -1};
                for (int startY = 0; startY <= height; ++startY) {
                    if (cancelled(cancel)) { return {{}, "Raster preparation cancelled."}; }
                    for (int startX = 0; startX <= width; ++startX) {
                        auto& available = edges[static_cast<std::size_t>(startY) * stride + startX];
                        while (available != 0) {
                            int direction = 0;
                            while ((available & (1u << direction)) == 0) { ++direction; }
                            const int initialDirection = direction;
                            int x = startX, y = startY;
                            Path path;
                            path.contour = true;
                            path.vertices.push_back({static_cast<double>(x), static_cast<double>(y)});
                            std::size_t steps = 0;
                            do {
                                edges[static_cast<std::size_t>(y) * stride + x] &= static_cast<std::uint8_t>(~(1u << direction));
                                x += dx[static_cast<std::size_t>(direction)];
                                y += dy[static_cast<std::size_t>(direction)];
                                if ((++steps & 255) == 0 && cancelled(cancel)) { return {{}, "Raster preparation cancelled."}; }
                                if (x == startX && y == startY) { break; }
                                const auto nextEdges = edges[static_cast<std::size_t>(y) * stride + x];
                                int nextDirection = -1;
                                // Keep foreground on the right, splitting diagonal contacts.
                                for (const int turn : {1, 0, 3, 2}) {
                                    const int candidate = (direction + turn) & 3;
                                    if ((nextEdges & (1u << candidate)) != 0) { nextDirection = candidate; break; }
                                }
                                if (nextDirection < 0) { return {{}, "Invalid raster contour topology."}; }
                                if (nextDirection != direction) {
                                    path.vertices.push_back({static_cast<double>(x), static_cast<double>(y)});
                                    if (minimum + path.vertices.size() + 3 > settings.pointsPerFrame) { return tooComplex(); }
                                }
                                direction = nextDirection;
                            } while (true);
                            // A start in a straight run is redundant; remove it
                            // before closing, retaining all actual corners.
                            if (direction == initialDirection && path.vertices.size() > 1) { path.vertices.erase(path.vertices.begin()); }
                            path.vertices.push_back(path.vertices.front());
                            if (!addPath(std::move(path))) { return tooComplex(); }
                        }
                    }
                }
            }
            if (paths.empty()) { return {std::vector<PointSample>(settings.pointsPerFrame), {}}; }

            struct Segment { std::size_t path, vertex, extra = 0; double length = 0, fraction = 0; };
            std::vector<Segment> segments;
            double totalLength = 0;
            for (std::size_t p = 0; p < paths.size(); ++p) {
                const auto& vertices = paths[p].vertices;
                for (std::size_t v = 1; v < vertices.size(); ++v) {
                    const auto length = std::hypot(vertices[v].x - vertices[v - 1].x, vertices[v].y - vertices[v - 1].y);
                    segments.push_back({p, v - 1, 0, length, 0});
                    totalLength += length;
                }
            }
            const auto remaining = settings.pointsPerFrame - minimum;
            if (segments.empty()) {
                // Isolated scanline pixels have zero geometric length. Dwell
                // evenly, with deterministic remainder order.
                for (std::size_t p = 0; p < paths.size(); ++p) { paths[p].dwell = remaining / paths.size() + (p < remaining % paths.size() ? 1 : 0); }
            } else {
                std::size_t assigned = 0;
                for (auto& segment : segments) {
                    const auto quota = static_cast<double>(remaining) * segment.length / totalLength;
                    segment.extra = static_cast<std::size_t>(std::floor(quota));
                    segment.fraction = quota - static_cast<double>(segment.extra);
                    assigned += segment.extra;
                }
                std::vector<std::size_t> order(segments.size());
                std::iota(order.begin(), order.end(), 0);
                std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return segments[a].fraction > segments[b].fraction; });
                for (std::size_t i = 0; i < remaining - assigned; ++i) { ++segments[order[i]].extra; }
            }
            std::vector<PointSample> output;
            output.reserve(settings.pointsPerFrame);
            const auto emit = [&](Vertex point, const std::array<float, 3>& rgb) {
                const auto scale = 2.0 / static_cast<double>(std::max(width, height));
                output.push_back({static_cast<float>((point.x - width * 0.5) * scale),
                    static_cast<float>((height * 0.5 - point.y) * scale), 0, rgb[0], rgb[1], rgb[2]});
            };
            // A Hilbert curve shows brightness by how dense it is, so it is
            // drawn in each pixel's hue at full brightness.
            const auto shown = [](const Path& path, std::array<float, 3> rgb) {
                const auto most = std::max({rgb[0], rgb[1], rgb[2]});
                if (!path.fullHue || !(most > 0)) { return rgb; }
                return std::array<float, 3> {rgb[0] / most, rgb[1] / most, rgb[2] / most};
            };
            std::size_t segmentIndex = 0;
            for (const auto& path : paths) {
                if (cancelled(cancel)) { return {{}, "Raster preparation cancelled."}; }
                emit(path.vertices.front(), {0, 0, 0});
                if (path.vertices.size() == 1) {
                    const auto point = path.vertices.front();
                    const auto rgb = shown(path, colour(static_cast<int>(point.x), static_cast<int>(point.y)));
                    for (std::size_t i = 0; i <= path.dwell; ++i) { emit(point, rgb); }
                } else {
                    for (std::size_t v = 1; v < path.vertices.size(); ++v) {
                        const auto& segment = segments[segmentIndex++];
                        const auto a = path.vertices[v - 1], b = path.vertices[v];
                        const auto ux = (b.x - a.x) / segment.length, uy = (b.y - a.y) / segment.length;
                        const auto segmentColour = [&](double distance) {
                            const auto inset = path.contour ? 0.25 : 0.0;
                            distance = std::clamp(distance, inset, segment.length - inset);
                            return shown(path, colour(static_cast<int>(std::floor(a.x + ux * distance - uy * inset)),
                                static_cast<int>(std::floor(a.y + uy * distance + ux * inset))));
                        };
                        // Emit each corner once, using its outgoing foreground.
                        for (std::size_t i = 0; i <= segment.extra; ++i) {
                            const auto distance = segment.length * static_cast<double>(i) / static_cast<double>(segment.extra + 1);
                            emit({a.x + ux * distance, a.y + uy * distance}, segmentColour(distance));
                        }
                        if (v + 1 == path.vertices.size()) { emit(b, segmentColour(segment.length)); }
                    }
                }
                emit(path.vertices.back(), {0, 0, 0});
            }
            if (cancelled(cancel)) { return {{}, "Raster preparation cancelled."}; }
            return {std::move(output), {}};
        } catch (const std::bad_alloc&) {
            return {{}, "Not enough memory to prepare raster geometry."};
        }
    }

private:
    struct Vertex { double x, y; };
    struct Path { std::vector<Vertex> vertices; bool contour = false; std::size_t dwell = 0; bool fullHue = false; };
    static Result tooComplex() { return {{}, "Raster is too complex for this point budget. Increase samples, reduce resolution, or simplify the image."}; }
};
}

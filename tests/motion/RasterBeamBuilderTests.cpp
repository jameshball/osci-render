#include "../../Source/motion/import/RasterBeamBuilder.h"
#include <cstdlib>
#include <iostream>
#include <cstring>

namespace {
void check(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
bool dark(const motion::PointSample& p) { return p.r == 0 && p.g == 0 && p.b == 0; }
bool near(float a, float b) { return std::abs(a - b) < 1.0e-6f; }
struct Image {
    int width, height;
    std::vector<std::uint8_t> pixels;
    Image(int w, int h) : width(w), height(h), pixels(static_cast<std::size_t>(w) * h * 4) {}
    void set(int x, int y, std::array<std::uint8_t, 4> rgba = {255, 255, 255, 255}) {
        std::copy(rgba.begin(), rgba.end(), pixels.begin() + (static_cast<std::size_t>(y) * width + x) * 4);
    }
    motion::RasterBeamBuilder::Result build(motion::RasterBeamBuilder::Settings settings = {}, const std::atomic<bool>* cancel = nullptr) const {
        return motion::RasterBeamBuilder::build(pixels.data(), pixels.size(), width, height, settings, cancel);
    }
};
void verify(const motion::RasterBeamBuilder::Result& result, std::size_t size) {
    check(static_cast<bool>(result), result.error.c_str());
    check(result.points.size() == size, "exact output size");
    check(dark(result.points.front()) && dark(result.points.back()), "cyclic seam is fully dark");
    for (std::size_t i = 0; i < size; ++i) {
        const auto& p = result.points[i];
        const auto& q = result.points[(i + 1) % size];
        check(std::isfinite(p.x) && std::isfinite(p.y) && p.x >= -1 && p.x <= 1 && p.y >= -1 && p.y <= 1, "finite normalized geometry");
        check(p.r >= 0 && p.r <= 1 && p.g >= 0 && p.g <= 1 && p.b >= 0 && p.b <= 1, "finite source colour");
        // An on/off transition must be a stationary fade; only pairs of dark
        // samples may travel between separate illuminated paths.
        if (dark(p) != dark(q)) { check(near(p.x, q.x) && near(p.y, q.y), "blanking preserves XY and prevents bright travel"); }
    }
}
std::size_t pathCount(const motion::RasterBeamBuilder::Result& result) {
    std::size_t count = 0;
    for (std::size_t i = 1; i < result.points.size(); ++i) {
        if (dark(result.points[i - 1]) && !dark(result.points[i])) { ++count; }
    }
    return count;
}
}
int main() {
    using Builder = motion::RasterBeamBuilder;
    Builder::Settings settings;
    settings.pointsPerFrame = 64;
    Image rectangle(8, 4);
    for (int y = 1; y < 3; ++y) { for (int x = 2; x < 6; ++x) { rectangle.set(x, y, {255, 64, 32, 255}); } }
    auto outline = rectangle.build(settings);
    verify(outline, 64);
    check(pathCount(outline) == 1, "rectangle is one closed path");
    float maxX = 0, maxY = 0;
    for (const auto& p : outline.points) {
        maxX = std::max(maxX, std::abs(p.x)); maxY = std::max(maxY, std::abs(p.y));
        if (!dark(p)) { check(near(p.r, 1) && near(p.g, 64.0f / 255) && near(p.b, 32.0f / 255), "contour samples source RGB"); }
    }
    check(near(maxX, .5f) && near(maxY, .25f), "aspect preserved and centered");
    check(outline.points[1].y > 0, "image top maps to positive Y");
    auto repeated = rectangle.build(settings);
    check(std::memcmp(outline.points.data(), repeated.points.data(), 64 * sizeof(motion::PointSample)) == 0, "deterministic exact samples");

    Image ring(5, 5);
    for (int y = 0; y < 5; ++y) { for (int x = 0; x < 5; ++x) { if (x == 0 || y == 0 || x == 4 || y == 4) { ring.set(x, y); } } }
    auto rings = ring.build(settings);
    verify(rings, 64);
    check(pathCount(rings) == 2, "ring includes outer and hole contours");
    Image islands(5, 2);
    islands.set(0, 0); islands.set(4, 1);
    auto separate = islands.build(settings);
    verify(separate, 64);
    check(pathCount(separate) == 2, "disconnected islands stay separate");
    Image diagonal(2, 2);
    diagonal.set(0, 0); diagonal.set(1, 1);
    auto diagonalResult = diagonal.build(settings);
    verify(diagonalResult, 64);
    check(pathCount(diagonalResult) == 2, "diagonal contact does not join contours");

    Image alpha(2, 1);
    alpha.set(0, 0, {255, 255, 255, 0}); alpha.set(1, 0, {255, 255, 255, 128});
    auto alphaResult = alpha.build(settings);
    verify(alphaResult, 64);
    check(pathCount(alphaResult) == 1, "transparent pixel excluded");
    for (const auto& p : alphaResult.points) { if (!dark(p)) { check(near(p.r, 128.0f / 255), "alpha multiplies emitted RGB"); } }
    settings.threshold = .75;
    auto thresholded = alpha.build(settings);
    verify(thresholded, 64);
    check(pathCount(thresholded) == 0, "threshold uses alpha weighted luminance");
    settings.threshold = .1;
    Image black(2, 1);
    black.set(0, 0, {0, 0, 0, 255}); black.set(1, 0, {0, 0, 0, 0});
    settings.invert = true;
    auto inverted = black.build(settings);
    verify(inverted, 64);
    check(pathCount(inverted) == 1, "inverted black artwork emits light without reviving transparency");
    settings.invert = false;
    settings.threshold = 0;
    check(pathCount(black.build(settings)) == 0, "zero threshold does not light black");

    settings.mode = Builder::Mode::scanlines;
    settings.threshold = .1;
    Image scan(4, 2);
    for (int y = 0; y < 2; ++y) { for (int x = 0; x < 4; ++x) { scan.set(x, y, {static_cast<std::uint8_t>(64 + 40 * x), 180, 255, 255}); } }
    auto scanResult = scan.build(settings);
    verify(scanResult, 64);
    check(pathCount(scanResult) == 2, "scanline lit runs are separate paths");
    std::vector<float> startX;
    for (std::size_t i = 1; i < scanResult.points.size(); ++i) { if (dark(scanResult.points[i - 1]) && !dark(scanResult.points[i])) { startX.push_back(scanResult.points[i].x); } }
    check(startX[0] < 0 && startX[1] > 0, "scanline traversal is serpentine");
    check(scanResult.points[1].r < scanResult.points[20].r, "scanlines retain varying source colour");
    verify(islands.build(settings), 64);

    settings.mode = Builder::Mode::contours;
    settings.pointsPerFrame = 16;
    Image complex(5, 5);
    for (int y = 0; y < 5; y += 2) { for (int x = 0; x < 5; x += 2) { complex.set(x, y); } }
    check(!complex.build(settings), "insufficient corner and blanking budget fails without dropping paths");
    std::atomic<bool> cancel {true};
    check(!rectangle.build(settings, &cancel), "cancellation rejects work");
    settings.threshold = std::numeric_limits<double>::quiet_NaN();
    check(!rectangle.build(settings), "nonfinite threshold rejected");
    settings.threshold = .1;
    settings.pointsPerFrame = 15;
    check(!rectangle.build(settings), "point budget lower bound");
    settings.pointsPerFrame = 16385;
    check(!rectangle.build(settings), "point budget upper bound");
    check(!Builder::build(rectangle.pixels.data(), rectangle.pixels.size() - 1, 8, 4, {}), "truncated input rejected");
    check(!Builder::build(rectangle.pixels.data(), rectangle.pixels.size(), 513, 4, {}), "oversized dimensions rejected");

    // Exhaust all 3x3 masks: exercises holes, diagonal junctions, one-pixel
    // runs, boundary contours and closure while sanitizers check indexing.
    settings.pointsPerFrame = 128;
    for (unsigned bits = 0; bits < 512; ++bits) {
        Image tiny(3, 3);
        for (int i = 0; i < 9; ++i) { if ((bits & (1u << i)) != 0) { tiny.set(i % 3, i / 3); } }
        for (const auto mode : {Builder::Mode::contours, Builder::Mode::scanlines}) {
            settings.mode = mode;
            verify(tiny.build(settings), 128);
        }
    }
    Image maximum(512, 512);
    for (int y = 0; y < 512; ++y) { for (int x = 0; x < 512; ++x) { maximum.set(x, y); } }
    settings.pointsPerFrame = 16384;
    settings.mode = Builder::Mode::contours;
    verify(maximum.build(settings), 16384);
    settings.mode = Builder::Mode::scanlines;
    auto maximumScan = maximum.build(settings);
    verify(maximumScan, 16384);
    check(pathCount(maximumScan) == 512, "maximum raster retains every scanline");
    std::cout << "RasterBeamBuilder tests passed\n";
}

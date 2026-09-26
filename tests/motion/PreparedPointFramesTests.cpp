#include "../../Source/motion/model/PreparedPointFrames.h"
#include <cstdlib>
#include <iostream>

namespace {
void check(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
bool near(float a, float b, double tolerance = 1.0e-6) { return std::abs(static_cast<double>(a) - b) < tolerance; }
std::vector<motion::PointSample> fixture() {
    std::vector<motion::PointSample> result;
    for (int frame = 0; frame < 2; ++frame) {
        for (int index = 0; index < 16; ++index) {
            result.push_back({ static_cast<float>(frame * 100 + index), static_cast<float>(index * 2), -2,
                index / 15.0f, frame == 0 ? 0.25f : 0.75f, 0.5f });
        }
    }
    return result;
}
}

int main() {
    using namespace motion;
    auto points = fixture();
    const auto source = PreparedPointFrames::create(2, 2, 16, std::move(points));
    check(static_cast<bool>(source), "valid finite point frames prepare");
    check(source.error.empty() && source.source->frameCount() == 2 && source.source->pointsPerFrame() == 16
        && source.source->frameRate() == 2 && source.source->duration() == 1 && source.source->data().size() == 32, "metadata preserves exact stored frame layout");
    check(source.source->hasExplicitColour(), "explicit colour cached during preparation");
    const auto half = source.source->sample(0, 0.5 / 16);
    check(near(half.x, 0.5f) && near(half.y, 1) && near(half.z, -2)
        && near(half.r, 0.5f / 15) && near(half.g, 0.25f) && near(half.b, 0.5f), "adjacent samples interpolate every channel by phase density");
    check(near(source.source->sample(0, 15.5 / 16).x, 7.5f), "last point interpolates toward first within same frame");
    check(near(source.source->sample(0, -1).x, 0) && near(source.source->sample(0, 2).x, 0), "phase clamps at endpoints with final wrapped segment");
    check(near(source.source->sample(0.5, 0).x, 100) && near(source.source->sample(std::nextafter(0.5, 0.0), 0).x, 0), "frame selection changes exactly at frame boundary");
    check(near(source.source->sample(1, 0).x, 0) && near(source.source->sample(-0.25, 0).x, 100)
        && near(source.source->sample(-1, 0).x, 0), "source time loops and negative offsets wrap deterministically");
    for (const double time : { 0.75, -0.25, 12.75, 0.25, -20.75, 0.75 }) {
        const auto expected = source.source->frameIndex(time) == 0 ? 4.0f : 104.0f;
        check(near(source.source->sample(time, 0.25).x, expected), "independent seeks have no accumulated frame state");
    }
    check(source.source->frameIndex(std::numeric_limits<double>::max()) < 2, "extreme finite time stays bounded");
    const auto nan = std::numeric_limits<double>::quiet_NaN();
    check(source.source->sample(nan, 0).r == 0 && source.source->sample(0, nan).x == 0, "invalid runtime inputs return dark zero");

    auto sentinelPoints = std::vector<PointSample>(16, PointSample { 1, 2, 3, -1, -1, -1 });
    const auto sentinel = PreparedPointFrames::create(24, 1, 16, std::move(sentinelPoints));
    check(static_cast<bool>(sentinel) && !sentinel.source->hasExplicitColour() && sentinel.source->sample(3, 0.5).r == -1, "sentinel-only source retains inherited colour");
    auto mixedPoints = std::vector<PointSample>(16, PointSample { 1, 2, 3, -1, -1, -1 });
    mixedPoints[0] = { 0, 0, 0, 1, 0.5f, 0 };
    const auto mixed = PreparedPointFrames::create(24, 1, 16, std::move(mixedPoints));
    check(static_cast<bool>(mixed) && mixed.source->hasExplicitColour(), "mixed source records explicit colour");
    const auto boundary = mixed.source->sample(0, 0.5 / 16);
    check(boundary.r == -1 && boundary.g == -1 && boundary.b == -1 && near(boundary.x, 0.5f), "mixed colour segment keeps whole sentinel while interpolating geometry");
    check(mixed.source->sample(0, 0).r == 1, "exact explicit point keeps its own colour");

    check(!PreparedPointFrames::validate(240, 1, 16384).size(), "maximum frame rate and stride accepted when budget allows");
    for (const double rate : { 0.0, -1.0, 240.01, nan, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::denorm_min() }) {
        check(!PreparedPointFrames::validate(rate, 2, 16).empty(), "invalid rate or overflowing duration rejected before allocation");
    }
    check(!PreparedPointFrames::validate(24, 0, 16).empty()
        && !PreparedPointFrames::validate(24, 100001, 16).empty()
        && !PreparedPointFrames::validate(24, std::numeric_limits<std::uint64_t>::max(), 16).empty(), "empty and extreme frame counts rejected");
    check(!PreparedPointFrames::validate(24, 2, 15).empty()
        && !PreparedPointFrames::validate(24, 2, 16385).empty(), "samples-per-frame bounds enforced");
    check(!PreparedPointFrames::validate(24, 100000, 16384).empty(), "payload budget rejected before multiplication or allocation");
    const auto maxFramesAtStride = (PreparedPointFrames::maximumBytes / sizeof(PointSample)) / 16384;
    check(PreparedPointFrames::validate(24, maxFramesAtStride, 16384).empty()
        && !PreparedPointFrames::validate(24, maxFramesAtStride + 1, 16384).empty(), "exact payload boundary is checked with overflow-safe arithmetic");
    auto badCount = fixture(); badCount.pop_back();
    const auto countResult = PreparedPointFrames::create(2, 2, 16, std::move(badCount));
    check(!countResult && !countResult.error.empty() && badCount.size() == 31, "mismatched data rejects without consuming caller storage");
    for (int channel = 0; channel < 6; ++channel) {
        auto invalid = fixture();
        auto& point = invalid[0];
        switch (channel) {
            case 0: point.x = std::numeric_limits<float>::infinity(); break;
            case 1: point.y = std::numeric_limits<float>::quiet_NaN(); break;
            case 2: point.z = std::numeric_limits<float>::infinity(); break;
            case 3: point.r = -1; break;
            case 4: point.g = 1.01f; break;
            case 5: point.b = std::numeric_limits<float>::quiet_NaN(); break;
        }
        check(!PreparedPointFrames::create(2, 2, 16, std::move(invalid)), "invalid coordinate or colour rejects whole source");
    }
    auto extremes = std::vector<PointSample>(16);
    extremes[0].x = std::numeric_limits<float>::max();
    extremes[1].x = -std::numeric_limits<float>::max();
    const auto extremeSource = PreparedPointFrames::create(1, 1, 16, std::move(extremes));
    check(static_cast<bool>(extremeSource) && extremeSource.source->sample(0, 0.5 / 16).x == 0, "finite float extremes interpolate without arithmetic overflow");
    std::vector<PointSample> guarded(4096 * 2, {2, -3, 4, 0.2f, 0.4f, 0.6f});
    guarded[1023].r = guarded[1023].g = guarded[1023].b = 0;
    guarded[0].r = guarded[0].g = guarded[0].b = 0;
    const auto guardedSource = PreparedPointFrames::create(1, 2, 4096, std::move(guarded));
    check(static_cast<bool>(guardedSource), "dark-guard occupancy prepares");
    const auto isDark = [](const PointSample& point) { return point.r == 0 && point.g == 0 && point.b == 0; };
    const auto guardPhase = 1023.0 / 4096;
    for (const auto phase : {199.0 / 800, 200.0 / 800}) {
        const auto original = guardedSource.source->sample(0, phase);
        const auto blanked = guardedSource.source->sample(0, phase, 1.0 / 800);
        check(!isDark(original) && isDark(blanked), "nonaligned 4096-to-800 resampling blanks both sides of a skipped guard");
        check(original.x == blanked.x && original.y == blanked.y && original.z == blanked.z, "blanking preserves XYZ exactly");
    }
    check(isDark(guardedSource.source->sample(0, guardPhase, 1.0 / 800)), "exact guard remains blank");
    for (const auto phase : {0.0008, 0.9992}) {
        check(isDark(guardedSource.source->sampleFrame(0, phase, 1.0 / 800)), "dark guard expands through both sides of cyclic seam");
    }
    for (const auto phase : {0.75, 0.125, 0.75, 0.3}) {
        const auto plain = guardedSource.source->sampleFrame(0, phase);
        const auto expanded = guardedSource.source->sampleFrame(0, phase, 1.0 / 800);
        check(plain.r == expanded.r && plain.g == expanded.g && plain.b == expanded.b, "distant source colours unchanged regardless of sampling order");
    }
    check(isDark(guardedSource.source->sampleFrame(0, .75, .5))
        && isDark(guardedSource.source->sampleFrame(0, .75, std::numeric_limits<double>::max())), "half-cycle or larger span covers every guard without overflow");
    check(!isDark(guardedSource.source->sampleFrame(1, .75, 1)), "prefix query never leaks dark samples from another frame");
    check(!isDark(source.source->sampleFrame(0, .5, 1)), "no-dark source retains normal colour even for whole-cycle span");
    for (const auto span : {-1.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        const auto invalid = guardedSource.source->sampleFrame(1, .3, span);
        check(isDark(invalid) && invalid.x == 2 && invalid.y == -3 && invalid.z == 4, "invalid span is defensively dark with geometry retained");
    }
    auto sentinelGuards = std::vector<PointSample>(16, {1, 2, 3, -1, -1, -1});
    sentinelGuards[0] = {1, 2, 3, 0, 0, 0};
    const auto sentinelGuardSource = PreparedPointFrames::create(1, 1, 16, std::move(sentinelGuards));
    check(isDark(sentinelGuardSource.source->sampleFrame(0, .5, 1)), "whole-cycle guard blanks inherited colour before transform can relight it");
    check(sentinelGuardSource.source->sampleFrame(0, .5, .01).r == -1, "inherited colour survives when no dark guard is crossed");
    check(sentinelGuardSource.source->sampleFrame(0, .5 / 16).r == -1
        && isDark(sentinelGuardSource.source->sampleFrame(0, .5 / 16, .01)), "mixed sentinel-dark interpolation obeys travel blanking for nonzero span");
    // Even at only two output samples per cycle, never emit a bright chord
    // through a guard skipped by the output phase lattice.
    check(isDark(guardedSource.source->sampleFrame(0, .125, .5))
        && isDark(guardedSource.source->sampleFrame(0, .625, .5)), "extreme downsampling becomes dark rather than connecting disconnected paths");
    auto indexed = std::vector<PointSample>(32, {1, 2, 3, 1, 1, 1});
    for (const std::size_t index : {0u, 5u, 15u, 23u}) { indexed[index].r = indexed[index].g = indexed[index].b = 0; }
    const auto indexedSource = PreparedPointFrames::create(1, 2, 16, std::move(indexed));
    for (std::size_t frame = 0; frame < 2; ++frame) {
        for (int step = 0; step < 101; ++step) {
            const double phase = step / 101.0;
            for (const double span : {0.0001, 0.03, 0.125, 0.49}) {
                const int low = static_cast<int>(std::floor((phase - span) * 16));
                const int high = static_cast<int>(std::ceil((phase + span) * 16));
                bool containsDark = false;
                for (int index = low; index <= high; ++index) {
                    const auto wrapped = static_cast<std::size_t>((index + 32) % 16);
                    containsDark = containsDark || isDark(indexedSource.source->data()[frame * 16 + wrapped]);
                }
                check(isDark(indexedSource.source->sampleFrame(frame, phase, span)) == containsDark, "constant-time cyclic prefix query matches exhaustive interval oracle");
            }
        }
    }
    std::cout << "Prepared point frame contracts passed\n";
}

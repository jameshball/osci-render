#include "../../Source/motion/live/BlenderFrame.h"
#include <cstdlib>
#include <iostream>
#include <limits>

namespace {
void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

void tag(std::vector<std::uint8_t>& bytes, const char (&value)[9]) {
    bytes.insert(bytes.end(), value, value + 8);
}

void u64(std::vector<std::uint8_t>& bytes, std::uint64_t value) {
    for (int index = 0; index < 8; ++index) {
        bytes.push_back(static_cast<std::uint8_t>(value >> (index * 8)));
    }
}

void number(std::vector<std::uint8_t>& bytes, double value) {
    u64(bytes, std::bit_cast<std::uint64_t>(value));
}

void header(std::vector<std::uint8_t>& bytes, std::uint64_t frameCount = 1, std::uint64_t frameRate = 24) {
    tag(bytes, "GPLA    "); u64(bytes, 2); u64(bytes, 0); u64(bytes, 0);
    tag(bytes, "FILE    "); tag(bytes, "fCount  "); u64(bytes, frameCount); tag(bytes, "fRate   "); u64(bytes, frameRate); tag(bytes, "DONE    ");
    tag(bytes, "FRAME   "); tag(bytes, "focalLen"); number(bytes, -1); tag(bytes, "OBJECTS ");
}

void matrix(std::vector<std::uint8_t>& bytes, const std::array<double, 16>& values) {
    tag(bytes, "OBJECT  "); tag(bytes, "MATRIX  ");
    for (const auto value : values) { number(bytes, value); }
    tag(bytes, "DONE    "); tag(bytes, "STROKES ");
}

void stroke(std::vector<std::uint8_t>& bytes, const std::vector<std::array<double, 3>>& points) {
    tag(bytes, "STROKE  "); tag(bytes, "vertexCt"); u64(bytes, points.size()); tag(bytes, "VERTICES");
    for (const auto& point : points) {
        number(bytes, point[0]); number(bytes, point[1]); number(bytes, point[2]);
    }
    tag(bytes, "DONE    "); tag(bytes, "DONE    ");
}

void finishObject(std::vector<std::uint8_t>& bytes) { tag(bytes, "DONE    "); tag(bytes, "DONE    "); }
void finishFrame(std::vector<std::uint8_t>& bytes) { tag(bytes, "DONE    "); tag(bytes, "DONE    "); tag(bytes, "END GPLA"); }

std::array<double, 16> identity() {
    return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
}

std::vector<std::uint8_t> fixture() {
    std::vector<std::uint8_t> bytes;
    header(bytes);
    matrix(bytes, identity());
    stroke(bytes, {{0, 0, -2}, {1, 0, -2}, {1, 1, -2}});
    finishObject(bytes); finishFrame(bytes);
    return bytes;
}
}

int main() {
    const auto bytes = fixture();
    const auto decoded = motion::decodeBlenderFrame(bytes);
    check(decoded && decoded.frame->frameRate == 24 && decoded.frame->segments.size() == 2, "current add-on GPLA fixture decodes");
    const auto& first = decoded.frame->segments[0];
    check(first.x1 == 0 && first.y1 == 0 && first.x2 == .5 && first.y2 == 0, "row-major camera projection preserves stroke ordering");
    const auto& second = decoded.frame->segments[1];
    check(second.x1 == .5 && second.y1 == 0 && second.x2 == .5 && second.y2 == .5, "successive vertices form ordered segments");

    auto camera = fixture();
    camera.clear(); header(camera); matrix(camera, identity());
    stroke(camera, {{0, 0, -2}, {1, 0, -2}, {3, 0, 2}, {4, 0, 2}, {5, 0, -2}});
    finishObject(camera); finishFrame(camera);
    const auto projected = motion::decodeBlenderFrame(camera);
    check(projected && projected.frame->segments.size() == 1, "only segments with two front-of-camera endpoints are emitted");
    check(projected.frame->segments[0].x2 == .5, "behind-camera vertices do not reorder or bridge strokes");

    std::vector<std::uint8_t> blank;
    header(blank); finishFrame(blank);
    const auto empty = motion::decodeBlenderFrame(blank);
    check(empty && empty.frame->segments.empty(), "empty object section is a valid blank frame");

    std::vector<std::uint8_t> emptyStroke;
    header(emptyStroke); matrix(emptyStroke, identity()); stroke(emptyStroke, {}); finishObject(emptyStroke); finishFrame(emptyStroke);
    check(static_cast<bool>(motion::decodeBlenderFrame(emptyStroke)), "empty strokes are a valid blank frame");

    auto unaligned = bytes;
    unaligned.insert(unaligned.begin(), 0xff);
    const auto shifted = std::span<const std::uint8_t>(unaligned).subspan(1);
    check(static_cast<bool>(motion::decodeBlenderFrame(shifted)), "unaligned input span decodes without typed aliasing");

    auto malformed = bytes;
    malformed.pop_back();
    check(!motion::decodeBlenderFrame(malformed), "truncated frame rejects");
    malformed = bytes;
    malformed[8] = 3;
    check(!motion::decodeBlenderFrame(malformed), "unsupported GPLA version rejects");
    malformed = bytes;
    malformed.push_back(0);
    check(!motion::decodeBlenderFrame(malformed), "trailing bytes reject");
    check(static_cast<bool>(motion::decodeBlenderFrame(bytes)), "fixture remains valid before focal-length mutation");
    malformed = bytes;
    const auto focal = 96;
    const auto nan = std::bit_cast<std::uint64_t>(std::numeric_limits<double>::quiet_NaN());
    for (int index = 0; index < 8; ++index) { malformed[focal + static_cast<std::size_t>(index)] = static_cast<std::uint8_t>(nan >> (index * 8)); }
    check(!motion::decodeBlenderFrame(malformed), "non-finite focal length rejects");

    std::vector<std::uint8_t> badNesting;
    header(badNesting); matrix(badNesting, identity());
    tag(badNesting, "DONE    "); tag(badNesting, "DONE    "); tag(badNesting, "END GPLA");
    check(!motion::decodeBlenderFrame(badNesting), "incomplete nested stroke section rejects");

    std::vector<std::uint8_t> badCount;
    header(badCount, 2); finishFrame(badCount);
    check(!motion::decodeBlenderFrame(badCount), "multi-frame payload rejects");
    badCount.clear(); header(badCount, 1, 241); finishFrame(badCount);
    check(!motion::decodeBlenderFrame(badCount), "out-of-range frame rate rejects");

    std::vector<std::uint8_t> overVertices;
    header(overVertices); matrix(overVertices, identity());
    tag(overVertices, "STROKE  "); tag(overVertices, "vertexCt"); u64(overVertices, 65537); tag(overVertices, "VERTICES");
    check(!motion::decodeBlenderFrame(overVertices), "vertex limit rejects before reading payload");

    auto nonfiniteCoordinate = fixture();
    const auto firstCoordinate = 80 + 32 + 16 + 16 * 8 + 16 + 32;
    const auto infinity = std::bit_cast<std::uint64_t>(std::numeric_limits<double>::infinity());
    for (int index = 0; index < 8; ++index) { nonfiniteCoordinate[firstCoordinate + static_cast<std::size_t>(index)] = static_cast<std::uint8_t>(infinity >> (index * 8)); }
    check(!motion::decodeBlenderFrame(nonfiniteCoordinate), "non-finite coordinates reject");

    std::vector<std::uint8_t> transformedOutOfBounds;
    header(transformedOutOfBounds);
    auto translated = identity();
    translated[3] = 1.0e6;
    matrix(transformedOutOfBounds, translated);
    stroke(transformedOutOfBounds, {{1.0e6, 0, -2}});
    finishObject(transformedOutOfBounds); finishFrame(transformedOutOfBounds);
    check(!motion::decodeBlenderFrame(transformedOutOfBounds), "finite inputs with out-of-bounds transformed geometry reject");

    std::vector<std::uint8_t> projectedOutOfBounds;
    header(projectedOutOfBounds); matrix(projectedOutOfBounds, identity());
    stroke(projectedOutOfBounds, {{1, 0, -1.0e-9}});
    finishObject(projectedOutOfBounds); finishFrame(projectedOutOfBounds);
    check(!motion::decodeBlenderFrame(projectedOutOfBounds), "near-camera finite geometry with out-of-bounds projection rejects");

    std::vector<std::uint8_t> large(8 * 1024 * 1024 + 1, 0);
    check(!motion::decodeBlenderFrame(large), "decoded byte limit rejects");
    std::cout << "PASS: bounded Blender GPLA frame decoding\n";
}

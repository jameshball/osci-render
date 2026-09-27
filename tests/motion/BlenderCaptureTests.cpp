#include "../../Source/motion/live/BlenderCaptureArchive.h"
#include <bit>
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

bool near(double left, double right) { return std::abs(left - right) < 1.0e-12; }

bool sameSegments(const std::vector<motion::BlenderSegment>& left, const std::vector<motion::BlenderSegment>& right) {
    if (left.size() != right.size()) { return false; }
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (!near(left[index].x1, right[index].x1) || !near(left[index].y1, right[index].y1)
            || !near(left[index].x2, right[index].x2) || !near(left[index].y2, right[index].y2)) {
            return false;
        }
    }
    return true;
}

std::shared_ptr<const motion::BlenderFrame> frame(std::initializer_list<motion::BlenderSegment> segments) {
    auto result = std::make_shared<motion::BlenderFrame>();
    result->frameRate = 24;
    result->segments.assign(segments);
    return result;
}

void writeU64(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint64_t value) {
    for (int index = 0; index < 8; ++index) {
        bytes[offset + static_cast<std::size_t>(index)] = static_cast<std::uint8_t>(value >> (index * 8));
    }
}

void writeDouble(std::vector<std::uint8_t>& bytes, std::size_t offset, double value) {
    writeU64(bytes, offset, std::bit_cast<std::uint64_t>(value));
}
}

int main() {
    const auto first = frame({{-0.25, 0, 0.25, 0}, {0.25, 0, 0.25, 0.5}});
    const auto second = frame({{-0.75, -0.5, 0.75, 0.5}});
    const auto third = frame({{0.1, 0.2, 0.3, 0.4}});

    motion::BlenderCapture burst(true);
    burst.begin(1000, first);
    burst.append(1000.000125, second);
    burst.append(1000.001, third);
    burst.finish(1000.001125);
    const auto encodedBurst = motion::BlenderCaptureArchive::encode(burst);
    check(static_cast<bool>(encodedBurst), "submillisecond capture encodes");
    const auto decodedBurst = motion::BlenderCaptureArchive::decode(encodedBurst.bytes);
    check(decodedBurst && decodedBurst.frames.size() == 3, "submillisecond capture decodes every accepted frame");
    check(near(decodedBurst.timing->frameEnd(0), .000125) && near(decodedBurst.timing->frameEnd(1), .001)
        && near(decodedBurst.timing->duration(), .001125), "submillisecond receive boundaries and final hold survive archive round-trip");
    const auto firstEnd = decodedBurst.timing->frameEnd(0);
    const auto secondEnd = decodedBurst.timing->frameEnd(1);
    check(decodedBurst.timing->frameIndex(std::nextafter(firstEnd, 0.0)) == 0 && decodedBurst.timing->frameIndex(firstEnd) == 1
        && decodedBurst.timing->frameIndex(std::nextafter(secondEnd, 0.0)) == 1 && decodedBurst.timing->frameIndex(secondEnd) == 2,
        "precise captured timing has half-open frame ownership");
    check(sameSegments(decodedBurst.frames[0]->segments, first->segments) && sameSegments(decodedBurst.frames[1]->segments, second->segments),
        "capture preserves line-segment order and exact endpoint values");

    motion::BlenderCapture collision(true);
    collision.begin(0, first);
    collision.append(0, second);
    collision.finish(.25);
    const auto encodedCollision = motion::BlenderCaptureArchive::encode(collision);
    const auto decodedCollision = motion::BlenderCaptureArchive::decode(encodedCollision.bytes);
    check(decodedCollision && decodedCollision.frames.size() == 1 && sameSegments(decodedCollision.frames.front()->segments, second->segments),
        "zero-duration timestamp collisions collapse to the later geometry");
    check(near(decodedCollision.timing->duration(), .25), "collision collapse retains final hold duration");

    motion::BlenderCapture blank(true);
    blank.begin(0, nullptr);
    blank.append(.1, first);
    blank.finish(.3);
    const auto encodedBlank = motion::BlenderCaptureArchive::encode(blank);
    const auto decodedBlank = motion::BlenderCaptureArchive::decode(encodedBlank.bytes);
    check(decodedBlank && decodedBlank.frames.size() == 2 && decodedBlank.frames[0]->segments.empty()
        && sameSegments(decodedBlank.frames[1]->segments, first->segments), "blank intervals remain explicit captured frames");
    check(near(decodedBlank.timing->frameEnd(0), .1) && near(decodedBlank.timing->frameEnd(1), .3), "blank interval timing remains source-local");

    auto truncated = encodedBurst.bytes;
    truncated.pop_back();
    check(!motion::BlenderCaptureArchive::decode(truncated), "truncated archive rejects without partial capture");
    auto trailing = encodedBurst.bytes;
    trailing.push_back(0);
    check(!motion::BlenderCaptureArchive::decode(trailing), "trailing archive bytes reject");
    auto malformed = encodedBurst.bytes;
    malformed[0] = 'X';
    check(!motion::BlenderCaptureArchive::decode(malformed), "archive magic rejects");
    malformed = encodedBurst.bytes;
    // Header is 16 bytes; first frame has 2 segments (80 bytes), so this is the second cumulative end.
    writeDouble(malformed, 16 + 16 + 2 * 32, .000125);
    check(!motion::BlenderCaptureArchive::decode(malformed), "non-increasing archived cumulative ends reject");
    malformed = encodedBurst.bytes;
    writeDouble(malformed, 16, std::numeric_limits<double>::quiet_NaN());
    check(!motion::BlenderCaptureArchive::decode(malformed), "non-finite archived timing rejects");

    std::atomic<bool> cancelled {true};
    check(!motion::BlenderCaptureArchive::encode(burst, &cancelled), "cancelled archive encode rejects");
    check(!motion::BlenderCaptureArchive::decode(encodedBurst.bytes, &cancelled), "cancelled archive decode rejects");

    motion::BlenderCapture clock(true);
    clock.begin(1, first);
    clock.append(.5, second);
    check(clock.failure == motion::BlenderCapture::Failure::clock && !motion::BlenderCaptureArchive::encode(clock),
        "backwards capture timestamps reject atomically");
    motion::BlenderCapture duration(true);
    duration.begin(0, first);
    duration.append(motion::BlenderCapture::maximumSeconds + .001, second);
    check(duration.failure == motion::BlenderCapture::Failure::duration && !motion::BlenderCaptureArchive::encode(duration),
        "ten-minute capture limit rejects atomically");
    motion::BlenderCapture updateLimit(true);
    updateLimit.begin(0, first);
    for (std::size_t index = 1; index <= motion::BlenderCapture::maximumFrames; ++index) {
        updateLimit.append(static_cast<double>(index) * .01, index % 2 == 0 ? second : third);
    }
    check(updateLimit.failure == motion::BlenderCapture::Failure::frames && !motion::BlenderCaptureArchive::encode(updateLimit),
        "frame-count limit rejects before archive publication");

    std::vector<std::uint8_t> oversized(motion::BlenderCaptureArchive::maximumBytes + 1, 0);
    check(!motion::BlenderCaptureArchive::decode(oversized), "archive byte limit rejects before parsing");
    std::cout << "PASS: Blender capture archive contracts\n";
}

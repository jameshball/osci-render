#include "../../Source/motion/model/PointFrameCache.h"
#include <cstdlib>
#include <iostream>

namespace {
void check(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
void put(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint64_t value, int length) {
    for (int i = 0; i < length; ++i) { bytes[offset + i] = static_cast<std::uint8_t>(value >> (8 * i)); }
}
}
int main() {
    using namespace motion;
    std::vector<PointSample> points(32, PointSample { 1, -2, 3, -1, -1, -1 });
    points[1] = { -0.0f, std::numeric_limits<float>::denorm_min(), std::numeric_limits<float>::max(), 0, 0.5f, 1 };
    const auto source = PreparedPointFrames::create(29.97, 2, 16, std::move(points));
    check(static_cast<bool>(source), "fixture prepares");
    const auto encoded = PointFrameCache::encode(*source.source);
    check(static_cast<bool>(encoded) && encoded.bytes.size() == 32 + 32 * 24, "encoded size matches fixed header and payload");
    check(encoded.bytes[0] == 'O' && encoded.bytes[7] == 'M' && encoded.bytes[8] == 1
        && encoded.bytes[9] == 0 && encoded.bytes[20] == 2 && encoded.bytes[28] == 16, "header uses specified magic and little-endian integers");
    check(encoded.bytes[32] == 0 && encoded.bytes[33] == 0 && encoded.bytes[34] == 0x80 && encoded.bytes[35] == 0x3f,
        "XYZ payload encodes IEEE 1.0 in little endian");
    const auto decoded = PointFrameCache::decode(encoded.bytes.data(), encoded.bytes.size());
    check(static_cast<bool>(decoded) && decoded.source->frameCount() == 2 && decoded.source->pointsPerFrame() == 16
        && decoded.source->frameRate() == 29.97 && decoded.source->hasExplicitColour(), "metadata and colour survive decode");
    const auto reencoded = PointFrameCache::encode(*decoded.source);
    check(reencoded.bytes == encoded.bytes, "roundtrip preserves all float bits including negative zero, subnormal, extrema and sentinel");
    check(PointFrameCache::encode(*source.source).bytes == encoded.bytes, "repeated encoding is deterministic");
    for (std::size_t size = 0; size < encoded.bytes.size(); ++size) {
        check(!PointFrameCache::decode(encoded.bytes.data(), size), "every truncation rejects atomically");
    }
    check(!PointFrameCache::decode(nullptr, encoded.bytes.size()), "null input rejects");
    check(!PointFrameCache::decode(encoded.bytes.data(), PointFrameCache::maximumEncodedBytes + 1), "oversized input rejects before buffer access");
    auto trailing = encoded.bytes; trailing.push_back(0);
    check(!PointFrameCache::decode(trailing.data(), trailing.size()), "trailing byte rejects");
    auto badMagic = encoded.bytes; badMagic[0] = 0;
    check(!PointFrameCache::decode(badMagic.data(), badMagic.size()), "bad signature rejects");
    for (const std::uint32_t version : { 0U, 2U, 0xffffffffU }) {
        auto bytes = encoded.bytes; put(bytes, 8, version, 4);
        check(!PointFrameCache::decode(bytes.data(), bytes.size()), "unsupported version rejects");
    }
    for (const double rate : { 0.0, -1.0, 241.0, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity() }) {
        auto bytes = encoded.bytes; put(bytes, 12, std::bit_cast<std::uint64_t>(rate), 8);
        check(!PointFrameCache::decode(bytes.data(), bytes.size()), "invalid frame rate rejects before allocating");
    }
    for (const std::uint64_t frames : { 0ULL, 3ULL, 100000ULL, 0xffffffffffffffffULL }) {
        auto bytes = encoded.bytes; put(bytes, 20, frames, 8);
        check(!PointFrameCache::decode(bytes.data(), bytes.size()), "invalid or inconsistent frame counts reject");
    }
    for (const std::uint32_t stride : { 0U, 15U, 17U, 16385U, 0xffffffffU }) {
        auto bytes = encoded.bytes; put(bytes, 28, stride, 4);
        check(!PointFrameCache::decode(bytes.data(), bytes.size()), "invalid or inconsistent sample strides reject");
    }
    auto overBudget = encoded.bytes; put(overBudget, 20, 100000, 8); put(overBudget, 28, 16384, 4);
    check(!PointFrameCache::decode(overBudget.data(), overBudget.size()), "metadata payload overflow budget rejects before allocation");
    for (int channel = 0; channel < 6; ++channel) {
        auto bytes = encoded.bytes; put(bytes, 32 + channel * 4, 0x7fc00000U, 4);
        check(!PointFrameCache::decode(bytes.data(), bytes.size()), "nonfinite payload rejects all six channels");
    }
    auto mixedSentinel = encoded.bytes; put(mixedSentinel, 32 + 12, std::bit_cast<std::uint32_t>(0.5f), 4);
    check(!PointFrameCache::decode(mixedSentinel.data(), mixedSentinel.size()), "partial sentinel RGB rejects");
    auto unaligned = std::vector<std::uint8_t>(encoded.bytes.size() + 1);
    std::copy(encoded.bytes.begin(), encoded.bytes.end(), unaligned.begin() + 1);
    check(static_cast<bool>(PointFrameCache::decode(unaligned.data() + 1, encoded.bytes.size())), "unaligned input decodes portably");
    std::cout << "Point frame cache contracts passed\n";
}

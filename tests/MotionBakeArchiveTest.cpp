#include <JuceHeader.h>
#include "../Source/motion/import/BakedSourceArchive.h"

class MotionBakeArchiveTest : public juce::UnitTest {
public:
    MotionBakeArchiveTest() : juce::UnitTest("Motion baked source archive", "MotionBake") {}
    void runTest() override {
        using namespace motion;
        beginTest("Gzip archive roundtrip preserves point cache exactly");
        std::vector<PointSample> points(32, PointSample { 1, -2, 3, -1, -1, -1 });
        points[4] = { -0.0f, 2, 1, 0, 0.5f, 1 };
        const auto source = PreparedPointFrames::create(24, 2, 16, std::move(points));
        expect(static_cast<bool>(source));
        if (!source) { return; }
        const auto encoded = BakedSourceArchive::encode(*source.source);
        expect(static_cast<bool>(encoded), encoded.error);
        if (!encoded) { return; }
        const auto decoded = BakedSourceArchive::decode(encoded.data);
        expect(static_cast<bool>(decoded), juce::String(decoded.error));
        if (decoded) {
            expect(PointFrameCache::encode(*decoded.source).bytes == PointFrameCache::encode(*source.source).bytes);
        }
        beginTest("Every gzip truncation and malformed stream is rejected");
        for (std::size_t size = 0; size < encoded.data.getSize(); ++size) {
            expect(!BakedSourceArchive::decode(juce::MemoryBlock(encoded.data.getData(), size)), "Accepted truncated stream at " + juce::String(static_cast<int>(size)));
        }
        auto trailing = encoded.data;
        const std::uint8_t zero = 0;
        trailing.append(&zero, 1);
        expect(!BakedSourceArchive::decode(trailing), "Trailing compressed byte must reject");
        auto concatenated = encoded.data;
        concatenated.append(encoded.data.getData(), encoded.data.getSize());
        expect(!BakedSourceArchive::decode(concatenated), "Concatenated gzip member must reject");
        auto corrupt = encoded.data;
        static_cast<std::uint8_t*>(corrupt.getData())[corrupt.getSize() - 8] ^= 1;
        expect(!BakedSourceArchive::decode(corrupt), "CRC mismatch must reject");
        corrupt = encoded.data;
        static_cast<std::uint8_t*>(corrupt.getData())[corrupt.getSize() - 4] ^= 1;
        expect(!BakedSourceArchive::decode(corrupt), "ISIZE mismatch must reject");
        corrupt = encoded.data;
        static_cast<std::uint8_t*>(corrupt.getData())[0] ^= 1;
        expect(!BakedSourceArchive::decode(corrupt), "Bad gzip signature must reject");
        beginTest("Decoded trailing bytes and invalid cache metadata reject");
        auto raw = PointFrameCache::encode(*source.source).bytes;
        raw.push_back(0);
        expect(!BakedSourceArchive::decode(compress(raw)), "Extra decoded payload must reject");
        raw.pop_back();
        raw[28] = 0; raw[29] = 0;
        expect(!BakedSourceArchive::decode(compress(raw)), "Invalid stride rejected in header preflight");
        raw = PointFrameCache::encode(*source.source).bytes;
        // 100000 frames * 16384 samples exceeds the decoded budget.
        raw[20] = 0xa0; raw[21] = 0x86; raw[22] = 1;
        raw[28] = 0; raw[29] = 0x40;
        raw.resize(PointFrameCache::headerBytes);
        expect(!BakedSourceArchive::decode(compress(raw)), "Oversized metadata rejected before payload allocation");
    }
private:
    static juce::MemoryBlock compress(const std::vector<std::uint8_t>& raw) {
        juce::MemoryOutputStream output;
        {
            juce::GZIPCompressorOutputStream compressor(output, 6, 31);
            compressor.write(raw.data(), raw.size());
            compressor.flush();
        }
        return output.getMemoryBlock();
    }
};
static MotionBakeArchiveTest motionBakeArchiveTest;

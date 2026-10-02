#include <JuceHeader.h>
#include <osci_file_import/img/osci_RasterDecoder.h>

namespace motion_raster_test {
using Bytes = std::vector<std::uint8_t>;
static void word(Bytes& bytes, unsigned value) { bytes.push_back(static_cast<std::uint8_t>(value)); bytes.push_back(static_cast<std::uint8_t>(value >> 8)); }
static Bytes gif(unsigned width, unsigned height) {
    Bytes bytes { 'G', 'I', 'F', '8', '9', 'a' };
    word(bytes, width); word(bytes, height);
    bytes.insert(bytes.end(), { 0x81, 0, 0, 0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255 });
    return bytes;
}
static void frame(Bytes& bytes, unsigned x, unsigned y, unsigned width, unsigned height, const Bytes& indices,
                  unsigned delay = 1, unsigned disposal = 1, int transparent = -1, bool interlaced = false, bool localPalette = false) {
    bytes.insert(bytes.end(), { 0x21, 0xf9, 4, static_cast<std::uint8_t>((disposal << 2) | (transparent >= 0 ? 1 : 0)) });
    word(bytes, delay); bytes.push_back(static_cast<std::uint8_t>(std::max(0, transparent))); bytes.push_back(0);
    bytes.push_back(0x2c); word(bytes, x); word(bytes, y); word(bytes, width); word(bytes, height);
    bytes.push_back(static_cast<std::uint8_t>((interlaced ? 0x40 : 0) | (localPalette ? 0x81 : 0)));
    if (localPalette) { bytes.insert(bytes.end(), { 0, 0, 0, 0, 0, 255, 0, 255, 0, 255, 0, 0 }); }
    bytes.push_back(2);
    Bytes raster;
    unsigned accumulator = 0, bits = 0;
    const auto code = [&](unsigned value) {
        accumulator |= value << bits; bits += 3;
        while (bits >= 8) { raster.push_back(static_cast<std::uint8_t>(accumulator)); accumulator >>= 8; bits -= 8; }
    };
    for (const auto index : indices) { code(4); code(index); }
    code(5);
    if (bits != 0) { raster.push_back(static_cast<std::uint8_t>(accumulator)); }
    for (std::size_t offset = 0; offset < raster.size();) {
        const auto count = std::min<std::size_t>(255, raster.size() - offset);
        bytes.push_back(static_cast<std::uint8_t>(count));
        bytes.insert(bytes.end(), raster.begin() + static_cast<std::ptrdiff_t>(offset), raster.begin() + static_cast<std::ptrdiff_t>(offset + count)); offset += count;
    }
    bytes.push_back(0);
}
static osci::RasterDecoder::Result decode(Bytes bytes) { bytes.push_back(0x3b); return osci::RasterDecoder::decode(bytes.data(), bytes.size()); }
// Independent reference for this test's generated 2x2 RGBA8 PNG: inflate IDAT
// through JUCE/zlib and reverse the first scanline's PNG filter. This inspects
// the writer's straight-alpha bytes without using our stb-backed decoder.
static std::optional<std::array<std::uint8_t, 8>> pngFirstRow(const void* source, std::size_t size) {
    const auto* data = static_cast<const std::uint8_t*>(source);
    juce::MemoryOutputStream compressed;
    for (std::size_t position = 8; position + 12 <= size;) {
        const auto count = (static_cast<std::uint32_t>(data[position]) << 24) | (static_cast<std::uint32_t>(data[position + 1]) << 16)
            | (static_cast<std::uint32_t>(data[position + 2]) << 8) | data[position + 3];
        if (count > size - position - 12) { return std::nullopt; }
        if (std::memcmp(data + position + 4, "IDAT", 4) == 0) { compressed.write(data + position + 8, count); }
        position += 12 + count;
    }
    juce::MemoryInputStream input(compressed.getData(), compressed.getDataSize(), false);
    juce::GZIPDecompressorInputStream inflater(input);
    std::array<std::uint8_t, 9> filtered {};
    if (inflater.read(filtered.data(), static_cast<int>(filtered.size())) != static_cast<int>(filtered.size()) || filtered[0] > 4) { return std::nullopt; }
    std::array<std::uint8_t, 8> row {};
    for (std::size_t index = 0; index < row.size(); ++index) {
        const auto left = index >= 4 ? row[index - 4] : 0;
        // First row has zero up/up-left, so Paeth reduces to the left byte.
        const auto prediction = filtered[0] == 1 || filtered[0] == 4 ? left : filtered[0] == 3 ? left / 2 : 0;
        row[index] = static_cast<std::uint8_t>(filtered[index + 1] + prediction);
    }
    return row;
}
static juce::Colour pixel(const osci::RasterImage& image, std::size_t frame, unsigned x, unsigned y = 0) {
    const auto offset = (static_cast<std::size_t>(y) * image.width + x) * 4;
    const auto* p = image.frames[frame].rgba.data() + offset;
    return juce::Colour(p[0], p[1], p[2], p[3]);
}
}
class MotionRasterDecoderTest : public juce::UnitTest {
public:
    MotionRasterDecoderTest() : juce::UnitTest("Motion bounded raster decoder", "MotionRaster") {}
    // __FILE__ is absolute from Xcode but relative to the Makefile directory on
    // Linux, so also search upwards from the working directory and the binary.
    static juce::File fixtures() {
        const auto source = juce::File::getCurrentWorkingDirectory().getChildFile(juce::String(__FILE__)).getParentDirectory().getChildFile("fixtures/motion");
        if (source.isDirectory()) { return source; }
        for (auto start : {juce::File::getCurrentWorkingDirectory(), juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory()}) {
            for (int depth = 0; depth < 8 && start.exists(); ++depth) {
                const auto candidate = start.getChildFile("tests/fixtures/motion");
                if (candidate.isDirectory()) { return candidate; }
                start = start.getParentDirectory();
            }
        }
        return source;
    }

    void runTest() override {
        using namespace motion_raster_test;
        beginTest("PNG preserves straight RGBA and JPEG decodes opaque colour");
        juce::Image input(juce::Image::ARGB, 2, 2, true);
        input.setPixelAt(0, 0, juce::Colours::red);
        input.setPixelAt(1, 0, juce::Colour(static_cast<juce::uint8>(0), static_cast<juce::uint8>(255), static_cast<juce::uint8>(0), static_cast<juce::uint8>(128)));
        input.setPixelAt(0, 1, juce::Colours::blue);
        juce::MemoryOutputStream png;
        juce::PNGImageFormat pngFormat;
        expect(pngFormat.writeImageToStream(input, png));
        const auto decodedPng = osci::RasterDecoder::decode(png.getData(), png.getDataSize());
        expect(static_cast<bool>(decodedPng), juce::String(decodedPng.error));
        if (decodedPng) {
            expectEquals(static_cast<int>(decodedPng.image->width), 2);
            expectEquals(static_cast<int>(decodedPng.image->frames.size()), 1);
            expect(pixel(*decodedPng.image, 0, 0) == juce::Colours::red);
            expectEquals(static_cast<int>(pixel(*decodedPng.image, 0, 1).getAlpha()), 128);
            const auto reference = pngFirstRow(png.getData(), png.getDataSize());
            expect(reference.has_value(), "Could not inspect the generated PNG IDAT scanline.");
            if (reference.has_value()) {
                // JUCE stores premultiplied G=127 at A=128, then writes straight
                // G=(127*255)/128=253. This rounding is in the test fixture writer.
                expectEquals(static_cast<int>((*reference)[5]), 253);
                expectEquals(static_cast<int>(pixel(*decodedPng.image, 0, 1).getGreen()), static_cast<int>((*reference)[5]));
                logMessage("Reference PNG IDAT green=" + juce::String((*reference)[5]) + ", alpha=" + juce::String((*reference)[7]));
            }
        }
        juce::MemoryOutputStream jpeg;
        juce::JPEGImageFormat jpegFormat;
        juce::Image solid(juce::Image::RGB, 8, 8, true); solid.clear(solid.getBounds(), juce::Colours::red);
        expect(jpegFormat.writeImageToStream(solid, jpeg));
        const auto decodedJpeg = osci::RasterDecoder::decode(jpeg.getData(), jpeg.getDataSize());
        expect(static_cast<bool>(decodedJpeg), juce::String(decodedJpeg.error));
        if (decodedJpeg) { expect(pixel(*decodedJpeg.image, 0, 0).getRed() > 245); expectEquals(static_cast<int>(pixel(*decodedJpeg.image, 0, 0).getAlpha()), 255); }

        beginTest("GIF disposal previous/background, local palette, transparency and variable delays");
        auto animation = gif(3, 1);
        frame(animation, 0, 0, 3, 1, { 1, 1, 1 }, 2);
        frame(animation, 0, 0, 1, 1, { 2 }, 7, 3);
        frame(animation, 1, 0, 1, 1, { 1 }, 13, 2, -1, false, true);
        frame(animation, 2, 0, 1, 1, { 0 }, 0, 1, 0);
        const auto result = decode(animation);
        expect(static_cast<bool>(result), juce::String(result.error));
        if (result) {
            expectEquals(static_cast<int>(result.image->frames.size()), 4);
            expectEquals(static_cast<int>(pixel(*result.image, 1, 0).getGreen()), 255);
            expect(pixel(*result.image, 2, 0) == juce::Colours::red);
            expect(pixel(*result.image, 2, 1) == juce::Colours::blue);
            expect(pixel(*result.image, 3, 1) == juce::Colours::black);
            expect(pixel(*result.image, 3, 2) == juce::Colours::red);
            expectEquals(static_cast<int>(result.image->frames[0].delayMilliseconds), 20);
            expectEquals(static_cast<int>(result.image->frames[1].delayMilliseconds), 70);
            expectEquals(static_cast<int>(result.image->frames[2].delayMilliseconds), 130);
            expectEquals(static_cast<int>(result.image->frames[3].delayMilliseconds), 0);
        }
        auto transparent = gif(2, 1); frame(transparent, 0, 0, 2, 1, { 1, 0 }, 1, 2, 0); frame(transparent, 1, 0, 1, 1, { 2 });
        const auto alpha = decode(transparent);
        expect(static_cast<bool>(alpha), juce::String(alpha.error));
        if (alpha) { expectEquals(static_cast<int>(pixel(*alpha.image, 0, 1).getAlpha()), 0); expectEquals(static_cast<int>(pixel(*alpha.image, 1, 0).getAlpha()), 0); }
        auto interlace = gif(1, 4); frame(interlace, 0, 0, 1, 4, { 1, 3, 2, 0 }, 1, 1, -1, true);
        const auto rows = decode(interlace);
        expect(static_cast<bool>(rows), juce::String(rows.error));
        if (rows) { expect(pixel(*rows.image, 0, 0, 0) == juce::Colours::red); expectEquals(static_cast<int>(pixel(*rows.image, 0, 0, 1).getGreen()), 255); expect(pixel(*rows.image, 0, 0, 2) == juce::Colours::blue); }

        beginTest("Independent GIF fixture exercises LZW dictionary growth");
        // Generated with Pillow from a deterministic 32-colour LCG raster.
        juce::MemoryOutputStream dictionaryGif;
        expect(juce::Base64::convertFromBase64(dictionaryGif,
            "R0lGODdhIAAgAIcAAAAAAAEHDQIOGgMVJwQcNAUjQQYqTgcxWwg4aAk/dQpGggtNjwxUnA1bqQ5itg9pwxBw0BF33RJ+6hOF9xSM"
            "BBWTERaaHhehKxioOBmvRRq2Uhu9XxzEbB3LeR7Shh/ZkyDgoCHnrSLuuiP1xyT81CUD4SYK7icR+ygYCCkfFSomIistLyw0PC07"
            "SS5CVi9JYzBQcDFXfTJeijNllzRspDVzsTZ6vjeByziI2DmP5TqW8jud/zykDD2rGT6yJj+5M0DAQEHHTULOWkPVZ0TcdEXjgUbq"
            "jkfxm0j4qEn/tUoGwksNz0wU3E0b6U4i9k8pA1AwEFE3HVI+KlNFN1RMRFVTUVZaXldha1hoeFlvhVp2klt9n1yErF2LuV6Sxl+Z"
            "02Cg4GGn7WKu+mO1B2S8FGXDIWbKLmfRO2jYSGnfVWrmYmvtb2z0fG37iW4Clm8Jo3AQsHEXvXIeynMl13Qs5HUz8XY6/ndBC3hI"
            "GHlPJXpWMntdP3xkTH1rWX5yZn95c4CAgIGHjYKOmoOVp4SctIWjwYaqzoex24i46Im/9YrGAovND4zUHI3bKY7iNo/pQ5DwUJH3"
            "XZL+apMFd5QMhJUTkZYanpchq5gouJkvxZo20ps935xE7J1L+Z5SBp9ZE6BgIKFnLaJuOqN1R6R8VKWDYaaKbqeRe6iYiKmflaqm"
            "oqutr6y0vK27ya7C1q/J47DQ8LHX/bLeCrPlF7TsJLXzMbb6PrcBS7gIWLkPZboWcrsdf7wkjL0rmb4ypr85s8BAwMFHzcJO2sNV"
            "58Rc9MVjAcZqDsdxG8h4KMl/NcqGQsuNT8yUXM2bac6ids+pg9CwkNG3ndK+qtPFt9TMxNXT0dba3tfh69jo+NnvBdr2Etv9H9wE"
            "LN0LOd4SRt8ZU+AgYOEnbeIueuM1h+Q8lOVDoeZKrudRu+hYyOlf1epm4utt7+x0/O17Ce6CFu+JI/CQMPGXPfKeSvOlV/SsZPWz"
            "cfa6fvfBi/jImPnPpfrWsvvdv/zkzP3r2f7y5v/58ywAAAAAIAAgAEAI/wA9SKjAwEKFBRYOOBAQAAOEARAeRKiAoQCHBRcEGGBw"
            "AAOACggyIBjwAMMBDRskOACgIcEEBQw0BGiggcKFCBsUUKgAgECDCwg0ZNDw4ECEDhQGIHBAQIKFCRs+bFgQgcICDB0gNFhAYAEH"
            "BAIgTGDAIQKBARIaNMgaQMEDtxACOKiQ4EEDhgAEeOiAwQCGBwAOHEAQ4UEBDBwISL0QwEOCDBsAFDVwAcKCBxuwOmAAAMOFChQA"
            "MMAwAQEFARss+MxZoEEFpU0DMNhAoEOCBkmvBlhgUWuHAQU8BOjQ1sGErw8sGGiwwUOGCQYWZIBQwYMH0R9YPqBwIPAHBQoGDP+o"
            "sJGD9QV0J3K4MGDChwQAAjzgkMErdw821yL40EGqBQsFaIAABgo0YIEGDnwgAQEJOHBAACg18B0HEojkAAcdHLCASwMYcIAHF2gg"
            "wAEQFDBgAxNEINZ4A2hggIIHTFBSBwwUwF4HZwnQgAcEKLCBgYFlAIACHjiwY2RjCSASBbdtcMEBAwCQFQMKOLDAABhIMEFXEFiw"
            "wUGUvUQABwd00ECLEEBgAAQZGElBgqOxJ8FVFPCVZgEPSDDABg8sEMBtAQBgZAIPbUCcWwB8YIAGEwxggQMB+PTBBQpcsAEEkDqw"
            "gV4RZBdBfVFloGAAE0zQAGEPMNCpAgLoyQEADWD/EEEABjHwwQO38iaoaxsckIFfC2yQAAIGCMBBixNQsAGTGGDJgV4QdODAQAe4"
            "JkAEChBAV4gN2hQVggAUEKihHjCQQZ+zubXZXl8y8AACISVQwQdaIQCABwUw6MGkEUigAQRkddBBBBggUCYEAjXrFmoApClwAgZ0"
            "EFYHF0gcgQAdaADAcxR4QNJNDiiggQcgNRABA7JNYIEA+W5HQGhQnXQAAwzIaEFQF9w2QV8KDltaBwXQ7EFkHmMAmo8YMNBtAgI8"
            "kMACBgww1E8IPLBSc7Z94EEECDBWbQUbMODBAh1E1YAEQZt5QAXpFZCBQziBNUDGBDBAwQMBQFnBWBI4vGaATkc22qMCGGBwnagT"
            "UMqgTTFWkIFOAgTFV9IXAYCeARR8MIF9LgmgKAYZcBDt2Oi9qVoAG3DAAIPDRUeAAw5o0MABFChAZmkRoPgyAwMs0DcABnBA0AMT"
            "wDq0WBstQKIHEiFw6mARd51nAwnwuHnxCvxGAQROaVBAVNd1cK4CCAwNutL3Yn5BjfMlIK5L9VVwAWAN1D1A7NWFpOGsFDQ1QAT4"
            "qtaxGKKxhwhgAFIpQFgM8Lh9KYkBCwgIADs="
        ));
        const auto dictionary = osci::RasterDecoder::decode(dictionaryGif.getData(), dictionaryGif.getDataSize());
        expect(static_cast<bool>(dictionary), juce::String(dictionary.error));
        if (dictionary) {
            std::uint32_t state = 17;
            for (unsigned index = 0; index < 1024; ++index) {
                state = state * 1664525U + 1013904223U;
                const auto value = (state >> 24) & 31;
                const auto colour = pixel(*dictionary.image, 0, index % 32, index / 32);
                expectEquals(static_cast<int>(colour.getRed()), static_cast<int>(value));
                expectEquals(static_cast<int>(colour.getGreen()), static_cast<int>(value * 7));
                expectEquals(static_cast<int>(colour.getBlue()), static_cast<int>((value * 13) % 256));
            }
        }

        beginTest("Independent Pillow fixtures reach LZW saturation, reset and KwKwK");
        // The adjacent generator independently decodes these fixtures with a
        // string dictionary and records code coverage in raster_lzw_manifest.json.
        // Saturation: width12, four full dictionaries and four subsequent clears.
        // Repetitive: 62 code==available (KwKwK) codes.
        const auto fixtureDirectory = fixtures();
        for (const auto* name : { "lzw_saturation.gif", "lzw_kwkwk.gif" }) {
            const auto file = fixtureDirectory.getChildFile(name);
            juce::MemoryBlock encodedFixture;
            expect(file.loadFileAsData(encodedFixture), "Could not load fixture: " + file.getFullPathName());
            const auto decoded = osci::RasterDecoder::decode(encodedFixture.getData(), encodedFixture.getSize());
            expect(static_cast<bool>(decoded), juce::String(decoded.error));
            if (!decoded) { continue; }
            const auto random = juce::String(name) == "lzw_saturation.gif";
            expectEquals(static_cast<int>(decoded.image->width), 128);
            expectEquals(static_cast<int>(decoded.image->height), random ? 128 : 16);
            expectEquals(static_cast<int>(decoded.image->frames.size()), 1);
            std::uint32_t state = 17;
            bool allPixelsMatch = true;
            for (unsigned index = 0; index < decoded.image->width * decoded.image->height; ++index) {
                state = state * 1664525U + 1013904223U;
                const auto value = random ? state >> 24 : 1U;
                const auto colour = pixel(*decoded.image, 0, index % 128, index / 128);
                allPixelsMatch = allPixelsMatch && colour.getRed() == value && colour.getGreen() == (value * 7) % 256
                    && colour.getBlue() == (value * 13) % 256 && colour.getAlpha() == 255;
            }
            expect(allPixelsMatch, "Decoded pixels differ from independent fixture generation.");
        }
        auto invalidDictionary = gif(1, 1);
        frame(invalidDictionary, 0, 0, 1, 1, { 1 });
        invalidDictionary.resize(invalidDictionary.size() - 5);
        // Minimum code size2, codes [clear=4, literal=1, invalid=7, end=5].
        // After the literal, the next available dictionary slot is6, not7.
        invalidDictionary.insert(invalidDictionary.end(), { 2, 2, 0xcc, 0x0b, 0 });
        expect(!decode(invalidDictionary), "A code beyond the next dictionary entry must reject.");
        auto missingEnd = gif(1, 1);
        frame(missingEnd, 0, 0, 1, 1, { 1 });
        missingEnd.resize(missingEnd.size() - 5);
        missingEnd.insert(missingEnd.end(), { 2, 1, 0x0c, 0 }); // clear, literal; no end code.
        expect(!decode(missingEnd), "A structurally complete GIF without an LZW end code must reject.");

        beginTest("Malformed inputs, bounds and cancellation return no partial images");
        animation.push_back(0x3b);
        for (std::size_t count = 0; count < animation.size(); ++count) { expect(!osci::RasterDecoder::decode(animation.data(), count)); }
        auto invalid = animation; invalid.push_back(0); expect(!osci::RasterDecoder::decode(invalid.data(), invalid.size()));
        auto huge = gif(65535, 65535); expect(!decode(huge));
        auto beyondCanvas = gif(1, 1); frame(beyondCanvas, 1, 0, 1, 1, { 1 }); expect(!decode(beyondCanvas));
        auto shortRaster = gif(2, 1); frame(shortRaster, 0, 0, 2, 1, { 1 }); expect(!decode(shortRaster));
        auto longRaster = gif(1, 1); frame(longRaster, 0, 0, 1, 1, { 1, 2 }); expect(!decode(longRaster));
        auto paletteInvalid = gif(1, 1); frame(paletteInvalid, 0, 0, 1, 1, { 1 }, 1, 1, 7); expect(!decode(paletteInvalid));
        auto overBudget = gif(4096, 4096);
        for (int i = 0; i < 5; ++i) { frame(overBudget, 0, 0, 1, 1, { 1 }); }
        expect(!decode(overBudget));
        auto tooMany = gif(1, 1);
        for (int i = 0; i < 10001; ++i) { frame(tooMany, 0, 0, 1, 1, { 1 }); }
        expect(!decode(tooMany));
        std::atomic<bool> cancel { true };
        expect(!osci::RasterDecoder::decode(animation.data(), animation.size(), &cancel));
        expect(!osci::RasterDecoder::decode(png.getData(), png.getDataSize(), &cancel));
        expect(!osci::RasterDecoder::decode(jpeg.getData(), jpeg.getDataSize() - 1));
        expect(!osci::RasterDecoder::decode(png.getData(), png.getDataSize() - 1));
        auto damagedPng = png.getMemoryBlock(); static_cast<std::uint8_t*>(damagedPng.getData())[damagedPng.getSize() - 1] ^= 1;
        expect(!osci::RasterDecoder::decode(damagedPng.getData(), damagedPng.getSize()));
        expect(!osci::RasterDecoder::decode(nullptr, 1));
        expect(!osci::RasterDecoder::decode(animation.data(), osci::RasterDecoder::maximumEncodedBytes + 1));
    }
};
static MotionRasterDecoderTest motionRasterDecoderTest;

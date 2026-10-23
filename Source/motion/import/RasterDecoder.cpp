#include "RasterDecoder.h"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace osci_raster_detail {
struct AllocationBudget {
    std::size_t live = 0;
    const std::atomic<bool>* cancel = nullptr;
};
static thread_local AllocationBudget* budget = nullptr;
struct alignas(std::max_align_t) Allocation { std::size_t size; };
static bool cancelled(const std::atomic<bool>* flag) { return flag != nullptr && flag->load(std::memory_order_relaxed); }
static void* allocate(std::size_t size) {
    if (budget == nullptr || cancelled(budget->cancel) || size > motion::RasterDecoder::maximumDecodedBytes - budget->live) { return nullptr; }
    auto* block = static_cast<Allocation*>(std::malloc(sizeof(Allocation) + size));
    if (block == nullptr) { return nullptr; }
    block->size = size;
    budget->live += size;
    return block + 1;
}
static void release(void* pointer) {
    if (pointer == nullptr) { return; }
    auto* block = static_cast<Allocation*>(pointer) - 1;
    if (budget != nullptr) { budget->live -= block->size; }
    std::free(block);
}
static void* resize(void* pointer, std::size_t size) {
    if (pointer == nullptr) { return allocate(size); }
    auto* old = static_cast<Allocation*>(pointer) - 1;
    const auto previous = old->size;
    if (budget == nullptr || cancelled(budget->cancel) || size > motion::RasterDecoder::maximumDecodedBytes - (budget->live - previous)) { return nullptr; }
    auto* block = static_cast<Allocation*>(std::realloc(old, sizeof(Allocation) + size));
    if (block == nullptr) { return nullptr; }
    block->size = size;
    budget->live = budget->live - previous + size;
    return block + 1;
}
}
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_MAX_DIMENSIONS 4096
#define STBI_MALLOC(size) osci_raster_detail::allocate(size)
#define STBI_REALLOC(pointer, size) osci_raster_detail::resize(pointer, size)
#define STBI_FREE(pointer) osci_raster_detail::release(pointer)
#include "third_party/stb/stb_image.h"
#undef STB_IMAGE_STATIC
#undef STB_IMAGE_IMPLEMENTATION
#undef STBI_ONLY_PNG
#undef STBI_ONLY_JPEG
#undef STBI_NO_STDIO
#undef STBI_NO_LINEAR
#undef STBI_NO_HDR
#undef STBI_MAX_DIMENSIONS
#undef STBI_MALLOC
#undef STBI_REALLOC
#undef STBI_FREE

namespace osci_raster_detail {
using Byte = std::uint8_t;
using Colour = std::array<Byte, 4>;
using Palette = std::array<Colour, 256>;
struct Failure : std::runtime_error { using std::runtime_error::runtime_error; };
static void checkCancel(const std::atomic<bool>* flag) { if (cancelled(flag)) { throw Failure("Raster import cancelled."); } }
static std::size_t canvasBytes(std::uint32_t width, std::uint32_t height) {
    if (width == 0 || height == 0 || width > motion::RasterDecoder::maximumDimension || height > motion::RasterDecoder::maximumDimension
        || static_cast<std::size_t>(width) * height > motion::RasterDecoder::maximumPixels) { throw Failure("This picture is too big: it can be at most 4096 pixels on each side, and 16 million pixels in all. Make it smaller."); }
    return static_cast<std::size_t>(width) * height * 4;
}
struct Cursor {
    const Byte* data;
    std::size_t size, position = 0;
    Byte byte() { if (position >= size) { throw Failure("Raster data is truncated."); } return data[position++]; }
    unsigned word() { const auto lo = byte(); return lo | (static_cast<unsigned>(byte()) << 8); }
    void skip(std::size_t count) { if (count > size - position) { throw Failure("Raster data is truncated."); } position += count; }
};
static Palette palette(Cursor& input, unsigned count) {
    Palette result {};
    for (unsigned i = 0; i < count; ++i) { result[i] = { input.byte(), input.byte(), input.byte(), 255 }; }
    return result;
}
static void skipBlocks(Cursor& input, const std::atomic<bool>* cancel) {
    for (;;) {
        checkCancel(cancel);
        const auto count = input.byte();
        if (count == 0) { return; }
        input.skip(count);
    }
}
struct GifFrame {
    unsigned x = 0, y = 0, width = 0, height = 0, disposal = 0, delay = 0, minimumCode = 0, colours = 0;
    int transparent = -1;
    bool interlaced = false;
    Palette palette {};
    std::size_t dataStart = 0, dataEnd = 0;
};
struct Gif {
    unsigned width = 0, height = 0;
    Colour background {};
    std::vector<GifFrame> frames;
};
static Gif inspectGif(const Byte* data, std::size_t size, const std::atomic<bool>* cancel) {
    Cursor input { data, size, 6 };
    Gif gif;
    gif.width = input.word(); gif.height = input.word();
    const auto bytes = canvasBytes(gif.width, gif.height);
    const auto flags = input.byte(), background = input.byte();
    input.byte();
    const unsigned globalCount = (flags & 128) != 0 ? (2U << (flags & 7)) : 0;
    const auto global = palette(input, globalCount);
    if (globalCount != 0) {
        if (background >= globalCount) { throw Failure("GIF background palette index is invalid."); }
        gif.background = global[background];
    }
    unsigned disposal = 0, delay = 0;
    int transparent = -1;
    for (;;) {
        checkCancel(cancel);
        const auto marker = input.byte();
        if (marker == 0x3b) {
            if (gif.frames.empty() || input.position != size) { throw Failure("GIF is empty or contains trailing data."); }
            return gif;
        }
        if (marker == 0x21) {
            const auto type = input.byte();
            if (type == 0xf9) {
                if (input.byte() != 4) { throw Failure("GIF graphic control block is invalid."); }
                const auto control = input.byte();
                disposal = (control >> 2) & 7;
                if (disposal > 3 || (control & 0xe0) != 0) { throw Failure("GIF disposal mode is unsupported."); }
                delay = input.word() * 10;
                const auto index = input.byte();
                transparent = (control & 1) != 0 ? index : -1;
                if (input.byte() != 0) { throw Failure("GIF graphic control block is unterminated."); }
            } else if (type == 0xfe || type == 0xff) {
                skipBlocks(input, cancel);
            } else {
                throw Failure("GIF contains an unsupported text or extension block.");
            }
            continue;
        }
        if (marker != 0x2c) { throw Failure("GIF image block marker is invalid."); }
        if (gif.frames.size() >= motion::RasterDecoder::maximumFrames || gif.frames.size() + 1 > motion::RasterDecoder::maximumDecodedBytes / bytes) {
            throw Failure("This GIF is too long to prepare: at most 10,000 frames, and about 256 MB of pictures in all (480 x 270 is about 500 frames). Shorten it or make it smaller.");
        }
        GifFrame frame;
        frame.x = input.word();
        frame.y = input.word();
        frame.width = input.word();
        frame.height = input.word();
        if (frame.width == 0 || frame.height == 0 || frame.x + frame.width > gif.width || frame.y + frame.height > gif.height) { throw Failure("GIF frame rectangle lies outside its canvas."); }
        const auto imageFlags = input.byte();
        if ((imageFlags & 0x18) != 0) { throw Failure("GIF image flags are invalid."); }
        frame.interlaced = (imageFlags & 0x40) != 0;
        frame.colours = (imageFlags & 0x80) != 0 ? (2U << (imageFlags & 7)) : globalCount;
        if (frame.colours == 0) { throw Failure("GIF frame has no colour palette."); }
        frame.palette = (imageFlags & 0x80) != 0 ? palette(input, frame.colours) : global;
        if (transparent >= static_cast<int>(frame.colours)) { throw Failure("GIF transparency index is outside its palette."); }
        frame.disposal = disposal; frame.delay = delay; frame.transparent = transparent;
        frame.minimumCode = input.byte();
        if (frame.minimumCode < 2 || frame.minimumCode > 8) { throw Failure("GIF LZW code size is invalid."); }
        frame.dataStart = input.position;
        skipBlocks(input, cancel);
        frame.dataEnd = input.position;
        gif.frames.push_back(std::move(frame));
        disposal = delay = 0; transparent = -1;
    }
}
struct GifBits {
    Cursor input;
    unsigned remaining = 0, bits = 0;
    std::uint32_t buffer = 0;
    int code(unsigned width) {
        while (bits < width) {
            if (remaining == 0) { remaining = input.byte(); if (remaining == 0) { return -1; } }
            buffer |= static_cast<std::uint32_t>(input.byte()) << bits;
            --remaining; bits += 8;
        }
        const auto result = buffer & ((1U << width) - 1);
        buffer >>= width; bits -= width;
        return static_cast<int>(result);
    }
};
static void drawGifFrame(const Byte* data, const Gif& gif, const GifFrame& frame, std::vector<Byte>& canvas, const std::atomic<bool>* cancel) {
    GifBits input { { data, frame.dataEnd, frame.dataStart } };
    std::array<unsigned, 4096> prefix {};
    std::array<Byte, 4096> suffix {}, stack {};
    const unsigned clear = 1U << frame.minimumCode, end = clear + 1;
    unsigned available = end + 1, width = frame.minimumCode + 1, first = 0;
    int previous = -1;
    std::size_t emitted = 0;
    unsigned column = 0, row = 0, pass = 0;
    static constexpr unsigned starts[] { 0, 4, 2, 1 }, steps[] { 8, 8, 4, 2 };
    if (input.code(width) != static_cast<int>(clear)) { throw Failure("GIF LZW stream must start with a clear code."); }
    const auto emit = [&](unsigned index) {
        if (emitted >= static_cast<std::size_t>(frame.width) * frame.height || index >= frame.colours) { throw Failure("GIF raster length or palette index is invalid."); }
        if ((emitted & 4095) == 0) { checkCancel(cancel); }
        if (static_cast<int>(index) != frame.transparent) {
            const auto offset = ((frame.y + row) * gif.width + frame.x + column) * 4;
            std::copy(frame.palette[index].begin(), frame.palette[index].end(), canvas.begin() + offset);
        }
        ++emitted;
        if (++column == frame.width) {
            column = 0;
            if (!frame.interlaced) {
                ++row;
            } else {
                row += steps[pass];
                while (row >= frame.height && pass < 3) { row = starts[++pass]; }
            }
        }
    };
    for (;;) {
        checkCancel(cancel);
        int code = input.code(width);
        if (code < 0) { throw Failure("GIF LZW end code is missing."); }
        if (code == static_cast<int>(end)) { break; }
        if (code == static_cast<int>(clear)) {
            available = end + 1;
            width = frame.minimumCode + 1;
            previous = -1;
            continue;
        }
        if (previous < 0) {
            if (code >= static_cast<int>(clear)) { throw Failure("GIF LZW initial code is invalid."); }
            emit(static_cast<unsigned>(code));
            first = static_cast<unsigned>(code);
            previous = code;
            continue;
        }
        const auto original = code;
        unsigned count = 0;
        if (code == static_cast<int>(available)) {
            stack[count++] = static_cast<Byte>(first);
            code = previous;
        } else if (code > static_cast<int>(available)) {
            throw Failure("GIF LZW dictionary code is invalid.");
        }
        while (code >= static_cast<int>(clear)) {
            if (code <= static_cast<int>(end) || code >= static_cast<int>(available) || count >= stack.size() - 1) { throw Failure("GIF LZW dictionary chain is invalid."); }
            stack[count++] = suffix[static_cast<unsigned>(code)];
            code = static_cast<int>(prefix[static_cast<unsigned>(code)]);
        }
        first = static_cast<unsigned>(code);
        stack[count++] = static_cast<Byte>(first);
        while (count != 0) { emit(stack[--count]); }
        if (available < 4096) {
            prefix[available] = static_cast<unsigned>(previous); suffix[available++] = static_cast<Byte>(first);
            if (available == (1U << width) && width < 12) { ++width; }
        }
        previous = original;
    }
    if (emitted != static_cast<std::size_t>(frame.width) * frame.height) { throw Failure("GIF raster does not fill its frame rectangle."); }
}
static void fill(std::vector<Byte>& canvas, unsigned canvasWidth, unsigned x, unsigned y, unsigned width, unsigned height, Colour colour, const std::atomic<bool>* cancel) {
    for (unsigned row = y; row < y + height; ++row) {
        checkCancel(cancel);
        for (unsigned column = x; column < x + width; ++column) {
            std::copy(colour.begin(), colour.end(), canvas.begin() + (static_cast<std::size_t>(row) * canvasWidth + column) * 4);
        }
    }
}
static std::shared_ptr<const motion::RasterImage> decodeGif(const Byte* data, std::size_t size, const std::atomic<bool>* cancel) {
    const auto gif = inspectGif(data, size, cancel);
    auto result = std::make_shared<motion::RasterImage>(); result->width = gif.width; result->height = gif.height;
    result->frames.reserve(gif.frames.size());
    std::vector<Byte> canvas(canvasBytes(gif.width, gif.height));
    if (gif.frames.front().transparent < 0) { fill(canvas, gif.width, 0, 0, gif.width, gif.height, gif.background, cancel); }
    for (const auto& frame : gif.frames) {
        checkCancel(cancel);
        std::vector<Byte> previous;
        if (frame.disposal == 3) { previous = canvas; }
        drawGifFrame(data, gif, frame, canvas, cancel);
        result->frames.push_back({ canvas, frame.delay });
        if (frame.disposal == 2) {
            fill(canvas, gif.width, frame.x, frame.y, frame.width, frame.height, frame.transparent >= 0 ? Colour {} : gif.background, cancel);
        } else if (frame.disposal == 3) {
            canvas = std::move(previous);
        }
    }
    checkCancel(cancel);
    return result;
}
struct Input {
    const Byte* data;
    std::size_t size, position = 0;
    const std::atomic<bool>* cancel;
};
static const stbi_io_callbacks callbacks {
    [](void* context, char* output, int requested) {
        auto& input = *static_cast<Input*>(context);
        if (cancelled(input.cancel) || requested <= 0) { return 0; }
        const auto count = std::min(static_cast<std::size_t>(requested), input.size - input.position);
        std::memcpy(output, input.data + input.position, count);
        input.position += count;
        return static_cast<int>(count);
    },
    [](void* context, int count) {
        auto& input = *static_cast<Input*>(context);
        if (count < 0) {
            input.position -= std::min(input.position, static_cast<std::size_t>(-static_cast<std::int64_t>(count)));
        } else {
            input.position += std::min(input.size - input.position, static_cast<std::size_t>(count));
        }
    },
    [](void* context) {
        const auto& input = *static_cast<Input*>(context);
        return static_cast<int>(input.position == input.size || cancelled(input.cancel));
    }
};
static unsigned big32(const Byte* data) { return (static_cast<unsigned>(data[0]) << 24) | (static_cast<unsigned>(data[1]) << 16) | (static_cast<unsigned>(data[2]) << 8) | data[3]; }
static std::uint32_t pngCrc(const Byte* data, std::size_t count, const std::atomic<bool>* cancel) {
    static const auto table = [] {
        std::array<std::uint32_t, 256> values {};
        for (std::uint32_t i = 0; i < 256; ++i) {
            auto value = i;
            for (unsigned bit = 0; bit < 8; ++bit) { value = (value >> 1) ^ ((value & 1) != 0 ? 0xedb88320U : 0); }
            values[i] = value;
        }
        return values;
    }();
    std::uint32_t crc = 0xffffffffU;
    for (std::size_t i = 0; i < count; ++i) {
        if ((i & 65535) == 0) { checkCancel(cancel); }
        crc = table[(crc ^ data[i]) & 255] ^ (crc >> 8);
    }
    return crc ^ 0xffffffffU;
}
static void inspectPng(const Byte* data, std::size_t size, const std::atomic<bool>* cancel) {
    Cursor input { data, size, 8 };
    bool first = true, pixels = false;
    while (input.position < size) {
        checkCancel(cancel);
        const auto start = input.position; input.skip(8);
        const auto count = big32(data + start);
        const auto* type = data + start + 4;
        input.skip(count); input.skip(4);
        if (pngCrc(type, static_cast<std::size_t>(count) + 4, cancel) != big32(type + 4 + count)) { throw Failure("PNG chunk checksum is invalid."); }
        if (first) {
            if (count != 13 || std::memcmp(type, "IHDR", 4) != 0) { throw Failure("PNG image header is invalid."); }
            canvasBytes(big32(type + 4), big32(type + 8)); first = false;
        }
        if (std::memcmp(type, "acTL", 4) == 0) { throw Failure("Animated PNG is not supported; use GIF for raster animation."); }
        if (std::memcmp(type, "IDAT", 4) == 0) { pixels = true; }
        if (std::memcmp(type, "IEND", 4) == 0) {
            if (count != 0 || !pixels || input.position != size) { throw Failure("PNG end marker or payload is invalid."); }
            return;
        }
    }
    throw Failure("PNG end marker is missing.");
}
}

motion::RasterDecoder::Result motion::RasterDecoder::decode(const void* data, std::size_t size, const std::atomic<bool>* cancel) {
    using namespace osci_raster_detail;
    try {
        checkCancel(cancel);
        if (data == nullptr || size == 0 || size > maximumEncodedBytes) { return { nullptr, "Raster source must contain 1 byte to 64 MiB." }; }
        const auto* bytes = static_cast<const Byte*>(data);
        if (size >= 6 && (std::memcmp(bytes, "GIF87a", 6) == 0 || std::memcmp(bytes, "GIF89a", 6) == 0)) { return { decodeGif(bytes, size, cancel), {} }; }
        const bool png = size >= 8 && std::memcmp(bytes, "\x89PNG\r\n\x1a\n", 8) == 0;
        const bool jpeg = size >= 4 && bytes[0] == 0xff && bytes[1] == 0xd8;
        if (!png && !jpeg) { return { nullptr, "Raster format is unsupported. Use PNG, JPEG or GIF." }; }
        if (png) { inspectPng(bytes, size, cancel); }
        if (jpeg && (bytes[size - 2] != 0xff || bytes[size - 1] != 0xd9)) { return { nullptr, "JPEG end marker is missing." }; }
        AllocationBudget allocations { 0, cancel };
        struct Scope {
            AllocationBudget* previous;
            explicit Scope(AllocationBudget& next) : previous(budget) { budget = &next; }
            ~Scope() { budget = previous; }
        } scope(allocations);
        Input input { bytes, size, 0, cancel };
        int width = 0, height = 0, components = 0;
        if (!stbi_info_from_callbacks(&callbacks, &input, &width, &height, &components)) { throw Failure("Raster header is invalid or decoding was cancelled."); }
        const auto count = canvasBytes(static_cast<unsigned>(width), static_cast<unsigned>(height));
        checkCancel(cancel);
        input.position = 0;
        std::unique_ptr<Byte, decltype(&release)> pixels(stbi_load_from_callbacks(&callbacks, &input, &width, &height, &components, 4), release);
        checkCancel(cancel);
        if (pixels == nullptr) { throw Failure("Raster decoding failed, or decoder scratch memory exceeded 256 MiB."); }
        if (canvasBytes(static_cast<unsigned>(width), static_cast<unsigned>(height)) != count) { throw Failure("Raster dimensions changed while decoding."); }
        auto result = std::make_shared<RasterImage>();
        result->width = static_cast<unsigned>(width);
        result->height = static_cast<unsigned>(height);
        result->frames.push_back({ std::vector<Byte>(pixels.get(), pixels.get() + count), 0 });
        checkCancel(cancel);
        return { std::move(result), {} };
    } catch (const std::bad_alloc&) {
        return { nullptr, "Not enough memory to decode raster source." };
    } catch (const std::exception& error) {
        return { nullptr, error.what() };
    } catch (...) {
        return { nullptr, "Raster decoding failed." };
    }
}

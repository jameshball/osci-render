#pragma once

#include "../model/PointFrameCache.h"
#include <JuceHeader.h>
#include <juce_core/zip/juce_zlib.h>

namespace motion {
// One gzip member containing exactly one PointFrameCache. JUCE's public gzip
// reader conflates errors with EOF and hides buffered trailing input; use its
// bundled zlib API for strict checksum, stream-end and consumed-byte validation.
class BakedSourceArchive {
public:
    static constexpr std::size_t maximumCompressedBytes = 64 * 1024 * 1024;
    struct EncodeResult {
        juce::MemoryBlock data;
        juce::String error;
        explicit operator bool() const { return data.getSize() != 0 && error.isEmpty(); }
    };
    static EncodeResult encode(const PreparedPointFrames& source) {
        try {
            const auto raw = PointFrameCache::encode(source);
            if (!raw) { return { {}, juce::String(raw.error) }; }
            BoundedOutput output;
            {
                juce::GZIPCompressorOutputStream compressor(output, 6, 31);
                if (!compressor.write(raw.bytes.data(), raw.bytes.size())) { return { {}, "Could not compress baked source." }; }
                compressor.flush();
            }
            if (output.failed || output.data.getSize() == 0) { return { {}, "Compressed baked source exceeds 64 MiB or could not be written." }; }
            return { std::move(output.data), {} };
        } catch (const std::bad_alloc&) {
            return { {}, "Not enough memory to compress baked source." };
        } catch (...) {
            return { {}, "Baked source compression failed." };
        }
    }
    static PreparedPointFrames::Result decode(const juce::MemoryBlock& data) {
        try {
            if (data.getSize() == 0 || data.getSize() > maximumCompressedBytes) {
                return { nullptr, "Compressed baked source must contain 1 byte to 64 MiB." };
            }
            Inflater inflater;
            if (!inflater.valid) { return { nullptr, "Could not initialise baked source decompression." }; }
            auto& stream = inflater.stream;
            stream.next_in = static_cast<const Bytef*>(data.getData());
            stream.avail_in = static_cast<uInt>(data.getSize());
            int status = Z_OK;
            const auto read = [&](void* destination, std::size_t size) {
                stream.next_out = static_cast<Bytef*>(destination);
                stream.avail_out = static_cast<uInt>(size);
                while (stream.avail_out != 0 && status != Z_STREAM_END) {
                    const auto beforeIn = stream.avail_in, beforeOut = stream.avail_out;
                    status = inflate(&stream, Z_NO_FLUSH);
                    if ((status != Z_OK && status != Z_STREAM_END)
                        || (beforeIn == stream.avail_in && beforeOut == stream.avail_out)) { return false; }
                }
                return stream.avail_out == 0;
            };
            std::array<std::uint8_t, PointFrameCache::headerBytes> headerBytes {};
            if (!read(headerBytes.data(), headerBytes.size())) { return { nullptr, "Baked source gzip header or cache header is invalid or truncated." }; }
            const auto header = PointFrameCache::inspectHeader(headerBytes.data(), headerBytes.size());
            if (!header) { return { nullptr, header.error }; }
            std::vector<std::uint8_t> raw(header.byteCount);
            std::copy(headerBytes.begin(), headerBytes.end(), raw.begin());
            for (std::size_t position = headerBytes.size(); position < raw.size();) {
                const auto count = std::min<std::size_t>(65536, raw.size() - position);
                if (!read(raw.data() + position, count)) { return { nullptr, "Baked source gzip payload is invalid or truncated." }; }
                position += count;
            }
            std::uint8_t extra = 0;
            read(&extra, 1); // Consume/check gzip trailer, permitting no more decoded bytes.
            if (status != Z_STREAM_END || stream.total_out != raw.size() || stream.avail_in != 0) {
                return { nullptr, "Baked source gzip checksum, length or trailing data is invalid." };
            }
            return PointFrameCache::decode(raw.data(), raw.size());
        } catch (const std::bad_alloc&) {
            return { nullptr, "Not enough memory to decompress baked source." };
        } catch (...) {
            return { nullptr, "Baked source decompression failed." };
        }
    }
private:
    struct Inflater {
        z_stream stream {};
        bool valid = inflateInit2(&stream, 31) == Z_OK;
        ~Inflater() { if (valid) { inflateEnd(&stream); } }
    };
    struct BoundedOutput : juce::OutputStream {
        juce::MemoryBlock data;
        bool failed = false;
        bool write(const void* source, std::size_t bytes) override {
            if (failed || bytes > maximumCompressedBytes - data.getSize()) { failed = true; return false; }
            data.append(source, bytes);
            return true;
        }
        void flush() override {}
        juce::int64 getPosition() override { return static_cast<juce::int64>(data.getSize()); }
        bool setPosition(juce::int64 position) override { return position == getPosition(); }
    };
};
}

#include "SourceDecoding.h"
#include "BakedSourceArchive.h"
#include "LuaBaker.h"
#include "MidiSourcePreparer.h"
#include "RasterSourcePreparer.h"
#include "VideoSourcePreparer.h"
#include "../live/BlenderCaptureArchive.h"
#include "../model/Cancellation.h"
#include "../model/Drawing.h"
#include "FractalPreparation.h"
#include <cstring>
#include <new>
#if OSCI_PREMIUM
#include "../../parser/lottie/LottieParser.h"
#include "../../parser/lottie/DotLottieArchive.h"
#endif

namespace motion {
namespace {
using ImportShapes = std::vector<std::unique_ptr<osci::Shape>>;

// What a baked cache was made from: the settings `write` records, then the
// source bytes, hashed. A cache with another key is stale.
juce::String bakeKey(const Asset& asset, const std::function<void(juce::MemoryOutputStream&)>& write) {
    juce::MemoryOutputStream metadata;
    metadata.writeInt(1); // Bake algorithm/context version, separate from cache wire version.
    write(metadata);
    metadata.write(asset.data.getData(), asset.data.getSize());
    return juce::SHA256(metadata.getData(), metadata.getDataSize()).toHexString();
}

juce::String sourceBakeKey(const Asset& asset) {
    return bakeKey(asset, [&settings = asset.bakeSettings](juce::MemoryOutputStream& metadata) {
        metadata.writeDouble(settings.duration);
        metadata.writeDouble(settings.frameRate);
        metadata.writeDouble(settings.bpm);
        metadata.writeInt64(static_cast<juce::int64>(settings.pointsPerFrame));
        metadata.writeInt64(settings.seed);
    });
}

juce::String videoBakeKey(const Asset& asset) {
    return bakeKey(asset, [&settings = asset.rasterSettings](juce::MemoryOutputStream& metadata) {
        metadata.writeInt(static_cast<int>(settings.mode));
        metadata.writeDouble(settings.threshold);
        metadata.writeBool(settings.invert);
        metadata.writeInt(settings.resolution);
        metadata.writeDouble(settings.videoFrameRate);
        metadata.writeInt64(static_cast<juce::int64>(settings.pointsPerFrame));
    });
}

// A source kept with a baked cache: the saved cache when its key matches,
// or `bake()` run afresh and archived. Either way the frames must have the
// shape the asset's settings ask for.
juce::Result prepareBaked(Asset& asset, const juce::String& key, const std::function<PreparedPointFrames::Result()>& bake, const std::function<bool(const PreparedPointFrames&)>& matches,
        const std::atomic<bool>* cancel, std::atomic<double>* progress) {
    PreparedPointFrames::Result prepared;
    juce::MemoryBlock archive;
    if (asset.bakedData.getSize() > 0) {
        if (asset.bakeKey != key) { return juce::Result::fail("The source's cache does not match its contents and settings. Prepare it again."); }
        prepared = BakedSourceArchive::decode(asset.bakedData);
    } else {
        prepared = bake();
        if (prepared) {
            auto encoded = BakedSourceArchive::encode(*prepared.source);
            if (!encoded) { return juce::Result::fail(encoded.error); }
            archive = std::move(encoded.data);
        }
    }
    if (!prepared) { return juce::Result::fail(prepared.error); }
    if (!matches(*prepared.source)) { return juce::Result::fail("The source's cache does not have the frames its settings ask for."); }
    if (cancelled(cancel)) { return juce::Result::fail("Source preparation cancelled."); }
    asset.source = std::make_shared<const PreparedSource>(prepared.source);
    asset.audio.reset();
    if (archive.getSize() > 0) { asset.bakedData = std::move(archive); }
    asset.bakeKey = key;
    if (progress != nullptr) { progress->store(1); }
    return juce::Result::ok();
}

// repeatsPrevious(frame) lets identical frames share one drawing (and count
// once against the geometry budget).
juce::Result prepareSourceFrames(Asset& asset, int frameCount, double frameRate, const std::function<juce::Result(int, ImportShapes&)>& draw, const std::atomic<bool>* cancel, std::atomic<double>* progress, const std::function<bool(int)>& repeatsPrevious = {}) {
    if (frameCount <= 0 || static_cast<std::size_t>(frameCount) > maximumSourceFrames
        || !std::isfinite(frameRate) || frameRate <= 0.0 || frameRate > 1000.0
        || !std::isfinite(frameCount / frameRate)) {
        return juce::Result::fail("Animation must contain 1-3600 frames with a frame rate between 0 and 1000 fps.");
    }
    std::vector<std::shared_ptr<const motion::PreparedDrawing>> frames;
    frames.reserve(static_cast<std::size_t>(frameCount));
    std::size_t totalShapes = 0;
    bool hasGeometry = false;
    for (int frame = 0; frame < frameCount; ++frame) {
        if (cancelled(cancel)) {
            return juce::Result::fail("Source import cancelled.");
        }
        if (frame > 0 && repeatsPrevious && repeatsPrevious(frame)) {
            frames.push_back(frames.back());
            continue;
        }
        ImportShapes shapes;
        const auto result = draw(frame, shapes);
        if (result.failed()) {
            return result;
        }
        totalShapes += shapes.size();
        if (shapes.size() > maximumShapesPerFrame || totalShapes > maximumSourceShapes) {
            return juce::Result::fail("Animation exceeds the geometry budget (100000 shapes per frame or 1000000 total). Simplify or shorten the source.");
        }
        auto drawing = std::make_shared<motion::PreparedDrawing>(std::move(shapes));
        hasGeometry = hasGeometry || !drawing->empty();
        frames.push_back(std::move(drawing));
        if (progress != nullptr) {
            progress->store(static_cast<double>(frame + 1) / (frameCount + 1), std::memory_order_relaxed);
        }
    }
    if (cancelled(cancel)) {
        return juce::Result::fail("Source import cancelled.");
    }
    if (!hasGeometry) {
        return juce::Result::fail("The source contains no drawable geometry.");
    }
    auto prepared = std::make_shared<PreparedSource>(std::move(frames), frameRate);
    asset.source = std::move(prepared);
    asset.audio.reset();
    if (progress != nullptr) {
        progress->store(1.0, std::memory_order_relaxed);
    }
    return juce::Result::ok();
}

// Per-character animation: each glyph is posed per frame about its own centre,
// and every frame shares the resting layout's normalisation, so the finished
// text matches the static source exactly.
juce::Result prepareAnimatedText(Asset& asset, const juce::GlyphArrangement& glyphs, float em, const std::atomic<bool>* cancel, std::atomic<double>* progress) {
    std::vector<juce::Path> paths;
    for (int index = 0; index < glyphs.getNumGlyphs(); ++index) {
        juce::Path glyph;
        glyphs.getGlyph(index).createPath(glyph);
        if (!glyph.isEmpty()) { paths.push_back(std::move(glyph)); }
    }
    if (paths.empty()) { return juce::Result::fail("The source contains no drawable geometry."); }
    juce::Path rest;
    for (const auto& glyph : paths) { rest.addPath(glyph); }
    ImportShapes restShapes;
    SvgParser::pathToShapes(rest, restShapes, false);
    float minX = std::numeric_limits<float>::max(), minY = minX, maxX = std::numeric_limits<float>::lowest(), maxY = maxX;
    for (const auto& shape : restShapes) {
        for (const auto t : {0.0f, 1.0f}) {
            const auto point = shape->nextVector(t);
            minX = std::min(minX, point.x); maxX = std::max(maxX, point.x);
            minY = std::min(minY, point.y); maxY = std::max(maxY, point.y);
        }
    }
    const auto centreX = (minX + maxX) / 2, centreY = (minY + maxY) / 2;
    const auto scale = 1.8f / std::max(maxX - minX, maxY - minY);
    constexpr double frameRate = 30;
    const auto& settings = asset.textSettings;
    const auto characters = static_cast<int>(paths.size());
    const auto length = std::ceil(settings.animationLength(characters) * frameRate);
    if (length > static_cast<double>(maximumSourceFrames)) {
        return juce::Result::fail("This text animation lasts over 120 seconds. Shorten the stagger or hold, or split the text.");
    }
    const auto frames = static_cast<int>(std::max(1.0, length));
    // Once every character has arrived, the remaining (hold) frames repeat.
    const auto settled = settings.animation == TextSettings::Animation::wave ? frames
        : static_cast<int>(std::ceil((settings.characterDelay * std::max(0, characters - 1) + settings.characterDuration) * frameRate));
    return prepareSourceFrames(asset, frames, frameRate, [&](int frame, ImportShapes& shapes) {
        juce::Path posed;
        const auto time = frame / frameRate;
        for (std::size_t index = 0; index < paths.size(); ++index) {
            const auto pose = settings.pose(static_cast<int>(index), time);
            if (!pose.visible || pose.scale <= 0) { continue; }
            const auto bounds = paths[index].getBounds();
            // Glyph space is y-down; a positive pose offset moves up the screen.
            const auto transform = juce::AffineTransform::translation(-bounds.getCentreX(), -bounds.getCentreY())
                .scaled(static_cast<float>(pose.scale))
                .rotated(static_cast<float>(-pose.rotation * std::numbers::pi / 180))
                .translated(bounds.getCentreX() + static_cast<float>(pose.x) * em, bounds.getCentreY() - static_cast<float>(pose.y) * em);
            posed.addPath(paths[index], transform);
        }
        SvgParser::pathToShapes(posed, shapes, false);
        for (auto& shape : shapes) {
            shape->translate(-centreX, -centreY, 0);
            shape->scale(scale, scale, 1);
        }
        return cancelled(cancel) ? juce::Result::fail("Source preparation cancelled.") : juce::Result::ok();
    }, cancel, progress, [settled](int frame) { return frame > settled; });
}

bool finiteNumber(const juce::var& value) {
    return (value.isInt() || value.isInt64() || value.isDouble()) && std::isfinite(static_cast<double>(value));
}

juce::Result validateGplaFrame(const juce::var& frame, std::size_t& totalVertices) {
    const auto focalLength = frame.getProperty("focalLength", {});
    const auto objectsValue = frame.getProperty("objects", {});
    const auto* objects = objectsValue.getArray();
    if (!finiteNumber(focalLength) || objects == nullptr) {
        return juce::Result::fail("GPLA frames require a finite focalLength and an objects array.");
    }
    std::size_t frameVertices = 0;
    for (const auto& object : *objects) {
        const auto matrixValue = object.getProperty("matrix", {});
        const auto strokesValue = object.getProperty("vertices", {});
        const auto* matrix = matrixValue.getArray();
        const auto* strokes = strokesValue.getArray();
        if (matrix == nullptr || matrix->size() != 16 || strokes == nullptr || strokes->size() > 4096) {
            return juce::Result::fail("GPLA objects require a 16-value matrix and at most 4096 strokes.");
        }
        for (const auto& value : *matrix) {
            if (!finiteNumber(value)) {
                return juce::Result::fail("GPLA matrices must contain finite numbers.");
            }
        }
        for (const auto& strokeValue : *strokes) {
            const auto* stroke = strokeValue.getArray();
            if (stroke == nullptr || stroke->size() < 2) {
                return juce::Result::fail("GPLA strokes must contain at least two vertices.");
            }
            frameVertices += static_cast<std::size_t>(stroke->size());
            totalVertices += static_cast<std::size_t>(stroke->size());
            if (frameVertices > maximumShapesPerFrame || totalVertices > maximumSourceShapes) {
                return juce::Result::fail("GPLA exceeds the preparation budget (100000 vertices per frame or 1000000 total). Simplify the source.");
            }
            for (const auto& vertex : *stroke) {
                for (const auto* axis : { "x", "y", "z" }) {
                    if (!finiteNumber(vertex.getProperty(axis, {}))) {
                        return juce::Result::fail("GPLA vertices must contain finite X, Y and Z values.");
                    }
                }
            }
        }
    }
    return juce::Result::ok();
}

void addLines(const std::vector<osci::Line>& lines, ImportShapes& shapes) {
    shapes.reserve(shapes.size() + lines.size());
    for (const auto& line : lines) { shapes.push_back(std::make_unique<osci::Line>(line)); }
}

// Structural preflight protects the shared binary parser's unchecked matrix
// and stroke assumptions, and finds each frame for LineArtParser to decode.
struct GplaBinaryLayout {
    explicit GplaBinaryLayout(const juce::MemoryBlock& data) : bytes(static_cast<const char*>(data.getData())), size(data.getSize()) {}

    bool tag(const char* value) {
        if (!peek(value)) {
            return false;
        }
        position += 8;
        return true;
    }
    bool peek(const char* value) const {
        return size - position >= 8 && std::memcmp(bytes + position, value, 8) == 0;
    }
    bool integer(juce::int64& value) {
        if (size - position < 8) {
            return false;
        }
        value = static_cast<juce::int64>(juce::ByteOrder::littleEndianInt64(bytes + position));
        position += 8;
        return true;
    }
    bool number() {
        juce::int64 bits = 0;
        if (!integer(bits)) {
            return false;
        }
        double value;
        std::memcpy(&value, &bits, sizeof(value));
        return std::isfinite(value);
    }

    bool validate(const std::atomic<bool>* cancel) {
        juce::int64 ignored = 0;
        if (size % 8 != 0 || !tag("GPLA    ") || !integer(ignored) || !integer(ignored) || !integer(ignored) || !tag("FILE    ")) {
            return false;
        }
        juce::int64 reportedFrames = 0;
        while (!peek("DONE    ")) {
            const bool count = peek("fCount  ");
            const bool rate = peek("fRate   ");
            if (!integer(ignored) || !integer(ignored)) {
                return false;
            }
            if (count) {
                reportedFrames = ignored;
            }
            if (rate) {
                frameRate = static_cast<double>(ignored);
            }
        }
        if (!tag("DONE    ") || reportedFrames <= 0 || reportedFrames > static_cast<juce::int64>(maximumSourceFrames)
            || frameRate <= 0.0 || frameRate > 1000.0) {
            return false;
        }
        std::size_t totalVertices = 0;
        while (!peek("END GPLA")) {
            if (cancelled(cancel) || frames.size() >= maximumSourceFrames) {
                return false;
            }
            const auto start = position;
            if (!tag("FRAME   ")) {
                return false;
            }
            bool hasFocalLength = false;
            while (!peek("OBJECTS ")) {
                const bool focal = peek("focalLen");
                if (!integer(ignored) || (focal ? !number() : !integer(ignored))) {
                    return false;
                }
                hasFocalLength = hasFocalLength || focal;
            }
            if (!hasFocalLength || !tag("OBJECTS ")) {
                return false;
            }
            std::size_t frameVertices = 0;
            while (!peek("DONE    ")) {
                if (!tag("OBJECT  ") || !tag("MATRIX  ")) {
                    return false;
                }
                for (int index = 0; index < 16; ++index) {
                    if (!number()) {
                        return false;
                    }
                }
                if (!tag("DONE    ") || !tag("STROKES ")) {
                    return false;
                }
                int strokes = 0;
                while (!peek("DONE    ")) {
                    juce::int64 count = 0;
                    if (++strokes > 4096 || !tag("STROKE  ") || !tag("vertexCt") || !integer(count)
                        || count < 2 || count > static_cast<juce::int64>(maximumShapesPerFrame) || !tag("VERTICES")) {
                        return false;
                    }
                    frameVertices += static_cast<std::size_t>(count);
                    totalVertices += static_cast<std::size_t>(count);
                    if (frameVertices > maximumShapesPerFrame || totalVertices > maximumSourceShapes) {
                        return false;
                    }
                    for (juce::int64 index = 0; index < count * 3; ++index) {
                        if (!number()) {
                            return false;
                        }
                    }
                    if (!tag("DONE    ") || !tag("DONE    ")) {
                        return false;
                    }
                }
                if (!tag("DONE    ") || !tag("DONE    ")) {
                    return false;
                }
            }
            if (!tag("DONE    ")) {
                return false;
            }
            frames.push_back({ start, position - start });
        }
        return tag("END GPLA") && position == size && frames.size() == static_cast<std::size_t>(reportedFrames);
    }

    const char* bytes;
    const std::size_t size;
    std::size_t position = 0;
    double frameRate = 30.0;
    std::vector<std::pair<std::size_t, std::size_t>> frames;
};

juce::Result decodeGpla(Asset& asset, const std::atomic<bool>* cancel, std::atomic<double>* progress) {
    const bool binary = asset.data.getSize() >= 8 && std::memcmp(asset.data.getData(), "GPLA    ", 8) == 0;
    if (binary) {
        GplaBinaryLayout layout(asset.data);
        if (!layout.validate(cancel)) {
            return juce::Result::fail(cancelled(cancel) ? "Source import cancelled."
                : "Invalid or unsupported binary GPLA structure, or animation exceeds the 3600-frame / 1000000-vertex preparation budget.");
        }
        return prepareSourceFrames(asset, static_cast<int>(layout.frames.size()), layout.frameRate,
            [&](int frame, ImportShapes& shapes) {
                const auto [start, length] = layout.frames[static_cast<std::size_t>(frame)];
                addLines(LineArtParser::parseBinaryFrame(layout.bytes + start, static_cast<int>(length)), shapes);
                return juce::Result::ok();
            }, cancel, progress);
    }
    const auto text = juce::String::fromUTF8(static_cast<const char*>(asset.data.getData()), static_cast<int>(asset.data.getSize()));
    const auto parsed = juce::JSON::parse(text);
    const auto framesValue = parsed.getProperty("frames", {});
    const auto* frames = framesValue.getArray();
    if (frames == nullptr || frames->isEmpty() || static_cast<std::size_t>(frames->size()) > maximumSourceFrames) {
        return juce::Result::fail("GPLA JSON requires a frames array containing 1-3600 frames.");
    }
    std::size_t totalVertices = 0;
    return prepareSourceFrames(asset, frames->size(), 30.0,
        [&](int frame, ImportShapes& shapes) {
            const auto& item = frames->getReference(frame);
            const auto validated = validateGplaFrame(item, totalVertices);
            if (validated.failed()) {
                return validated;
            }
            const auto objects = item.getProperty("objects", {});
            addLines(LineArtParser::generateFrame(*objects.getArray(), static_cast<double>(item.getProperty("focalLength", {}))), shapes);
            return juce::Result::ok();
        }, cancel, progress);
}
}

juce::Result decodeAsset(Asset& asset, const std::atomic<bool>* cancel, std::atomic<double>* progress, const juce::File& videoDecoder) try {
    if (progress != nullptr) {
        progress->store(0.0, std::memory_order_relaxed);
    }
    if (cancelled(cancel)) {
        return juce::Result::fail("Source import cancelled.");
    }
    if (asset.extension.equalsIgnoreCase(".blender")) {
        if (!asset.blenderSettings.valid() || asset.data.getSize() != 0) { return juce::Result::fail("Invalid Blender source settings."); }
        asset.liveIdentity = std::make_shared<const LiveSourceIdentity>();
        asset.source.reset(); asset.audio.reset(); asset.midi.reset();
        if (progress != nullptr) { progress->store(1); }
        return juce::Result::ok();
    }
    if (asset.data.getSize() == 0 || asset.data.getSize() > maximumSourceBytes) {
        return juce::Result::fail("Source files must contain data and be no larger than 64 MiB.");
    }
    const auto extension = asset.extension.toLowerCase();
    if (extension == ".blender-capture") {
        auto capture = BlenderCaptureArchive::decode({static_cast<const std::uint8_t*>(asset.data.getData()), asset.data.getSize()}, cancel);
        if (!capture) { return juce::Result::fail(capture.error); }
        std::vector<std::shared_ptr<const motion::PreparedDrawing>> drawings;
        drawings.reserve(capture.frames.size());
        for (const auto& frame : capture.frames) {
            if (cancelled(cancel)) { return juce::Result::fail("Capture preparation cancelled."); }
            std::vector<std::unique_ptr<osci::Shape>> lines;
            lines.reserve(frame->segments.size());
            for (const auto& segment : frame->segments) {
                lines.push_back(std::make_unique<osci::Line>(osci::Point(segment.x1, segment.y1, 0), osci::Point(segment.x2, segment.y2, 0)));
            }
            drawings.push_back(std::make_shared<const motion::PreparedDrawing>(std::move(lines)));
            if (progress != nullptr) { progress->store(static_cast<double>(drawings.size()) / capture.frames.size()); }
        }
        asset.source = std::make_shared<const PreparedSource>(std::move(drawings), std::move(capture.timing));
        asset.liveIdentity.reset(); asset.audio.reset(); asset.midi.reset();
        return juce::Result::ok();
    }
    if (isMidiSource(extension)) {
        const auto prepared = MidiSourcePreparer::prepare(asset.data.getData(), asset.data.getSize(), asset.midiImportBpm, cancel);
        if (!prepared) { return juce::Result::fail(prepared.error); }
        asset.midi = prepared.source;
        asset.midiSuggestedBpm = prepared.suggestedBpm;
        asset.midiTempoChanges = prepared.tempoChanges.empty() ? nullptr : std::make_shared<const std::vector<TempoChange>>(prepared.tempoChanges);
        asset.midiIgnoredEvents = prepared.ignoredEvents;
        asset.source.reset();
        asset.audio.reset();
        if (progress != nullptr) { progress->store(1); }
        return juce::Result::ok();
    }
    if (osci::files::isVideo(extension)) {
        const auto invalid = asset.rasterSettings.validate();
        if (!invalid.empty()) { return juce::Result::fail(invalid); }
        const auto& settings = asset.rasterSettings;
        return prepareBaked(asset, videoBakeKey(asset), [&] { return VideoSourcePreparer::prepare(asset.data, videoDecoder, settings, cancel, progress); },
            [&settings](const PreparedPointFrames& frames) { return frames.frameRate() == settings.videoFrameRate && frames.pointsPerFrame() == settings.pointsPerFrame; }, cancel, progress);
    }
    if (osci::files::isImage(extension)) {
        const auto prepared = RasterSourcePreparer::prepare(asset.data.getData(), asset.data.getSize(), asset.rasterSettings, cancel, progress);
        if (!prepared) { return juce::Result::fail(prepared.error); }
        if (cancelled(cancel)) { return juce::Result::fail("Image preparation cancelled."); }
        asset.source = prepared.source;
        asset.audio.reset();
        if (progress != nullptr) { progress->store(1); }
        return juce::Result::ok();
    }
    if (extension == ".lua") {
        const auto settingsError = asset.bakeSettings.validate();
        if (!settingsError.empty()) { return juce::Result::fail(settingsError); }
        const auto& settings = asset.bakeSettings;
        const auto script = juce::String::fromUTF8(static_cast<const char*>(asset.data.getData()), static_cast<int>(asset.data.getSize()));
        return prepareBaked(asset, sourceBakeKey(asset), [&] { return LuaBaker::bake(asset.name, script, settings, cancel, progress); },
            [&settings](const PreparedPointFrames& frames) {
                return frames.frameCount() == settings.frameCount() && frames.frameRate() == settings.frameRate && frames.pointsPerFrame() == settings.pointsPerFrame;
            }, cancel, progress);
    }
    if (osci::files::isAudio(extension)) {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        auto input = std::make_unique<juce::MemoryInputStream>(asset.data, false);
        std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(std::move(input)));
        if (reader == nullptr || reader->lengthInSamples <= 0) {
            return juce::Result::fail("Cannot decode this audio file. Check its contents and whether its codec is enabled in this build.");
        }
        const auto prepared = PreparedAudio::create(reader->sampleRate, reader->numChannels, static_cast<std::uint64_t>(reader->lengthInSamples),
            [&](float* const* channels, std::size_t channelCount, std::size_t first, std::size_t frames) {
                if (cancelled(cancel)) {
                    return false;
                }
                const bool read = reader->read(channels, static_cast<int>(channelCount), static_cast<juce::int64>(first), static_cast<int>(frames));
                if (read && progress != nullptr) {
                    progress->store(0.9 * static_cast<double>(first + frames) / reader->lengthInSamples, std::memory_order_relaxed);
                }
                return read;
            });
        if (cancelled(cancel)) {
            return juce::Result::fail("Source import cancelled.");
        }
        if (!prepared) {
            return juce::Result::fail(juce::String(prepared.error));
        }
        asset.audio = prepared.audio;
        asset.source.reset();
        if (progress != nullptr) {
            progress->store(1.0, std::memory_order_relaxed);
        }
        return juce::Result::ok();
    }
    if (extension == ".gpla") {
        return decodeGpla(asset, cancel, progress);
    }
    if (osci::files::isLottie(extension)) {
#if OSCI_PREMIUM
        const auto content = extension == ".lottie" ? osci::lottie::extractAnimationJsonFromDotLottie(asset.data)
            : juce::String::fromUTF8(static_cast<const char*>(asset.data.getData()), static_cast<int>(asset.data.getSize()));
        if (content.isEmpty() || static_cast<std::size_t>(content.getNumBytesAsUTF8()) > maximumSourceBytes) {
            return juce::Result::fail("The Lottie source must contain an animation JSON no larger than 64 MiB.");
        }
        const auto json = juce::JSON::parse(content);
        const auto frameRate = json.getProperty("fr", {});
        const auto firstFrame = json.getProperty("ip", {});
        const auto lastFrame = json.getProperty("op", {});
        if (json.getDynamicObject() == nullptr || !json.getProperty("layers", {}).isArray()
            || !finiteNumber(frameRate) || !finiteNumber(firstFrame) || !finiteNumber(lastFrame)) {
            return juce::Result::fail("The JSON file is not a valid Lottie animation.");
        }
        const auto count = std::round(static_cast<double>(lastFrame) - static_cast<double>(firstFrame));
        if (count < 1.0 || count > maximumSourceFrames || static_cast<double>(frameRate) <= 0.0 || static_cast<double>(frameRate) > 1000.0) {
            return juce::Result::fail("Lottie animations must contain 1-3600 frames with a frame rate between 0 and 1000 fps.");
        }
        if (cancelled(cancel)) {
            return juce::Result::fail("Source import cancelled.");
        }
        juce::String error;
        OsciLottieParser parser(content, [&](juce::String message) { error = std::move(message); });
        if (error.isNotEmpty()) {
            return juce::Result::fail(error);
        }
        return prepareSourceFrames(asset, parser.getNumFrames(), parser.getFrameRate(),
            [&](int frame, ImportShapes& shapes) {
                parser.setFrame(frame);
                shapes = parser.draw();
                return error.isEmpty() ? juce::Result::ok() : juce::Result::fail(error);
            }, cancel, progress);
#else
        return juce::Result::fail("Lottie import is not available in this build.");
#endif
    }
    ImportShapes shapes;
    const auto content = juce::String::fromUTF8(static_cast<const char*>(asset.data.getData()), static_cast<int>(asset.data.getSize()));
    if (extension == ".lsystem") {
        const auto prepared = motion::fractal::prepare(content, asset.fractalDepth, cancel);
        if (!prepared) { return juce::Result::fail(prepared.error); }
        shapes.reserve(prepared.segments.size());
        for (const auto& segment : prepared.segments) {
            shapes.push_back(std::make_unique<osci::Line>(osci::Point(segment[0], segment[1], 0), osci::Point(segment[2], segment[3], 0)));
        }
    } else if (extension == ".obj") {
        WorldObject object(content.toStdString());
        shapes = object.draw();
    } else if (extension == ".svg" && drawing::isDrawing(content)) {
        // Drawings keep the place and size they were drawn at.
        for (auto [path, smooth] : drawing::paths(content)) {
            path.applyTransform(juce::AffineTransform::scale(1.0f, -1.0f));
            SvgParser::pathToShapes(path, shapes, false);
        }
    } else if (extension == ".svg") {
        SvgParser svg(content);
        shapes = svg.draw();
    } else if (extension == ".txt") {
        if (content.length() > 16384) {
            return juce::Result::fail("Text sources support up to 16,384 characters. Split longer text into separate sources.");
        }
        const auto error = asset.textSettings.validate();
        if (error.isNotEmpty()) { return juce::Result::fail(error); }
        const auto font = asset.textSettings.font(30);
        // Motion titles retain their natural proportions and explicit line breaks.
        // The synth's fitted two-line text box is unsuitable for composition work.
        juce::GlyphArrangement glyphs;
        juce::StringArray lines;
        lines.addLines(content);
        float baseline = 0.0f;
        for (const auto& line : lines) {
            if (cancelled(cancel)) { return juce::Result::fail("Source preparation cancelled."); }
            juce::GlyphArrangement row;
            row.addLineOfText(font, line, 0.0f, baseline);
            const auto width = row.getBoundingBox(0, row.getNumGlyphs(), true).getWidth();
            const auto offset = asset.textSettings.alignment == 1 ? -0.5f * width : asset.textSettings.alignment == 2 ? -width : 0.0f;
            row.moveRangeOfGlyphs(0, row.getNumGlyphs(), offset, 0.0f);
            glyphs.addGlyphArrangement(row);
            baseline += font.getHeight() * static_cast<float>(asset.textSettings.lineSpacing);
        }
        if (asset.textSettings.animated()) { return prepareAnimatedText(asset, glyphs, font.getHeight(), cancel, progress); }
        juce::Path path;
        glyphs.createPath(path);
        SvgParser::pathToShapes(path, shapes, true);
    } else {
        return juce::Result::fail("This source type is not connected to the Motion importer yet.");
    }
    return prepareSourceFrames(asset, 1, 30.0, [&](int, ImportShapes& frame) {
        frame = std::move(shapes);
        return juce::Result::ok();
    }, cancel, progress);
} catch (const std::bad_alloc&) {
    return juce::Result::fail("Not enough memory to prepare this source. Simplify or shorten it.");
} catch (const std::exception& error) {
    return juce::Result::fail("Source preparation failed: " + juce::String::fromUTF8(error.what()));
} catch (...) {
    return juce::Result::fail("Source preparation failed. Check that the file is valid and try a simpler source.");
}
}

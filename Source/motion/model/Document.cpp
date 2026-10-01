#include "../live/BlenderCaptureArchive.h"
#include "Document.h"
#include "PropertyTarget.h"
#include "PropertySchema.h"
#include "../../parser/fractal/FractalPreparation.h"
#include "CompositionGraph.h"
#include "ModulationGraph.h"
#include "LuaClipBake.h"
#include "../import/LuaBaker.h"
#include "../import/BakedSourceArchive.h"
#include "../import/RasterSourcePreparer.h"
#include "../import/VideoSourcePreparer.h"
#include "../import/MidiSourcePreparer.h"
#include <osci_file_import/osci_file_import.h>
#include <set>
#include <cstring>
#include <exception>
#include <new>
#include <sstream>
#include <iomanip>
#include <locale>
#include <charconv>
#if OSCI_PREMIUM
#include "../../parser/lottie/LottieParser.h"
#include "../../parser/lottie/DotLottieArchive.h"
#endif

namespace motion {
namespace {
using ImportShapes = std::vector<std::unique_ptr<osci::Shape>>;

juce::String exactBakeNumber(double value) {
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::setprecision(std::numeric_limits<double>::max_digits10) << value;
    return juce::String(stream.str());
}

juce::String sourceBakeKey(const Asset& asset) {
    juce::MemoryOutputStream metadata;
    metadata.writeInt(1); // Bake algorithm/context version, separate from cache wire version.
    metadata.writeDouble(asset.bakeSettings.duration);
    metadata.writeDouble(asset.bakeSettings.frameRate);
    metadata.writeDouble(asset.bakeSettings.bpm);
    metadata.writeInt64(static_cast<juce::int64>(asset.bakeSettings.pointsPerFrame));
    metadata.writeInt64(asset.bakeSettings.seed);
    metadata.write(asset.data.getData(), asset.data.getSize());
    return juce::SHA256(metadata.getData(), metadata.getDataSize()).toHexString();
}

juce::String videoBakeKey(const Asset& asset) {
    juce::MemoryOutputStream metadata;
    metadata.writeInt(1);
    metadata.writeInt(static_cast<int>(asset.rasterSettings.mode));
    metadata.writeDouble(asset.rasterSettings.threshold);
    metadata.writeBool(asset.rasterSettings.invert);
    metadata.writeInt(asset.rasterSettings.resolution);
    metadata.writeDouble(asset.rasterSettings.videoFrameRate);
    metadata.writeInt64(static_cast<juce::int64>(asset.rasterSettings.pointsPerFrame));
    metadata.write(asset.data.getData(), asset.data.getSize());
    return juce::SHA256(metadata.getData(), metadata.getDataSize()).toHexString();
}

bool importCancelled(const std::atomic<bool>* cancel) {
    return cancel != nullptr && cancel->load(std::memory_order_relaxed);
}

// repeatsPrevious(frame) lets identical frames share one drawing (and count
// once against the geometry budget).
juce::Result prepareSourceFrames(Asset& asset, int frameCount, double frameRate, const std::function<juce::Result(int, ImportShapes&)>& draw, const std::atomic<bool>* cancel, std::atomic<double>* progress, const std::function<bool(int)>& repeatsPrevious = {}) {
    if (frameCount <= 0 || static_cast<std::size_t>(frameCount) > Document::maximumSourceFrames
        || !std::isfinite(frameRate) || frameRate <= 0.0 || frameRate > 1000.0
        || !std::isfinite(frameCount / frameRate)) {
        return juce::Result::fail("Animation must contain 1-3600 frames with a frame rate between 0 and 1000 fps.");
    }
    std::vector<std::shared_ptr<const osci::PreparedDrawing>> frames;
    frames.reserve(static_cast<std::size_t>(frameCount));
    std::size_t totalShapes = 0;
    bool hasGeometry = false;
    for (int frame = 0; frame < frameCount; ++frame) {
        if (importCancelled(cancel)) {
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
        if (shapes.size() > Document::maximumShapesPerFrame || totalShapes > Document::maximumSourceShapes) {
            return juce::Result::fail("Animation exceeds the geometry budget (100000 shapes per frame or 1000000 total). Simplify or shorten the source.");
        }
        auto drawing = std::make_shared<osci::PreparedDrawing>(std::move(shapes));
        hasGeometry = hasGeometry || !drawing->empty();
        frames.push_back(std::move(drawing));
        if (progress != nullptr) {
            progress->store(static_cast<double>(frame + 1) / (frameCount + 1), std::memory_order_relaxed);
        }
    }
    if (importCancelled(cancel)) {
        return juce::Result::fail("Source import cancelled.");
    }
    if (!hasGeometry) {
        return juce::Result::fail("The source contains no drawable geometry.");
    }
    auto prepared = std::make_shared<PreparedSource>(std::move(frames), frameRate);
    asset.drawing = prepared->firstFrame();
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
    if (length > static_cast<double>(Document::maximumSourceFrames)) {
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
        return importCancelled(cancel) ? juce::Result::fail("Source preparation cancelled.") : juce::Result::ok();
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
            if (frameVertices > Document::maximumShapesPerFrame || totalVertices > Document::maximumSourceShapes) {
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

// Structural preflight protects the shared binary parser's unchecked matrix
// and stroke assumptions. Geometry decoding remains in LineArtParser.
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
        if (!tag("DONE    ") || reportedFrames <= 0 || reportedFrames > static_cast<juce::int64>(Document::maximumSourceFrames)
            || frameRate <= 0.0 || frameRate > 1000.0) {
            return false;
        }
        std::size_t totalVertices = 0;
        while (!peek("END GPLA")) {
            if (importCancelled(cancel) || frames.size() >= Document::maximumSourceFrames) {
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
                        || count < 2 || count > static_cast<juce::int64>(Document::maximumShapesPerFrame) || !tag("VERTICES")) {
                        return false;
                    }
                    frameVertices += static_cast<std::size_t>(count);
                    totalVertices += static_cast<std::size_t>(count);
                    if (frameVertices > Document::maximumShapesPerFrame || totalVertices > Document::maximumSourceShapes) {
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
            return juce::Result::fail(importCancelled(cancel) ? "Source import cancelled."
                : "Invalid or unsupported binary GPLA structure, or animation exceeds the 3600-frame / 1000000-vertex preparation budget.");
        }
        return prepareSourceFrames(asset, static_cast<int>(layout.frames.size()), layout.frameRate,
            [&](int frame, ImportShapes& shapes) {
                // Feed one validated frame to the shared parser, bounding its
                // temporary ownership and allowing cancellation between frames.
                juce::MemoryOutputStream single;
                single.write("GPLA    ", 8);
                single.writeInt64(1);
                single.writeInt64(0);
                single.writeInt64(0);
                single.write("FILE    fCount  ", 16);
                single.writeInt64(1);
                single.write("fRate   ", 8);
                single.writeInt64(static_cast<juce::int64>(layout.frameRate));
                single.write("DONE    ", 8);
                const auto range = layout.frames[static_cast<std::size_t>(frame)];
                single.write(layout.bytes + range.first, range.second);
                single.write("END GPLA", 8);
                LineArtParser parser(static_cast<const char*>(single.getData()), static_cast<int>(single.getDataSize()));
                shapes = parser.draw();
                return juce::Result::ok();
            }, cancel, progress);
    }
    const auto text = juce::String::fromUTF8(static_cast<const char*>(asset.data.getData()), static_cast<int>(asset.data.getSize()));
    const auto parsed = juce::JSON::parse(text);
    const auto framesValue = parsed.getProperty("frames", {});
    const auto* frames = framesValue.getArray();
    if (frames == nullptr || frames->isEmpty() || static_cast<std::size_t>(frames->size()) > Document::maximumSourceFrames) {
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
            auto lines = LineArtParser::generateFrame(*objects.getArray(), static_cast<double>(item.getProperty("focalLength", {})));
            shapes.reserve(lines.size());
            for (auto& line : lines) {
                shapes.push_back(line.clone());
            }
            return juce::Result::ok();
        }, cancel, progress);
}

void saveProperty(juce::XmlElement& item, const std::string& name, const Curve& curve) {
    auto* property = item.createNewChildElement("property");
    property->setAttribute("name", juce::String(name));
    property->setAttribute("base", exactBakeNumber(curve.base));
    auto* modulation = property->createNewChildElement("modulation");
    modulation->setAttribute("enabled", curve.modulation.enabled);
    modulation->setAttribute("waveform", static_cast<int>(curve.modulation.waveform));
    modulation->setAttribute("amount", exactBakeNumber(curve.modulation.amount));
    modulation->setAttribute("rateHz", exactBakeNumber(curve.modulation.rateHz));
    modulation->setAttribute("phase", exactBakeNumber(curve.modulation.phase));
    modulation->setAttribute("tempoSync", curve.modulation.tempoSync);
    modulation->setAttribute("beatsPerCycle", exactBakeNumber(curve.modulation.beatsPerCycle));
    modulation->setAttribute("seed", juce::String(static_cast<juce::int64>(curve.modulation.seed)));
    modulation->setAttribute("mode", static_cast<int>(curve.modulation.mode));
    if (curve.link.has_value()) {
        auto* link = property->createNewChildElement("link");
        link->setAttribute("source", juce::String(curve.link->source));
        link->setAttribute("property", juce::String(curve.link->property));
        link->setAttribute("scale", exactBakeNumber(curve.link->scale));
        link->setAttribute("offset", exactBakeNumber(curve.link->offset));
        link->setAttribute("delay", exactBakeNumber(curve.link->delay));
    }
    for (const auto& key : curve.keyframes()) {
        auto* point = property->createNewChildElement("key");
        point->setAttribute("time", exactBakeNumber(key.time));
        point->setAttribute("value", exactBakeNumber(key.value));
        point->setAttribute("interpolation", static_cast<int>(key.interpolation));
        point->setAttribute("in", exactBakeNumber(key.incomingSlope));
        point->setAttribute("out", exactBakeNumber(key.outgoingSlope));
        if (key.incomingInfluence != Keyframe::defaultInfluence) { point->setAttribute("inInfluence", exactBakeNumber(key.incomingInfluence)); }
        if (key.outgoingInfluence != Keyframe::defaultInfluence) { point->setAttribute("outInfluence", exactBakeNumber(key.outgoingInfluence)); }
    }
}

juce::Result loadProperty(const juce::XmlElement& property, Curve& curve) {
    curve = Curve(property.getDoubleAttribute("base"));
    if (!std::isfinite(curve.base)) {
        return juce::Result::fail("Invalid property value.");
    }
    bool hasModulation = false;
    for (auto* item : property.getChildWithTagNameIterator("modulation")) {
        const auto enabled = item->getIntAttribute("enabled");
        const auto tempoSync = item->getIntAttribute("tempoSync");
        const auto seed = item->getStringAttribute("seed", "0").getLargeIntValue();
        if (hasModulation || enabled < 0 || enabled > 1 || tempoSync < 0 || tempoSync > 1
            || seed < 0 || seed > static_cast<juce::int64>(std::numeric_limits<std::uint32_t>::max())) {
            return juce::Result::fail("Invalid or duplicate curve modulation settings.");
        }
        hasModulation = true;
        auto& modulation = curve.modulation;
        modulation.enabled = enabled != 0;
        modulation.waveform = static_cast<ModulationWaveform>(item->getIntAttribute("waveform"));
        modulation.amount = item->getDoubleAttribute("amount", 0.25);
        modulation.rateHz = item->getDoubleAttribute("rateHz", 1);
        modulation.phase = item->getDoubleAttribute("phase", 0);
        modulation.tempoSync = tempoSync != 0;
        modulation.beatsPerCycle = item->getDoubleAttribute("beatsPerCycle", 1);
        modulation.seed = static_cast<std::uint32_t>(seed);
        modulation.mode = static_cast<ModulationMode>(item->getIntAttribute("mode"));
        if (!modulation.valid()) {
            return juce::Result::fail("Modulation requires a known waveform/mode, finite amount, 0.001-1000 Hz, phase 0-1, and 0.0625-64 beats per cycle.");
        }
    }
    for (auto* item : property.getChildWithTagNameIterator("link")) {
        PropertyLink link;
        const auto source = item->getStringAttribute("source").getLargeIntValue();
        link.source = source > 0 ? static_cast<Id>(source) : 0;
        link.property = item->getStringAttribute("property").toStdString();
        link.scale = item->getDoubleAttribute("scale", 1);
        link.offset = item->getDoubleAttribute("offset", 0);
        link.delay = item->getDoubleAttribute("delay", 0);
        if (curve.link.has_value() || !link.valid()) { return juce::Result::fail("Invalid or duplicate property link."); }
        curve.link = std::move(link);
    }
    for (auto* point : property.getChildWithTagNameIterator("key")) {
        const auto interpolation = point->getIntAttribute("interpolation");
        if (interpolation < 0 || interpolation > 3) {
            return juce::Result::fail("Unknown interpolation.");
        }
        try {
            curve.setKey({ point->getDoubleAttribute("time"), point->getDoubleAttribute("value"),
                static_cast<Interpolation>(interpolation), point->getDoubleAttribute("in"), point->getDoubleAttribute("out"),
                point->getDoubleAttribute("inInfluence", Keyframe::defaultInfluence), point->getDoubleAttribute("outInfluence", Keyframe::defaultInfluence) });
        } catch (const std::invalid_argument&) {
            return juce::Result::fail("Invalid animation key.");
        }
    }
    return juce::Result::ok();
}
void saveEffects(juce::XmlElement& owner, const std::vector<EffectInstance>& effects) {
    for (const auto& effect : effects) {
        auto* item = owner.createNewChildElement("effect");
        item->setAttribute("id", juce::String(effect.id));
        item->setAttribute("type", juce::String(effect.type));
        item->setAttribute("name", juce::String(effect.name));
        item->setAttribute("enabled", effect.enabled);
        if (effect.range.has_value()) {
            item->setAttribute("start", exactBakeNumber(effect.range->start));
            item->setAttribute("duration", exactBakeNumber(effect.range->duration));
        }
        for (const auto& [name, curve] : effect.properties) {
            saveProperty(*item, name, curve);
        }
    }
}

juce::Result loadEffects(const juce::XmlElement& owner, std::vector<EffectInstance>& effects, std::set<Id>& identities) {
    for (auto* item : owner.getChildWithTagNameIterator("effect")) {
        const auto identity = item->getStringAttribute("id").getLargeIntValue();
        const auto* definition = effectDefinition(item->getStringAttribute("type").toStdString());
        if (identity <= 0 || definition == nullptr || !identities.insert(static_cast<Id>(identity)).second) {
            return juce::Result::fail("Unknown effect type or invalid / duplicate effect identity.");
        }
        if (effects.size() >= maximumEffectsPerOwner) {
            return juce::Result::fail("Each clip, track or composition supports at most 64 effects.");
        }
        auto effect = makeEffect(static_cast<Id>(identity), *definition);
        effect.name = item->getStringAttribute("name", juce::String(definition->name)).toStdString();
        effect.enabled = item->getBoolAttribute("enabled", true);
        if (item->hasAttribute("start") || item->hasAttribute("duration")) {
            if (!item->hasAttribute("start") || !item->hasAttribute("duration")) {
                return juce::Result::fail("Effect time ranges require both start and duration.");
            }
            effect.range = EffectRange { item->getDoubleAttribute("start"), item->getDoubleAttribute("duration") };
        }
        std::set<std::string> properties;
        for (auto* property : item->getChildWithTagNameIterator("property")) {
            const auto name = property->getStringAttribute("name").toStdString();
            const auto found = effect.properties.find(name);
            if (found == effect.properties.end() || !properties.insert(name).second) {
                return juce::Result::fail("Unknown or duplicate effect parameter.");
            }
            const auto result = loadProperty(*property, found->second);
            if (result.failed()) {
                return result;
            }
        }
        if (properties.size() != effect.properties.size() || !effect.valid()) {
            return juce::Result::fail("Invalid effect range or parameter value.");
        }
        effects.push_back(std::move(effect));
    }
    return juce::Result::ok();
}

}

// Approximate bytes a snapshot holds by value (curves, keys, clips, tracks).
// Shared, immutable payloads are counted separately, per step, only when that
// step alone keeps them alive.
static std::size_t curveBytes(const Curve& curve) {
    return sizeof(Curve) + curve.keyframes().size() * (sizeof(Keyframe) + sizeof(double));
}
static std::size_t propertyBytes(const std::map<std::string, Curve>& properties) {
    std::size_t bytes = 0;
    for (const auto& [name, curve] : properties) { bytes += name.size() + curveBytes(curve); }
    return bytes;
}
static std::size_t compositionBytes(const Composition& composition) {
    std::size_t bytes = sizeof(Composition);
    const auto effects = [&](const std::vector<EffectInstance>& list) {
        for (const auto& effect : list) { bytes += sizeof(EffectInstance) + propertyBytes(effect.properties); }
    };
    effects(composition.effects);
    for (const auto& group : composition.groups) { bytes += sizeof(Group) + propertyBytes(group.properties); effects(group.effects); }
    for (const auto& track : composition.tracks) {
        bytes += sizeof(Track);
        effects(track.effects);
        for (const auto& clip : track.clips) { bytes += sizeof(Clip) + clip.name.size() + propertyBytes(clip.properties); effects(clip.effects); }
    }
    for (const auto& camera : composition.cameras) { bytes += sizeof(Camera) + propertyBytes(camera.properties); }
    bytes += composition.markers.size() * sizeof(Marker) + composition.cameraCuts.size() * sizeof(CameraCut);
    bytes += composition.modulators.size() * sizeof(Modulator) + composition.routes.size() * sizeof(ModulationRoute);
    return bytes;
}
static std::size_t assetBytes(const Asset& asset) {
    // Encoded data plus a rough allowance for the prepared frames.
    return sizeof(Asset) + asset.data.getSize() + 2 * asset.bakedData.getSize() + 4 * asset.data.getSize();
}

struct Document::Change : juce::UndoableAction {
    Change(Document& owner, Project before, Project after) : owner(owner), before(std::move(before)), after(std::move(after)) {}
    bool perform() override { owner.apply(after); return true; }
    bool undo() override { owner.apply(before); return true; }
    // KiB, so the undo manager can bound history by memory as well as count.
    int getSizeInUnits() override {
        auto bytes = compositionBytes(before) + compositionBytes(after);
        const auto unique = [&](const auto& mine, const auto& theirs, const auto& size) {
            for (const auto& item : mine) {
                if (item != nullptr && std::find(theirs.begin(), theirs.end(), item) == theirs.end()) { bytes += size(*item); }
            }
        };
        unique(before.assets, after.assets, [](const Asset& asset) { return assetBytes(asset); });
        unique(after.assets, before.assets, [](const Asset& asset) { return assetBytes(asset); });
        unique(before.definitions, after.definitions, [](const Composition& definition) { return compositionBytes(definition); });
        unique(after.definitions, before.definitions, [](const Composition& definition) { return compositionBytes(definition); });
        const auto bakes = [](const Project& project) {
            std::vector<std::shared_ptr<const LuaClipBake>> result;
            for (const auto& track : project.tracks) {
                for (const auto& clip : track.clips) { if (clip.luaBake != nullptr) { result.push_back(clip.luaBake); } }
            }
            return result;
        };
        const auto bakeBytes = [](const LuaClipBake& bake) { return 3 * bake.archive.getSize(); };
        unique(bakes(before), bakes(after), bakeBytes);
        unique(bakes(after), bakes(before), bakeBytes);
        return static_cast<int>(std::clamp<std::size_t>(bytes / 1024, 1, static_cast<std::size_t>(std::numeric_limits<int>::max() / 4)));
    }
    Document& owner;
    Project before, after;
};

Project Document::mergeScope(Project view) const {
    if (scopeId == 0) { return view; }
    auto whole = state;
    whole.scope = view.scope;
    whole.assets = std::move(view.assets);
    whole.definitions = std::move(view.definitions);
    const auto found = std::find_if(whole.definitions.begin(), whole.definitions.end(), [this](const auto& value) { return value->id == scopeId; });
    if (found != whole.definitions.end()) {
        auto replacement = std::make_shared<CompositionDefinition>();
        static_cast<Composition&>(*replacement) = std::move(static_cast<Composition&>(view));
        replacement->id = scopeId;
        *found = std::move(replacement);
    }
    return whole;
}

void Document::refreshScope() {
    if (scopeId == 0) { scopeView = {}; return; }
    const auto found = std::find_if(state.definitions.begin(), state.definitions.end(), [this](const auto& value) { return value->id == scopeId; });
    if (found == state.definitions.end()) { scopeId = 0; ++projectGeneration; scopeView = {}; return; }
    scopeView = state;
    static_cast<Composition&>(scopeView) = **found;
}

juce::Result Document::setMarker(Id id, double time, juce::String name) {
    name = name.trim();
    if (!std::isfinite(time) || time < 0 || time > project().duration) { return juce::Result::fail("Place the marker within the composition duration."); }
    if (name.isEmpty() || name.length() > 120 || name.containsChar('\n') || name.containsChar('\r')) { return juce::Result::fail("Use a marker name of 1-120 characters on one line."); }
    const auto& markers = project().markers;
    const auto found = std::find_if(markers.begin(), markers.end(), [id](const auto& marker) { return marker.id == id; });
    const bool adding = id == 0;
    if (std::any_of(markers.begin(), markers.end(), [id, time](const auto& marker) { return marker.id != id && std::abs(marker.time - time) < 1.0e-9; })) { return juce::Result::fail("A marker already exists at this position."); }
    if (!adding && found == markers.end()) { return juce::Result::fail("The marker no longer exists."); }
    if (!adding && found->time == time && found->name == name) { return juce::Result::ok(); }
    if (adding) {
        const auto highest = highestId();
        if (highest >= static_cast<Id>(std::numeric_limits<juce::int64>::max())) { return juce::Result::fail("No marker identities remain."); }
        id = highest + 1;
        lastId = id;
    }
    edit(adding ? "Add marker" : "Edit marker", [id, time, name, adding](Project& project) {
        if (adding) { project.markers.push_back({id, time, name}); }
        else {
            for (auto& marker : project.markers) { if (marker.id == id) { marker.time = time; marker.name = name; } }
        }
        std::sort(project.markers.begin(), project.markers.end(), [](const auto& a, const auto& b) { return a.time != b.time ? a.time < b.time : a.id < b.id; });
    });
    return juce::Result::ok();
}

juce::Result Document::removeMarker(Id id) {
    const auto& markers = project().markers;
    if (std::none_of(markers.begin(), markers.end(), [id](const auto& marker) { return marker.id == id; })) { return juce::Result::fail("The marker no longer exists."); }
    edit("Delete marker", [id](Project& project) { std::erase_if(project.markers, [id](const auto& marker) { return marker.id == id; }); });
    return juce::Result::ok();
}

juce::Result Document::enterComposition(Id id) {
    if (id == scopeId) { return juce::Result::ok(); }
    if (id != 0 && std::none_of(state.definitions.begin(), state.definitions.end(), [id](const auto& value) { return value->id == id; })) {
        return juce::Result::fail("The composition no longer exists.");
    }
    scopeId = id; ++projectGeneration; ++stateRevision;
    refreshScope();
    if (onChanged) { onChanged(); }
    sendChangeMessage();
    return juce::Result::ok();
}

// Snapshots taken before a slider bake landed (undo history, gesture
// previews) carry the installed bake forward; the baker re-checks its key.
static void carryLuaBakes(Project& next, const Project& current) {
    std::map<Id, std::shared_ptr<const LuaClipBake>> installed;
    const auto collect = [&](const Composition& composition) {
        for (const auto& track : composition.tracks) {
            for (const auto& clip : track.clips) { if (clip.luaBake != nullptr) { installed.emplace(clip.id, clip.luaBake); } }
        }
    };
    collect(current);
    for (const auto& definition : current.definitions) { if (definition != nullptr) { collect(*definition); } }
    if (installed.empty()) { return; }
    const auto carry = [&](Composition& composition) {
        bool changed = false;
        for (auto& track : composition.tracks) {
            for (auto& clip : track.clips) {
                const auto found = installed.find(clip.id);
                if (clip.luaBake == nullptr && found != installed.end()) { clip.luaBake = found->second; changed = true; }
            }
        }
        return changed;
    };
    carry(next);
    for (auto& definition : next.definitions) {
        if (definition == nullptr) { continue; }
        auto copy = std::make_shared<CompositionDefinition>(*definition);
        if (carry(*copy)) { definition = std::move(copy); }
    }
}

// A loop never reaches past the composition; one that no longer fits is dropped.
static void clampLoop(Project& project) {
    if (!project.hasLoop()) { return; }
    project.loopEnd = std::min(project.loopEnd, project.duration);
    if (!project.hasLoop()) { project.loopStart = project.loopEnd = 0; project.looping = false; }
}

// Track heights are view state: every snapshot shows the current heights, so
// undo and redo never resize rows. A new document starts from its own.
static void carryTrackHeights(Project& next, const Project& current) {
    std::map<Id, int> heights;
    const auto collect = [&](const Composition& composition) { for (const auto& track : composition.tracks) { heights[track.id] = track.height; } };
    collect(current);
    for (const auto& definition : current.definitions) { if (definition != nullptr) { collect(*definition); } }
    const auto differs = [&](const Composition& composition) {
        return std::any_of(composition.tracks.begin(), composition.tracks.end(), [&](const auto& track) {
            const auto found = heights.find(track.id);
            return found != heights.end() && found->second != track.height;
        });
    };
    const auto carry = [&](Composition& composition) {
        for (auto& track : composition.tracks) {
            const auto found = heights.find(track.id);
            if (found != heights.end()) { track.height = found->second; }
        }
    };
    if (differs(next)) { carry(next); }
    for (auto& definition : next.definitions) {
        // Shared definitions are copied only when a height actually differs.
        if (definition == nullptr || !differs(*definition)) { continue; }
        auto copy = std::make_shared<CompositionDefinition>(*definition);
        carry(*copy);
        definition = std::move(copy);
    }
}

void Document::apply(Project value) {
    ++stateRevision;
    if (carryView) {
        carryTrackHeights(value, state);
        carryLuaBakes(value, state);
    }
    state = std::move(value);
    refreshScope();
    if (onChanged) {
        onChanged();
    }
    sendChangeMessage();
}

void Document::preview(Project project) {
    pruneReferences(project);
    clampLoop(project);
    apply(mergeScope(std::move(project)));
}

void Document::edit(juce::String label, std::function<void(Project&)> operation) {
    auto after = project();
    operation(after);
    pruneReferences(after);
    clampLoop(after);
    after = mergeScope(std::move(after));
    undo.beginNewTransaction(label);
    undo.perform(new Change(*this, state, std::move(after)));
}

void Document::editCoalesced(juce::String label, const juce::String& control, std::function<void(Project&)> operation) {
    const auto now = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    const bool joins = control.isNotEmpty() && control == coalescingControl && revision() == coalescingRevision && now - coalescingTime < 1.0;
    auto after = project();
    operation(after);
    pruneReferences(after);
    clampLoop(after);
    after = mergeScope(std::move(after));
    if (!joins) { undo.beginNewTransaction(label); }
    undo.perform(new Change(*this, state, std::move(after)));
    coalescingControl = control;
    coalescingRevision = revision();
    coalescingTime = now;
}

bool Document::tryEdit(juce::String label, std::function<bool(Project&)> operation) {
    auto after = project();
    if (!operation(after)) { return false; }
    pruneReferences(after);
    clampLoop(after);
    after = mergeScope(std::move(after));
    undo.beginNewTransaction(label);
    undo.perform(new Change(*this, state, std::move(after)));
    return true;
}

void Document::commit(juce::String label, Project before) {
    undo.beginNewTransaction(label);
    undo.perform(new Change(*this, mergeScope(std::move(before)), state));
}

void Document::reset(Project project) {
    scopeId = 0;
    ++projectGeneration;
    undo.clearUndoHistory();
    lastId = highestProjectIdentity(project, lastId);
    // A different document: nothing carries over from the previous one.
    carryView = false;
    apply(std::move(project));
    carryView = true;
}

// In musical time, project-time items keep their beat when the tempo map
// changes: markers, cuts, effect ranges and project-level keys (with slopes
// scaled by the local tempo ratio), and the composition's length in bars.
static void keepBeats(Project& next, const Tempo& before, const Tempo& after) {
    const auto remap = [&](double time) { return after.seconds(before.beats(time)); };
    const auto stretch = [&](double time) { return before.bpmAt(time) / after.bpmAt(remap(time)); };
    const auto scaleCurve = [&](Curve& curve) {
        auto keys = curve.keyframes();
        for (const auto& key : keys) { curve.removeKey(key.time); }
        for (auto key : keys) {
            const auto ratio = stretch(key.time);
            key.time = remap(key.time);
            key.incomingSlope /= ratio;
            key.outgoingSlope /= ratio;
            curve.setKey(key);
        }
    };
    const auto scaleEffects = [&](std::vector<EffectInstance>& effects) {
        for (auto& effect : effects) {
            for (auto& [name, curve] : effect.properties) { scaleCurve(curve); }
            if (effect.range.has_value()) {
                const auto end = remap(effect.range->end());
                effect.range->start = remap(effect.range->start);
                effect.range->duration = end - effect.range->start;
            }
        }
    };
    next.duration = remap(next.duration);
    if (next.hasLoop()) { next.loopStart = remap(next.loopStart); next.loopEnd = remap(next.loopEnd); }
    for (auto& marker : next.markers) { marker.time = std::min(remap(marker.time), next.duration); }
    for (auto& cut : next.cameraCuts) {
        const auto end = remap(cut.end());
        cut.start = remap(cut.start);
        cut.duration = spanUntil(cut.start, end);
    }
    for (auto& camera : next.cameras) { for (auto& [name, curve] : camera.properties) { scaleCurve(curve); } }
    for (auto& group : next.groups) {
        for (auto& [name, curve] : group.properties) { scaleCurve(curve); }
        scaleEffects(group.effects);
    }
    for (auto& track : next.tracks) { scaleEffects(track.effects); }
    scaleEffects(next.effects);
}

juce::Result Document::setTempoChange(double beat, double bpm, std::optional<double> replacing, std::optional<bool> ramp) {
    const auto& current = project();
    std::vector<TempoChange> changes;
    if (current.tempoChanges != nullptr) { changes = *current.tempoChanges; }
    // An edit keeps the change's ramp unless told otherwise.
    const auto previous = std::find_if(changes.begin(), changes.end(), [&](const auto& change) { return change.beat == replacing.value_or(beat); });
    const auto glide = ramp.value_or(previous != changes.end() && previous->ramp);
    if (replacing.has_value()) { std::erase_if(changes, [&](const auto& change) { return change.beat == *replacing; }); }
    std::erase_if(changes, [beat](const auto& change) { return change.beat == beat; });
    changes.push_back({beat, bpm, glide});
    std::sort(changes.begin(), changes.end(), [](const auto& a, const auto& b) { return a.beat < b.beat; });
    auto shared = std::make_shared<const std::vector<TempoChange>>(std::move(changes));
    if (!Tempo(current.bpm, shared).valid()) { return juce::Result::fail("A tempo change needs a position after the start and 1-1000 BPM."); }
    return retempo(std::move(shared), replacing.has_value() ? "Change tempo change" : "Add tempo change");
}

juce::Result Document::removeTempoChange(double beat) {
    const auto& current = project();
    if (current.tempoChanges == nullptr) { return juce::Result::fail("There is no tempo change there."); }
    auto changes = *current.tempoChanges;
    const auto before = changes.size();
    std::erase_if(changes, [beat](const auto& change) { return change.beat == beat; });
    if (changes.size() == before) { return juce::Result::fail("There is no tempo change there."); }
    return retempo(changes.empty() ? nullptr : std::make_shared<const std::vector<TempoChange>>(std::move(changes)), "Remove tempo change");
}

// Musical clips follow the new map; overlapping results are refused.
juce::Result Document::retempo(std::shared_ptr<const std::vector<TempoChange>> changes, juce::String label) {
    return setTempoMap(project().bpm, std::move(changes), std::move(label));
}

juce::Result Document::changeTempo(double bpm) {
    if (!std::isfinite(bpm) || bpm < 1 || bpm > 1000) { return juce::Result::fail("Tempo must be between 1 and 1000 BPM."); }
    if (bpm == project().bpm) { return juce::Result::ok(); }
    return setTempoMap(bpm, project().tempoChanges, "Change tempo");
}

// Builds the project under a new tempo map: project-time items keep their
// beats in musical display, clips re-sort and overlaps are refused.
static juce::Result retimed(const Project& state, double initialBpm, std::shared_ptr<const std::vector<TempoChange>> changes, Project& next) {
    if (changes != nullptr && changes->empty()) { changes.reset(); }
    next = state;
    next.bpm = initialBpm;
    next.tempoChanges = std::move(changes);
    const auto before = state.tempo();
    const auto after = next.tempo();
    if (!after.valid()) { return juce::Result::fail("Tempo must be between 1 and 1000 BPM, with changes after the start at increasing beats."); }
    // In musical time, everything placed in project time keeps its bar
    // position, like beat-anchored clips. Seconds projects keep seconds.
    if (state.timeDisplay == TimeDisplay::beats) { keepBeats(next, before, after); }
    for (auto& track : next.tracks) {
        std::sort(track.clips.begin(), track.clips.end(), [&after](const auto& a, const auto& b) { return a.timing(after).start < b.timing(after).start; });
        double previousEnd = 0;
        for (const auto& clip : track.clips) {
            const auto timing = clip.timing(after);
            if (!clip.valid() || !timing.valid() || timing.start < previousEnd) {
                return juce::Result::fail("That tempo would overlap clips on " + juce::String(track.name) + ". Move them apart or onto separate tracks first.");
            }
            previousEnd = timing.end();
            next.duration = std::max(next.duration, previousEnd);
        }
    }
    return juce::Result::ok();
}

juce::Result Document::setTempoMap(double initialBpm, std::shared_ptr<const std::vector<TempoChange>> changes, juce::String label, bool showBars) {
    Project next;
    const auto result = retimed(project(), initialBpm, std::move(changes), next);
    if (result.failed()) { return result; }
    if (showBars) { next.timeDisplay = TimeDisplay::beats; }
    edit(label, [next = std::move(next)](Project& project) { project = next; });
    return juce::Result::ok();
}

juce::Result Document::setTempoFromAudio(Id clipId, double bpm, double downbeat, double& moved) {
    moved = 0;
    Project next;
    const auto result = retimed(project(), bpm, nullptr, next);
    if (result.failed()) { return result; }
    const auto tempo = next.tempo();
    for (auto& track : next.tracks) {
        for (auto& clip : track.clips) {
            if (clip.id != clipId) { continue; }
            if (track.locked) { return juce::Result::fail("Unlock the soundtrack's track first."); }
            // Move the clip later (never cutting audio) until its first
            // downbeat sits on a bar line; a pickup lands in the bar before.
            auto timing = clip.timing(tempo);
            const auto bar = 60 / bpm * std::max(1, next.beatsPerBar);
            const auto shift = std::fmod(timing.projectTime(downbeat), bar);
            if (shift > 0.005 && bar - shift > 0.005) {
                moved = bar - shift;
                timing.moveTo(timing.start + moved);
                auto placed = clip;
                if (!placed.setTiming(timing, tempo) || !track.canPlace(placed, clip.id, tempo)) {
                    return juce::Result::fail("There is no room to move the soundtrack onto the bar grid.");
                }
                clip = placed;
                next.duration = std::max(next.duration, timing.end());
            }
            // A tempo taken from the music is for working in bars.
            next.timeDisplay = TimeDisplay::beats;
            edit("Set tempo from soundtrack", [next = std::move(next)](Project& project) { project = next; });
            return juce::Result::ok();
        }
    }
    return juce::Result::fail("The soundtrack clip no longer exists.");
}

Id Document::highestId() const {
    return highestProjectIdentity(state, lastId);
}

juce::Result Document::makeSourceUnique(Id clipId, const std::shared_ptr<const Asset>& expected, const std::shared_ptr<Asset>& copy) {
    const auto& state = project();
    if (expected == nullptr || copy == nullptr || expected == copy || std::find(state.assets.begin(), state.assets.end(), copy) != state.assets.end()
        || std::find(state.assets.begin(), state.assets.end(), expected) == state.assets.end()) {
        return juce::Result::fail("The source changed while preparing its copy.");
    }
    const auto references = sourceReferenceCount(mainProject(), expected->id);
    if (references < 2) { return juce::Result::fail("This clip already has its own source."); }
    for (std::size_t trackIndex = 0; trackIndex < state.tracks.size(); ++trackIndex) {
        const auto& track = state.tracks[trackIndex];
        for (std::size_t clipIndex = 0; clipIndex < track.clips.size(); ++clipIndex) {
            const auto& clip = track.clips[clipIndex];
            if (clip.id != clipId) { continue; }
            if (track.locked || clip.asset != expected->id) { return juce::Result::fail("The selected clip is locked or its source has changed."); }
            const auto highest = highestId();
            if (highest == std::numeric_limits<Id>::max()) { return juce::Result::fail("There are no remaining source identities."); }
            if (copy->liveIdentity != nullptr) { copy->liveIdentity = std::make_shared<const LiveSourceIdentity>(); }
            copy->id = highest + 1;
            copy->name = copy->liveIdentity != nullptr ? expected->name + " copy" : expected->name.upToLastOccurrenceOf(".", false, false) + " copy " + juce::String(static_cast<juce::uint64>(copy->id)) + expected->extension;
            lastId = copy->id;
            edit("Make source unique", [trackIndex, clipIndex, copy](Project& project) {
                project.assets.push_back(copy);
                project.tracks[trackIndex].clips[clipIndex].asset = copy->id;
            });
            return juce::Result::ok();
        }
    }
    return juce::Result::fail("The selected clip no longer exists.");
}

juce::Result Document::duplicateClip(Id sourceId, Id& duplicateId) {
    duplicateId = 0;
    std::vector<Id> copies;
    const auto result = duplicateClips({sourceId}, copies);
    if (result.wasOk()) { duplicateId = copies.front(); }
    return result;
}

std::size_t Document::assetUses(Id assetId) const {
    const auto& whole = mainProject();
    auto count = sourceReferenceCount(whole, assetId);
    const auto midi = [&](const auto& composition) {
        for (const auto& track : composition.tracks) {
            for (const auto& clip : track.clips) { if (clip.midiAsset == assetId && assetId != 0) { ++count; } }
        }
    };
    midi(whole);
    for (const auto& definition : whole.definitions) { if (definition != nullptr) { midi(*definition); } }
    return count;
}

bool Document::setTrackHeight(Id trackId, int height) {
    height = height == 0 ? 0 : std::clamp(height, Track::minimumHeight, Track::maximumHeight);
    bool found = false;
    const auto update = [&](Composition& composition) {
        bool changed = false;
        for (auto& track : composition.tracks) {
            if (track.id == trackId) { found = true; changed = track.height != height; track.height = height; }
        }
        return changed;
    };
    auto next = state;
    bool changed = update(next);
    for (auto& definition : next.definitions) {
        const auto owns = definition != nullptr && std::any_of(definition->tracks.begin(), definition->tracks.end(), [trackId](const auto& track) { return track.id == trackId; });
        if (!owns) { continue; }
        auto copy = std::make_shared<CompositionDefinition>(*definition);
        if (update(*copy)) { definition = std::move(copy); changed = true; }
    }
    if (!found || !changed) { return found; }
    // View state: no undo step, no revision bump and no change broadcast (it
    // affects neither playback nor any other view); the timeline relayouts
    // itself and the next save writes it.
    state = std::move(next);
    refreshScope();
    return true;
}

// Several heights with one copy of the project.
void Document::setTrackHeights(const std::vector<std::pair<Id, int>>& heights) {
    if (heights.empty()) { return; }
    std::map<Id, int> wanted;
    for (const auto& [id, height] : heights) { wanted[id] = height == 0 ? 0 : std::clamp(height, Track::minimumHeight, Track::maximumHeight); }
    const auto update = [&](Composition& composition) {
        bool changed = false;
        for (auto& track : composition.tracks) {
            const auto found = wanted.find(track.id);
            if (found != wanted.end() && track.height != found->second) { track.height = found->second; changed = true; }
        }
        return changed;
    };
    auto next = state;
    bool changed = update(next);
    for (auto& definition : next.definitions) {
        if (definition == nullptr) { continue; }
        const auto owns = std::any_of(definition->tracks.begin(), definition->tracks.end(), [&](const auto& track) { return wanted.contains(track.id); });
        if (!owns) { continue; }
        auto copy = std::make_shared<CompositionDefinition>(*definition);
        if (update(*copy)) { definition = std::move(copy); changed = true; }
    }
    if (!changed) { return; }
    state = std::move(next);
    refreshScope();
}

bool Document::setLuaBake(Id clipId, std::shared_ptr<const LuaClipBake> bake) {
    // A cache, not an edit: no undo step and no revision-guarded gesture is
    // disturbed beyond a normal state refresh.
    bool found = false;
    const auto update = [&](auto& composition) {
        for (auto& track : composition.tracks) {
            for (auto& clip : track.clips) {
                if (clip.id == clipId) { clip.luaBake = bake; found = true; }
            }
        }
    };
    auto next = state;
    update(next);
    for (auto& definition : next.definitions) {
        if (definition == nullptr) { continue; }
        auto copy = std::make_shared<CompositionDefinition>(*definition);
        const auto before = found;
        update(*copy);
        if (found != before) { definition = std::move(copy); }
    }
    if (!found) { return false; }
    // A cache: publish the new frames without an edit's revision, so open
    // gestures, coalescing and change guards are undisturbed.
    state = std::move(next);
    refreshScope();
    if (onChanged) { onChanged(); }
    sendChangeMessage();
    return true;
}

juce::Result Document::replaceAsset(Id assetId, std::shared_ptr<const Asset> replacement) {
    const auto& assets = project().assets;
    const auto found = std::find_if(assets.begin(), assets.end(), [assetId](const auto& asset) { return asset != nullptr && asset->id == assetId; });
    if (found == assets.end()) { return juce::Result::fail("The source no longer exists."); }
    if (replacement == nullptr || replacement->id != assetId) { return juce::Result::fail("Invalid replacement source."); }
    const auto audio = [](const Asset& asset) { return asset.audio != nullptr; };
    const auto midiOnly = [](const Asset& asset) { return asset.midi != nullptr && asset.drawing == nullptr && asset.source == nullptr; };
    if (audio(**found) != audio(*replacement) || midiOnly(**found) || midiOnly(*replacement)) {
        return juce::Result::fail(audio(**found) ? "Replace a soundtrack with another audio file." : "Replace a visual source with another visual file.");
    }
    const bool lua = replacement->extension.equalsIgnoreCase(".lua");
    edit("Replace source", [assetId, replacement, lua](Project& project) {
        for (auto& asset : project.assets) {
            if (asset != nullptr && asset->id == assetId) { asset = replacement; }
        }
        // Slider curves and their bake belong to the old script.
        const auto clear = [&](Composition& composition) {
            for (auto& track : composition.tracks) {
                for (auto& clip : track.clips) {
                    if (clip.asset != assetId) { continue; }
                    clip.luaBake.reset();
                    if (!lua) { std::erase_if(clip.properties, [](const auto& item) { return item.first.starts_with("slider."); }); }
                }
            }
        };
        clear(project);
        for (auto& definition : project.definitions) {
            if (definition == nullptr) { continue; }
            auto copy = std::make_shared<CompositionDefinition>(*definition);
            clear(*copy);
            definition = std::move(copy);
        }
    });
    return juce::Result::ok();
}

juce::Result Document::renameAsset(Id assetId, juce::String name) {
    name = name.trim();
    if (name.isEmpty() || name.length() > 200 || name.containsAnyOf("\r\n")) { return juce::Result::fail("Use a source name of 1-200 characters on one line."); }
    const auto& assets = mainProject().assets;
    const auto found = std::find_if(assets.begin(), assets.end(), [assetId](const auto& asset) { return asset != nullptr && asset->id == assetId; });
    if (found == assets.end()) { return juce::Result::fail("The source no longer exists."); }
    if ((*found)->name == name) { return juce::Result::ok(); }
    edit("Rename source", [assetId, name](Project& project) {
        for (auto& asset : project.assets) {
            if (asset != nullptr && asset->id == assetId) {
                auto renamed = std::make_shared<Asset>(*asset);
                renamed->name = name;
                asset = std::move(renamed);
            }
        }
    });
    return juce::Result::ok();
}

juce::Result Document::removeUnusedAssets(std::vector<Id> assetIds, int& removed) {
    removed = 0;
    const auto& assets = mainProject().assets;
    if (assetIds.empty()) {
        for (const auto& asset : assets) { if (asset != nullptr) { assetIds.push_back(asset->id); } }
    }
    std::set<Id> unused;
    for (const auto id : assetIds) {
        if (std::none_of(assets.begin(), assets.end(), [id](const auto& asset) { return asset != nullptr && asset->id == id; })) { continue; }
        if (assetUses(id) == 0) { unused.insert(id); }
    }
    if (unused.empty()) { return juce::Result::fail("Only sources that no clip uses can be removed."); }
    removed = static_cast<int>(unused.size());
    edit(unused.size() > 1 ? "Remove unused sources" : "Remove source", [&unused](Project& project) {
        std::erase_if(project.assets, [&](const auto& asset) { return asset != nullptr && unused.contains(asset->id); });
    });
    return juce::Result::ok();
}

juce::Result Document::pasteClips(const std::vector<CopiedClip>& clips, double time, std::vector<Id>& pastedIds) {
    pastedIds.clear();
    const auto& state = project();
    if (clips.empty()) { return juce::Result::fail("The clipboard has no clips."); }
    if (!std::isfinite(time) || time < 0) { return juce::Result::fail("Paste at a valid position."); }
    double first = std::numeric_limits<double>::infinity();
    for (const auto& copied : clips) {
        if (!copied.clip.valid()) { return juce::Result::fail("A copied clip is no longer valid."); }
        if (copied.clip.asset != 0 && std::none_of(state.assets.begin(), state.assets.end(), [&](const auto& asset) { return asset != nullptr && asset->id == copied.clip.asset; })) {
            return juce::Result::fail("A copied clip's source is not in this composition.");
        }
        if (copied.clip.composition != 0 && !canReferenceComposition(copied.clip.composition)) {
            return juce::Result::fail("A copied composition cannot be placed here.");
        }
        first = std::min(first, copied.clip.timing(state.tempo()).start);
    }
    auto highest = highestId();
    auto candidate = state;
    std::map<Id, Id> overflowTracks, owners;
    for (const auto& copied : clips) {
        auto clip = copied.clip;
        if (static_cast<Id>(clip.effects.size() + state.routes.size()) + 2 > std::numeric_limits<Id>::max() - highest) { return juce::Result::fail("There are no remaining identities for pasted clips."); }
        auto timing = clip.timing(state.tempo());
        timing.moveTo(timing.start - first + time);
        if (!clip.setTiming(timing, state.tempo())) { return juce::Result::fail("The pasted selection has invalid timing."); }
        owners.emplace(clip.id, highest + 1);
        clip.id = ++highest;
        for (auto& effect : clip.effects) {
            owners.emplace(effect.id, highest + 1);
            effect.id = ++highest;
        }
        const auto original = std::find_if(candidate.tracks.begin(), candidate.tracks.end(), [&](const auto& track) { return track.id == copied.track; });
        const bool fits = original != candidate.tracks.end() && !original->locked && original->kind == copied.kind && original->canPlace(clip, 0, state.tempo());
        if (fits) {
            original->insert(clip, state.tempo());
        } else {
            auto existing = overflowTracks.find(copied.track);
            auto overflow = existing != overflowTracks.end() ? std::find_if(candidate.tracks.begin(), candidate.tracks.end(), [&](const auto& track) { return track.id == existing->second; }) : candidate.tracks.end();
            if (overflow == candidate.tracks.end() || !overflow->canPlace(clip, 0, state.tempo())) {
                Track track;
                track.id = ++highest;
                track.kind = copied.kind;
                track.name = copied.trackName.empty() ? clip.name : copied.trackName;
                track.group = original != candidate.tracks.end() ? original->group : 0;
                const auto position = original != candidate.tracks.end() ? original + 1 : candidate.tracks.end();
                overflow = candidate.tracks.insert(position, std::move(track));
                overflowTracks[copied.track] = overflow->id;
            }
            overflow->insert(clip, state.tempo());
        }
        candidate.duration = std::max(candidate.duration, clip.timing(state.tempo()).end());
        pastedIds.push_back(clip.id);
    }
    // Pasting while the originals exist copies their routes too.
    cloneDrivers(candidate, owners, [&highest] { return ++highest; });
    lastId = highest;
    edit(clips.size() > 1 ? "Paste clips" : "Paste clip", [&candidate](Project& project) { project = candidate; });
    return juce::Result::ok();
}

juce::Result Document::pasteKeys(Id clipId, const std::vector<CopiedKey>& keys, double time) {
    if (keys.empty()) { return juce::Result::fail("The clipboard has no keyframes."); }
    const auto target = findPropertyTarget(project(), clipId);
    if (!target.has_value() || target->isEffect) { return juce::Result::fail("Select a clip, group or camera to paste keyframes onto."); }
    for (const auto& track : project().tracks) {
        for (const auto& clip : track.clips) {
            if (clip.id == clipId && track.locked) { return juce::Result::fail("Unlock the track before pasting keyframes."); }
        }
    }
    const auto changed = tryEdit(keys.size() > 1 ? "Paste keyframes" : "Paste keyframe", [&](Project& updated) {
        const auto found = findPropertyTarget(updated, clipId);
        if (!found.has_value()) { return false; }
        bool any = false;
        for (const auto& copied : keys) {
            auto* curve = found->curve(copied.property);
            if (curve == nullptr) { continue; }
            auto key = copied.key;
            key.time = found->localTime(time + copied.offset);
            if (!key.valid()) { continue; }
            curve->setKey(key);
            any = true;
        }
        return any;
    });
    return changed ? juce::Result::ok() : juce::Result::fail("None of the copied properties exist on the selection.");
}

juce::Result Document::duplicateClips(const std::vector<Id>& sourceIds, std::vector<Id>& duplicateIds) {
    const auto& state = project();
    duplicateIds.clear();
    const std::set<Id> requested(sourceIds.begin(), sourceIds.end());
    if (requested.empty() || requested.size() != sourceIds.size()) { return juce::Result::fail("Select distinct clips to duplicate."); }
    double first = std::numeric_limits<double>::infinity(), last = 0;
    std::vector<std::pair<std::size_t, Clip>> copies;
    for (std::size_t index = 0; index < state.tracks.size(); ++index) {
        const auto& track = state.tracks[index];
        for (const auto& clip : track.clips) {
            if (!requested.contains(clip.id)) { continue; }
            if (track.locked) { return juce::Result::fail("Unlock selected tracks before duplicating clips."); }
            const auto timing = clip.timing(state.tempo());
            if (!clip.valid() || !timing.valid() || clip.effects.size() > maximumEffectsPerOwner
                || std::any_of(clip.effects.begin(), clip.effects.end(), [](const auto& effect) { return !effect.valid(); })) {
                return juce::Result::fail("A selected clip has invalid timing, properties or effects.");
            }
            first = std::min(first, timing.start);
            last = std::max(last, timing.end());
            copies.emplace_back(index, clip);
        }
    }
    if (copies.size() != requested.size()) { return juce::Result::fail("A selected clip no longer exists."); }
    // Provisional identities are published only after every placement succeeds.
    auto highest = highestId();
    auto candidate = state;
    std::vector<Id> ids;
    std::map<Id, Id> owners;
    for (auto& [index, copy] : copies) {
        const auto required = static_cast<Id>(copy.effects.size() + state.routes.size()) + 1;
        if (required > std::numeric_limits<Id>::max() - highest) { return juce::Result::fail("There are no remaining identities for duplicated clips."); }
        const auto original = copy.id;
        auto timing = copy.timing(state.tempo());
        timing.moveTo(timing.start + (last - first));
        if (copies.size() == 1) {
            copy.start = copy.end();
        } else if (!copy.setTiming(timing, state.tempo())) {
            return juce::Result::fail("The duplicated selection has invalid timing.");
        }
        copy.id = ++highest;
        owners.emplace(original, copy.id);
        for (auto& effect : copy.effects) {
            const auto old = effect.id;
            effect.id = ++highest;
            owners.emplace(old, effect.id);
        }
        if (!candidate.tracks[index].insert(copy, state.tempo())) {
            return juce::Result::fail("There is not enough free space after the selection. Move the following clips first.");
        }
        candidate.duration = std::max(candidate.duration, copy.timing(state.tempo()).end());
        ids.push_back(copy.id);
    }
    // A duplicate keeps its routed modulators, like its own keys.
    cloneDrivers(candidate, owners, [&highest] { return ++highest; });
    lastId = highest;
    edit(copies.size() == 1 ? "Duplicate clip" : "Duplicate clips", [candidate = std::move(candidate)](Project& project) { project = candidate; });
    duplicateIds = std::move(ids);
    return juce::Result::ok();
}

juce::Result Document::removeClips(const std::vector<Id>& clipIds, bool ripple) {
    const std::set<Id> requested(clipIds.begin(), clipIds.end());
    if (requested.empty() || requested.contains(0) || requested.size() != clipIds.size()) {
        return juce::Result::fail("Select distinct clips to delete.");
    }
    auto candidate = project();
    std::size_t found = 0;
    for (auto& track : candidate.tracks) {
        std::vector<ClipTiming> removed;
        for (const auto& clip : track.clips) {
            if (!requested.contains(clip.id)) { continue; }
            if (track.locked) { return juce::Result::fail("Unlock selected tracks before deleting clips."); }
            const auto timing = clip.timing(candidate.tempo());
            if (!timing.valid()) { return juce::Result::fail("A selected clip has invalid timing."); }
            removed.push_back(timing);
            ++found;
        }
        if (removed.empty()) { continue; }
        if (ripple) {
            std::sort(track.clips.begin(), track.clips.end(), [&](const auto& a, const auto& b) { return a.timing(candidate.tempo()).start < b.timing(candidate.tempo()).start; });
            double previousEnd = 0;
            for (const auto& clip : track.clips) {
                const auto timing = clip.timing(candidate.tempo());
                if (!timing.valid() || timing.start < previousEnd) { return juce::Result::fail("Ripple delete requires non-overlapping clip intervals."); }
                previousEnd = timing.end();
            }
        }
        std::erase_if(track.clips, [&](const auto& clip) { return requested.contains(clip.id); });
        if (!ripple) { continue; }
        // Close only the selected occupied intervals on each affected track.
        // Gaps, other tracks, source clocks, keys and composition cues retain
        // their own timing. Use resolved seconds for mixed musical/media clips.
        auto originals = std::move(track.clips);
        track.clips.clear();
        for (auto clip : originals) {
            auto timing = clip.timing(candidate.tempo());
            double displacement = 0;
            for (const auto& interval : removed) {
                if (interval.end() <= timing.start) { displacement += interval.duration(); }
                else if (interval.start < timing.end() && timing.start < interval.end()) {
                    return juce::Result::fail("Ripple delete cannot close an overlapping clip interval.");
                }
            }
            if (displacement > 0) {
                timing.moveTo(std::max(0.0, timing.start - displacement));
                if (!clip.setTiming(timing, candidate.tempo())) { return juce::Result::fail("Ripple delete produced invalid clip timing."); }
                // Subtracting the same span from touching boundaries can round
                // them in opposite directions. Original intervals were checked
                // above; correct only arithmetic-sized overlaps, never content.
                if (!track.clips.empty()) {
                    const auto previousEnd = track.clips.back().timing(candidate.tempo()).end();
                    const auto first = clip.timing(candidate.tempo()).start;
                    const auto tolerance = 32 * std::numeric_limits<double>::epsilon() * std::max({1.0, std::abs(first), std::abs(previousEnd), displacement});
                    if (first < previousEnd && previousEnd - first <= tolerance) {
                        auto aligned = clip.timing(candidate.tempo()); aligned.moveTo(previousEnd);
                        if (!clip.setTiming(aligned, candidate.tempo())) { return juce::Result::fail("Ripple delete produced invalid clip timing."); }
                        for (int step = 0; step < 4 && clip.timing(candidate.tempo()).start < previousEnd; ++step) {
                            clip.start = std::nextafter(clip.start, std::numeric_limits<double>::infinity());
                        }
                    }
                }
            }
            if (!track.insert(std::move(clip), candidate.tempo())) { return juce::Result::fail("Ripple delete would overlap remaining clips."); }
        }
    }
    if (found != requested.size()) { return juce::Result::fail("A selected clip no longer exists."); }
    edit(ripple ? "Ripple delete clips" : (found == 1 ? "Delete clip" : "Delete clips"),
        [candidate = std::move(candidate)](Project& value) { value = candidate; });
    return juce::Result::ok();
}

std::size_t Document::compositionReferenceCount(Id definition) const {
    if (definition == 0) { return 0; }
    std::size_t count = 0;
    const auto scope = [&](const auto& value) {
        for (const auto& track : value.tracks) {
            for (const auto& clip : track.clips) { if (clip.composition == definition) { ++count; } }
        }
    };
    scope(state);
    for (const auto& value : state.definitions) { scope(*value); }
    return count;
}

juce::Result Document::removeComposition(Id definition) {
    if (definition == 0 || definition == scopeId || compositionReferenceCount(definition) != 0) {
        return juce::Result::fail("Only unused, closed compositions can be removed from the library.");
    }
    auto candidate = project();
    if (std::erase_if(candidate.definitions, [definition](const auto& value) { return value->id == definition; }) == 0) {
        return juce::Result::fail("The composition no longer exists.");
    }
    edit("Remove unused composition", [candidate = std::move(candidate)](Project& value) { value = candidate; });
    return juce::Result::ok();
}

bool Document::canReferenceComposition(Id definition) const {
    std::set<Id> visited;
    const auto visit = [&](auto&& self, Id id) -> bool {
        if (id == scopeId) { return false; }
        if (!visited.insert(id).second) { return true; }
        const auto found = std::find_if(state.definitions.begin(), state.definitions.end(), [id](const auto& value) { return value->id == id; });
        if (found == state.definitions.end()) { return false; }
        for (const auto& track : (*found)->tracks) {
            for (const auto& clip : track.clips) {
                if (clip.composition != 0 && !self(self, clip.composition)) { return false; }
            }
        }
        return true;
    };
    return definition != 0 && visit(visit, definition);
}

Clip Document::makeCompositionClip(Id id, const CompositionDefinition& definition, double time) {
    Clip clip; clip.id = id; clip.composition = definition.id; clip.name = definition.name.toStdString();
    double first = definition.duration, last = 0;
    for (const auto& track : definition.tracks) {
        for (const auto& source : track.clips) {
            const auto timing = source.timing(definition.tempo());
            if (!timing.valid() || timing.start >= definition.duration) { continue; }
            first = std::min(first, timing.start); last = std::max(last, std::min(timing.end(), definition.duration));
        }
    }
    if (last <= first) { first = 0; last = definition.duration; }
    clip.start = time; clip.offset = first; clip.duration = last - first;
    for (std::size_t index = 0; index < propertyNames.size(); ++index) { clip.properties.emplace(propertyNames[index], Curve(index >= 6 ? 1 : 0)); }
    return clip;
}

juce::Result Document::insertComposition(Id definition, double time, Id trackId, Id groupId, Id& clipId) {
    clipId = 0;
    if (!std::isfinite(time) || time < 0 || !canReferenceComposition(definition)) {
        return juce::Result::fail("Choose a valid composition that does not contain the current editing scope.");
    }
    auto candidate = project();
    const auto source = std::find_if(candidate.definitions.begin(), candidate.definitions.end(), [definition](const auto& value) { return value->id == definition; });
    if (source == candidate.definitions.end()) { return juce::Result::fail("The composition no longer exists."); }
    auto highest = highestId();
    const auto required = trackId == 0 ? 2u : 1u;
    if (required > std::numeric_limits<Id>::max() - highest) { return juce::Result::fail("There are no remaining clip identities."); }
    auto clip = makeCompositionClip(++highest, **source, time);
    if (!clip.valid() || !clip.timing(candidate.tempo()).valid()) { return juce::Result::fail("The composition has invalid timing."); }
    if (trackId != 0) {
        const auto track = std::find_if(candidate.tracks.begin(), candidate.tracks.end(), [trackId](const auto& value) { return value.id == trackId; });
        if (track == candidate.tracks.end() || track->locked || track->kind != TrackKind::visual || !track->insert(clip, candidate.tempo())) {
            return juce::Result::fail("Use an unlocked visual track with room for the composition.");
        }
    } else {
        if (groupId != 0 && findGroup(candidate, groupId) == nullptr) { return juce::Result::fail("The destination group no longer exists."); }
        Track track; track.id = ++highest; track.name = clip.name; track.group = groupId; track.clips = {clip};
        candidate.tracks.push_back(std::move(track));
    }
    candidate.duration = std::max(candidate.duration, clip.end());
    const auto graph = validateCompositionGraph(mergeScope(candidate));
    if (!graph) { return juce::Result::fail(graph.error); }
    lastId = highest;
    edit("Insert composition", [candidate = std::move(candidate)](Project& value) { value = candidate; });
    clipId = clip.id;
    return juce::Result::ok();
}

juce::Result Document::makeCompositionUnique(Id clipId, Id& definitionId) {
    definitionId = 0;
    auto candidate = project();
    Clip* target = nullptr;
    for (auto& track : candidate.tracks) {
        for (auto& clip : track.clips) {
            if (clip.id != clipId) { continue; }
            if (track.locked) { return juce::Result::fail("Unlock the track before making its composition unique."); }
            target = &clip;
        }
    }
    if (target == nullptr || target->composition == 0) { return juce::Result::fail("Select a composition instance."); }
    const auto found = std::find_if(candidate.definitions.begin(), candidate.definitions.end(), [&](const auto& value) { return value->id == target->composition; });
    if (found == candidate.definitions.end()) { return juce::Result::fail("The referenced composition no longer exists."); }
    auto copy = std::make_shared<CompositionDefinition>(**found);
    std::size_t required = 1 + copy->groups.size() + copy->tracks.size() + copy->cameras.size() + copy->cameraCuts.size() + copy->effects.size() + copy->markers.size()
        + copy->modulators.size() + copy->routes.size();
    for (const auto& group : copy->groups) { required += group.effects.size(); }
    for (const auto& track : copy->tracks) {
        required += track.effects.size() + track.clips.size();
        for (const auto& clip : track.clips) { required += clip.effects.size(); }
    }
    auto highest = highestId();
    if (required > std::numeric_limits<Id>::max() - highest) { return juce::Result::fail("There are no remaining composition identities."); }
    // Every renumbered property owner, so routes and links can follow.
    std::map<Id, Id> owners;
    const auto effects = [&](auto& values) {
        for (auto& value : values) {
            const auto old = value.id;
            value.id = ++highest;
            owners.emplace(old, value.id);
        }
    };
    copy->id = ++highest;
    copy->name += " copy";
    std::map<Id, Id> groups, cameras;
    for (auto& group : copy->groups) {
        const auto old = group.id; group.id = ++highest; groups.emplace(old, group.id); owners.emplace(old, group.id); effects(group.effects);
    }
    for (auto& group : copy->groups) {
        if (group.parent != 0) {
            if (!groups.contains(group.parent)) { return juce::Result::fail("Invalid composition group hierarchy."); }
            group.parent = groups.at(group.parent);
        }
    }
    for (auto& track : copy->tracks) {
        track.id = ++highest; effects(track.effects);
        if (track.group != 0) {
            if (!groups.contains(track.group)) { return juce::Result::fail("Invalid composition group reference."); }
            track.group = groups.at(track.group);
        }
        for (auto& clip : track.clips) {
            const auto old = clip.id;
            clip.id = ++highest;
            owners.emplace(old, clip.id);
            effects(clip.effects);
        }
    }
    for (auto& camera : copy->cameras) { const auto old = camera.id; camera.id = ++highest; cameras.emplace(old, camera.id); owners.emplace(old, camera.id); }
    for (auto& cut : copy->cameraCuts) {
        if (!cameras.contains(cut.camera)) { return juce::Result::fail("Invalid composition camera reference."); }
        cut.id = ++highest; cut.camera = cameras.at(cut.camera);
    }
    for (auto& marker : copy->markers) { marker.id = ++highest; }
    effects(copy->effects);
    const auto remap = [&](Id id) { const auto found = owners.find(id); return found != owners.end() ? found->second : Id(0); };
    std::map<Id, Id> modulators;
    for (auto& modulator : copy->modulators) {
        const auto old = modulator.id;
        modulator.id = ++highest;
        modulators.emplace(old, modulator.id);
        modulator.source = remap(modulator.source);
    }
    for (auto& route : copy->routes) {
        route.id = ++highest;
        route.modulator = modulators.contains(route.modulator) ? modulators.at(route.modulator) : 0;
        route.target = remap(route.target);
    }
    forEachPropertyMap(*copy, [&](Id, auto& properties) {
        for (auto& [name, curve] : properties) {
            if (curve.link.has_value()) { curve.link->source = remap(curve.link->source); }
        }
    });
    for (auto& camera : copy->cameras) {
        camera.target = remap(camera.target);
        camera.parent = remap(camera.parent);
    }
    pruneReferences(*copy);
    // Only this definition is forked. Media and referenced child definitions
    // remain shared, while all authored identities within this scope are fresh.
    target->composition = copy->id;
    target->name = copy->name.toStdString();
    candidate.definitions.push_back(copy);
    const auto graph = validateCompositionGraph(mergeScope(candidate));
    if (!graph || !validGroupHierarchy(*copy)) { return juce::Result::fail(graph ? "Invalid copied group hierarchy." : graph.error); }
    lastId = highest;
    edit("Make composition unique", [candidate = std::move(candidate)](Project& value) { value = candidate; });
    definitionId = copy->id;
    return juce::Result::ok();
}

juce::Result Document::createComposition(const std::vector<Id>& clipIds, juce::String name, Id& instanceId) {
    const auto& state = project();
    instanceId = 0;
    name = name.trim();
    const std::set<Id> selected(clipIds.begin(), clipIds.end());
    if (selected.empty() || selected.size() != clipIds.size() || name.isEmpty() || name.length() > 200) {
        return juce::Result::fail("Select distinct clips and name the composition (up to 200 characters).");
    }
    const auto graph = validateCompositionGraph(state);
    if (!graph || !validGroupHierarchy(state)) { return juce::Result::fail("The existing composition structure is invalid."); }
    auto definition = std::make_shared<CompositionDefinition>();
    // Keep the owning time coordinate: track/group curves and modulation phase
    // must not restart when only part of a group is moved into a composition.
    // The new instance trims this source to the selected interval.
    definition->name = name;
    definition->duration = state.duration;
    definition->bpm = state.bpm; definition->tempoChanges = state.tempoChanges; definition->frameRate = state.frameRate;
    definition->timeDisplay = state.timeDisplay; definition->beatsPerBar = state.beatsPerBar;
    definition->snapBeats = state.snapBeats; definition->gridSnap = state.gridSnap;
    double first = std::numeric_limits<double>::infinity(), last = 0;
    std::size_t count = 0, insertion = state.tracks.size();
    std::set<Id> requiredGroups;
    for (std::size_t index = 0; index < state.tracks.size(); ++index) {
        const auto& track = state.tracks[index];
        auto copy = track;
        std::erase_if(copy.clips, [&](const auto& clip) { return !selected.contains(clip.id); });
        if (copy.clips.empty()) { continue; }
        if (track.locked) { return juce::Result::fail("Unlock selected tracks before creating a composition."); }
        insertion = std::min(insertion, index);
        for (const auto& clip : copy.clips) {
            const auto timing = clip.timing(state.tempo());
            if (!clip.valid() || !timing.valid()) { return juce::Result::fail("A selected clip has invalid timing or properties."); }
            first = std::min(first, timing.start); last = std::max(last, timing.end()); ++count;
        }
        // Capture effective visibility across the new scope boundary. Solo is
        // scoped independently inside the definition after this operation.
        copy.muted = !trackIsAudible(state, track);
        copy.solo = false;
        copy.midiInput = 0; // live input reaches main-timeline tracks only
        auto group = track.group;
        while (group != 0) {
            requiredGroups.insert(group);
            group = findGroup(state, group)->parent;
        }
        definition->tracks.push_back(std::move(copy));
    }
    if (count != selected.size()) { return juce::Result::fail("A selected clip no longer exists."); }
    auto candidate = state;
    auto highest = highestId();
    std::size_t required = 3 + definition->tracks.size() + requiredGroups.size();
    for (const auto& track : definition->tracks) { required += track.effects.size(); }
    for (const auto& group : state.groups) { if (requiredGroups.contains(group.id)) { required += group.effects.size(); } }
    required += 2 * state.routes.size() + state.modulators.size();
    if (required > std::numeric_limits<Id>::max() - highest) { return juce::Result::fail("There are no remaining composition identities."); }
    definition->id = ++highest;
    // copies: every root owner copied (not moved) into the definition.
    std::map<Id, Id> groupIds, copies;
    const auto renumber = [&](std::vector<EffectInstance>& effects) {
        for (auto& effect : effects) {
            const auto old = effect.id;
            effect.id = ++highest;
            copies.emplace(old, effect.id);
        }
    };
    for (const auto& group : state.groups) {
        if (!requiredGroups.contains(group.id)) { continue; }
        auto copy = group; copy.id = ++highest; copy.solo = false;
        groupIds.emplace(group.id, copy.id);
        copies.emplace(group.id, copy.id);
        renumber(copy.effects);
        definition->groups.push_back(std::move(copy));
    }
    for (auto& group : definition->groups) { if (group.parent != 0) { group.parent = groupIds.at(group.parent); } }
    for (auto& track : definition->tracks) {
        track.id = ++highest;
        if (track.group != 0) { track.group = groupIds.at(track.group); }
        renumber(track.effects);
    }
    // Routes follow their targets: clip (and clip effect) routes move into the
    // definition; routes on copied groups are copied. Each modulator they use
    // is copied once, so the definition owns its own clocks.
    std::set<Id> movedTargets(selected.begin(), selected.end());
    for (const auto& track : definition->tracks) {
        for (const auto& clip : track.clips) { for (const auto& effect : clip.effects) { movedTargets.insert(effect.id); } }
    }
    std::map<Id, Id> modulatorIds;
    std::set<Id> movedRoutes;
    for (const auto& route : state.routes) {
        const auto group = copies.find(route.target);
        const bool moved = movedTargets.contains(route.target);
        if (!moved && group == copies.end()) { continue; }
        if (!modulatorIds.contains(route.modulator)) {
            const auto source = std::find_if(state.modulators.begin(), state.modulators.end(), [&](const auto& item) { return item.id == route.modulator; });
            if (source == state.modulators.end()) { continue; }
            auto copy = *source;
            copy.id = ++highest;
            if (copy.kind != ModulatorKind::oscillator && !selected.contains(copy.source)) { copy.source = 0; }
            modulatorIds.emplace(route.modulator, copy.id);
            definition->modulators.push_back(std::move(copy));
        }
        auto copy = route;
        copy.id = ++highest;
        copy.modulator = modulatorIds.at(route.modulator);
        if (!moved) { copy.target = group->second; }
        definition->routes.push_back(std::move(copy));
        if (moved) { movedRoutes.insert(route.id); }
    }
    std::erase_if(candidate.routes, [&](const auto& route) { return movedRoutes.contains(route.id); });
    // Links follow copied owners; links to material left in the root cannot
    // cross the composition boundary and are pruned.
    forEachPropertyMap(*definition, [&](Id, auto& properties) {
        for (auto& [property, curve] : properties) {
            if (!curve.link.has_value()) { continue; }
            const auto copy = copies.find(curve.link->source);
            if (copy != copies.end()) { curve.link->source = copy->second; }
        }
    });
    pruneReferences(*definition);
    Clip instance;
    instance.id = ++highest; instance.composition = definition->id; instance.name = name.toStdString();
    instance.start = first; instance.duration = last - first; instance.offset = first;
    for (std::size_t index = 0; index < propertyNames.size(); ++index) {
        instance.properties.emplace(propertyNames[index], Curve(index >= 6 ? 1 : 0));
    }
    Track replacement; replacement.id = ++highest; replacement.name = name.toStdString(); replacement.clips = {instance};
    replacement.solo = std::any_of(state.tracks.begin(), state.tracks.end(), [](const auto& track) { return track.solo; })
        || std::any_of(state.groups.begin(), state.groups.end(), [](const auto& group) { return group.solo; });
    std::set<Id> emptiedTracks;
    for (auto& track : candidate.tracks) {
        const auto removed = std::erase_if(track.clips, [&](const auto& clip) { return selected.contains(clip.id); });
        if (removed != 0 && track.clips.empty()) { emptiedTracks.insert(track.id); }
    }
    std::erase_if(candidate.tracks, [&](const auto& track) { return emptiedTracks.contains(track.id); });
    // Prune only ancestors emptied by this operation, retaining unrelated empty
    // tracks/groups and any partial group still containing unselected material.
    for (;;) {
        std::set<Id> emptyGroups;
        for (const auto& group : candidate.groups) {
            if (!requiredGroups.contains(group.id)) { continue; }
            const bool used = std::any_of(candidate.tracks.begin(), candidate.tracks.end(), [&](const auto& track) { return track.group == group.id; })
                || std::any_of(candidate.groups.begin(), candidate.groups.end(), [&](const auto& child) { return child.parent == group.id; });
            if (!used) { emptyGroups.insert(group.id); }
        }
        if (emptyGroups.empty()) { break; }
        std::erase_if(candidate.groups, [&](const auto& group) { return emptyGroups.contains(group.id); });
    }
    candidate.tracks.insert(candidate.tracks.begin() + static_cast<std::ptrdiff_t>(insertion), std::move(replacement));
    definition->duration = std::max(definition->duration, last);
    candidate.duration = std::max(candidate.duration, last);
    candidate.definitions.push_back(definition);
    const auto expanded = validateCompositionGraph(candidate);
    if (!expanded) { return juce::Result::fail(expanded.error); }
    // Publish identities and one undo snapshot only after every check succeeds.
    lastId = highest;
    edit("Create composition", [candidate = std::move(candidate)](Project& project) { project = candidate; });
    instanceId = instance.id;
    return juce::Result::ok();
}

juce::Result Document::setClipTimings(const std::vector<std::pair<Id, ClipTiming>>& timings) {
    if (timings.size() == 1) { return setClipTiming(timings.front().first, timings.front().second); }
    const auto& state = project();
    const auto tempo = state.tempo();
    auto tracks = state.tracks;
    bool changed = false;
    for (const auto& [clipId, timing] : timings) {
        if (!timing.valid()) { return juce::Result::fail("Clip timing needs a finite non-negative start, positive duration and speed, and finite source offset."); }
        bool found = false;
        for (auto& track : tracks) {
            for (auto& clip : track.clips) {
                if (clip.id != clipId) { continue; }
                if (track.locked) { return juce::Result::fail("Unlock the track before changing clip timing."); }
                auto next = clip;
                if (!next.setTiming(timing, tempo)) { return juce::Result::fail("The requested timing is invalid."); }
                changed = changed || next.start != clip.start || next.duration != clip.duration || next.offset != clip.offset || next.rate != clip.rate;
                clip = std::move(next);
                found = true;
            }
        }
        if (!found) { return juce::Result::fail("A selected clip no longer exists."); }
    }
    for (const auto& track : tracks) {
        for (const auto& clip : track.clips) {
            if (!track.canPlace(clip, clip.id, tempo)) { return juce::Result::fail("The requested timing overlaps another clip on the same track."); }
        }
    }
    if (!changed) { return juce::Result::ok(); }
    edit("Change clip timing", [tracks = std::move(tracks)](Project& project) mutable {
        const auto tempo = project.tempo();
        for (auto& track : tracks) {
            for (const auto& clip : track.clips) { project.duration = std::max(project.duration, clip.timing(tempo).end()); }
            std::sort(track.clips.begin(), track.clips.end(), [&tempo](const auto& a, const auto& b) { return a.timing(tempo).start < b.timing(tempo).start; });
        }
        project.tracks = std::move(tracks);
    });
    return juce::Result::ok();
}

juce::Result Document::setClipTiming(Id clipId, ClipTiming resolvedSeconds) {
    const auto& state = project();
    if (!resolvedSeconds.valid()) { return juce::Result::fail("Clip timing needs a finite non-negative start, positive duration and speed, and finite source offset."); }
    for (std::size_t trackIndex = 0; trackIndex < state.tracks.size(); ++trackIndex) {
        const auto& track = state.tracks[trackIndex];
        for (std::size_t clipIndex = 0; clipIndex < track.clips.size(); ++clipIndex) {
            const auto& original = track.clips[clipIndex];
            if (original.id != clipId) { continue; }
            if (track.locked) { return juce::Result::fail("Unlock the track before changing clip timing."); }
            auto changed = original;
            if (!changed.setTiming(resolvedSeconds, state.tempo()) || !track.canPlace(changed, clipId, state.tempo())) {
                return juce::Result::fail("The requested timing is invalid or overlaps another clip on this track.");
            }
            if (changed.start == original.start && changed.duration == original.duration
                && changed.offset == original.offset && changed.rate == original.rate) {
                return juce::Result::ok();
            }
            edit("Change clip timing", [trackIndex, clipIndex, changed = std::move(changed)](Project& project) {
                auto& clips = project.tracks[trackIndex].clips;
                project.duration = std::max(project.duration, changed.timing(project.tempo()).end());
                clips[clipIndex] = changed;
                const auto tempo = project.tempo();
                std::sort(clips.begin(), clips.end(), [&tempo](const auto& a, const auto& b) {
                    return a.timing(tempo).start < b.timing(tempo).start;
                });
            });
            return juce::Result::ok();
        }
    }
    return juce::Result::fail("The selected clip no longer exists.");
}

juce::Result Document::editMidi(Id clipId, juce::String label, const std::function<juce::Result(Clip&)>& operation) {
    const auto& state = project();
    for (std::size_t trackIndex = 0; trackIndex < state.tracks.size(); ++trackIndex) {
        const auto& track = state.tracks[trackIndex];
        for (std::size_t clipIndex = 0; clipIndex < track.clips.size(); ++clipIndex) {
            const auto& original = track.clips[clipIndex];
            if (original.id != clipId) { continue; }
            if (track.kind != TrackKind::visual) { return juce::Result::fail("MIDI performance requires a visual track."); }
            if (original.composition != 0) { return juce::Result::fail("Open the composition and assign MIDI to one of its media clips."); }
            if (track.locked) { return juce::Result::fail("Unlock the track before editing its MIDI performance."); }
            auto changed = original;
            const auto result = operation(changed);
            if (result.failed()) { return result; }
            if (!track.canPlace(changed, clipId, state.tempo())) {
                return juce::Result::fail("MIDI assignment would produce invalid or overlapping clip timing.");
            }
            if (changed.instrument == original.instrument && changed.midi == original.midi && changed.midiAsset == original.midiAsset
                && changed.timeBase == original.timeBase && changed.contentBpm == original.contentBpm
                && changed.start == original.start && changed.duration == original.duration
                && changed.offset == original.offset && changed.rate == original.rate) {
                return juce::Result::ok();
            }
            edit(label, [trackIndex, clipIndex, changed = std::move(changed)](Project& project) {
                project.tracks[trackIndex].clips[clipIndex] = changed;
            });
            return juce::Result::ok();
        }
    }
    return juce::Result::fail("The selected clip no longer exists.");
}

juce::Result Document::setMidiInstrument(Id clipId, MidiInstrument settings) {
    if (!settings.valid()) { return juce::Result::fail("Envelope times must be between 0 and 30 seconds; sustain must be between 0 and 1."); }
    return editMidi(clipId, "Change MIDI envelope", [settings](Clip& clip) {
        clip.instrument = settings;
        return juce::Result::ok();
    });
}

juce::Result Document::assignMidi(Id clipId, Id assetId) {
    const auto& state = project();
    std::shared_ptr<const MidiNotes> notes;
    if (assetId == 0) {
        const auto empty = MidiNotes::create({});
        if (!empty) { return juce::Result::fail(empty.error); }
        notes = empty.source;
    } else {
        for (const auto& asset : state.assets) {
            if (asset != nullptr && asset->id == assetId && isMidiSource(asset->extension)) {
                notes = asset->midi;
                break;
            }
        }
        if (notes == nullptr) { return juce::Result::fail("Choose an imported MIDI source that has been decoded successfully."); }
    }
    return editMidi(clipId, "Assign MIDI performance", [&](Clip& clip) {
        if (!clip.anchorToBeats(state.tempo())) { return juce::Result::fail("Cannot anchor this clip to the project tempo."); }
        clip.midi = notes;
        clip.midiAsset = assetId;
        return juce::Result::ok();
    });
}

juce::Result Document::setMidiNotes(Id clipId, std::shared_ptr<const MidiNotes> notes, juce::String undoLabel) {
    if (notes == nullptr) { return juce::Result::fail("MIDI note content must not be null. Use Clear MIDI to remove a performance."); }
    return editMidi(clipId, undoLabel.isEmpty() ? "Edit MIDI notes" : undoLabel, [&](Clip& clip) {
        if (clip.midi == nullptr) { return juce::Result::fail("Assign a MIDI performance before editing notes."); }
        if (clip.midi->sameContent(*notes)) { return juce::Result::ok(); }
        clip.midi = notes;
        return juce::Result::ok();
    });
}

juce::Result Document::recordMidiNotes(Id clipId, std::shared_ptr<const MidiNotes> expected, std::shared_ptr<const MidiNotes> merged, std::uint64_t expectedGeneration) {
    if (merged == nullptr) { return juce::Result::fail("Recorded MIDI note content must not be null."); }
    if (generation() != expectedGeneration) {
        return juce::Result::fail("The recording target changed before the take was saved.");
    }
    const auto bpm = project().tempo();
    return editMidi(clipId, "Record MIDI notes", [expected = std::move(expected), merged = std::move(merged), bpm](Clip& clip) {
        if (clip.midi != expected) { return juce::Result::fail("The recording target changed before the take was saved."); }
        if (expected != nullptr && expected->sameContent(*merged)) { return juce::Result::ok(); }
        if (clip.timeBase != ClipTimeBase::beats && !clip.anchorToBeats(bpm)) {
            return juce::Result::fail("Cannot anchor this clip to the project tempo.");
        }
        clip.midi = merged;
        clip.midiAsset = 0;
        return juce::Result::ok();
    });
}

juce::Result Document::clearMidi(Id clipId) {
    return editMidi(clipId, "Clear MIDI performance", [](Clip& clip) {
        clip.midi.reset();
        clip.midiAsset = 0;
        return juce::Result::ok();
    });
}

static bool hasCamera(const Project& project, Id id) {
    return std::any_of(project.cameras.begin(), project.cameras.end(), [id](const auto& camera) { return camera.id == id; });
}

static bool isVisualClipOrGroup(const Project& project, Id id, bool groupsOnly) {
    if (id == 0) { return true; }
    if (findGroup(project, id) != nullptr) { return true; }
    if (groupsOnly) { return false; }
    for (const auto& track : project.tracks) {
        if (track.kind != TrackKind::visual) { continue; }
        for (const auto& clip : track.clips) { if (clip.id == id) { return true; } }
    }
    return false;
}

juce::Result Document::cutToCamera(Id camera, double time, Id& cutId) {
    const auto& current = project();
    if (!hasCamera(current, camera)) { return juce::Result::fail("Choose a camera to cut to."); }
    if (!std::isfinite(time) || time < 0 || time >= current.duration) { return juce::Result::fail("Cuts must start inside the project."); }
    cutId = newId();
    const auto id = cutId;
    tryEdit("Cut to camera", [time, camera, id](Project& updated) {
        auto end = updated.duration;
        for (const auto& cut : updated.cameraCuts) {
            if (cut.start > time) { end = std::min(end, cut.start); }
        }
        std::erase_if(updated.cameraCuts, [time](const auto& cut) { return cut.start == time; });
        for (auto& cut : updated.cameraCuts) {
            if (cut.start < time && cut.end() > time) { cut.duration = spanUntil(cut.start, time); }
        }
        updated.cameraCuts.push_back({id, camera, time, spanUntil(time, end)});
        std::sort(updated.cameraCuts.begin(), updated.cameraCuts.end(), [](const auto& left, const auto& right) { return left.start < right.start; });
        return true;
    });
    return juce::Result::ok();
}

juce::Result Document::setCutCamera(Id cut, Id camera) {
    if (!hasCamera(project(), camera)) { return juce::Result::fail("The camera no longer exists."); }
    const auto changed = tryEdit("Change cut camera", [cut, camera](Project& updated) {
        for (auto& item : updated.cameraCuts) {
            if (item.id == cut && item.camera != camera) {
                item.camera = camera;
                return true;
            }
        }
        return false;
    });
    return changed ? juce::Result::ok() : juce::Result::fail("The cut no longer exists or already shows that camera.");
}

juce::Result Document::setCutRange(Id cut, double start, double end, juce::String label) {
    const auto& current = project();
    const auto frame = current.frameRate > 0 ? 1.0 / current.frameRate : 1.0 / 30;
    const auto found = std::find_if(current.cameraCuts.begin(), current.cameraCuts.end(), [cut](const auto& item) { return item.id == cut; });
    if (found == current.cameraCuts.end()) { return juce::Result::fail("The cut no longer exists."); }
    if (!std::isfinite(start) || !std::isfinite(end) || start < 0 || end > current.duration + 1.0e-9 || end - start < frame - 1.0e-9) {
        return juce::Result::fail("A cut must stay inside the project and last at least one frame.");
    }
    for (const auto& other : current.cameraCuts) {
        if (other.id != cut && start < other.end() && other.start < end) { return juce::Result::fail("Cuts cannot overlap; trim the neighbouring cut first."); }
    }
    if (found->start == start && found->end() == std::min(end, current.duration)) { return juce::Result::ok(); }
    editCoalesced(label, "cut:" + juce::String(cut), [cut, start, end](Project& updated) {
        for (auto& item : updated.cameraCuts) {
            if (item.id == cut) {
                item.start = start;
                item.duration = spanUntil(start, std::min(end, updated.duration));
            }
        }
        std::sort(updated.cameraCuts.begin(), updated.cameraCuts.end(), [](const auto& left, const auto& right) { return left.start < right.start; });
    });
    return juce::Result::ok();
}

juce::Result Document::removeCut(Id cut) {
    const auto removed = tryEdit("Remove camera cut", [cut](Project& updated) {
        const auto before = updated.cameraCuts.size();
        std::erase_if(updated.cameraCuts, [cut](const auto& item) { return item.id == cut; });
        return updated.cameraCuts.size() != before;
    });
    return removed ? juce::Result::ok() : juce::Result::fail("The cut no longer exists.");
}

juce::Result Document::setCameraRig(Id camera, Id target, Id parent) {
    const auto& current = project();
    if (!hasCamera(current, camera)) { return juce::Result::fail("The camera no longer exists."); }
    if (!isVisualClipOrGroup(current, target, false) || !isVisualClipOrGroup(current, parent, true)) { return juce::Result::fail("Aim at an object or group, and parent to a group."); }
    const auto changed = tryEdit("Change camera rig", [camera, target, parent](Project& updated) {
        for (auto& item : updated.cameras) {
            if (item.id == camera && (item.target != target || item.parent != parent)) {
                item.target = target;
                item.parent = parent;
                return true;
            }
        }
        return false;
    });
    juce::ignoreUnused(changed);
    return juce::Result::ok();
}

// Route depth lives on routes; the shape's own depth and switch are fixed.
static void normaliseModulator(Modulator& modulator) {
    modulator.shape.enabled = true;
    modulator.shape.amount = 1;
    modulator.shape.mode = ModulationMode::add;
    modulator.shape.soundtrack.reset();
}

juce::Result Document::addModulator(Modulator modulator, Id& id) {
    modulator.id = newId();
    normaliseModulator(modulator);
    if (!modulator.valid()) { return juce::Result::fail("Invalid modulator settings."); }
    if (modulator.kind != ModulatorKind::oscillator && modulator.source != 0 && !hasVisualClip(project(), modulator.source)) { return juce::Result::fail("The modulator's source clip does not exist."); }
    id = modulator.id;
    edit("Add modulator", [modulator](Project& project) { project.modulators.push_back(modulator); });
    return juce::Result::ok();
}

juce::Result Document::setModulator(Modulator modulator) {
    normaliseModulator(modulator);
    const auto& list = project().modulators;
    const auto found = std::find_if(list.begin(), list.end(), [&](const auto& item) { return item.id == modulator.id; });
    if (found == list.end()) { return juce::Result::fail("The modulator no longer exists."); }
    if (!modulator.valid()) { return juce::Result::fail("Invalid modulator settings."); }
    if (modulator.kind != ModulatorKind::oscillator && modulator.source != 0 && !hasVisualClip(project(), modulator.source)) { return juce::Result::fail("The modulator's source clip does not exist."); }
    if (*found == modulator) { return juce::Result::ok(); }
    editCoalesced("Change modulator", "modulator:" + juce::String(modulator.id), [modulator](Project& project) {
        for (auto& item : project.modulators) {
            if (item.id == modulator.id) { item = modulator; }
        }
    });
    return juce::Result::ok();
}

juce::Result Document::removeModulator(Id id) {
    const auto removed = tryEdit("Delete modulator", [id](Project& project) {
        const auto before = project.modulators.size();
        std::erase_if(project.modulators, [id](const auto& item) { return item.id == id; });
        std::erase_if(project.routes, [id](const auto& route) { return route.modulator == id; });
        return project.modulators.size() != before;
    });
    return removed ? juce::Result::ok() : juce::Result::fail("The modulator no longer exists.");
}

juce::Result Document::addRoute(ModulationRoute route, Id& id) {
    route.id = newId();
    const auto& current = project();
    const auto hasModulator = std::any_of(current.modulators.begin(), current.modulators.end(), [&](const auto& item) { return item.id == route.modulator; });
    if (!route.valid() || !hasModulator || !drivableProperty(current, route.target, route.property)) { return juce::Result::fail("A route needs an existing modulator and a visual property."); }
    id = route.id;
    edit("Route modulator", [route](Project& project) { project.routes.push_back(route); });
    return juce::Result::ok();
}

juce::Result Document::addRoutedModulator(Modulator modulator, ModulationRoute route, Id& modulatorId) {
    modulator.id = newId();
    normaliseModulator(modulator);
    route.id = newId();
    route.modulator = modulator.id;
    if (!modulator.valid() || !route.valid() || !drivableProperty(project(), route.target, route.property)) {
        return juce::Result::fail("A route needs an existing visual property.");
    }
    modulatorId = modulator.id;
    edit("Route new modulator", [modulator, route](Project& project) {
        project.modulators.push_back(modulator);
        project.routes.push_back(route);
    });
    return juce::Result::ok();
}

juce::Result Document::setRoute(const ModulationRoute& route) {
    const auto& list = project().routes;
    const auto found = std::find_if(list.begin(), list.end(), [&](const auto& item) { return item.id == route.id; });
    if (found == list.end()) { return juce::Result::fail("The route no longer exists."); }
    if (!route.valid() || found->modulator != route.modulator || found->target != route.target || found->property != route.property) {
        return juce::Result::fail("Only a route's amount and mode can change.");
    }
    if (*found == route) { return juce::Result::ok(); }
    editCoalesced("Change route", "route:" + juce::String(route.id), [route](Project& project) {
        for (auto& item : project.routes) {
            if (item.id == route.id) { item = route; }
        }
    });
    return juce::Result::ok();
}

juce::Result Document::removeRoute(Id id) {
    const auto removed = tryEdit("Delete route", [id](Project& project) {
        const auto before = project.routes.size();
        std::erase_if(project.routes, [id](const auto& route) { return route.id == id; });
        return project.routes.size() != before;
    });
    return removed ? juce::Result::ok() : juce::Result::fail("The route no longer exists.");
}

juce::Result Document::setLink(Id target, const std::string& property, std::optional<PropertyLink> link) {
    const auto& current = project();
    const auto* curve = findPropertyCurve(current, target, property);
    if (curve == nullptr) { return juce::Result::fail("The property no longer exists."); }
    if (link.has_value() && !drivableProperty(current, target, property)) { return juce::Result::fail("Audio clip gain and pan cannot be linked."); }
    if (link.has_value()) {
        if (!link->valid() || !hasPropertyCurve(current, link->source, link->property)) { return juce::Result::fail("Choose an existing property to link to."); }
        if (linkCreatesCycle(current, target, property, *link)) { return juce::Result::fail("That link would make the property depend on itself."); }
    }
    if (curve->link == link) { return juce::Result::ok(); }
    const auto label = link.has_value() ? (curve->link.has_value() ? "Change property link" : "Link property") : "Unlink property";
    editCoalesced(label, "link:" + juce::String(target) + ":" + juce::String(property), [target, property, link](Project& project) {
        auto* edited = findPropertyCurve(project, target, property);
        if (edited != nullptr) { edited->link = link; }
    });
    return juce::Result::ok();
}

juce::Result Document::addBlenderSource(juce::String name, BlenderSourceSettings settings, Id& id) {
    if (!settings.valid() || name.trim().isEmpty()) { return juce::Result::fail("Enter a source name and a port from 51600 to 51699."); }
    auto asset = std::make_shared<Asset>();
    asset->id = newId(); asset->name = name.trim(); asset->extension = ".blender";
    asset->blenderSettings = settings;
    const auto result = decodeAsset(*asset);
    if (result.failed()) { return result; }
    edit("Add Blender source", [asset](Project& project) { project.assets.push_back(asset); });
    id = asset->id;
    return juce::Result::ok();
}

juce::Result Document::setBlenderSource(Id id, juce::String name, BlenderSourceSettings settings) {
    const auto found = std::find_if(state.assets.begin(), state.assets.end(), [id](const auto& asset) { return asset->id == id; });
    if (found == state.assets.end() || !(*found)->extension.equalsIgnoreCase(".blender")) { return juce::Result::fail("The Blender source no longer exists."); }
    if (!settings.valid() || name.trim().isEmpty()) { return juce::Result::fail("Enter a source name and a port from 51600 to 51699."); }
    if ((*found)->name == name.trim() && (*found)->blenderSettings == settings) { return juce::Result::ok(); }
    auto replacement = std::make_shared<Asset>(**found);
    replacement->name = name.trim(); replacement->blenderSettings = settings;
    if ((*found)->blenderSettings.port != settings.port) { replacement->liveIdentity = std::make_shared<const LiveSourceIdentity>(); }
    edit("Change Blender source", [id, replacement](Project& project) {
        for (auto& asset : project.assets) { if (asset->id == id) { asset = replacement; break; } }
    });
    return juce::Result::ok();
}

Clip Document::makeClip(Id id, const Asset& asset, double time) {
    Clip clip;
    clip.id = id;
    clip.asset = asset.id;
    clip.name = asset.name.toStdString();
    clip.start = time;
    if (asset.audio != nullptr) {
        clip.duration = asset.audio->duration();
        clip.properties["gain"] = Curve(1);
        clip.properties["pan"] = Curve(0);
        return clip;
    }
    if (asset.source != nullptr && (asset.source->frameCount() > 1 || asset.extension.equalsIgnoreCase(".lua") || asset.extension.equalsIgnoreCase(".blender-capture") || isVideoSource(asset.extension))) {
        clip.duration = asset.source->duration();
    }
    for (const auto* axis : { "x", "y", "z" }) {
        clip.properties[std::string("position.") + axis] = Curve(0);
        clip.properties[std::string("rotation.") + axis] = Curve(0);
        clip.properties[std::string("scale.") + axis] = Curve(1);
    }
    const bool sourceColour = asset.source != nullptr && asset.source->hasExplicitColour();
    clip.properties["red"] = Curve(sourceColour ? 1 : 0.2);
    clip.properties["green"] = Curve(1);
    clip.properties["blue"] = Curve(sourceColour ? 1 : 0.35);
    clip.properties["weight"] = Curve(1);
    return clip;
}

juce::Result Document::decodeAsset(Asset& asset, const std::atomic<bool>* cancel, std::atomic<double>* progress, const juce::File& videoDecoder) try {
    if (progress != nullptr) {
        progress->store(0.0, std::memory_order_relaxed);
    }
    if (importCancelled(cancel)) {
        return juce::Result::fail("Source import cancelled.");
    }
    if (asset.extension.equalsIgnoreCase(".blender")) {
        if (!asset.blenderSettings.valid() || asset.data.getSize() != 0) { return juce::Result::fail("Invalid Blender source settings."); }
        asset.liveIdentity = std::make_shared<const LiveSourceIdentity>();
        asset.source.reset(); asset.drawing.reset(); asset.audio.reset(); asset.midi.reset();
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
        std::vector<std::shared_ptr<const osci::PreparedDrawing>> drawings;
        drawings.reserve(capture.frames.size());
        for (const auto& frame : capture.frames) {
            if (importCancelled(cancel)) { return juce::Result::fail("Capture preparation cancelled."); }
            std::vector<std::unique_ptr<osci::Shape>> lines;
            lines.reserve(frame->segments.size());
            for (const auto& segment : frame->segments) {
                lines.push_back(std::make_unique<osci::Line>(osci::Point(segment.x1, segment.y1, 0), osci::Point(segment.x2, segment.y2, 0)));
            }
            drawings.push_back(std::make_shared<const osci::PreparedDrawing>(std::move(lines)));
            if (progress != nullptr) { progress->store(static_cast<double>(drawings.size()) / capture.frames.size()); }
        }
        asset.source = std::make_shared<const PreparedSource>(std::move(drawings), std::move(capture.timing));
        asset.drawing = asset.source->firstFrame();
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
        asset.drawing.reset();
        asset.audio.reset();
        if (progress != nullptr) { progress->store(1); }
        return juce::Result::ok();
    }
    if (isVideoSource(extension)) {
        const auto invalid = asset.rasterSettings.validate();
        if (!invalid.empty()) { return juce::Result::fail(invalid); }
        const auto key = videoBakeKey(asset);
        PreparedPointFrames::Result prepared;
        juce::MemoryBlock archive;
        if (asset.bakedData.getSize() > 0) {
            if (asset.bakeKey != key) { return juce::Result::fail("Video cache does not match its source and tracing settings. Prepare the source again."); }
            prepared = BakedSourceArchive::decode(asset.bakedData);
        } else {
            prepared = VideoSourcePreparer::prepare(asset.data, videoDecoder, asset.rasterSettings, cancel, progress);
            if (prepared) {
                auto encoded = BakedSourceArchive::encode(*prepared.source);
                if (!encoded) { return juce::Result::fail(encoded.error); }
                archive = std::move(encoded.data);
            }
        }
        if (!prepared) { return juce::Result::fail(prepared.error); }
        if (prepared.source->frameRate() != asset.rasterSettings.videoFrameRate || prepared.source->pointsPerFrame() != asset.rasterSettings.pointsPerFrame) {
            return juce::Result::fail("Video cache metadata does not match its tracing settings.");
        }
        if (importCancelled(cancel)) { return juce::Result::fail("Video preparation cancelled."); }
        asset.source = std::make_shared<const PreparedSource>(prepared.source);
        asset.drawing.reset(); asset.audio.reset();
        if (archive.getSize() > 0) { asset.bakedData = std::move(archive); }
        asset.bakeKey = key;
        if (progress != nullptr) { progress->store(1); }
        return juce::Result::ok();
    }
    if (isRasterSource(extension)) {
        const auto prepared = RasterSourcePreparer::prepare(asset.data.getData(), asset.data.getSize(), asset.rasterSettings, cancel, progress);
        if (!prepared) { return juce::Result::fail(prepared.error); }
        if (importCancelled(cancel)) { return juce::Result::fail("Image preparation cancelled."); }
        asset.source = prepared.source;
        asset.drawing.reset();
        asset.audio.reset();
        if (progress != nullptr) { progress->store(1); }
        return juce::Result::ok();
    }
    if (extension == ".lua") {
        const auto settingsError = asset.bakeSettings.validate();
        if (!settingsError.empty()) { return juce::Result::fail(settingsError); }
        const auto key = sourceBakeKey(asset);
        PreparedPointFrames::Result prepared;
        juce::MemoryBlock archive;
        if (asset.bakedData.getSize() > 0) {
            if (asset.bakeKey != key) { return juce::Result::fail("Baked source does not match its script and settings. Rebuild the source cache."); }
            prepared = BakedSourceArchive::decode(asset.bakedData);
        } else {
            prepared = LuaBaker::bake(asset.name, juce::String::fromUTF8(static_cast<const char*>(asset.data.getData()), static_cast<int>(asset.data.getSize())), asset.bakeSettings, cancel, progress);
            if (prepared) {
                auto encoded = BakedSourceArchive::encode(*prepared.source);
                if (!encoded) { return juce::Result::fail(encoded.error); }
                archive = std::move(encoded.data);
            }
        }
        if (!prepared) { return juce::Result::fail(prepared.error); }
        if (prepared.source->frameCount() != asset.bakeSettings.frameCount()
            || prepared.source->frameRate() != asset.bakeSettings.frameRate
            || prepared.source->pointsPerFrame() != asset.bakeSettings.pointsPerFrame) {
            return juce::Result::fail("Baked source metadata does not match its settings.");
        }
        if (importCancelled(cancel)) { return juce::Result::fail("Source import cancelled."); }
        auto source = std::make_shared<const PreparedSource>(prepared.source);
        asset.source = std::move(source);
        asset.drawing.reset();
        asset.audio.reset();
        if (archive.getSize() > 0) { asset.bakedData = std::move(archive); }
        asset.bakeKey = key;
        if (progress != nullptr) { progress->store(1); }
        return juce::Result::ok();
    }
    if (extension == ".wav" || extension == ".wave" || extension == ".aif" || extension == ".aiff" || extension == ".flac" || extension == ".ogg") {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        auto input = std::make_unique<juce::MemoryInputStream>(asset.data, false);
        std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(std::move(input)));
        if (reader == nullptr || reader->lengthInSamples <= 0) {
            return juce::Result::fail("Cannot decode this audio file. Check its contents and whether its codec is enabled in this build.");
        }
        const auto prepared = PreparedAudio::create(reader->sampleRate, reader->numChannels, static_cast<std::uint64_t>(reader->lengthInSamples),
            [&](float* const* channels, std::size_t channelCount, std::size_t first, std::size_t frames) {
                if (importCancelled(cancel)) {
                    return false;
                }
                const bool read = reader->read(channels, static_cast<int>(channelCount), static_cast<juce::int64>(first), static_cast<int>(frames));
                if (read && progress != nullptr) {
                    progress->store(0.9 * static_cast<double>(first + frames) / reader->lengthInSamples, std::memory_order_relaxed);
                }
                return read;
            });
        if (importCancelled(cancel)) {
            return juce::Result::fail("Source import cancelled.");
        }
        if (!prepared) {
            return juce::Result::fail(juce::String(prepared.error));
        }
        asset.audio = prepared.audio;
        asset.source.reset();
        asset.drawing.reset();
        if (progress != nullptr) {
            progress->store(1.0, std::memory_order_relaxed);
        }
        return juce::Result::ok();
    }
    if (extension == ".gpla") {
        return decodeGpla(asset, cancel, progress);
    }
    if (extension == ".json" || extension == ".lottie") {
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
        if (importCancelled(cancel)) {
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
        const auto prepared = osci::fractal::prepare(content, asset.fractalDepth, cancel);
        if (!prepared) { return juce::Result::fail(prepared.error); }
        shapes.reserve(prepared.segments.size());
        for (const auto& segment : prepared.segments) {
            shapes.push_back(std::make_unique<osci::Line>(osci::Point(segment[0], segment[1], 0), osci::Point(segment[2], segment[3], 0)));
        }
    } else if (extension == ".obj") {
        WorldObject object(content.toStdString());
        shapes = object.draw();
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
            if (importCancelled(cancel)) { return juce::Result::fail("Source preparation cancelled."); }
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

static juce::XmlElement saveCompositionContent(const Composition& state) {
    juce::XmlElement xml("composition");
    xml.setAttribute("name", state.name);
    xml.setAttribute("duration", exactBakeNumber(state.duration));
    xml.setAttribute("fps", exactBakeNumber(state.frameRate));
    xml.setAttribute("bpm", exactBakeNumber(state.bpm));
    xml.setAttribute("timeDisplay", static_cast<int>(state.timeDisplay));
    xml.setAttribute("beatsPerBar", state.beatsPerBar);
    xml.setAttribute("snapBeats", exactBakeNumber(state.snapBeats));
    xml.setAttribute("gridSnap", state.gridSnap);
    if (state.hasLoop()) {
        xml.setAttribute("loopStart", exactBakeNumber(state.loopStart));
        xml.setAttribute("loopEnd", exactBakeNumber(state.loopEnd));
        xml.setAttribute("looping", state.looping);
    }
    if (state.tempoChanges != nullptr) {
        for (const auto& change : *state.tempoChanges) {
            auto* item = xml.createNewChildElement("tempo");
            item->setAttribute("beat", exactBakeNumber(change.beat));
            item->setAttribute("bpm", exactBakeNumber(change.bpm));
            if (change.ramp) { item->setAttribute("ramp", true); }
        }
    }
    saveEffects(xml, state.effects);
    for (const auto& group : state.groups) {
        auto* item = xml.createNewChildElement("group");
        item->setAttribute("id", juce::String(group.id));
        item->setAttribute("name", juce::String(group.name));
        item->setAttribute("parent", juce::String(group.parent));
        item->setAttribute("muted", group.muted);
        item->setAttribute("solo", group.solo);
        if (group.spatialPath) { item->setAttribute("spatialPath", true); }
        if (group.quaternionRotation) { item->setAttribute("quaternionRotation", true); }
        saveEffects(*item, group.effects);
        for (const auto& [name, curve] : group.properties) {
            saveProperty(*item, name, curve);
        }
    }
    for (const auto& track : state.tracks) {
        auto* row = xml.createNewChildElement("track");
        row->setAttribute("id", juce::String(track.id));
        row->setAttribute("name", juce::String(track.name));
        row->setAttribute("muted", track.muted);
        row->setAttribute("solo", track.solo);
        row->setAttribute("locked", track.locked);
        row->setAttribute("group", juce::String(track.group));
        row->setAttribute("kind", track.kind == TrackKind::audio ? "audio" : "visual");
        if (track.midiInput != 0) { row->setAttribute("midiInput", track.midiInput); }
        if (track.height != 0) { row->setAttribute("height", track.height); }
        if (track.label != 0) { row->setAttribute("label", track.label); }
        saveEffects(*row, track.effects);
        for (const auto& clip : track.clips) {
            auto* item = row->createNewChildElement("clip");
            item->setAttribute("id", juce::String(clip.id));
            if (clip.composition != 0) {
                item->setAttribute("composition", juce::String(clip.composition));
            } else {
                item->setAttribute("asset", juce::String(clip.asset));
            }
            item->setAttribute("name", juce::String(clip.name));
            item->setAttribute("timeBase", clip.timeBase == ClipTimeBase::beats ? "beats" : "seconds");
            item->setAttribute("contentBpm", exactBakeNumber(clip.contentBpm));
            item->setAttribute("start", exactBakeNumber(clip.start));
            item->setAttribute("duration", exactBakeNumber(clip.duration));
            item->setAttribute("offset", exactBakeNumber(clip.offset));
            item->setAttribute("rate", exactBakeNumber(clip.rate));
            if (clip.spatialPath) { item->setAttribute("spatialPath", true); }
            if (clip.quaternionRotation) { item->setAttribute("quaternionRotation", true); }
            if (track.kind == TrackKind::visual && clip.composition == 0) {
                auto* instrument = item->createNewChildElement("instrument");
                instrument->setAttribute("attack", exactBakeNumber(clip.instrument.attack));
                instrument->setAttribute("decay", exactBakeNumber(clip.instrument.decay));
                instrument->setAttribute("sustain", exactBakeNumber(clip.instrument.sustain));
                instrument->setAttribute("release", exactBakeNumber(clip.instrument.release));
                if (clip.instrument.bendRange != MidiInstrument{}.bendRange) { instrument->setAttribute("bendRange", exactBakeNumber(clip.instrument.bendRange)); }
            }
            if (clip.midi != nullptr) {
                auto* pattern = item->createNewChildElement("midi");
                pattern->setAttribute("asset", juce::String(clip.midiAsset));
                for (const auto& note : clip.midi->notes()) {
                    auto* event = pattern->createNewChildElement("note");
                    event->setAttribute("id", juce::String(note.id));
                    event->setAttribute("start", exactBakeNumber(note.start));
                    event->setAttribute("duration", exactBakeNumber(note.duration));
                    event->setAttribute("pitch", note.pitch);
                    event->setAttribute("velocity", note.velocity);
                    event->setAttribute("channel", note.channel);
                }
                for (const auto& control : clip.midi->controls()) {
                    auto* change = pattern->createNewChildElement("control");
                    change->setAttribute("beat", exactBakeNumber(control.beat));
                    change->setAttribute("channel", control.channel);
                    change->setAttribute("number", control.number);
                    change->setAttribute("value", control.value);
                }
            }
            saveEffects(*item, clip.effects);
            for (const auto& [name, curve] : clip.properties) {
                saveProperty(*item, name, curve);
            }
            if (clip.luaBake != nullptr && clip.luaBake->archive.getSize() > 0) {
                auto* bake = item->createNewChildElement("luaBake");
                bake->setAttribute("key", juce::String(clip.luaBake->key));
                bake->addTextElement(clip.luaBake->archive.toBase64Encoding());
            }
        }
    }
    for (const auto& camera : state.cameras) {
        auto* item = xml.createNewChildElement("camera");
        item->setAttribute("id", juce::String(camera.id));
        item->setAttribute("name", juce::String(camera.name));
        if (camera.target != 0) { item->setAttribute("target", juce::String(camera.target)); }
        if (camera.parent != 0) { item->setAttribute("parent", juce::String(camera.parent)); }
        for (const auto& [name, curve] : camera.properties) {
            saveProperty(*item, name, curve);
        }
    }
    for (const auto& marker : state.markers) {
        auto* item = xml.createNewChildElement("marker");
        item->setAttribute("id", juce::String(marker.id));
        item->setAttribute("time", exactBakeNumber(marker.time));
        item->setAttribute("name", marker.name);
    }
    for (const auto& cut : state.cameraCuts) {
        auto* item = xml.createNewChildElement("cameraCut");
        item->setAttribute("id", juce::String(cut.id));
        item->setAttribute("camera", juce::String(cut.camera));
        item->setAttribute("start", exactBakeNumber(cut.start));
        item->setAttribute("duration", exactBakeNumber(cut.duration));
    }
    for (const auto& modulator : state.modulators) {
        auto* item = xml.createNewChildElement("modulator");
        item->setAttribute("id", juce::String(modulator.id));
        item->setAttribute("name", juce::String(modulator.name));
        item->setAttribute("kind", modulator.kind == ModulatorKind::envelope ? "envelope" : modulator.kind == ModulatorKind::controller ? "controller" : "oscillator");
        item->setAttribute("controller", modulator.controller);
        item->setAttribute("controllerChannel", modulator.controllerChannel);
        item->setAttribute("waveform", static_cast<int>(modulator.shape.waveform));
        item->setAttribute("rateHz", exactBakeNumber(modulator.shape.rateHz));
        item->setAttribute("phase", exactBakeNumber(modulator.shape.phase));
        item->setAttribute("tempoSync", modulator.shape.tempoSync);
        item->setAttribute("beatsPerCycle", exactBakeNumber(modulator.shape.beatsPerCycle));
        item->setAttribute("seed", juce::String(static_cast<juce::int64>(modulator.shape.seed)));
        item->setAttribute("source", juce::String(modulator.source));
        item->setAttribute("attack", exactBakeNumber(modulator.attack));
        item->setAttribute("decay", exactBakeNumber(modulator.decay));
        item->setAttribute("sustain", exactBakeNumber(modulator.sustain));
        item->setAttribute("release", exactBakeNumber(modulator.release));
        item->setAttribute("velocity", exactBakeNumber(modulator.velocity));
        item->setAttribute("lowestPitch", modulator.lowestPitch);
        item->setAttribute("highestPitch", modulator.highestPitch);
    }
    for (const auto& route : state.routes) {
        auto* item = xml.createNewChildElement("route");
        item->setAttribute("id", juce::String(route.id));
        item->setAttribute("modulator", juce::String(route.modulator));
        item->setAttribute("target", juce::String(route.target));
        item->setAttribute("property", juce::String(route.property));
        item->setAttribute("amount", exactBakeNumber(route.amount));
        item->setAttribute("mode", static_cast<int>(route.mode));
    }
    return xml;
}

juce::XmlElement Document::save() const {
    auto xml = saveCompositionContent(state);
    xml.setAttribute("scopeDwell", exactBakeNumber(state.scope.dwellMicros));
    xml.setAttribute("scopeTravel", exactBakeNumber(state.scope.travelMicrosPerUnit));
    xml.setAttribute("scopeSettle", exactBakeNumber(state.scope.settleMicros));
    for (const auto& asset : state.assets) {
        auto* item = xml.createNewChildElement("asset");
        item->setAttribute("id", juce::String(asset->id));
        item->setAttribute("name", asset->name);
        item->setAttribute("extension", asset->extension);
        if (asset->extension.equalsIgnoreCase(".blender")) {
            auto* live = item->createNewChildElement("blender");
            live->setAttribute("port", asset->blenderSettings.port);
            live->setAttribute("disconnect", asset->blenderSettings.freezeOnDisconnect ? "freeze" : "blank");
            continue;
        }
        if (asset->extension.equalsIgnoreCase(".lsystem")) { item->setAttribute("fractalDepth", asset->fractalDepth); }
        if (isMidiSource(asset->extension)) { item->setAttribute("midiImportBpm", exactBakeNumber(asset->midiImportBpm)); }
        if (asset->extension.equalsIgnoreCase(".lua")) {
            item->createNewChildElement("source")->addTextElement(asset->data.toBase64Encoding());
            auto* bake = item->createNewChildElement("bake");
            bake->setAttribute("duration", exactBakeNumber(asset->bakeSettings.duration));
            bake->setAttribute("frameRate", exactBakeNumber(asset->bakeSettings.frameRate));
            bake->setAttribute("bpm", exactBakeNumber(asset->bakeSettings.bpm));
            bake->setAttribute("pointsPerFrame", static_cast<int>(asset->bakeSettings.pointsPerFrame));
            bake->setAttribute("seed", juce::String(asset->bakeSettings.seed));
            bake->setAttribute("key", asset->bakeKey);
            bake->addTextElement(asset->bakedData.toBase64Encoding());
        } else {
            if (isRasterSource(asset->extension)) {
                auto* raster = item->createNewChildElement("raster");
                raster->setAttribute("mode", asset->rasterSettings.mode == RasterSettings::Mode::contours ? "contours" : "scanlines");
                raster->setAttribute("threshold", exactBakeNumber(asset->rasterSettings.threshold));
                raster->setAttribute("invert", asset->rasterSettings.invert);
                raster->setAttribute("resolution", asset->rasterSettings.resolution);
                if (isVideoSource(asset->extension)) { raster->setAttribute("frameRate", exactBakeNumber(asset->rasterSettings.videoFrameRate)); }
                raster->setAttribute("pointsPerFrame", static_cast<int>(asset->rasterSettings.pointsPerFrame));
            }
            if (asset->extension.equalsIgnoreCase(".txt")) {
                auto* text = item->createNewChildElement("typography");
                text->setAttribute("family", asset->textSettings.family);
                text->setAttribute("style", asset->textSettings.style);
                text->setAttribute("alignment", asset->textSettings.alignment);
                text->setAttribute("lineSpacing", exactBakeNumber(asset->textSettings.lineSpacing));
                text->setAttribute("tracking", exactBakeNumber(asset->textSettings.tracking));
                if (asset->textSettings.animated()) {
                    text->setAttribute("animation", static_cast<int>(asset->textSettings.animation));
                    text->setAttribute("characterDelay", exactBakeNumber(asset->textSettings.characterDelay));
                    text->setAttribute("characterDuration", exactBakeNumber(asset->textSettings.characterDuration));
                    text->setAttribute("hold", exactBakeNumber(asset->textSettings.hold));
                    text->setAttribute("amount", exactBakeNumber(asset->textSettings.amount));
                }
            }
            // Keep mixed-content payloads last. JUCE's single-line binary XML
            // writer can attempt a null newline when wrapping attributes on an
            // element following a text node.
            if (isVideoSource(asset->extension)) {
                item->createNewChildElement("source")->addTextElement(asset->data.toBase64Encoding());
                auto* cache = item->createNewChildElement("video-cache");
                cache->setAttribute("key", asset->bakeKey);
                cache->addTextElement(asset->bakedData.toBase64Encoding());
            } else {
                item->addTextElement(asset->data.toBase64Encoding());
            }
        }
    }
    for (const auto& definition : state.definitions) {
        auto* item = xml.createNewChildElement("definition");
        item->setAttribute("id", juce::String(definition->id));
        item->addChildElement(new juce::XmlElement(saveCompositionContent(*definition)));
    }
    return xml;
}

static juce::Result loadCompositionContent(const juce::XmlElement& xml, Composition& project, const std::vector<std::shared_ptr<const Asset>>& assets, std::set<Id>& identities, const std::set<Id>& compositionIds) {
    if (!xml.hasTagName("composition")) {
        return juce::Result::fail("Missing composition.");
    }
    project.name = xml.getStringAttribute("name", "Untitled");
    project.duration = xml.getDoubleAttribute("duration", 180);
    project.frameRate = xml.getDoubleAttribute("fps", 30);
    project.bpm = xml.getDoubleAttribute("bpm", 120);
    const auto display = xml.getIntAttribute("timeDisplay", 0);
    const auto snap = xml.getIntAttribute("gridSnap", 1);
    project.timeDisplay = static_cast<TimeDisplay>(display);
    project.beatsPerBar = xml.getIntAttribute("beatsPerBar", 4);
    project.snapBeats = xml.getDoubleAttribute("snapBeats", 0.25);
    project.gridSnap = snap != 0;
    project.loopStart = xml.getDoubleAttribute("loopStart", 0);
    project.loopEnd = xml.getDoubleAttribute("loopEnd", 0);
    project.looping = xml.getBoolAttribute("looping", false);
    project.loopEnd = std::min(project.loopEnd, project.duration);
    if (!project.hasLoop()) { project.loopStart = project.loopEnd = 0; project.looping = false; }
    if (!std::isfinite(project.duration) || project.duration <= 0 || !std::isfinite(project.frameRate)
        || project.frameRate < 0.001 || project.frameRate > 1000 || !std::isfinite(project.bpm) || project.bpm < 1 || project.bpm > 1000
        || display < 0 || display > 2 || snap < 0 || snap > 1 || project.beatsPerBar < 1 || project.beatsPerBar > 32
        || !std::isfinite(project.snapBeats) || project.snapBeats < 1.0 / 64 || project.snapBeats > 64) {
        return juce::Result::fail("Invalid composition timing.");
    }
    std::vector<TempoChange> tempoChanges;
    for (auto* item : xml.getChildWithTagNameIterator("tempo")) {
        tempoChanges.push_back({item->getDoubleAttribute("beat", -1), item->getDoubleAttribute("bpm", -1), item->getBoolAttribute("ramp", false)});
    }
    if (tempoChanges.size() > 10000) { return juce::Result::fail("A composition supports at most 10000 tempo changes."); }
    if (!tempoChanges.empty()) { project.tempoChanges = std::make_shared<const std::vector<TempoChange>>(std::move(tempoChanges)); }
    if (!project.tempo().valid()) { return juce::Result::fail("Tempo changes need increasing beats after the start and 1-1000 BPM."); }
    const auto projectEffects = loadEffects(xml, project.effects, identities);
    if (projectEffects.failed()) {
        return projectEffects;
    }
    for (auto* item : xml.getChildWithTagNameIterator("group")) {
        Group group;
        const auto identity = item->getStringAttribute("id").getLargeIntValue();
        const auto parent = item->getStringAttribute("parent", "0").getLargeIntValue();
        if (identity <= 0 || parent < 0 || !identities.insert(static_cast<Id>(identity)).second) {
            return juce::Result::fail("Invalid or duplicate group identity.");
        }
        group.id = static_cast<Id>(identity);
        group.parent = static_cast<Id>(parent);
        group.name = item->getStringAttribute("name", "Group").toStdString();
        group.muted = item->getBoolAttribute("muted", false);
        group.solo = item->getBoolAttribute("solo", false);
        group.spatialPath = item->getBoolAttribute("spatialPath", false);
        group.quaternionRotation = item->getBoolAttribute("quaternionRotation", false);
        const auto effects = loadEffects(*item, group.effects, identities);
        if (effects.failed()) {
            return effects;
        }
        std::set<std::string> properties;
        for (auto* property : item->getChildWithTagNameIterator("property")) {
            const auto name = property->getStringAttribute("name").toStdString();
            const auto found = group.properties.find(name);
            if (found == group.properties.end() || !properties.insert(name).second) {
                return juce::Result::fail("Unknown or duplicate group property.");
            }
            const auto result = loadProperty(*property, found->second);
            if (result.failed()) {
                return result;
            }
        }
        if (!group.valid()) {
            return juce::Result::fail("Invalid group transform, tint or drawing weight.");
        }
        project.groups.push_back(std::move(group));
    }
    for (auto* row : xml.getChildWithTagNameIterator("track")) {
        Track track;
        track.id = static_cast<Id>(row->getStringAttribute("id").getLargeIntValue());
        track.name = row->getStringAttribute("name").toStdString();
        const auto kind = row->getStringAttribute("kind");
        if (kind != "visual" && kind != "audio") {
            return juce::Result::fail("Track kind must be visual or audio.");
        }
        track.kind = kind == "audio" ? TrackKind::audio : TrackKind::visual;
        track.muted = row->getBoolAttribute("muted", false);
        track.solo = row->getBoolAttribute("solo", false);
        track.locked = row->getBoolAttribute("locked", false);
        track.midiInput = row->getIntAttribute("midiInput", 0);
        track.height = std::clamp(row->getIntAttribute("height", 0), 0, Track::maximumHeight);
        if (track.height != 0) { track.height = std::max(track.height, Track::minimumHeight); }
        track.label = std::clamp(row->getIntAttribute("label", 0), 0, 8);
        if (track.midiInput < 0 || track.midiInput > Track::anyMidiChannel || (track.midiInput != 0 && track.kind != TrackKind::visual)) {
            return juce::Result::fail("MIDI input needs a visual track and a channel 1-16 (or any).");
        }
        const auto groupIdentity = row->getStringAttribute("group", "0").getLargeIntValue();
        if (groupIdentity < 0) {
            return juce::Result::fail("Invalid track group identity.");
        }
        track.group = static_cast<Id>(groupIdentity);
        if (track.id == 0 || !identities.insert(track.id).second) {
            return juce::Result::fail("Invalid track identity.");
        }
        const auto trackEffects = loadEffects(*row, track.effects, identities);
        if (trackEffects.failed()) {
            return trackEffects;
        }
        if (track.kind == TrackKind::audio && !track.effects.empty()) {
            return juce::Result::fail("Audio tracks cannot contain visual effects.");
        }
        for (auto* item : row->getChildWithTagNameIterator("clip")) {
            Clip clip;
            clip.id = static_cast<Id>(item->getStringAttribute("id").getLargeIntValue());
            clip.asset = static_cast<Id>(item->getStringAttribute("asset").getLargeIntValue());
            clip.composition = static_cast<Id>(item->getStringAttribute("composition").getLargeIntValue());
            clip.name = item->getStringAttribute("name").toStdString();
            const auto timeBase = item->getStringAttribute("timeBase");
            if (timeBase != "seconds" && timeBase != "beats") { return juce::Result::fail("Clip timing must be seconds or beats."); }
            clip.timeBase = timeBase == "beats" ? ClipTimeBase::beats : ClipTimeBase::seconds;
            clip.contentBpm = item->getDoubleAttribute("contentBpm", 0);
            clip.start = item->getDoubleAttribute("start");
            clip.duration = item->getDoubleAttribute("duration");
            clip.offset = item->getDoubleAttribute("offset");
            clip.rate = item->getDoubleAttribute("rate", 1);
            clip.spatialPath = item->getBoolAttribute("spatialPath", false);
            clip.quaternionRotation = item->getBoolAttribute("quaternionRotation", false);
            const auto found = std::find_if(assets.begin(), assets.end(), [&](const auto& asset) { return asset->id == clip.asset; });
            if (clip.id == 0 || !identities.insert(clip.id).second) { return juce::Result::fail("Invalid clip identity."); }
            if (clip.composition != 0) {
                if (clip.asset != 0 || !compositionIds.contains(clip.composition) || track.kind != TrackKind::visual) {
                    return juce::Result::fail("Invalid reusable composition reference or track kind.");
                }
            } else {
                if (found == assets.end()) { return juce::Result::fail("Invalid clip asset."); }
                if ((track.kind == TrackKind::audio) != ((*found)->audio != nullptr)) {
                    return juce::Result::fail("The clip source type does not match its audio or visual track.");
                }
                if ((*found)->midi != nullptr) { return juce::Result::fail("A MIDI pattern requires a visual instrument source for its clip."); }
            }
            const auto* instrument = item->getChildByName("instrument");
            if (instrument != nullptr) {
                clip.instrument = {instrument->getDoubleAttribute("attack", -1), instrument->getDoubleAttribute("decay", -1),
                    instrument->getDoubleAttribute("sustain", -1), instrument->getDoubleAttribute("release", -1),
                    instrument->getDoubleAttribute("bendRange", MidiInstrument{}.bendRange)};
                if (track.kind != TrackKind::visual || clip.composition != 0 || instrument->getNextElementWithTagName("instrument") != nullptr || !clip.instrument.valid()) {
                    return juce::Result::fail("Invalid MIDI envelope settings.");
                }
            }
            const auto* pattern = item->getChildByName("midi");
            if (pattern != nullptr) {
                if (track.kind != TrackKind::visual || pattern->getNextElementWithTagName("midi") != nullptr) {
                    return juce::Result::fail("MIDI performances require a single pattern on a visual clip.");
                }
                const auto assetText = pattern->getStringAttribute("asset").toStdString();
                const auto parsedAsset = std::from_chars(assetText.data(), assetText.data() + assetText.size(), clip.midiAsset);
                if (parsedAsset.ec != std::errc() || parsedAsset.ptr != assetText.data() + assetText.size()) {
                    return juce::Result::fail("Invalid MIDI source identity.");
                }
                if (clip.midiAsset != 0) {
                    const auto source = std::find_if(assets.begin(), assets.end(), [&](const auto& asset) { return asset->id == clip.midiAsset; });
                    if (source == assets.end() || (*source)->midi == nullptr) { return juce::Result::fail("MIDI pattern source is missing or is not a MIDI asset."); }
                }
                std::vector<MidiNote> notes;
                for (auto* event : pattern->getChildWithTagNameIterator("note")) {
                    if (notes.size() >= MidiNotes::maximumNotes) { return juce::Result::fail("MIDI content exceeds 100000 notes."); }
                    const auto idText = event->getStringAttribute("id").toStdString();
                    Id id = 0;
                    const auto parsedId = std::from_chars(idText.data(), idText.data() + idText.size(), id);
                    if (id == 0 || parsedId.ec != std::errc() || parsedId.ptr != idText.data() + idText.size()) { return juce::Result::fail("Invalid MIDI note identity."); }
                    const auto readNumber = [&](const char* name, auto& value) {
                        std::istringstream stream(event->getStringAttribute(name).toStdString());
                        stream.imbue(std::locale::classic());
                        stream >> std::noskipws >> value;
                        return !stream.fail() && stream.peek() == std::char_traits<char>::eof();
                    };
                    MidiNote note;
                    note.id = id;
                    if (!readNumber("start", note.start) || !readNumber("duration", note.duration)
                        || !readNumber("pitch", note.pitch) || !readNumber("velocity", note.velocity) || !readNumber("channel", note.channel)) {
                        return juce::Result::fail("MIDI note fields must contain valid numbers.");
                    }
                    notes.push_back(note);
                }
                std::vector<MidiControl> controls;
                for (auto* change : pattern->getChildWithTagNameIterator("control")) {
                    if (controls.size() >= MidiNotes::maximumControls) { return juce::Result::fail("MIDI content exceeds 400000 controller changes."); }
                    controls.push_back({change->getDoubleAttribute("beat", -1), change->getIntAttribute("channel", 0), change->getIntAttribute("number", -1), change->getIntAttribute("value", 100000)});
                }
                const auto prepared = MidiNotes::create(std::move(notes), std::move(controls));
                if (!prepared) { return juce::Result::fail(prepared.error); }
                clip.midi = prepared.source;
            }
            // Every clip property must be a known, unique schema entry whose
            // values lie inside its declared range.
            const auto specs = track.kind == TrackKind::audio ? audioPropertySpecs() : objectPropertySpecs();
            const bool luaClip = clip.composition == 0 && found != assets.end() && (*found)->extension.equalsIgnoreCase(".lua");
            for (auto* property : item->getChildWithTagNameIterator("property")) {
                const auto name = property->getStringAttribute("name").toStdString();
                auto* spec = findPropertySpec(specs, name);
                if (spec == nullptr && luaClip) { spec = findPropertySpec(luaSliderSpecs(), name); }
                if (spec == nullptr || clip.properties.contains(name)) {
                    return juce::Result::fail("Unknown or duplicate clip property \"" + juce::String(name) + "\".");
                }
                Curve curve;
                const auto result = loadProperty(*property, curve);
                if (result.failed()) {
                    return result;
                }
                const auto inRange = [spec](double value) { return std::isfinite(value) && value >= spec->minimum && value <= spec->maximum; };
                if (!inRange(curve.base) || std::any_of(curve.keyframes().begin(), curve.keyframes().end(), [&](const auto& key) { return !inRange(key.value); })) {
                    return juce::Result::fail(juce::String(spec->label.data(), spec->label.size()) + " is outside its allowed range.");
                }
                clip.properties[name] = std::move(curve);
            }
            const auto* luaBake = item->getChildByName("luaBake");
            if (luaBake != nullptr) {
                // Saved slider bakes load without executing the script.
                auto bake = std::make_shared<LuaClipBake>();
                bake->key = luaBake->getStringAttribute("key").toStdString();
                const auto encoded = luaBake->getAllSubText();
                if (!luaClip || bake->key.empty() || static_cast<std::size_t>(encoded.length()) > (64 * 1024 * 1024 / 3 + 1) * 4
                    || !bake->archive.fromBase64Encoding(encoded) || bake->archive.getSize() == 0) {
                    return juce::Result::fail("Invalid or oversized Lua slider bake.");
                }
                const auto frames = BakedSourceArchive::decode(bake->archive);
                if (!frames) { return juce::Result::fail(juce::String(frames.error)); }
                // Only a bake matching this clip's current sliders is kept; a
                // stale one is dropped and the editor bakes again.
                const auto plan = luaSliderPlan(**found, clip, project.tempo());
                if (plan.has_value() && plan->key == bake->key && frames.source->frameCount() == plan->settings.frameCount()
                    && frames.source->frameRate() == plan->settings.frameRate) {
                    bake->source = std::make_shared<const PreparedSource>(frames.source);
                    clip.luaBake = std::move(bake);
                }
            }
            const auto clipEffects = loadEffects(*item, clip.effects, identities);
            if (clipEffects.failed()) {
                return clipEffects;
            }
            if (track.kind == TrackKind::audio) {
                if (!clip.effects.empty() || clip.properties.size() != 2 || !clip.properties.contains("gain") || !clip.properties.contains("pan")) {
                    return juce::Result::fail("Audio clips require gain and pan curves and cannot contain visual effects or properties.");
                }
                for (const auto* name : { "gain", "pan" }) {
                    const auto& curve = clip.properties.at(name);
                    const bool gain = std::string_view(name) == "gain";
                    const auto validValue = [gain](double value) { return std::isfinite(value) && value >= (gain ? 0.0 : -1.0) && value <= (gain ? 4.0 : 1.0); };
                    if (!curve.valid() || !validValue(curve.base) || std::any_of(curve.keyframes().begin(), curve.keyframes().end(), [&](const auto& key) { return !validValue(key.value); })) {
                        return juce::Result::fail("Audio gain must be between 0 and 4, and pan between -1 and 1.");
                    }
                }
            }
            if (!track.insert(std::move(clip), project.tempo())) {
                return juce::Result::fail("Invalid or overlapping clip range.");
            }
        }
        project.tracks.push_back(std::move(track));
    }
    for (auto* item : xml.getChildWithTagNameIterator("camera")) {
        Camera camera;
        const auto identity = item->getStringAttribute("id").getLargeIntValue();
        camera.id = static_cast<Id>(identity);
        camera.name = item->getStringAttribute("name", "Camera").toStdString();
        if (identity <= 0 || !identities.insert(camera.id).second) {
            return juce::Result::fail("Invalid camera identity.");
        }
        const auto target = item->getStringAttribute("target", "0").getLargeIntValue();
        const auto parent = item->getStringAttribute("parent", "0").getLargeIntValue();
        camera.target = target > 0 ? static_cast<Id>(target) : 0;
        camera.parent = parent > 0 ? static_cast<Id>(parent) : 0;
        if (target < 0 || parent < 0 || (camera.parent != 0 && findGroup(project, camera.parent) == nullptr)
            || (camera.target != 0 && findGroup(project, camera.target) == nullptr && !hasVisualClip(project, camera.target))) {
            return juce::Result::fail("A camera aims at or is parented to a missing object.");
        }
        std::set<std::string> properties;
        for (auto* property : item->getChildWithTagNameIterator("property")) {
            const auto name = property->getStringAttribute("name").toStdString();
            const auto found = camera.properties.find(name);
            if (found == camera.properties.end() || !properties.insert(name).second) {
                return juce::Result::fail("Unknown or duplicate camera property.");
            }
            const auto result = loadProperty(*property, found->second);
            if (result.failed()) {
                return result;
            }
        }
        if (!camera.valid()) {
            return juce::Result::fail("Invalid camera transform or field of view.");
        }
        project.cameras.push_back(std::move(camera));
    }
    for (auto* item : xml.getChildWithTagNameIterator("marker")) {
        const auto id = item->getStringAttribute("id").getLargeIntValue();
        Marker marker {static_cast<Id>(id), item->getDoubleAttribute("time", -1), item->getStringAttribute("name")};
        if (id <= 0 || !identities.insert(marker.id).second || !std::isfinite(marker.time) || marker.time < 0 || marker.time > project.duration
            || marker.name.trim().isEmpty() || marker.name.length() > 120 || marker.name.containsChar('\n') || marker.name.containsChar('\r')) {
            return juce::Result::fail("Invalid marker identity, position or name.");
        }
        project.markers.push_back(std::move(marker));
    }
    std::sort(project.markers.begin(), project.markers.end(), [](const auto& a, const auto& b) { return a.time != b.time ? a.time < b.time : a.id < b.id; });
    for (std::size_t index = 1; index < project.markers.size(); ++index) {
        if (project.markers[index].time - project.markers[index - 1].time < 1.0e-9) { return juce::Result::fail("Markers must have distinct positions."); }
    }
    for (auto* item : xml.getChildWithTagNameIterator("cameraCut")) {
        CameraCut cut;
        const auto identity = item->getStringAttribute("id").getLargeIntValue();
        const auto cameraIdentity = item->getStringAttribute("camera").getLargeIntValue();
        cut.id = static_cast<Id>(identity);
        cut.camera = static_cast<Id>(cameraIdentity);
        cut.start = item->getDoubleAttribute("start");
        cut.duration = item->getDoubleAttribute("duration");
        const auto camera = std::find_if(project.cameras.begin(), project.cameras.end(), [&](const auto& value) { return value.id == cut.camera; });
        if (identity <= 0 || cameraIdentity <= 0 || !cut.valid() || !identities.insert(cut.id).second || camera == project.cameras.end()) {
            return juce::Result::fail("Invalid camera cut range, reference or identity.");
        }
        project.cameraCuts.push_back(cut);
    }
    std::sort(project.cameraCuts.begin(), project.cameraCuts.end(), [](const auto& left, const auto& right) { return left.start < right.start; });
    for (std::size_t index = 1; index < project.cameraCuts.size(); ++index) {
        if (project.cameraCuts[index].start < project.cameraCuts[index - 1].end()) {
            return juce::Result::fail("Camera cuts must not overlap.");
        }
    }
    if (!validGroupHierarchy(project)) {
        return juce::Result::fail("Groups require existing parents and track references, no cycles, and at most 32 nesting levels.");
    }
    const auto identity = [](const juce::XmlElement& item, const char* name) {
        const auto value = item.getStringAttribute(name).getLargeIntValue();
        return value > 0 ? static_cast<Id>(value) : Id(0);
    };
    for (auto* item : xml.getChildWithTagNameIterator("modulator")) {
        Modulator modulator;
        modulator.id = identity(*item, "id");
        modulator.name = item->getStringAttribute("name").toStdString();
        const auto kind = item->getStringAttribute("kind");
        const auto tempoSync = item->getIntAttribute("tempoSync", -1);
        const auto seed = item->getStringAttribute("seed", "0").getLargeIntValue();
        if ((kind != "oscillator" && kind != "envelope" && kind != "controller") || tempoSync < 0 || tempoSync > 1 || seed < 0 || seed > static_cast<juce::int64>(std::numeric_limits<std::uint32_t>::max())) {
            return juce::Result::fail("Invalid modulator settings.");
        }
        modulator.kind = kind == "envelope" ? ModulatorKind::envelope : kind == "controller" ? ModulatorKind::controller : ModulatorKind::oscillator;
        modulator.controller = item->getIntAttribute("controller", 1);
        modulator.controllerChannel = item->getIntAttribute("controllerChannel", 0);
        modulator.shape.enabled = true;
        modulator.shape.amount = 1;
        modulator.shape.waveform = static_cast<ModulationWaveform>(item->getIntAttribute("waveform", -1));
        modulator.shape.rateHz = item->getDoubleAttribute("rateHz", 1);
        modulator.shape.phase = item->getDoubleAttribute("phase", 0);
        modulator.shape.tempoSync = tempoSync != 0;
        modulator.shape.beatsPerCycle = item->getDoubleAttribute("beatsPerCycle", 1);
        modulator.shape.seed = static_cast<std::uint32_t>(seed);
        modulator.source = identity(*item, "source");
        modulator.attack = item->getDoubleAttribute("attack", -1);
        modulator.decay = item->getDoubleAttribute("decay", -1);
        modulator.sustain = item->getDoubleAttribute("sustain", -1);
        modulator.release = item->getDoubleAttribute("release", -1);
        modulator.velocity = item->getDoubleAttribute("velocity", -1);
        modulator.lowestPitch = item->getIntAttribute("lowestPitch", -1);
        modulator.highestPitch = item->getIntAttribute("highestPitch", -1);
        if (!modulator.valid() || !identities.insert(modulator.id).second) { return juce::Result::fail("Invalid modulator settings or identity."); }
        project.modulators.push_back(std::move(modulator));
    }
    for (auto* item : xml.getChildWithTagNameIterator("route")) {
        ModulationRoute route;
        route.id = identity(*item, "id");
        route.modulator = identity(*item, "modulator");
        route.target = identity(*item, "target");
        route.property = item->getStringAttribute("property").toStdString();
        route.amount = item->getDoubleAttribute("amount", 1);
        const auto mode = item->getIntAttribute("mode", -1);
        route.mode = static_cast<ModulationMode>(mode);
        if (mode < 0 || mode > 1 || !route.valid() || !identities.insert(route.id).second) { return juce::Result::fail("Invalid modulation route settings or identity."); }
        project.routes.push_back(std::move(route));
    }
    const auto modulation = validateModulation(project);
    if (!modulation.empty()) { return juce::Result::fail(juce::String(modulation)); }
    return juce::Result::ok();
}
juce::Result Document::load(const juce::XmlElement& xml) {
    Project prepared;
    const auto result = prepareLoad(xml, prepared);
    if (result.wasOk()) { reset(std::move(prepared)); }
    return result;
}

juce::Result Document::prepareLoad(const juce::XmlElement& xml, Project& output, const std::atomic<bool>* cancel) try {
    if (importCancelled(cancel)) { return juce::Result::fail("Project loading cancelled."); }
    if (!xml.hasTagName("composition")) { return juce::Result::fail("Missing composition."); }
    Project project;
    const ScopeProfile defaults;
    project.scope.dwellMicros = xml.getDoubleAttribute("scopeDwell", defaults.dwellMicros);
    project.scope.travelMicrosPerUnit = xml.getDoubleAttribute("scopeTravel", defaults.travelMicrosPerUnit);
    project.scope.settleMicros = xml.getDoubleAttribute("scopeSettle", defaults.settleMicros);
    if (!project.scope.valid()) { return juce::Result::fail("Invalid scope timing profile."); }
    std::set<Id> identities, compositionIds;
    for (auto* item : xml.getChildWithTagNameIterator("definition")) {
        const auto identity = item->getStringAttribute("id").getLargeIntValue();
        if (identity <= 0 || !identities.insert(static_cast<Id>(identity)).second) { return juce::Result::fail("Invalid reusable composition identity."); }
        compositionIds.insert(static_cast<Id>(identity));
    }
    for (auto* item : xml.getChildWithTagNameIterator("asset")) {
        if (importCancelled(cancel)) { return juce::Result::fail("Project loading cancelled."); }
        auto asset = std::make_shared<Asset>();
        asset->id = static_cast<Id>(item->getStringAttribute("id").getLargeIntValue());
        asset->name = item->getStringAttribute("name");
        asset->extension = item->getStringAttribute("extension");
        if (asset->extension.equalsIgnoreCase(".blender")) {
            const auto* live = item->getChildByName("blender");
            if (live == nullptr || live->getNextElement() != nullptr || item->getNumChildElements() != 1 || asset->name.trim().isEmpty() || asset->id == 0 || !identities.insert(asset->id).second) { return juce::Result::fail("Invalid Blender source identity or settings."); }
            asset->blenderSettings.port = live->getIntAttribute("port", 0);
            const auto policy = live->getStringAttribute("disconnect");
            if (policy != "freeze" && policy != "blank") { return juce::Result::fail("Invalid Blender disconnect policy."); }
            asset->blenderSettings.freezeOnDisconnect = policy == "freeze";
            const auto result = decodeAsset(*asset, cancel);
            if (result.failed()) { return result; }
            project.assets.push_back(std::move(asset));
            continue;
        }
        if (asset->extension.equalsIgnoreCase(".lsystem")) { asset->fractalDepth = item->getIntAttribute("fractalDepth", -1); }
        if (isMidiSource(asset->extension)) { asset->midiImportBpm = item->getDoubleAttribute("midiImportBpm", 0); }
        if (asset->extension.equalsIgnoreCase(".txt")) {
            const auto* text = item->getChildByName("typography");
            if (text != nullptr) {
                asset->textSettings.family = text->getStringAttribute("family");
                asset->textSettings.style = text->getIntAttribute("style", -1);
                asset->textSettings.alignment = text->getIntAttribute("alignment", -1);
                asset->textSettings.lineSpacing = text->getDoubleAttribute("lineSpacing", -1);
                asset->textSettings.tracking = text->getDoubleAttribute("tracking", -1);
                const TextSettings defaults;
                asset->textSettings.animation = static_cast<TextSettings::Animation>(text->getIntAttribute("animation", 0));
                asset->textSettings.characterDelay = text->getDoubleAttribute("characterDelay", defaults.characterDelay);
                asset->textSettings.characterDuration = text->getDoubleAttribute("characterDuration", defaults.characterDuration);
                asset->textSettings.hold = text->getDoubleAttribute("hold", defaults.hold);
                asset->textSettings.amount = text->getDoubleAttribute("amount", defaults.amount);
            }
        }
        const bool luaSource = asset->extension.equalsIgnoreCase(".lua");
        const bool videoSource = isVideoSource(asset->extension);
        auto* source = luaSource || videoSource ? item->getChildByName("source") : item;
        if (source == nullptr) { return juce::Result::fail("Baked asset is missing its source."); }
        const auto encoded = source->getAllSubText();
        if (static_cast<std::size_t>(encoded.length()) > (maximumSourceBytes / 3 + 1) * 4) {
            return juce::Result::fail("Embedded source exceeds the 64 MiB import limit.");
        }
        if (asset->id == 0 || !identities.insert(asset->id).second || !asset->data.fromBase64Encoding(encoded)) {
            return juce::Result::fail("Invalid asset data or identity.");
        }
        if (isRasterSource(asset->extension)) {
            const auto* raster = item->getChildByName("raster");
            if (raster == nullptr) { return juce::Result::fail("Image asset is missing its preparation settings."); }
            const auto mode = raster->getStringAttribute("mode");
            if (mode != "contours" && mode != "scanlines") { return juce::Result::fail("Unknown image preparation mode."); }
            asset->rasterSettings.mode = mode == "contours" ? RasterSettings::Mode::contours : RasterSettings::Mode::scanlines;
            asset->rasterSettings.threshold = raster->getDoubleAttribute("threshold", -1);
            asset->rasterSettings.invert = raster->getBoolAttribute("invert");
            asset->rasterSettings.resolution = raster->getIntAttribute("resolution", 0);
            if (videoSource) { asset->rasterSettings.videoFrameRate = raster->getDoubleAttribute("frameRate", 0); }
            const auto points = raster->getIntAttribute("pointsPerFrame", 0);
            if (points <= 0) { return juce::Result::fail("Invalid image sample count."); }
            asset->rasterSettings.pointsPerFrame = static_cast<std::size_t>(points);
        }
        if (videoSource) {
            const auto* cache = item->getChildByName("video-cache");
            if (cache == nullptr) { return juce::Result::fail("Video asset is missing its prepared cache. Project loading never launches a decoder."); }
            asset->bakeKey = cache->getStringAttribute("key");
            const auto encodedCache = cache->getAllSubText();
            if (static_cast<std::size_t>(encodedCache.length()) > (64 * 1024 * 1024 / 3 + 1) * 4
                || !asset->bakedData.fromBase64Encoding(encodedCache) || asset->bakedData.getSize() == 0) {
                return juce::Result::fail("Invalid or oversized video source cache.");
            }
        }
        if (luaSource) {
            auto* bake = item->getChildByName("bake");
            if (bake == nullptr) { return juce::Result::fail("Lua asset is missing its prepared cache. Project loading never executes scripts."); }
            asset->bakeSettings.duration = bake->getDoubleAttribute("duration", 0);
            asset->bakeSettings.frameRate = bake->getDoubleAttribute("frameRate", 0);
            asset->bakeSettings.bpm = bake->getDoubleAttribute("bpm", 0);
            const auto points = bake->getIntAttribute("pointsPerFrame", 0);
            const auto seed = bake->getStringAttribute("seed", "-1").getLargeIntValue();
            if (points < 0 || seed < 0 || seed > std::numeric_limits<std::uint32_t>::max()) {
                return juce::Result::fail("Invalid Lua bake settings.");
            }
            asset->bakeSettings.pointsPerFrame = static_cast<std::size_t>(points);
            asset->bakeSettings.seed = static_cast<std::uint32_t>(seed);
            asset->bakeKey = bake->getStringAttribute("key");
            const auto cache = bake->getAllSubText();
            if (static_cast<std::size_t>(cache.length()) > (64 * 1024 * 1024 / 3 + 1) * 4
                || !asset->bakedData.fromBase64Encoding(cache) || asset->bakedData.getSize() == 0) {
                return juce::Result::fail("Invalid or oversized Lua source cache.");
            }
        }
        const auto result = decodeAsset(*asset, cancel);
        if (result.failed()) {
            return result;
        }
        project.assets.push_back(std::move(asset));
    }
    const auto main = loadCompositionContent(xml, project, project.assets, identities, compositionIds);
    if (main.failed()) { return main; }
    for (auto* item : xml.getChildWithTagNameIterator("definition")) {
        const auto* content = item->getChildByName("composition");
        if (content == nullptr || content->getNextElementWithTagName("composition") != nullptr) {
            return juce::Result::fail("A reusable definition requires exactly one composition.");
        }
        if (content->getChildByName("asset") != nullptr || content->getChildByName("definition") != nullptr) {
            return juce::Result::fail("Reusable definitions share the project media and definition registries.");
        }
        auto definition = std::make_shared<CompositionDefinition>();
        definition->id = static_cast<Id>(item->getStringAttribute("id").getLargeIntValue());
        const auto result = loadCompositionContent(*content, *definition, project.assets, identities, compositionIds);
        if (result.failed()) { return result; }
        project.definitions.push_back(std::move(definition));
    }
    const auto graph = validateCompositionGraph(project);
    if (!graph) { return juce::Result::fail(graph.error); }
    if (importCancelled(cancel)) { return juce::Result::fail("Project loading cancelled."); }
    output = std::move(project);
    return juce::Result::ok();
} catch (const std::exception& error) {
    return juce::Result::fail("Cannot prepare project: " + juce::String(error.what()));
}
}

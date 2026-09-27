#include "Document.h"
#include "CompositionGraph.h"
#include "../import/LuaBaker.h"
#include "../import/BakedSourceArchive.h"
#include "../import/RasterSourcePreparer.h"
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

bool importCancelled(const std::atomic<bool>* cancel) {
    return cancel != nullptr && cancel->load(std::memory_order_relaxed);
}

juce::Result prepareSourceFrames(Asset& asset, int frameCount, double frameRate, const std::function<juce::Result(int, ImportShapes&)>& draw, const std::atomic<bool>* cancel, std::atomic<double>* progress) {
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
    property->setAttribute("base", curve.base);
    auto* modulation = property->createNewChildElement("modulation");
    modulation->setAttribute("enabled", curve.modulation.enabled);
    modulation->setAttribute("waveform", static_cast<int>(curve.modulation.waveform));
    modulation->setAttribute("amount", curve.modulation.amount);
    modulation->setAttribute("rateHz", curve.modulation.rateHz);
    modulation->setAttribute("phase", curve.modulation.phase);
    modulation->setAttribute("tempoSync", curve.modulation.tempoSync);
    modulation->setAttribute("beatsPerCycle", curve.modulation.beatsPerCycle);
    modulation->setAttribute("seed", juce::String(static_cast<juce::int64>(curve.modulation.seed)));
    modulation->setAttribute("mode", static_cast<int>(curve.modulation.mode));
    for (const auto& key : curve.keyframes()) {
        auto* point = property->createNewChildElement("key");
        point->setAttribute("time", key.time);
        point->setAttribute("value", key.value);
        point->setAttribute("interpolation", static_cast<int>(key.interpolation));
        point->setAttribute("in", key.incomingSlope);
        point->setAttribute("out", key.outgoingSlope);
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
    for (auto* point : property.getChildWithTagNameIterator("key")) {
        const auto interpolation = point->getIntAttribute("interpolation");
        if (interpolation < 0 || interpolation > 3) {
            return juce::Result::fail("Unknown interpolation.");
        }
        try {
            curve.setKey({ point->getDoubleAttribute("time"), point->getDoubleAttribute("value"),
                static_cast<Interpolation>(interpolation), point->getDoubleAttribute("in"), point->getDoubleAttribute("out") });
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
            item->setAttribute("start", effect.range->start);
            item->setAttribute("duration", effect.range->duration);
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

struct Document::Change : juce::UndoableAction {
    Change(Document& owner, Project before, Project after) : owner(owner), before(std::move(before)), after(std::move(after)) {}
    bool perform() override { owner.apply(after); return true; }
    bool undo() override { owner.apply(before); return true; }
    Document& owner;
    Project before, after;
};

Project Document::mergeScope(Project view) const {
    if (scopeId == 0) { return view; }
    auto whole = state;
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

void Document::apply(Project value) {
    ++stateRevision;
    state = std::move(value);
    refreshScope();
    if (onChanged) {
        onChanged();
    }
    sendChangeMessage();
}

void Document::edit(juce::String label, std::function<void(Project&)> operation) {
    auto after = project();
    operation(after);
    after = mergeScope(std::move(after));
    undo.beginNewTransaction(label);
    undo.perform(new Change(*this, state, std::move(after)));
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
    apply(std::move(project));
}

juce::Result Document::changeTempo(double bpm) {
    const auto& state = project();
    if (!std::isfinite(bpm) || bpm < 1 || bpm > 1000) { return juce::Result::fail("Tempo must be between 1 and 1000 BPM."); }
    if (bpm == state.bpm) { return juce::Result::ok(); }
    auto next = state;
    next.bpm = bpm;
    for (auto& track : next.tracks) {
        std::sort(track.clips.begin(), track.clips.end(), [bpm](const auto& a, const auto& b) { return a.timing(bpm).start < b.timing(bpm).start; });
        double previousEnd = 0;
        for (const auto& clip : track.clips) {
            const auto timing = clip.timing(bpm);
            if (!clip.valid() || !timing.valid() || timing.start < previousEnd) {
                return juce::Result::fail("Tempo change would overlap clips on " + juce::String(track.name) + ". Move the clips apart or onto separate tracks first.");
            }
            previousEnd = timing.end();
            next.duration = std::max(next.duration, previousEnd);
        }
    }
    edit("Change tempo", [next = std::move(next)](Project& project) { project = next; });
    return juce::Result::ok();
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
            copy->id = highest + 1;
            copy->name = expected->name.upToLastOccurrenceOf(".", false, false) + " copy " + juce::String(static_cast<juce::uint64>(copy->id)) + expected->extension;
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
            const auto timing = clip.timing(state.bpm);
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
    for (auto& [index, copy] : copies) {
        const auto required = static_cast<Id>(copy.effects.size()) + 1;
        if (required > std::numeric_limits<Id>::max() - highest) { return juce::Result::fail("There are no remaining identities for duplicated clips."); }
        auto timing = copy.timing(state.bpm);
        timing.moveTo(timing.start + (last - first));
        if (copies.size() == 1) {
            copy.start = copy.end();
        } else if (!copy.setTiming(timing, state.bpm)) {
            return juce::Result::fail("The duplicated selection has invalid timing.");
        }
        copy.id = ++highest;
        for (auto& effect : copy.effects) { effect.id = ++highest; }
        if (!candidate.tracks[index].insert(copy, state.bpm)) {
            return juce::Result::fail("There is not enough free space after the selection. Move the following clips first.");
        }
        candidate.duration = std::max(candidate.duration, copy.timing(state.bpm).end());
        ids.push_back(copy.id);
    }
    lastId = highest;
    edit(copies.size() == 1 ? "Duplicate clip" : "Duplicate clips", [candidate = std::move(candidate)](Project& project) { project = candidate; });
    duplicateIds = std::move(ids);
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
            const auto timing = source.timing(definition.bpm);
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
    if (!clip.valid() || !clip.timing(candidate.bpm).valid()) { return juce::Result::fail("The composition has invalid timing."); }
    if (trackId != 0) {
        const auto track = std::find_if(candidate.tracks.begin(), candidate.tracks.end(), [trackId](const auto& value) { return value.id == trackId; });
        if (track == candidate.tracks.end() || track->locked || track->kind != TrackKind::visual || !track->insert(clip, candidate.bpm)) {
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
    std::size_t required = 1 + copy->groups.size() + copy->tracks.size() + copy->cameras.size() + copy->cameraCuts.size() + copy->effects.size() + copy->markers.size();
    for (const auto& group : copy->groups) { required += group.effects.size(); }
    for (const auto& track : copy->tracks) {
        required += track.effects.size() + track.clips.size();
        for (const auto& clip : track.clips) { required += clip.effects.size(); }
    }
    auto highest = highestId();
    if (required > std::numeric_limits<Id>::max() - highest) { return juce::Result::fail("There are no remaining composition identities."); }
    const auto effects = [&](auto& values) { for (auto& value : values) { value.id = ++highest; } };
    copy->id = ++highest;
    copy->name += " copy";
    std::map<Id, Id> groups, cameras;
    for (auto& group : copy->groups) {
        const auto old = group.id; group.id = ++highest; groups.emplace(old, group.id); effects(group.effects);
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
        for (auto& clip : track.clips) { clip.id = ++highest; effects(clip.effects); }
    }
    for (auto& camera : copy->cameras) { const auto old = camera.id; camera.id = ++highest; cameras.emplace(old, camera.id); }
    for (auto& cut : copy->cameraCuts) {
        if (!cameras.contains(cut.camera)) { return juce::Result::fail("Invalid composition camera reference."); }
        cut.id = ++highest; cut.camera = cameras.at(cut.camera);
    }
    for (auto& marker : copy->markers) { marker.id = ++highest; }
    effects(copy->effects);
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
    definition->bpm = state.bpm; definition->frameRate = state.frameRate;
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
            const auto timing = clip.timing(state.bpm);
            if (!clip.valid() || !timing.valid()) { return juce::Result::fail("A selected clip has invalid timing or properties."); }
            first = std::min(first, timing.start); last = std::max(last, timing.end()); ++count;
        }
        // Capture effective visibility across the new scope boundary. Solo is
        // scoped independently inside the definition after this operation.
        copy.muted = !trackIsAudible(state, track);
        copy.solo = false;
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
    if (required > std::numeric_limits<Id>::max() - highest) { return juce::Result::fail("There are no remaining composition identities."); }
    definition->id = ++highest;
    std::map<Id, Id> groupIds;
    for (const auto& group : state.groups) {
        if (!requiredGroups.contains(group.id)) { continue; }
        auto copy = group; copy.id = ++highest; copy.solo = false;
        groupIds.emplace(group.id, copy.id);
        for (auto& effect : copy.effects) { effect.id = ++highest; }
        definition->groups.push_back(std::move(copy));
    }
    for (auto& group : definition->groups) { if (group.parent != 0) { group.parent = groupIds.at(group.parent); } }
    for (auto& track : definition->tracks) {
        track.id = ++highest;
        if (track.group != 0) { track.group = groupIds.at(track.group); }
        for (auto& effect : track.effects) { effect.id = ++highest; }
    }
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
            if (!changed.setTiming(resolvedSeconds, state.bpm) || !track.canPlace(changed, clipId, state.bpm)) {
                return juce::Result::fail("The requested timing is invalid or overlaps another clip on this track.");
            }
            if (changed.start == original.start && changed.duration == original.duration
                && changed.offset == original.offset && changed.rate == original.rate) {
                return juce::Result::ok();
            }
            edit("Change clip timing", [trackIndex, clipIndex, changed = std::move(changed)](Project& project) {
                auto& clips = project.tracks[trackIndex].clips;
                project.duration = std::max(project.duration, changed.timing(project.bpm).end());
                clips[clipIndex] = changed;
                std::sort(clips.begin(), clips.end(), [bpm = project.bpm](const auto& a, const auto& b) {
                    return a.timing(bpm).start < b.timing(bpm).start;
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
            if (!track.canPlace(changed, clipId, state.bpm)) {
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
        if (!clip.anchorToBeats(state.bpm)) { return juce::Result::fail("Cannot anchor this clip to the project tempo."); }
        clip.midi = notes;
        clip.midiAsset = assetId;
        return juce::Result::ok();
    });
}

juce::Result Document::setMidiNotes(Id clipId, std::shared_ptr<const MidiNotes> notes, juce::String undoLabel) {
    if (notes == nullptr) { return juce::Result::fail("MIDI note content must not be null. Use Clear MIDI to remove a performance."); }
    return editMidi(clipId, undoLabel.isEmpty() ? "Edit MIDI notes" : undoLabel, [&](Clip& clip) {
        if (clip.midi == nullptr) { return juce::Result::fail("Assign a MIDI performance before editing notes."); }
        if (clip.midi->notes() == notes->notes()) { return juce::Result::ok(); }
        clip.midi = notes;
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
    if (asset.source != nullptr && (asset.source->frameCount() > 1 || asset.extension.equalsIgnoreCase(".lua"))) {
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

juce::Result Document::decodeAsset(Asset& asset, const std::atomic<bool>* cancel, std::atomic<double>* progress) try {
    if (progress != nullptr) {
        progress->store(0.0, std::memory_order_relaxed);
    }
    if (importCancelled(cancel)) {
        return juce::Result::fail("Source import cancelled.");
    }
    if (asset.data.getSize() == 0 || asset.data.getSize() > maximumSourceBytes) {
        return juce::Result::fail("Source files must contain data and be no larger than 64 MiB.");
    }
    const auto extension = asset.extension.toLowerCase();
    if (isMidiSource(extension)) {
        const auto prepared = MidiSourcePreparer::prepare(asset.data.getData(), asset.data.getSize(), asset.midiImportBpm, cancel);
        if (!prepared) { return juce::Result::fail(prepared.error); }
        asset.midi = prepared.source;
        asset.midiSuggestedBpm = prepared.suggestedBpm;
        asset.midiIgnoredEvents = prepared.ignoredEvents;
        asset.source.reset();
        asset.drawing.reset();
        asset.audio.reset();
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
    if (extension == ".obj") {
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
    xml.setAttribute("duration", state.duration);
    xml.setAttribute("fps", state.frameRate);
    xml.setAttribute("bpm", state.bpm);
    xml.setAttribute("timeDisplay", static_cast<int>(state.timeDisplay));
    xml.setAttribute("beatsPerBar", state.beatsPerBar);
    xml.setAttribute("snapBeats", state.snapBeats);
    xml.setAttribute("gridSnap", state.gridSnap);
    saveEffects(xml, state.effects);
    for (const auto& group : state.groups) {
        auto* item = xml.createNewChildElement("group");
        item->setAttribute("id", juce::String(group.id));
        item->setAttribute("name", juce::String(group.name));
        item->setAttribute("parent", juce::String(group.parent));
        item->setAttribute("muted", group.muted);
        item->setAttribute("solo", group.solo);
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
            if (track.kind == TrackKind::visual && clip.composition == 0) {
                auto* instrument = item->createNewChildElement("instrument");
                instrument->setAttribute("attack", exactBakeNumber(clip.instrument.attack));
                instrument->setAttribute("decay", exactBakeNumber(clip.instrument.decay));
                instrument->setAttribute("sustain", exactBakeNumber(clip.instrument.sustain));
                instrument->setAttribute("release", exactBakeNumber(clip.instrument.release));
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
            }
            saveEffects(*item, clip.effects);
            for (const auto& [name, curve] : clip.properties) {
                saveProperty(*item, name, curve);
            }
        }
    }
    for (const auto& camera : state.cameras) {
        auto* item = xml.createNewChildElement("camera");
        item->setAttribute("id", juce::String(camera.id));
        item->setAttribute("name", juce::String(camera.name));
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
        item->setAttribute("start", cut.start);
        item->setAttribute("duration", cut.duration);
    }
    return xml;
}

juce::XmlElement Document::save() const {
    auto xml = saveCompositionContent(state);
    for (const auto& asset : state.assets) {
        auto* item = xml.createNewChildElement("asset");
        item->setAttribute("id", juce::String(asset->id));
        item->setAttribute("name", asset->name);
        item->setAttribute("extension", asset->extension);
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
                raster->setAttribute("pointsPerFrame", static_cast<int>(asset->rasterSettings.pointsPerFrame));
            }
            if (asset->extension.equalsIgnoreCase(".txt")) {
                auto* text = item->createNewChildElement("typography");
                text->setAttribute("family", asset->textSettings.family);
                text->setAttribute("style", asset->textSettings.style);
                text->setAttribute("alignment", asset->textSettings.alignment);
                text->setAttribute("lineSpacing", exactBakeNumber(asset->textSettings.lineSpacing));
                text->setAttribute("tracking", exactBakeNumber(asset->textSettings.tracking));
            }
            // Keep mixed-content payloads last. JUCE's single-line binary XML
            // writer can attempt a null newline when wrapping attributes on an
            // element following a text node.
            item->addTextElement(asset->data.toBase64Encoding());
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
    if (!std::isfinite(project.duration) || project.duration <= 0 || !std::isfinite(project.frameRate)
        || project.frameRate < 0.001 || project.frameRate > 1000 || !std::isfinite(project.bpm) || project.bpm < 1 || project.bpm > 1000
        || display < 0 || display > 2 || snap < 0 || snap > 1 || project.beatsPerBar < 1 || project.beatsPerBar > 32
        || !std::isfinite(project.snapBeats) || project.snapBeats < 1.0 / 64 || project.snapBeats > 64) {
        return juce::Result::fail("Invalid composition timing.");
    }
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
                    instrument->getDoubleAttribute("sustain", -1), instrument->getDoubleAttribute("release", -1)};
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
                const auto prepared = MidiNotes::create(std::move(notes));
                if (!prepared) { return juce::Result::fail(prepared.error); }
                clip.midi = prepared.source;
            }
            for (auto* property : item->getChildWithTagNameIterator("property")) {
                Curve curve;
                const auto result = loadProperty(*property, curve);
                if (result.failed()) {
                    return result;
                }
                clip.properties[property->getStringAttribute("name").toStdString()] = std::move(curve);
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
            if (!track.insert(std::move(clip), project.bpm)) {
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
        if (isMidiSource(asset->extension)) { asset->midiImportBpm = item->getDoubleAttribute("midiImportBpm", 0); }
        if (asset->extension.equalsIgnoreCase(".txt")) {
            const auto* text = item->getChildByName("typography");
            if (text != nullptr) {
                asset->textSettings.family = text->getStringAttribute("family");
                asset->textSettings.style = text->getIntAttribute("style", -1);
                asset->textSettings.alignment = text->getIntAttribute("alignment", -1);
                asset->textSettings.lineSpacing = text->getDoubleAttribute("lineSpacing", -1);
                asset->textSettings.tracking = text->getDoubleAttribute("tracking", -1);
            }
        }
        const bool luaSource = asset->extension.equalsIgnoreCase(".lua");
        auto* source = luaSource ? item->getChildByName("source") : item;
        if (source == nullptr) { return juce::Result::fail("Baked Lua asset is missing its source."); }
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
            const auto points = raster->getIntAttribute("pointsPerFrame", 0);
            if (points <= 0) { return juce::Result::fail("Invalid image sample count."); }
            asset->rasterSettings.pointsPerFrame = static_cast<std::size_t>(points);
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

#include "Document.h"
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

void Document::apply(Project value) {
    ++stateRevision;
    state = std::move(value);
    if (onChanged) {
        onChanged();
    }
    sendChangeMessage();
}

void Document::edit(juce::String label, std::function<void(Project&)> operation) {
    auto after = state;
    operation(after);
    undo.beginNewTransaction(label);
    undo.perform(new Change(*this, state, std::move(after)));
}

void Document::commit(juce::String label, Project before) {
    undo.beginNewTransaction(label);
    undo.perform(new Change(*this, std::move(before), state));
}

void Document::reset(Project project) {
    ++projectGeneration;
    undo.clearUndoHistory();
    const auto updateEffects = [&](const auto& effects) {
        for (const auto& effect : effects) {
            lastId = std::max(lastId, effect.id);
        }
    };
    updateEffects(project.effects);
    for (const auto& group : project.groups) {
        lastId = std::max(lastId, group.id);
        updateEffects(group.effects);
    }
    for (const auto& asset : project.assets) {
        lastId = std::max(lastId, asset->id);
    }
    for (const auto& track : project.tracks) {
        lastId = std::max(lastId, track.id);
        updateEffects(track.effects);
        for (const auto& clip : track.clips) {
            lastId = std::max(lastId, clip.id);
            updateEffects(clip.effects);
        }
    }
    for (const auto& camera : project.cameras) {
        lastId = std::max(lastId, camera.id);
    }
    for (const auto& cut : project.cameraCuts) {
        lastId = std::max(lastId, cut.id);
    }
    apply(std::move(project));
}

juce::Result Document::changeTempo(double bpm) {
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

juce::Result Document::editMidi(Id clipId, juce::String label, const std::function<juce::Result(Clip&)>& operation) {
    for (std::size_t trackIndex = 0; trackIndex < state.tracks.size(); ++trackIndex) {
        const auto& track = state.tracks[trackIndex];
        for (std::size_t clipIndex = 0; clipIndex < track.clips.size(); ++clipIndex) {
            const auto& original = track.clips[clipIndex];
            if (original.id != clipId) { continue; }
            if (track.kind != TrackKind::visual) { return juce::Result::fail("MIDI performance requires a visual track."); }
            if (track.locked) { return juce::Result::fail("Unlock the track before editing its MIDI performance."); }
            auto changed = original;
            const auto result = operation(changed);
            if (result.failed()) { return result; }
            if (!track.canPlace(changed, clipId, state.bpm)) {
                return juce::Result::fail("MIDI assignment would produce invalid or overlapping clip timing.");
            }
            if (changed.midi == original.midi && changed.midiAsset == original.midiAsset
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

juce::Result Document::assignMidi(Id clipId, Id assetId) {
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
        auto font = juce::Font(juce::FontOptions(30));
        TextParser text(content, font);
        shapes = text.draw();
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

juce::XmlElement Document::save() const {
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
            item->addTextElement(asset->data.toBase64Encoding());
            if (isRasterSource(asset->extension)) {
                auto* raster = item->createNewChildElement("raster");
                raster->setAttribute("mode", asset->rasterSettings.mode == RasterSettings::Mode::contours ? "contours" : "scanlines");
                raster->setAttribute("threshold", exactBakeNumber(asset->rasterSettings.threshold));
                raster->setAttribute("invert", asset->rasterSettings.invert);
                raster->setAttribute("resolution", asset->rasterSettings.resolution);
                raster->setAttribute("pointsPerFrame", static_cast<int>(asset->rasterSettings.pointsPerFrame));
            }
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
            item->setAttribute("asset", juce::String(clip.asset));
            item->setAttribute("name", juce::String(clip.name));
            item->setAttribute("timeBase", clip.timeBase == ClipTimeBase::beats ? "beats" : "seconds");
            item->setAttribute("contentBpm", exactBakeNumber(clip.contentBpm));
            item->setAttribute("start", exactBakeNumber(clip.start));
            item->setAttribute("duration", exactBakeNumber(clip.duration));
            item->setAttribute("offset", exactBakeNumber(clip.offset));
            item->setAttribute("rate", exactBakeNumber(clip.rate));
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
    for (const auto& cut : state.cameraCuts) {
        auto* item = xml.createNewChildElement("cameraCut");
        item->setAttribute("id", juce::String(cut.id));
        item->setAttribute("camera", juce::String(cut.camera));
        item->setAttribute("start", cut.start);
        item->setAttribute("duration", cut.duration);
    }
    return xml;
}

juce::Result Document::load(const juce::XmlElement& xml) {
    if (!xml.hasTagName("composition")) {
        return juce::Result::fail("Missing composition.");
    }
    Project project;
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
    std::set<Id> identities;
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
    for (auto* item : xml.getChildWithTagNameIterator("asset")) {
        auto asset = std::make_shared<Asset>();
        asset->id = static_cast<Id>(item->getStringAttribute("id").getLargeIntValue());
        asset->name = item->getStringAttribute("name");
        asset->extension = item->getStringAttribute("extension");
        if (isMidiSource(asset->extension)) { asset->midiImportBpm = item->getDoubleAttribute("midiImportBpm", 0); }
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
        const auto result = decodeAsset(*asset);
        if (result.failed()) {
            return result;
        }
        project.assets.push_back(std::move(asset));
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
            clip.name = item->getStringAttribute("name").toStdString();
            const auto timeBase = item->getStringAttribute("timeBase");
            if (timeBase != "seconds" && timeBase != "beats") { return juce::Result::fail("Clip timing must be seconds or beats."); }
            clip.timeBase = timeBase == "beats" ? ClipTimeBase::beats : ClipTimeBase::seconds;
            clip.contentBpm = item->getDoubleAttribute("contentBpm", 0);
            clip.start = item->getDoubleAttribute("start");
            clip.duration = item->getDoubleAttribute("duration");
            clip.offset = item->getDoubleAttribute("offset");
            clip.rate = item->getDoubleAttribute("rate", 1);
            const auto found = std::find_if(project.assets.begin(), project.assets.end(), [&](const auto& asset) { return asset->id == clip.asset; });
            if (found == project.assets.end() || !identities.insert(clip.id).second) {
                return juce::Result::fail("Invalid clip asset or identity.");
            }
            if ((track.kind == TrackKind::audio) != ((*found)->audio != nullptr)) {
                return juce::Result::fail("The clip source type does not match its audio or visual track.");
            }
            if ((*found)->midi != nullptr) { return juce::Result::fail("A MIDI pattern requires a visual instrument source for its clip."); }
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
                    const auto source = std::find_if(project.assets.begin(), project.assets.end(), [&](const auto& asset) { return asset->id == clip.midiAsset; });
                    if (source == project.assets.end() || (*source)->midi == nullptr) { return juce::Result::fail("MIDI pattern source is missing or is not a MIDI asset."); }
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
    reset(std::move(project));
    return juce::Result::ok();
}
}

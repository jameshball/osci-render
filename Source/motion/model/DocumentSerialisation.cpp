#include "Document.h"
#include "Cancellation.h"
#include "CompositionGraph.h"
#include "LuaClipBake.h"
#include "ModulationGraph.h"
#include "PropertySchema.h"
#include "../import/BakedSourceArchive.h"
#include "../import/SourceDecoding.h"
#include <charconv>
#include <iomanip>
#include <locale>
#include <set>
#include <sstream>

namespace motion {
namespace {
// Shortest text that reads back as exactly the same double.
juce::String exactNumber(double value) {
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::setprecision(std::numeric_limits<double>::max_digits10) << value;
    return juce::String(stream.str());
}

// An identity attribute: absent reads as 0 (none), and anything but a whole
// non-negative number as nothing, so the file is refused.
std::optional<Id> readId(const juce::XmlElement& item, const char* name) {
    if (!item.hasAttribute(name)) { return Id(0); }
    const auto text = item.getStringAttribute(name).toStdString();
    Id value = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc() || parsed.ptr != text.data() + text.size()) { return std::nullopt; }
    return value;
}

// `item`'s own identity: positive and not yet used in the project.
std::optional<Id> claimId(const juce::XmlElement& item, std::set<Id>& identities) {
    const auto id = readId(item, "id");
    if (!id.has_value() || *id == 0 || !identities.insert(*id).second) { return std::nullopt; }
    return id;
}

// Embedded binary, refused before decoding when it would exceed `maximumBytes`.
bool readBase64(const juce::String& encoded, juce::MemoryBlock& data, std::size_t maximumBytes) {
    return static_cast<std::size_t>(encoded.length()) <= (maximumBytes / 3 + 1) * 4 && data.fromBase64Encoding(encoded);
}

void saveProperty(juce::XmlElement& item, const std::string& name, const Curve& curve) {
    auto* property = item.createNewChildElement("property");
    property->setAttribute("name", juce::String(name));
    property->setAttribute("base", exactNumber(curve.base));
    if (curve.link.has_value()) {
        auto* link = property->createNewChildElement("link");
        link->setAttribute("source", juce::String(curve.link->source));
        link->setAttribute("property", juce::String(curve.link->property));
        link->setAttribute("scale", exactNumber(curve.link->scale));
        link->setAttribute("offset", exactNumber(curve.link->offset));
        link->setAttribute("delay", exactNumber(curve.link->delay));
    }
    for (const auto& key : curve.keyframes()) {
        auto* point = property->createNewChildElement("key");
        point->setAttribute("time", exactNumber(key.time));
        point->setAttribute("value", exactNumber(key.value));
        point->setAttribute("interpolation", static_cast<int>(key.interpolation));
        // Absent slopes and influences read back as their defaults.
        if (key.incomingSlope != 0) { point->setAttribute("in", exactNumber(key.incomingSlope)); }
        if (key.outgoingSlope != 0) { point->setAttribute("out", exactNumber(key.outgoingSlope)); }
        if (key.incomingInfluence != Keyframe::defaultInfluence) { point->setAttribute("inInfluence", exactNumber(key.incomingInfluence)); }
        if (key.outgoingInfluence != Keyframe::defaultInfluence) { point->setAttribute("outInfluence", exactNumber(key.outgoingInfluence)); }
    }
}

void saveProperties(juce::XmlElement& item, const PropertyMap& properties) {
    for (const auto& [name, curve] : properties) { saveProperty(item, name, curve); }
}

juce::Result loadProperty(const juce::XmlElement& property, Curve& curve) {
    curve = Curve(property.getDoubleAttribute("base"));
    if (!std::isfinite(curve.base)) {
        return juce::Result::fail("Invalid property value.");
    }
    for (auto* item : property.getChildWithTagNameIterator("link")) {
        PropertyLink link;
        link.source = readId(*item, "source").value_or(0);
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

// Properties whose names are fixed by their owner: each must be one of
// `properties`, at most once. `loaded` counts those read.
juce::Result loadKnownProperties(const juce::XmlElement& owner, PropertyMap& properties, const char* unknown, std::size_t* loaded = nullptr) {
    std::set<std::string> names;
    for (auto* property : owner.getChildWithTagNameIterator("property")) {
        const auto name = property->getStringAttribute("name").toStdString();
        const auto found = properties.find(name);
        if (found == properties.end() || !names.insert(name).second) { return juce::Result::fail(unknown); }
        const auto result = loadProperty(*property, found->second);
        if (result.failed()) { return result; }
    }
    if (loaded != nullptr) { *loaded = names.size(); }
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
            item->setAttribute("start", exactNumber(effect.range->start));
            item->setAttribute("duration", exactNumber(effect.range->duration));
        }
        saveProperties(*item, effect.properties);
    }
}

juce::Result loadEffects(const juce::XmlElement& owner, std::vector<EffectInstance>& effects, std::set<Id>& identities) {
    for (auto* item : owner.getChildWithTagNameIterator("effect")) {
        const auto* definition = effectDefinition(item->getStringAttribute("type").toStdString());
        const auto identity = definition != nullptr ? claimId(*item, identities) : std::nullopt;
        if (!identity.has_value()) {
            return juce::Result::fail("Unknown effect type or invalid / duplicate effect identity.");
        }
        if (effects.size() >= maximumEffectsPerOwner) {
            return juce::Result::fail("Each clip, track or composition supports at most 64 effects.");
        }
        auto effect = makeEffect(*identity, *definition);
        effect.name = item->getStringAttribute("name", juce::String(definition->name)).toStdString();
        effect.enabled = item->getBoolAttribute("enabled", true);
        if (item->hasAttribute("start") || item->hasAttribute("duration")) {
            if (!item->hasAttribute("start") || !item->hasAttribute("duration")) {
                return juce::Result::fail("Effect time ranges require both start and duration.");
            }
            effect.range = EffectRange { item->getDoubleAttribute("start"), item->getDoubleAttribute("duration") };
        }
        std::size_t loaded = 0;
        const auto properties = loadKnownProperties(*item, effect.properties, "Unknown or duplicate effect parameter.", &loaded);
        if (properties.failed()) {
            return properties;
        }
        if (loaded != effect.properties.size() || !effect.valid()) {
            return juce::Result::fail("Invalid effect range or parameter value.");
        }
        effects.push_back(std::move(effect));
    }
    return juce::Result::ok();
}

juce::XmlElement saveCompositionContent(const Composition& state) {
    juce::XmlElement xml("composition");
    xml.setAttribute("name", state.name);
    xml.setAttribute("duration", exactNumber(state.duration));
    xml.setAttribute("fps", exactNumber(state.frameRate));
    xml.setAttribute("bpm", exactNumber(state.bpm));
    xml.setAttribute("timeDisplay", static_cast<int>(state.timeDisplay));
    xml.setAttribute("beatsPerBar", state.beatsPerBar);
    xml.setAttribute("snapBeats", exactNumber(state.snapBeats));
    xml.setAttribute("gridSnap", state.gridSnap);
    if (state.hasLoop()) {
        xml.setAttribute("loopStart", exactNumber(state.loopStart));
        xml.setAttribute("loopEnd", exactNumber(state.loopEnd));
        xml.setAttribute("looping", state.looping);
    }
    if (state.tempoChanges != nullptr) {
        for (const auto& change : *state.tempoChanges) {
            auto* item = xml.createNewChildElement("tempo");
            item->setAttribute("beat", exactNumber(change.beat));
            item->setAttribute("bpm", exactNumber(change.bpm));
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
        saveProperties(*item, group.properties);
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
            item->setAttribute("contentBpm", exactNumber(clip.contentBpm));
            item->setAttribute("start", exactNumber(clip.start));
            item->setAttribute("duration", exactNumber(clip.duration));
            item->setAttribute("offset", exactNumber(clip.offset));
            item->setAttribute("rate", exactNumber(clip.rate));
            if (clip.spatialPath) { item->setAttribute("spatialPath", true); }
            if (clip.quaternionRotation) { item->setAttribute("quaternionRotation", true); }
            saveEffects(*item, clip.effects);
            saveProperties(*item, clip.properties);
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
        saveProperties(*item, camera.properties);
    }
    for (const auto& marker : state.markers) {
        auto* item = xml.createNewChildElement("marker");
        item->setAttribute("id", juce::String(marker.id));
        item->setAttribute("time", exactNumber(marker.time));
        item->setAttribute("name", marker.name);
    }
    for (const auto& cut : state.cameraCuts) {
        auto* item = xml.createNewChildElement("cameraCut");
        item->setAttribute("id", juce::String(cut.id));
        item->setAttribute("camera", juce::String(cut.camera));
        item->setAttribute("start", exactNumber(cut.start));
        item->setAttribute("duration", exactNumber(cut.duration));
    }
    for (const auto& modulator : state.modulators) {
        auto* item = xml.createNewChildElement("modulator");
        item->setAttribute("id", juce::String(modulator.id));
        item->setAttribute("name", juce::String(modulator.name));
        item->setAttribute("waveform", static_cast<int>(modulator.shape.waveform));
        item->setAttribute("rateHz", exactNumber(modulator.shape.rateHz));
        item->setAttribute("phase", exactNumber(modulator.shape.phase));
        item->setAttribute("tempoSync", modulator.shape.tempoSync);
        item->setAttribute("beatsPerCycle", exactNumber(modulator.shape.beatsPerCycle));
        item->setAttribute("seed", juce::String(static_cast<juce::int64>(modulator.shape.seed)));
    }
    for (const auto& route : state.routes) {
        auto* item = xml.createNewChildElement("route");
        item->setAttribute("id", juce::String(route.id));
        item->setAttribute("modulator", juce::String(route.modulator));
        item->setAttribute("target", juce::String(route.target));
        item->setAttribute("property", juce::String(route.property));
        item->setAttribute("amount", exactNumber(route.amount));
        item->setAttribute("mode", static_cast<int>(route.mode));
    }
    return xml;
}

// The main composition loads as the Project, so routes may target its Scope.
template <typename CompositionType>
juce::Result loadCompositionContent(const juce::XmlElement& xml, CompositionType& project, const std::vector<std::shared_ptr<const Asset>>& assets, std::set<Id>& identities, const std::set<Id>& compositionIds) {
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
    project.clampLoop();
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
        const auto parent = readId(*item, "parent");
        const auto identity = parent.has_value() ? claimId(*item, identities) : std::nullopt;
        if (!identity.has_value()) {
            return juce::Result::fail("Invalid or duplicate group identity.");
        }
        group.id = *identity;
        group.parent = *parent;
        group.name = item->getStringAttribute("name", "Group").toStdString();
        group.muted = item->getBoolAttribute("muted", false);
        group.solo = item->getBoolAttribute("solo", false);
        group.spatialPath = item->getBoolAttribute("spatialPath", false);
        group.quaternionRotation = item->getBoolAttribute("quaternionRotation", false);
        const auto effects = loadEffects(*item, group.effects, identities);
        if (effects.failed()) {
            return effects;
        }
        const auto properties = loadKnownProperties(*item, group.properties, "Unknown or duplicate group property.");
        if (properties.failed()) {
            return properties;
        }
        if (!group.valid()) {
            return juce::Result::fail("Invalid group transform, tint or drawing weight.");
        }
        project.groups.push_back(std::move(group));
    }
    for (auto* row : xml.getChildWithTagNameIterator("track")) {
        Track track;
        const auto trackIdentity = claimId(*row, identities);
        if (!trackIdentity.has_value()) {
            return juce::Result::fail("Invalid track identity.");
        }
        track.id = *trackIdentity;
        track.name = row->getStringAttribute("name").toStdString();
        const auto kind = row->getStringAttribute("kind");
        if (kind != "visual" && kind != "audio") {
            return juce::Result::fail("Track kind must be visual or audio.");
        }
        track.kind = kind == "audio" ? TrackKind::audio : TrackKind::visual;
        track.muted = row->getBoolAttribute("muted", false);
        track.solo = row->getBoolAttribute("solo", false);
        track.locked = row->getBoolAttribute("locked", false);
        track.height = std::clamp(row->getIntAttribute("height", 0), 0, Track::maximumHeight);
        if (track.height != 0) { track.height = std::max(track.height, Track::minimumHeight); }
        track.label = std::clamp(row->getIntAttribute("label", 0), 0, 8);
        const auto groupIdentity = readId(*row, "group");
        if (!groupIdentity.has_value()) {
            return juce::Result::fail("Invalid track group identity.");
        }
        track.group = *groupIdentity;
        const auto trackEffects = loadEffects(*row, track.effects, identities);
        if (trackEffects.failed()) {
            return trackEffects;
        }
        if (track.kind == TrackKind::audio && !track.effects.empty()) {
            return juce::Result::fail("Audio tracks cannot contain visual effects.");
        }
        for (auto* item : row->getChildWithTagNameIterator("clip")) {
            Clip clip;
            const auto clipIdentity = claimId(*item, identities);
            const auto asset = readId(*item, "asset"), composition = readId(*item, "composition");
            if (!clipIdentity.has_value() || !asset.has_value() || !composition.has_value()) { return juce::Result::fail("Invalid clip identity."); }
            clip.id = *clipIdentity;
            clip.asset = *asset;
            clip.composition = *composition;
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
            const auto found = findAsset(assets, clip.asset);
            if (clip.composition != 0) {
                if (clip.asset != 0 || !compositionIds.contains(clip.composition) || track.kind != TrackKind::visual) {
                    return juce::Result::fail("Invalid reusable composition reference or track kind.");
                }
            } else {
                if (found == nullptr) { return juce::Result::fail("Invalid clip asset."); }
                if ((track.kind == TrackKind::audio) != (found->audio != nullptr)) {
                    return juce::Result::fail("The clip source type does not match its audio or visual track.");
                }
            }
            // Every clip property must be a known, unique schema entry whose
            // values lie inside its declared range.
            const auto specs = track.kind == TrackKind::audio ? std::span<const PropertySpec>(audioPropertySpecs) : objectPropertySpecs;
            const bool luaClip = clip.composition == 0 && found != nullptr && found->extension.equalsIgnoreCase(".lua");
            for (auto* property : item->getChildWithTagNameIterator("property")) {
                const auto name = property->getStringAttribute("name").toStdString();
                auto* spec = findPropertySpec(specs, name);
                if (spec == nullptr && luaClip) { spec = findPropertySpec(luaSliderSpecs, name); }
                if (spec == nullptr || clip.properties.contains(name)) {
                    return juce::Result::fail("Unknown or duplicate clip property \"" + juce::String(name) + "\".");
                }
                Curve curve;
                const auto result = loadProperty(*property, curve);
                if (result.failed()) {
                    return result;
                }
                if (!spec->contains(curve.base) || std::any_of(curve.keyframes().begin(), curve.keyframes().end(), [spec](const auto& key) { return !spec->contains(key.value); })) {
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
                if (!luaClip || bake->key.empty() || !readBase64(encoded, bake->archive, BakedSourceArchive::maximumCompressedBytes) || bake->archive.getSize() == 0) {
                    return juce::Result::fail("Invalid or oversized Lua slider bake.");
                }
                const auto frames = BakedSourceArchive::decode(bake->archive);
                if (!frames) { return juce::Result::fail(juce::String(frames.error)); }
                // Checked against the clip's sliders once routes and links load.
                bake->source = std::make_shared<const PreparedSource>(frames.source);
                clip.luaBake = std::move(bake);
            }
            const auto clipEffects = loadEffects(*item, clip.effects, identities);
            if (clipEffects.failed()) {
                return clipEffects;
            }
            if (track.kind == TrackKind::audio) {
                if (!clip.effects.empty() || !validProperties(clip.properties, audioPropertySpecs)) {
                    return juce::Result::fail("Audio clips need gain (0 to 4) and pan (-1 to 1) curves and cannot contain visual effects or properties.");
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
        const auto identity = claimId(*item, identities);
        if (!identity.has_value()) {
            return juce::Result::fail("Invalid camera identity.");
        }
        camera.id = *identity;
        camera.name = item->getStringAttribute("name", "Camera").toStdString();
        const auto target = readId(*item, "target"), parent = readId(*item, "parent");
        camera.target = target.value_or(0);
        camera.parent = parent.value_or(0);
        if (!target.has_value() || !parent.has_value() || (camera.parent != 0 && findGroup(project, camera.parent) == nullptr)
            || (camera.target != 0 && findGroup(project, camera.target) == nullptr && !hasVisualClip(project, camera.target))) {
            return juce::Result::fail("A camera aims at or is parented to a missing object.");
        }
        const auto properties = loadKnownProperties(*item, camera.properties, "Unknown or duplicate camera property.");
        if (properties.failed()) {
            return properties;
        }
        if (!camera.valid()) {
            return juce::Result::fail("Invalid camera transform or field of view.");
        }
        project.cameras.push_back(std::move(camera));
    }
    for (auto* item : xml.getChildWithTagNameIterator("marker")) {
        const auto id = claimId(*item, identities);
        Marker marker {id.value_or(0), item->getDoubleAttribute("time", -1), item->getStringAttribute("name")};
        if (!id.has_value() || !marker.valid(project.duration)) {
            return juce::Result::fail("Invalid marker identity, position or name.");
        }
        project.markers.push_back(std::move(marker));
    }
    sortMarkers(project.markers);
    for (std::size_t index = 1; index < project.markers.size(); ++index) {
        if (project.markers[index].time - project.markers[index - 1].time < Marker::minimumSpacing) { return juce::Result::fail("Markers must have distinct positions."); }
    }
    for (auto* item : xml.getChildWithTagNameIterator("cameraCut")) {
        CameraCut cut;
        const auto identity = claimId(*item, identities);
        cut.id = identity.value_or(0);
        cut.camera = readId(*item, "camera").value_or(0);
        cut.start = item->getDoubleAttribute("start");
        cut.duration = item->getDoubleAttribute("duration");
        const auto camera = std::find_if(project.cameras.begin(), project.cameras.end(), [&](const auto& value) { return value.id == cut.camera; });
        if (!identity.has_value() || !cut.valid() || camera == project.cameras.end()) {
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
    for (auto* item : xml.getChildWithTagNameIterator("modulator")) {
        Modulator modulator;
        const auto identity = claimId(*item, identities);
        modulator.id = identity.value_or(0);
        modulator.name = item->getStringAttribute("name").toStdString();
        const auto tempoSync = item->getIntAttribute("tempoSync", -1);
        const auto seed = item->getStringAttribute("seed", "0").getLargeIntValue();
        if (tempoSync < 0 || tempoSync > 1 || seed < 0 || seed > static_cast<juce::int64>(std::numeric_limits<std::uint32_t>::max())) {
            return juce::Result::fail("Invalid modulator settings.");
        }
        modulator.shape.enabled = true;
        modulator.shape.amount = 1;
        modulator.shape.waveform = static_cast<ModulationWaveform>(item->getIntAttribute("waveform", -1));
        modulator.shape.rateHz = item->getDoubleAttribute("rateHz", 1);
        modulator.shape.phase = item->getDoubleAttribute("phase", 0);
        modulator.shape.tempoSync = tempoSync != 0;
        modulator.shape.beatsPerCycle = item->getDoubleAttribute("beatsPerCycle", 1);
        modulator.shape.seed = static_cast<std::uint32_t>(seed);
        if (!identity.has_value() || !modulator.valid()) { return juce::Result::fail("Invalid modulator settings or identity."); }
        project.modulators.push_back(std::move(modulator));
    }
    for (auto* item : xml.getChildWithTagNameIterator("route")) {
        ModulationRoute route;
        const auto identity = claimId(*item, identities);
        route.id = identity.value_or(0);
        route.modulator = readId(*item, "modulator").value_or(0);
        route.target = readId(*item, "target").value_or(0);
        route.property = item->getStringAttribute("property").toStdString();
        route.amount = item->getDoubleAttribute("amount", 1);
        const auto mode = item->getIntAttribute("mode", -1);
        route.mode = static_cast<ModulationMode>(mode);
        if (!identity.has_value() || mode < 0 || mode > 1 || !route.valid()) { return juce::Result::fail("Invalid modulation route settings or identity."); }
        project.routes.push_back(std::move(route));
    }
    const auto modulation = validateModulation(project);
    if (!modulation.empty()) { return juce::Result::fail(juce::String(modulation)); }
    // Only a slider bake matching its clip's sliders and what drives them is
    // kept; a stale one is dropped and the editor bakes again.
    const auto stale = [&](const Clip& clip) {
        if (clip.luaBake == nullptr) { return false; }
        const auto asset = findAsset(assets, clip.asset);
        const auto plan = asset == nullptr ? std::nullopt : luaSliderPlan(*asset, clip, project);
        const auto& source = *clip.luaBake->source;
        return !(plan.has_value() && plan->key == clip.luaBake->key && source.frameCount() == plan->settings.frameCount() && source.frameRate() == plan->settings.frameRate);
    };
    project.tracks.changeEach([&](const Track& track) { return std::any_of(track.clips.begin(), track.clips.end(), stale); }, [&](Track& track) {
        for (auto& clip : track.clips) {
            if (stale(clip)) { clip.luaBake.reset(); }
        }
    });
    return juce::Result::ok();
}
}

juce::XmlElement Document::save() const {
    auto xml = saveCompositionContent(state);
    xml.setAttribute("scopeDwell", exactNumber(state.scope.dwellMicros));
    xml.setAttribute("scopeTravel", exactNumber(state.scope.travelMicrosPerUnit));
    xml.setAttribute("scopeSettle", exactNumber(state.scope.settleMicros));
    auto* beam = xml.createNewChildElement("scopeBeam");
    saveProperties(*beam, state.beam.properties);
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
        if (asset->extension.equalsIgnoreCase(".lua")) {
            item->createNewChildElement("source")->addTextElement(asset->data.toBase64Encoding());
            auto* bake = item->createNewChildElement("bake");
            bake->setAttribute("duration", exactNumber(asset->bakeSettings.duration));
            bake->setAttribute("frameRate", exactNumber(asset->bakeSettings.frameRate));
            bake->setAttribute("bpm", exactNumber(asset->bakeSettings.bpm));
            bake->setAttribute("pointsPerFrame", static_cast<int>(asset->bakeSettings.pointsPerFrame));
            bake->setAttribute("seed", juce::String(asset->bakeSettings.seed));
            bake->setAttribute("key", asset->bakeKey);
            bake->addTextElement(asset->bakedData.toBase64Encoding());
        } else {
            if (osci::files::isImage(asset->extension)) {
                auto* raster = item->createNewChildElement("raster");
                raster->setAttribute("mode", asset->rasterSettings.mode == RasterSettings::Mode::contours ? "contours" : "scanlines");
                raster->setAttribute("threshold", exactNumber(asset->rasterSettings.threshold));
                raster->setAttribute("invert", asset->rasterSettings.invert);
                raster->setAttribute("resolution", asset->rasterSettings.resolution);
                if (osci::files::isVideo(asset->extension)) { raster->setAttribute("frameRate", exactNumber(asset->rasterSettings.videoFrameRate)); }
                raster->setAttribute("pointsPerFrame", static_cast<int>(asset->rasterSettings.pointsPerFrame));
            }
            if (asset->extension.equalsIgnoreCase(".txt")) {
                auto* text = item->createNewChildElement("typography");
                text->setAttribute("family", asset->textSettings.family);
                text->setAttribute("style", asset->textSettings.style);
                text->setAttribute("alignment", asset->textSettings.alignment);
                text->setAttribute("lineSpacing", exactNumber(asset->textSettings.lineSpacing));
                text->setAttribute("tracking", exactNumber(asset->textSettings.tracking));
                if (asset->textSettings.animated()) {
                    text->setAttribute("animation", static_cast<int>(asset->textSettings.animation));
                    text->setAttribute("characterDelay", exactNumber(asset->textSettings.characterDelay));
                    text->setAttribute("characterDuration", exactNumber(asset->textSettings.characterDuration));
                    text->setAttribute("hold", exactNumber(asset->textSettings.hold));
                    text->setAttribute("amount", exactNumber(asset->textSettings.amount));
                }
            }
            // Keep mixed-content payloads last. JUCE's single-line binary XML
            // writer can attempt a null newline when wrapping attributes on an
            // element following a text node.
            if (osci::files::isVideo(asset->extension)) {
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

juce::Result Document::load(const juce::XmlElement& xml) {
    Project prepared;
    const auto result = prepareLoad(xml, prepared);
    if (result.wasOk()) { reset(std::move(prepared)); }
    return result;
}

juce::Result Document::prepareLoad(const juce::XmlElement& xml, Project& output, const std::atomic<bool>* cancel) try {
    if (cancelled(cancel)) { return juce::Result::fail("Project loading cancelled."); }
    if (!xml.hasTagName("composition")) { return juce::Result::fail("Missing composition."); }
    Project project;
    const ScopeProfile defaults;
    project.scope.dwellMicros = xml.getDoubleAttribute("scopeDwell", defaults.dwellMicros);
    project.scope.travelMicrosPerUnit = xml.getDoubleAttribute("scopeTravel", defaults.travelMicrosPerUnit);
    project.scope.settleMicros = xml.getDoubleAttribute("scopeSettle", defaults.settleMicros);
    if (!project.scope.valid()) { return juce::Result::fail("Invalid scope timing profile."); }
    // The Scope's identity is reserved before anything else claims one.
    std::set<Id> identities {beamIdentity}, compositionIds;
    for (auto* item : xml.getChildWithTagNameIterator("definition")) {
        const auto identity = claimId(*item, identities);
        if (!identity.has_value()) { return juce::Result::fail("Invalid reusable composition identity."); }
        compositionIds.insert(*identity);
    }
    for (auto* item : xml.getChildWithTagNameIterator("asset")) {
        if (cancelled(cancel)) { return juce::Result::fail("Project loading cancelled."); }
        auto asset = std::make_shared<Asset>();
        const auto identity = claimId(*item, identities);
        if (!identity.has_value()) { return juce::Result::fail("Invalid asset identity."); }
        asset->id = *identity;
        asset->name = item->getStringAttribute("name");
        asset->extension = item->getStringAttribute("extension");
        if (asset->extension.equalsIgnoreCase(".blender")) {
            const auto* live = item->getChildByName("blender");
            if (live == nullptr || live->getNextElement() != nullptr || item->getNumChildElements() != 1 || asset->name.trim().isEmpty()) { return juce::Result::fail("Invalid Blender source identity or settings."); }
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
        const bool videoSource = osci::files::isVideo(asset->extension);
        auto* source = luaSource || videoSource ? item->getChildByName("source") : item;
        if (source == nullptr) { return juce::Result::fail("Baked asset is missing its source."); }
        if (!readBase64(source->getAllSubText(), asset->data, maximumSourceBytes)) {
            return juce::Result::fail("Invalid embedded source, or larger than the 64 MiB import limit.");
        }
        if (osci::files::isImage(asset->extension)) {
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
            if (!readBase64(cache->getAllSubText(), asset->bakedData, BakedSourceArchive::maximumCompressedBytes) || asset->bakedData.getSize() == 0) {
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
            if (!readBase64(bake->getAllSubText(), asset->bakedData, BakedSourceArchive::maximumCompressedBytes) || asset->bakedData.getSize() == 0) {
                return juce::Result::fail("Invalid or oversized Lua source cache.");
            }
        }
        const auto result = decodeAsset(*asset, cancel);
        if (result.failed()) {
            return result;
        }
        project.assets.push_back(std::move(asset));
    }
    // The Scope's picture; a project saved without it keeps the defaults.
    const auto* beam = xml.getChildByName("scopeBeam");
    if (beam != nullptr) {
        if (beam->getNextElementWithTagName("scopeBeam") != nullptr) { return juce::Result::fail("A project has one Scope."); }
        const auto properties = loadKnownProperties(*beam, project.beam.properties, "Unknown or duplicate Scope property.");
        if (properties.failed()) { return properties; }
        if (!project.beam.valid()) { return juce::Result::fail("Invalid Scope property."); }
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
        definition->id = readId(*item, "id").value_or(0);
        const auto result = loadCompositionContent(*content, *definition, project.assets, identities, compositionIds);
        if (result.failed()) { return result; }
        project.definitions.push_back(std::move(definition));
    }
    const auto graph = validateCompositionGraph(project);
    if (!graph) { return juce::Result::fail(graph.error); }
    if (cancelled(cancel)) { return juce::Result::fail("Project loading cancelled."); }
    // New identities count up from the highest, so they never reach the Scope's.
    if (highestProjectIdentity(project) >= beamIdentity) { return juce::Result::fail("A project identity is out of range."); }
    output = std::move(project);
    return juce::Result::ok();
} catch (const std::exception& error) {
    return juce::Result::fail("Cannot prepare project: " + juce::String(error.what()));
}
}

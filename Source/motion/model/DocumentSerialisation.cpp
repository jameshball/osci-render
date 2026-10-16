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

// A property's curve from `property`. A link or key this version can't use
// is left out, said in `notes` against `name`; false when its value isn't a
// number at all.
bool loadProperty(const juce::XmlElement& property, Curve& curve, LoadNotes& notes, const juce::String& name) {
    curve = Curve(property.getDoubleAttribute("base"));
    if (!std::isfinite(curve.base)) { return false; }
    for (auto* item : property.getChildWithTagNameIterator("link")) {
        PropertyLink link;
        link.source = readId(*item, "source").value_or(0);
        link.property = item->getStringAttribute("property").toStdString();
        link.scale = item->getDoubleAttribute("scale", 1);
        link.offset = item->getDoubleAttribute("offset", 0);
        link.delay = item->getDoubleAttribute("delay", 0);
        if (curve.link.has_value() || !link.valid()) {
            notes.note("Property link that isn't valid", name);
            continue;
        }
        curve.link = std::move(link);
    }
    for (auto* point : property.getChildWithTagNameIterator("key")) {
        auto interpolation = point->getIntAttribute("interpolation");
        if (interpolation < 0 || interpolation > 3) {
            notes.note("Key easing this version doesn't have, made smooth", name);
            interpolation = static_cast<int>(Interpolation::smooth);
        }
        try {
            curve.setKey({ point->getDoubleAttribute("time"), point->getDoubleAttribute("value"),
                static_cast<Interpolation>(interpolation), point->getDoubleAttribute("in"), point->getDoubleAttribute("out"),
                point->getDoubleAttribute("inInfluence", Keyframe::defaultInfluence), point->getDoubleAttribute("outInfluence", Keyframe::defaultInfluence) });
        } catch (const std::invalid_argument&) {
            notes.note("Key that isn't valid", name);
        }
    }
    return true;
}

// Properties whose names are fixed by their owner (`owner` names its kind:
// "Camera"): one this version doesn't have, one given twice, or one that
// isn't a number is left out (its default kept) and said in `notes`.
void loadKnownProperties(const juce::XmlElement& item, PropertyMap& properties, const juce::String& owner, LoadNotes& notes) {
    std::set<std::string> names;
    for (auto* property : item.getChildWithTagNameIterator("property")) {
        const auto name = property->getStringAttribute("name").toStdString();
        const auto found = properties.find(name);
        if (found == properties.end()) {
            notes.note(owner + " property this version doesn't have", juce::String(name));
            continue;
        }
        if (!names.insert(name).second) {
            notes.note(owner + " property given twice", juce::String(name));
            continue;
        }
        Curve curve;
        if (!loadProperty(*property, curve, notes, juce::String(name))) {
            notes.note(owner + " property that isn't a number", juce::String(name));
            continue;
        }
        found->second = std::move(curve);
    }
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

// An effect this version doesn't have, or past the most an owner holds, is
// left out; a range or parameter that isn't valid is put back to none or its
// default.
void loadEffects(const juce::XmlElement& owner, std::vector<EffectInstance>& effects, std::set<Id>& identities, LoadNotes& notes) {
    for (auto* item : owner.getChildWithTagNameIterator("effect")) {
        const auto type = item->getStringAttribute("type");
        const auto* definition = effectDefinition(type.toStdString());
        if (definition == nullptr) {
            notes.note("Effect this version doesn't have", type);
            continue;
        }
        const auto name = juce::String(definition->name);
        const auto identity = claimId(*item, identities);
        if (!identity.has_value()) {
            notes.note("Effect with a damaged identity", name);
            continue;
        }
        if (effects.size() >= maximumEffectsPerOwner) {
            notes.note("Effect past the 64 one owner holds", name);
            continue;
        }
        auto effect = makeEffect(*identity, *definition);
        effect.name = item->getStringAttribute("name", name).toStdString();
        effect.enabled = item->getBoolAttribute("enabled", true);
        if (item->hasAttribute("start") && item->hasAttribute("duration")) {
            effect.range = EffectRange { item->getDoubleAttribute("start"), item->getDoubleAttribute("duration") };
            if (!effect.range->valid()) {
                notes.note("Effect time range that isn't valid, made the whole clip", name);
                effect.range.reset();
            }
        } else if (item->hasAttribute("start") || item->hasAttribute("duration")) {
            notes.note("Effect time range missing its start or end, made the whole clip", name);
        }
        loadKnownProperties(*item, effect.properties, "Effect", notes);
        if (!effect.valid()) {
            notes.note("Effect settings that aren't valid, reset", name);
            const auto range = effect.range;
            effect.properties = makeEffect(*identity, *definition).properties;
            effect.range = range;
        }
        effects.push_back(std::move(effect));
    }
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
        if (group.height != 0) { item->setAttribute("height", group.height); }
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

// Groups nested under one that's missing, too deep or in a loop move to the
// top level, and tracks filed under a missing group leave it.
template <typename CompositionType>
void repairGroups(CompositionType& project, LoadNotes& notes) {
    if (validGroupHierarchy(project)) { return; }
    for (auto& group : project.groups) {
        Id current = group.parent;
        std::size_t depth = 1;
        while (current != 0 && current != group.id && depth <= maximumGroupDepth) {
            const auto* ancestor = findGroup(project, current);
            if (ancestor == nullptr) { break; }
            current = ancestor->parent;
            ++depth;
        }
        if (current != 0) {
            notes.note("Group under one that's missing or in a loop, moved to the top", juce::String(group.name));
            group.parent = 0;
        }
    }
    const auto lost = [&project](const Track& track) { return track.group != 0 && findGroup(project, track.group) == nullptr; };
    project.tracks.changeEach(lost, [&](Track& track) {
        notes.note("Track in a group that's missing, moved out", juce::String(track.name));
        track.group = 0;
    });
    if (!validGroupHierarchy(project)) {
        for (auto& group : project.groups) { group.parent = 0; }
    }
}

// Routes from a modulator or to a property that's missing are left out, and
// links to a property that's missing, or that would go round in a loop.
template <typename CompositionType>
void repairModulation(CompositionType& project, LoadNotes& notes) {
    std::set<Id> modulators;
    for (const auto& modulator : project.modulators) { modulators.insert(modulator.id); }
    {
        const DrivableProperties drivable(project);
        std::erase_if(project.routes, [&](const ModulationRoute& route) {
            const auto missing = !modulators.contains(route.modulator) || !drivable.contains(route.target, route.property);
            if (missing) { notes.note("Modulation of something that's missing", juce::String(route.property)); }
            return missing;
        });
    }
    std::vector<std::pair<Id, std::string>> broken;
    {
        const DrivableProperties drivable(project);
        forEachPropertyMap(project, [&](Id owner, const PropertyMap& properties) {
            for (const auto& [name, curve] : properties) {
                if (!curve.link.has_value()) { continue; }
                if (!drivable.contains(owner, name) || !hasPropertyCurve(project, curve.link->source, curve.link->property) || linkCreatesCycle(project, owner, name, *curve.link)) { broken.emplace_back(owner, name); }
            }
        });
    }
    for (const auto& [owner, name] : broken) {
        notes.note("Property link to something missing, or in a loop", juce::String(name));
        forEachPropertyMap(project, [&](Id each, auto& properties) {
            const auto found = properties.find(name);
            if (each == owner && found != properties.end()) { found->second.link.reset(); }
        });
    }
}

// A clip property's curve brought inside its range, keys and all.
void clampCurve(Curve& curve, const PropertySpec& spec) {
    curve.base = spec.clamp(curve.base);
    const auto keys = curve.keyframes();
    for (auto key : keys) {
        key.value = spec.clamp(key.value);
        curve.setKey(key);
    }
}

// The main composition loads as the Project, so routes may target its Scope.
// Whatever this version can't use is left out or put right (see LoadNotes).
template <typename CompositionType>
juce::Result loadCompositionContent(const juce::XmlElement& xml, CompositionType& project, const std::vector<std::shared_ptr<const Asset>>& assets, std::set<Id>& identities, const std::set<Id>& compositionIds, LoadNotes& notes) {
    if (!xml.hasTagName("composition")) {
        return juce::Result::fail("Missing composition.");
    }
    const Composition defaults;
    project.name = xml.getStringAttribute("name", "Untitled");
    // Each timing setting outside what this version allows is put back to
    // its default.
    const auto setting = [&](auto& field, auto value, bool allowed, const char* name, auto otherwise) {
        field = allowed ? value : otherwise;
        if (!allowed) { notes.note("Composition setting that isn't valid, reset", name); }
    };
    const auto duration = xml.getDoubleAttribute("duration", 180);
    setting(project.duration, duration, std::isfinite(duration) && duration > 0, "duration", 180.0);
    const auto fps = xml.getDoubleAttribute("fps", defaults.frameRate);
    setting(project.frameRate, fps, std::isfinite(fps) && fps >= 0.001 && fps <= 1000, "frame rate", defaults.frameRate);
    const auto bpm = xml.getDoubleAttribute("bpm", 120);
    setting(project.bpm, bpm, std::isfinite(bpm) && bpm >= 1 && bpm <= 1000, "tempo", 120.0);
    const auto display = xml.getIntAttribute("timeDisplay", 0);
    setting(project.timeDisplay, static_cast<TimeDisplay>(display), display >= 0 && display <= 2, "time display", defaults.timeDisplay);
    const auto beatsPerBar = xml.getIntAttribute("beatsPerBar", 4);
    setting(project.beatsPerBar, beatsPerBar, beatsPerBar >= 1 && beatsPerBar <= 32, "meter", 4);
    const auto snapBeats = xml.getDoubleAttribute("snapBeats", 0.25);
    setting(project.snapBeats, snapBeats, std::isfinite(snapBeats) && snapBeats >= 1.0 / 64 && snapBeats <= 64, "beat grid", 0.25);
    project.gridSnap = xml.getIntAttribute("gridSnap", 1) != 0;
    project.loopStart = xml.getDoubleAttribute("loopStart", 0);
    project.loopEnd = xml.getDoubleAttribute("loopEnd", 0);
    project.looping = xml.getBoolAttribute("looping", false);
    project.clampLoop();
    std::vector<TempoChange> tempoChanges;
    for (auto* item : xml.getChildWithTagNameIterator("tempo")) {
        if (tempoChanges.size() >= 10000) {
            notes.note("Tempo change past the 10000 a composition holds");
            continue;
        }
        tempoChanges.push_back({item->getDoubleAttribute("beat", -1), item->getDoubleAttribute("bpm", -1), item->getBoolAttribute("ramp", false)});
    }
    if (!tempoChanges.empty()) { project.tempoChanges = std::make_shared<const std::vector<TempoChange>>(std::move(tempoChanges)); }
    if (!project.tempo().valid()) {
        notes.note("Tempo changes that aren't in order or in range, left out");
        project.tempoChanges.reset();
    }
    loadEffects(xml, project.effects, identities, notes);
    for (auto* item : xml.getChildWithTagNameIterator("group")) {
        Group group;
        group.name = item->getStringAttribute("name", "Group").toStdString();
        const auto parent = readId(*item, "parent");
        const auto identity = parent.has_value() ? claimId(*item, identities) : std::nullopt;
        if (!identity.has_value()) {
            notes.note("Group with a damaged identity", juce::String(group.name));
            continue;
        }
        group.id = *identity;
        group.parent = *parent;
        group.muted = item->getBoolAttribute("muted", false);
        group.solo = item->getBoolAttribute("solo", false);
        group.height = Track::rowHeight(item->getIntAttribute("height", 0));
        group.spatialPath = item->getBoolAttribute("spatialPath", false);
        group.quaternionRotation = item->getBoolAttribute("quaternionRotation", false);
        loadEffects(*item, group.effects, identities, notes);
        loadKnownProperties(*item, group.properties, "Group", notes);
        if (group.parent == group.id) { group.parent = 0; }
        if (!group.valid()) {
            notes.note("Group transform, colour or opacity that isn't valid, reset", juce::String(group.name));
            group.properties = Group {}.properties;
        }
        project.groups.push_back(std::move(group));
    }
    for (auto* row : xml.getChildWithTagNameIterator("track")) {
        Track track;
        track.name = row->getStringAttribute("name").toStdString();
        const auto trackIdentity = claimId(*row, identities);
        if (!trackIdentity.has_value()) {
            notes.note("Track with a damaged identity", juce::String(track.name));
            continue;
        }
        track.id = *trackIdentity;
        const auto kind = row->getStringAttribute("kind");
        if (kind != "visual" && kind != "audio") {
            notes.note("Track of a kind this version doesn't have", kind);
            continue;
        }
        track.kind = kind == "audio" ? TrackKind::audio : TrackKind::visual;
        track.muted = row->getBoolAttribute("muted", false);
        track.solo = row->getBoolAttribute("solo", false);
        track.locked = row->getBoolAttribute("locked", false);
        track.height = Track::rowHeight(row->getIntAttribute("height", 0));
        track.label = std::clamp(row->getIntAttribute("label", 0), 0, 8);
        track.group = readId(*row, "group").value_or(0);
        loadEffects(*row, track.effects, identities, notes);
        if (track.kind == TrackKind::audio && !track.effects.empty()) {
            notes.note("Effect on an audio track", juce::String(track.name));
            track.effects.clear();
        }
        for (auto* item : row->getChildWithTagNameIterator("clip")) {
            Clip clip;
            clip.name = item->getStringAttribute("name").toStdString();
            const auto clipName = juce::String(clip.name);
            const auto clipIdentity = claimId(*item, identities);
            const auto asset = readId(*item, "asset"), composition = readId(*item, "composition");
            if (!clipIdentity.has_value() || !asset.has_value() || !composition.has_value()) {
                notes.note("Clip with a damaged identity", clipName);
                continue;
            }
            clip.id = *clipIdentity;
            clip.asset = *asset;
            clip.composition = *composition;
            const auto timeBase = item->getStringAttribute("timeBase");
            if (timeBase != "seconds" && timeBase != "beats") { notes.note("Clip timing this version doesn't have, made seconds", clipName); }
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
                    notes.note("Clip of a composition that's missing", clipName);
                    continue;
                }
            } else {
                if (found == nullptr) {
                    notes.note("Clip whose source is missing", clipName);
                    continue;
                }
                if ((track.kind == TrackKind::audio) != (found->audio != nullptr)) {
                    notes.note("Clip on a track of the wrong kind", clipName);
                    continue;
                }
            }
            // Every clip property is one its owner knows, once, inside its
            // declared range; the rest are left out or brought inside.
            const auto specs = track.kind == TrackKind::audio ? std::span<const PropertySpec>(audioPropertySpecs) : objectPropertySpecs;
            const bool luaClip = clip.composition == 0 && found != nullptr && found->extension.equalsIgnoreCase(".lua");
            const bool fractalClip = clip.composition == 0 && found != nullptr && isFractal(found->extension);
            const auto* generator = clip.composition == 0 && found != nullptr ? generatorOf(*found) : nullptr;
            for (auto* property : item->getChildWithTagNameIterator("property")) {
                const auto name = property->getStringAttribute("name").toStdString();
                auto* spec = findPropertySpec(specs, name);
                if (spec == nullptr && luaClip) { spec = findPropertySpec(luaSliderSpecs, name); }
                if (spec == nullptr && fractalClip) { spec = findPropertySpec(fractalPropertySpecs, name); }
                if (spec == nullptr && generator != nullptr) { spec = findPropertySpec(generator->parameters, name); }
                if (spec == nullptr) {
                    notes.note("Clip property this version doesn't have", name.empty() ? juce::String("(unnamed)") : juce::String(name));
                    continue;
                }
                if (clip.properties.contains(name)) {
                    notes.note("Clip property given twice", juce::String(name));
                    continue;
                }
                Curve curve;
                if (!loadProperty(*property, curve, notes, juce::String(name))) {
                    notes.note("Clip property that isn't a number", juce::String(name));
                    continue;
                }
                if (!spec->contains(curve.base) || std::any_of(curve.keyframes().begin(), curve.keyframes().end(), [spec](const auto& key) { return !spec->contains(key.value); })) {
                    notes.note("Clip property outside its range, brought inside", juce::String(spec->label.data(), spec->label.size()));
                    clampCurve(curve, *spec);
                }
                clip.properties[name] = std::move(curve);
            }
            const auto* luaBake = item->getChildByName("luaBake");
            if (luaBake != nullptr) {
                // Saved slider bakes load without executing the script; one
                // that isn't valid is left out, and the editor bakes again.
                auto bake = std::make_shared<LuaClipBake>();
                bake->key = luaBake->getStringAttribute("key").toStdString();
                const auto encoded = luaBake->getAllSubText();
                if (luaClip && !bake->key.empty() && readBase64(encoded, bake->archive, BakedSourceArchive::maximumCompressedBytes) && bake->archive.getSize() > 0) {
                    const auto frames = BakedSourceArchive::decode(bake->archive);
                    // Checked against the clip's sliders once routes and links load.
                    if (frames) {
                        bake->source = std::make_shared<const PreparedSource>(frames.source);
                        clip.luaBake = std::move(bake);
                    }
                }
            }
            loadEffects(*item, clip.effects, identities, notes);
            if (track.kind == TrackKind::audio) {
                if (!clip.effects.empty()) {
                    notes.note("Effect on an audio clip", clipName);
                    clip.effects.clear();
                }
                if (!validProperties(clip.properties, audioPropertySpecs)) {
                    notes.note("Audio clip gain or pan that isn't valid, reset", clipName);
                    clip.properties = defaultProperties(audioPropertySpecs);
                }
            }
            if (!track.insert(std::move(clip), project.tempo())) {
                notes.note("Clip that overlaps another on its track, or has no length", clipName);
            }
        }
        project.tracks.push_back(std::move(track));
    }
    for (auto* item : xml.getChildWithTagNameIterator("camera")) {
        Camera camera;
        camera.name = item->getStringAttribute("name", "Camera").toStdString();
        const auto identity = claimId(*item, identities);
        if (!identity.has_value()) {
            notes.note("Camera with a damaged identity", juce::String(camera.name));
            continue;
        }
        camera.id = *identity;
        camera.target = readId(*item, "target").value_or(0);
        camera.parent = readId(*item, "parent").value_or(0);
        if (camera.parent != 0 && findGroup(project, camera.parent) == nullptr) {
            notes.note("Camera carried by a group that's missing", juce::String(camera.name));
            camera.parent = 0;
        }
        if (camera.target != 0 && findGroup(project, camera.target) == nullptr && !hasVisualClip(project, camera.target)) {
            notes.note("Camera aimed at something that's missing", juce::String(camera.name));
            camera.target = 0;
        }
        loadKnownProperties(*item, camera.properties, "Camera", notes);
        if (!camera.valid()) {
            notes.note("Camera transform or field of view that isn't valid, reset", juce::String(camera.name));
            camera.properties = Camera {}.properties;
        }
        project.cameras.push_back(std::move(camera));
    }
    for (auto* item : xml.getChildWithTagNameIterator("marker")) {
        const auto id = claimId(*item, identities);
        Marker marker {id.value_or(0), item->getDoubleAttribute("time", -1), item->getStringAttribute("name")};
        if (!id.has_value() || !marker.valid(project.duration)) {
            notes.note("Marker that isn't valid", marker.name);
            continue;
        }
        project.markers.push_back(std::move(marker));
    }
    sortMarkers(project.markers);
    for (std::size_t index = 1; index < project.markers.size();) {
        if (project.markers[index].time - project.markers[index - 1].time < Marker::minimumSpacing) {
            notes.note("Marker at the same time as another", project.markers[index].name);
            project.markers.erase(project.markers.begin() + static_cast<std::ptrdiff_t>(index));
            continue;
        }
        ++index;
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
            notes.note("Camera cut that isn't valid, or to a camera that's missing");
            continue;
        }
        project.cameraCuts.push_back(cut);
    }
    std::sort(project.cameraCuts.begin(), project.cameraCuts.end(), [](const auto& left, const auto& right) { return left.start < right.start; });
    for (std::size_t index = 1; index < project.cameraCuts.size();) {
        if (project.cameraCuts[index].start < project.cameraCuts[index - 1].end()) {
            notes.note("Camera cut that overlaps another");
            project.cameraCuts.erase(project.cameraCuts.begin() + static_cast<std::ptrdiff_t>(index));
            continue;
        }
        ++index;
    }
    repairGroups(project, notes);
    for (auto* item : xml.getChildWithTagNameIterator("modulator")) {
        Modulator modulator;
        modulator.name = item->getStringAttribute("name").toStdString();
        const auto identity = claimId(*item, identities);
        modulator.id = identity.value_or(0);
        const auto tempoSync = item->getIntAttribute("tempoSync", -1);
        const auto seed = item->getStringAttribute("seed", "0").getLargeIntValue();
        modulator.shape.waveform = static_cast<ModulationWaveform>(item->getIntAttribute("waveform", -1));
        modulator.shape.rateHz = item->getDoubleAttribute("rateHz", 1);
        modulator.shape.phase = item->getDoubleAttribute("phase", 0);
        modulator.shape.tempoSync = tempoSync == 1;
        modulator.shape.beatsPerCycle = item->getDoubleAttribute("beatsPerCycle", 1);
        modulator.shape.seed = static_cast<std::uint32_t>(std::clamp<juce::int64>(seed, 0, std::numeric_limits<std::uint32_t>::max()));
        if (!identity.has_value() || tempoSync < 0 || tempoSync > 1 || !modulator.valid()) {
            notes.note("Modulator that isn't valid", juce::String(modulator.name));
            continue;
        }
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
        if (!identity.has_value() || mode < 0 || mode > 1 || !route.valid()) {
            notes.note("Modulation that isn't valid", juce::String(route.property));
            continue;
        }
        project.routes.push_back(std::move(route));
    }
    repairModulation(project, notes);
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

juce::XmlElement Document::save(const Project& project) {
    auto xml = saveCompositionContent(project);
    xml.setAttribute("scopeDwell", exactNumber(project.scope.dwellMicros));
    xml.setAttribute("scopeTravel", exactNumber(project.scope.travelMicrosPerUnit));
    xml.setAttribute("scopeSettle", exactNumber(project.scope.settleMicros));
    if (project.scope.xyOnly) { xml.setAttribute("scopeXYOnly", true); }
    xml.setAttribute("sampleRate", exactNumber(project.sampleRate));
    if (project.description.isNotEmpty()) { xml.setAttribute("description", project.description); }
    auto* exporting = xml.createNewChildElement("export");
    exporting->setAttribute("width", project.exportSettings.width);
    exporting->setAttribute("height", project.exportSettings.height);
    exporting->setAttribute("codec", project.exportSettings.codec);
    exporting->setAttribute("crf", project.exportSettings.crf);
    exporting->setAttribute("soundtrack", project.exportSettings.soundtrack);
    auto* beam = xml.createNewChildElement("scopeBeam");
    saveProperties(*beam, project.beam.properties);
    for (const auto& asset : project.assets) {
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
        if (asset->originalSize) { item->setAttribute("originalSize", true); }
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
                const auto mode = asset->rasterSettings.mode;
                raster->setAttribute("mode", mode == RasterSettings::Mode::contours ? "contours" : mode == RasterSettings::Mode::scanlines ? "scanlines" : "hilbert");
                raster->setAttribute("shading", asset->rasterSettings.shading == RasterSettings::Shading::colour ? "colour" : "density");
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
    for (const auto& definition : project.definitions) {
        auto* item = xml.createNewChildElement("definition");
        item->setAttribute("id", juce::String(definition->id));
        item->addChildElement(new juce::XmlElement(saveCompositionContent(*definition)));
    }
    return xml;
}

juce::XmlElement Document::save() const { return save(state); }

juce::Result Document::load(const juce::XmlElement& xml, LoadNotes* notes) {
    Project prepared;
    const auto result = prepareLoad(xml, prepared, nullptr, notes);
    if (result.wasOk()) { reset(std::move(prepared)); }
    return result;
}

juce::Result Document::prepareLoad(const juce::XmlElement& xml, Project& output, const std::atomic<bool>* cancel, LoadNotes* given) try {
    LoadNotes ignored;
    auto& notes = given != nullptr ? *given : ignored;
    if (cancelled(cancel)) { return juce::Result::fail("Project loading cancelled."); }
    if (!xml.hasTagName("composition")) { return juce::Result::fail("Missing composition."); }
    Project project;
    const ScopeProfile defaults;
    project.scope.dwellMicros = xml.getDoubleAttribute("scopeDwell", defaults.dwellMicros);
    project.scope.travelMicrosPerUnit = xml.getDoubleAttribute("scopeTravel", defaults.travelMicrosPerUnit);
    project.scope.settleMicros = xml.getDoubleAttribute("scopeSettle", defaults.settleMicros);
    project.scope.xyOnly = xml.getBoolAttribute("scopeXYOnly", defaults.xyOnly);
    if (!project.scope.valid()) {
        notes.note("Scope timing that isn't valid, reset");
        project.scope = defaults;
    }
    project.sampleRate = xml.getDoubleAttribute("sampleRate", Project::defaultSampleRate);
    if (!Project::validSampleRate(project.sampleRate)) {
        notes.note("Sample rate this version can't draw at, set to 192 kHz", juce::String(project.sampleRate) + " Hz");
        project.sampleRate = Project::defaultSampleRate;
    }
    project.description = xml.getStringAttribute("description").trim().substring(0, Project::longestDescription);
    const auto* exporting = xml.getChildByName("export");
    if (exporting != nullptr) {
        ExportSettings settings;
        settings.width = exporting->getIntAttribute("width", settings.width);
        settings.height = exporting->getIntAttribute("height", settings.height);
        settings.codec = exporting->getIntAttribute("codec", settings.codec);
        settings.crf = exporting->getIntAttribute("crf", settings.crf);
        settings.soundtrack = exporting->getBoolAttribute("soundtrack", settings.soundtrack);
        if (settings.valid()) {
            project.exportSettings = settings;
        } else {
            notes.note("Export settings this version can't use, set to the defaults");
        }
    }
    // The Scope's identity is reserved before anything else claims one.
    std::set<Id> identities {beamIdentity}, compositionIds;
    std::vector<std::pair<const juce::XmlElement*, Id>> definitions;
    for (auto* item : xml.getChildWithTagNameIterator("definition")) {
        const auto* content = item->getChildByName("composition");
        const auto identity = claimId(*item, identities);
        if (!identity.has_value() || content == nullptr || content->getNextElementWithTagName("composition") != nullptr
            || content->getChildByName("asset") != nullptr || content->getChildByName("definition") != nullptr) {
            notes.note("Reusable composition that's damaged", content != nullptr ? content->getStringAttribute("name") : juce::String());
            continue;
        }
        compositionIds.insert(*identity);
        definitions.emplace_back(content, *identity);
    }
    for (auto* item : xml.getChildWithTagNameIterator("asset")) {
        if (cancelled(cancel)) { return juce::Result::fail("Project loading cancelled."); }
        auto asset = std::make_shared<Asset>();
        // A name is only shown, so a missing one is replaced below, not refused.
        asset->name = item->getStringAttribute("name").trim().removeCharacters("\r\n");
        asset->extension = item->getStringAttribute("extension");
        const auto identity = claimId(*item, identities);
        // A source that can't be used is left out; its clips go with it.
        const auto leaveOut = [&](const juce::String& why) { notes.note(why, asset->name.isNotEmpty() ? asset->name : asset->extension); };
        if (!identity.has_value()) {
            leaveOut("Source with a damaged identity");
            continue;
        }
        asset->id = *identity;
        if (asset->extension.equalsIgnoreCase(".blender")) {
            const auto* live = item->getChildByName("blender");
            if (live == nullptr || asset->name.trim().isEmpty()) {
                leaveOut("Blender source whose settings are damaged");
                continue;
            }
            if (item->getNumChildElements() != 1) { notes.note("Blender source with settings given twice, the first used", asset->name); }
            asset->blenderSettings.port = live->getIntAttribute("port", 0);
            const auto policy = live->getStringAttribute("disconnect");
            if (policy != "freeze" && policy != "blank") { notes.note("Blender source's disconnect setting this version doesn't have, made freeze", asset->name); }
            asset->blenderSettings.freezeOnDisconnect = policy != "blank";
            const auto result = decodeAsset(*asset, cancel);
            if (result.failed()) {
                leaveOut("Blender source that couldn't be set up");
                continue;
            }
            project.assets.push_back(std::move(asset));
            continue;
        }
        if (asset->name.isEmpty()) { asset->name = "Untitled" + asset->extension; }
        if (asset->extension.equalsIgnoreCase(".lsystem")) { asset->fractalDepth = item->getIntAttribute("fractalDepth", -1); }
        asset->originalSize = asset->extension.equalsIgnoreCase(".obj") && item->getBoolAttribute("originalSize", false);
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
        if (source == nullptr || !readBase64(source->getAllSubText(), asset->data, maximumSourceBytes)) {
            leaveOut("Source whose file is damaged, or larger than the 64 MiB limit");
            continue;
        }
        if (osci::files::isImage(asset->extension)) {
            // Settings this version doesn't have are its defaults.
            const auto* raster = item->getChildByName("raster");
            const RasterSettings rasterDefaults;
            if (raster == nullptr) { notes.note("Image whose settings are missing, given the defaults", asset->name); }
            const auto mode = raster != nullptr ? raster->getStringAttribute("mode") : juce::String("contours");
            if (mode != "contours" && mode != "scanlines" && mode != "hilbert") { notes.note("Image trace this version doesn't have, made outlines", asset->name); }
            asset->rasterSettings.mode = mode == "scanlines" ? RasterSettings::Mode::scanlines : mode == "hilbert" ? RasterSettings::Mode::hilbert : RasterSettings::Mode::contours;
            const auto shading = raster != nullptr ? raster->getStringAttribute("shading", "density") : juce::String("density");
            asset->rasterSettings.shading = shading == "colour" ? RasterSettings::Shading::colour : RasterSettings::Shading::density;
            if (raster != nullptr) {
                asset->rasterSettings.threshold = raster->getDoubleAttribute("threshold", rasterDefaults.threshold);
                asset->rasterSettings.invert = raster->getBoolAttribute("invert");
                asset->rasterSettings.resolution = raster->getIntAttribute("resolution", rasterDefaults.resolution);
                if (videoSource) { asset->rasterSettings.videoFrameRate = raster->getDoubleAttribute("frameRate", 0); }
                const auto points = raster->getIntAttribute("pointsPerFrame", 0);
                if (points > 0) { asset->rasterSettings.pointsPerFrame = static_cast<std::size_t>(points); }
            }
        }
        if (videoSource) {
            const auto* cache = item->getChildByName("video-cache");
            if (cache == nullptr || !readBase64(cache->getAllSubText(), asset->bakedData, BakedSourceArchive::maximumCompressedBytes) || asset->bakedData.getSize() == 0) {
                leaveOut("Video whose prepared frames are missing (import it again)");
                continue;
            }
            asset->bakeKey = cache->getStringAttribute("key");
        }
        if (luaSource) {
            auto* bake = item->getChildByName("bake");
            const auto points = bake != nullptr ? bake->getIntAttribute("pointsPerFrame", 0) : -1;
            const auto seed = bake != nullptr ? bake->getStringAttribute("seed", "-1").getLargeIntValue() : -1;
            if (bake == nullptr || points < 0 || seed < 0 || seed > std::numeric_limits<std::uint32_t>::max()
                || !readBase64(bake->getAllSubText(), asset->bakedData, BakedSourceArchive::maximumCompressedBytes) || asset->bakedData.getSize() == 0) {
                leaveOut("Lua source whose prepared frames are missing (import it again)");
                continue;
            }
            asset->bakeSettings.duration = bake->getDoubleAttribute("duration", 0);
            asset->bakeSettings.frameRate = bake->getDoubleAttribute("frameRate", 0);
            asset->bakeSettings.bpm = bake->getDoubleAttribute("bpm", 0);
            asset->bakeSettings.pointsPerFrame = static_cast<std::size_t>(points);
            asset->bakeSettings.seed = static_cast<std::uint32_t>(seed);
            asset->bakeKey = bake->getStringAttribute("key");
        }
        const auto result = decodeAsset(*asset, cancel);
        if (cancelled(cancel)) { return juce::Result::fail("Project loading cancelled."); }
        if (result.failed()) {
            notes.note("Source that couldn't be prepared", asset->name + " (" + result.getErrorMessage().trimCharactersAtEnd(".") + ")");
            continue;
        }
        project.assets.push_back(std::move(asset));
    }
    // The Scope's picture; a project saved without it keeps the defaults.
    const auto* beam = xml.getChildByName("scopeBeam");
    if (beam != nullptr) {
        if (beam->getNextElementWithTagName("scopeBeam") != nullptr) { notes.note("Second Scope, left out"); }
        loadKnownProperties(*beam, project.beam.properties, "Scope", notes);
        if (!project.beam.valid()) {
            notes.note("Scope setting that isn't valid, reset");
            project.beam = Beam {};
        }
    }
    const auto main = loadCompositionContent(xml, project, project.assets, identities, compositionIds, notes);
    if (main.failed()) { return main; }
    for (const auto& [content, identity] : definitions) {
        auto definition = std::make_shared<CompositionDefinition>();
        definition->id = identity;
        const auto result = loadCompositionContent(*content, *definition, project.assets, identities, compositionIds, notes);
        if (result.failed()) { return result; }
        project.definitions.push_back(std::move(definition));
    }
    const auto graph = validateCompositionGraph(project);
    if (!graph) { return juce::Result::fail(graph.error); }
    if (cancelled(cancel)) { return juce::Result::fail("Project loading cancelled."); }
    // New identities count up from the highest, so they never reach the Scope's.
    if (highestProjectIdentity(project) > Document::highestLoadedId) { return juce::Result::fail("A project identity is out of range."); }
    output = std::move(project);
    return juce::Result::ok();
} catch (const std::exception& error) {
    return juce::Result::fail("Cannot prepare project: " + juce::String(error.what()));
}
}

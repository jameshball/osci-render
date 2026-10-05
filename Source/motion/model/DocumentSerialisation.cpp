#include "Document.h"
#include "Cancellation.h"
#include "CompositionGraph.h"
#include "LuaClipBake.h"
#include "ModulationGraph.h"
#include "PropertySchema.h"
#include "../import/BakedSourceArchive.h"
#include "../import/MidiSourcePreparer.h"
#include "../import/SourceDecoding.h"
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
        point->setAttribute("in", exactNumber(key.incomingSlope));
        point->setAttribute("out", exactNumber(key.outgoingSlope));
        if (key.incomingInfluence != Keyframe::defaultInfluence) { point->setAttribute("inInfluence", exactNumber(key.incomingInfluence)); }
        if (key.outgoingInfluence != Keyframe::defaultInfluence) { point->setAttribute("outInfluence", exactNumber(key.outgoingInfluence)); }
    }
}

juce::Result loadProperty(const juce::XmlElement& property, Curve& curve) {
    curve = Curve(property.getDoubleAttribute("base"));
    if (!std::isfinite(curve.base)) {
        return juce::Result::fail("Invalid property value.");
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
            item->setAttribute("start", exactNumber(effect.range->start));
            item->setAttribute("duration", exactNumber(effect.range->duration));
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
            item->setAttribute("contentBpm", exactNumber(clip.contentBpm));
            item->setAttribute("start", exactNumber(clip.start));
            item->setAttribute("duration", exactNumber(clip.duration));
            item->setAttribute("offset", exactNumber(clip.offset));
            item->setAttribute("rate", exactNumber(clip.rate));
            if (clip.spatialPath) { item->setAttribute("spatialPath", true); }
            if (clip.quaternionRotation) { item->setAttribute("quaternionRotation", true); }
            if (track.kind == TrackKind::visual && clip.composition == 0) {
                auto* instrument = item->createNewChildElement("instrument");
                instrument->setAttribute("attack", exactNumber(clip.instrument.attack));
                instrument->setAttribute("decay", exactNumber(clip.instrument.decay));
                instrument->setAttribute("sustain", exactNumber(clip.instrument.sustain));
                instrument->setAttribute("release", exactNumber(clip.instrument.release));
                if (clip.instrument.bendRange != MidiInstrument{}.bendRange) { instrument->setAttribute("bendRange", exactNumber(clip.instrument.bendRange)); }
            }
            if (clip.midi != nullptr) {
                auto* pattern = item->createNewChildElement("midi");
                pattern->setAttribute("asset", juce::String(clip.midiAsset));
                for (const auto& note : clip.midi->notes()) {
                    auto* event = pattern->createNewChildElement("note");
                    event->setAttribute("id", juce::String(note.id));
                    event->setAttribute("start", exactNumber(note.start));
                    event->setAttribute("duration", exactNumber(note.duration));
                    event->setAttribute("pitch", note.pitch);
                    event->setAttribute("velocity", note.velocity);
                    event->setAttribute("channel", note.channel);
                }
                for (const auto& control : clip.midi->controls()) {
                    auto* change = pattern->createNewChildElement("control");
                    change->setAttribute("beat", exactNumber(control.beat));
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
        item->setAttribute("kind", modulator.kind == ModulatorKind::envelope ? "envelope" : modulator.kind == ModulatorKind::controller ? "controller" : "oscillator");
        item->setAttribute("controller", modulator.controller);
        item->setAttribute("controllerChannel", modulator.controllerChannel);
        item->setAttribute("waveform", static_cast<int>(modulator.shape.waveform));
        item->setAttribute("rateHz", exactNumber(modulator.shape.rateHz));
        item->setAttribute("phase", exactNumber(modulator.shape.phase));
        item->setAttribute("tempoSync", modulator.shape.tempoSync);
        item->setAttribute("beatsPerCycle", exactNumber(modulator.shape.beatsPerCycle));
        item->setAttribute("seed", juce::String(static_cast<juce::int64>(modulator.shape.seed)));
        item->setAttribute("source", juce::String(modulator.source));
        item->setAttribute("attack", exactNumber(modulator.attack));
        item->setAttribute("decay", exactNumber(modulator.decay));
        item->setAttribute("sustain", exactNumber(modulator.sustain));
        item->setAttribute("release", exactNumber(modulator.release));
        item->setAttribute("velocity", exactNumber(modulator.velocity));
        item->setAttribute("lowestPitch", modulator.lowestPitch);
        item->setAttribute("highestPitch", modulator.highestPitch);
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
            const auto found = findAsset(assets, clip.asset);
            if (clip.id == 0 || !identities.insert(clip.id).second) { return juce::Result::fail("Invalid clip identity."); }
            if (clip.composition != 0) {
                if (clip.asset != 0 || !compositionIds.contains(clip.composition) || track.kind != TrackKind::visual) {
                    return juce::Result::fail("Invalid reusable composition reference or track kind.");
                }
            } else {
                if (found == nullptr) { return juce::Result::fail("Invalid clip asset."); }
                if ((track.kind == TrackKind::audio) != (found->audio != nullptr)) {
                    return juce::Result::fail("The clip source type does not match its audio or visual track.");
                }
                if (found->midi != nullptr) { return juce::Result::fail("A MIDI pattern requires a visual instrument source for its clip."); }
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
                    const auto source = findAsset(assets, clip.midiAsset);
                    if (source == nullptr || source->midi == nullptr) { return juce::Result::fail("MIDI pattern source is missing or is not a MIDI asset."); }
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
                // Checked against the clip's sliders once routes and links load.
                bake->source = std::make_shared<const PreparedSource>(frames.source);
                clip.luaBake = std::move(bake);
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
    // Only a slider bake matching its clip's sliders and what drives them is
    // kept; a stale one is dropped and the editor bakes again.
    for (auto& track : project.tracks) {
        for (auto& clip : track.clips) {
            if (clip.luaBake == nullptr) { continue; }
            const auto asset = std::find_if(assets.begin(), assets.end(), [&clip](const auto& item) { return item->id == clip.asset; });
            const auto plan = asset == assets.end() ? std::nullopt : luaSliderPlan(**asset, clip, project);
            const auto& source = *clip.luaBake->source;
            const auto current = plan.has_value() && plan->key == clip.luaBake->key && source.frameCount() == plan->settings.frameCount() && source.frameRate() == plan->settings.frameRate;
            if (!current) { clip.luaBake.reset(); }
        }
    }
    return juce::Result::ok();
}
}

juce::XmlElement Document::save() const {
    auto xml = saveCompositionContent(state);
    xml.setAttribute("scopeDwell", exactNumber(state.scope.dwellMicros));
    xml.setAttribute("scopeTravel", exactNumber(state.scope.travelMicrosPerUnit));
    xml.setAttribute("scopeSettle", exactNumber(state.scope.settleMicros));
    auto* beam = xml.createNewChildElement("scopeBeam");
    for (const auto& [name, curve] : state.beam.properties) { saveProperty(*beam, name, curve); }
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
        if (isMidiSource(asset->extension)) { item->setAttribute("midiImportBpm", exactNumber(asset->midiImportBpm)); }
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
        const auto identity = item->getStringAttribute("id").getLargeIntValue();
        if (identity <= 0 || !identities.insert(static_cast<Id>(identity)).second) { return juce::Result::fail("Invalid reusable composition identity."); }
        compositionIds.insert(static_cast<Id>(identity));
    }
    for (auto* item : xml.getChildWithTagNameIterator("asset")) {
        if (cancelled(cancel)) { return juce::Result::fail("Project loading cancelled."); }
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
        const bool videoSource = osci::files::isVideo(asset->extension);
        auto* source = luaSource || videoSource ? item->getChildByName("source") : item;
        if (source == nullptr) { return juce::Result::fail("Baked asset is missing its source."); }
        const auto encoded = source->getAllSubText();
        if (static_cast<std::size_t>(encoded.length()) > (maximumSourceBytes / 3 + 1) * 4) {
            return juce::Result::fail("Embedded source exceeds the 64 MiB import limit.");
        }
        if (asset->id == 0 || !identities.insert(asset->id).second || !asset->data.fromBase64Encoding(encoded)) {
            return juce::Result::fail("Invalid asset data or identity.");
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
    // The Scope's picture; a project saved without it keeps the defaults.
    const auto* beam = xml.getChildByName("scopeBeam");
    if (beam != nullptr) {
        if (beam->getNextElementWithTagName("scopeBeam") != nullptr) { return juce::Result::fail("A project has one Scope."); }
        std::set<std::string> properties;
        for (auto* property : beam->getChildWithTagNameIterator("property")) {
            const auto name = property->getStringAttribute("name").toStdString();
            const auto found = project.beam.properties.find(name);
            if (found == project.beam.properties.end() || !properties.insert(name).second) { return juce::Result::fail("Unknown or duplicate Scope property."); }
            const auto result = loadProperty(*property, found->second);
            if (result.failed()) { return result; }
        }
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
        definition->id = static_cast<Id>(item->getStringAttribute("id").getLargeIntValue());
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

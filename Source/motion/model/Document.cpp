#include "Document.h"
#include "DocumentEdits.h"
#include "CompositionGraph.h"
#include "LuaClipBake.h"
#include "ModulationGraph.h"
#include "PropertySchema.h"
#include "../import/SourceDecoding.h"
#include <numbers>
#include <set>

namespace motion {

// Approximate bytes a snapshot holds by value (curves, keys, clips, tracks).
// Shared, immutable payloads are counted separately, per step, only when that
// step alone keeps them alive.
static std::size_t curveBytes(const Curve& curve) {
    return sizeof(Curve) + curve.keyframes().size() * (sizeof(Keyframe) + sizeof(double));
}

static std::size_t propertyBytes(const PropertyMap& properties) {
    std::size_t bytes = 0;
    for (const auto& [name, curve] : properties) { bytes += name.size() + curveBytes(curve); }
    return bytes;
}

static std::size_t effectBytes(const std::vector<EffectInstance>& effects) {
    std::size_t bytes = 0;
    for (const auto& effect : effects) { bytes += sizeof(EffectInstance) + propertyBytes(effect.properties); }
    return bytes;
}

static std::size_t trackBytes(const Track& track) {
    auto bytes = sizeof(Track) + effectBytes(track.effects);
    for (const auto& clip : track.clips) { bytes += sizeof(Clip) + clip.name.size() + propertyBytes(clip.properties) + effectBytes(clip.effects); }
    return bytes;
}

// Tracks also held by `sharedWith` belong to both snapshots and are not counted.
static std::size_t compositionBytes(const Composition& composition, const SharedList<Track>* sharedWith = nullptr) {
    std::size_t bytes = sizeof(Composition) + effectBytes(composition.effects);
    for (const auto& group : composition.groups) { bytes += sizeof(Group) + propertyBytes(group.properties) + effectBytes(group.effects); }
    for (std::size_t index = 0; index < composition.tracks.size(); ++index) {
        if (sharedWith == nullptr || !composition.tracks.shares(index, *sharedWith)) { bytes += trackBytes(composition.tracks[index]); }
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
    // Redo and undo keep the current view options; the first perform sets them.
    bool perform() override {
        const juce::ScopedValueSetter<const Project*> carry(owner.carryOptionsFrom, performed ? &before : nullptr);
        owner.previewBase.reset();
        owner.previewOrigin.reset();
        owner.apply(after);
        performed = true;
        return true;
    }
    bool undo() override {
        const juce::ScopedValueSetter<const Project*> carry(owner.carryOptionsFrom, &after);
        owner.previewBase.reset();
        owner.previewOrigin.reset();
        owner.apply(before);
        return true;
    }
    bool performed = false;
    // KiB, so the undo manager can bound history by memory as well as count.
    int getSizeInUnits() override {
        auto bytes = compositionBytes(before, &after.tracks) + compositionBytes(after, &before.tracks) + propertyBytes(before.beam.properties) + propertyBytes(after.beam.properties);
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
    whole.sampleRate = view.sampleRate;
    whole.exportSettings = view.exportSettings;
    whole.description = view.description;
    whole.beam = view.beam;
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
    const auto found = findDefinition(state, scopeId);
    if (found == nullptr) { scopeId = 0; ++scopeChanges; scopeView = {}; return; }
    scopeView = state;
    static_cast<Composition&>(scopeView) = *found;
}

juce::Result Document::setMarker(Id id, double time, juce::String name) {
    name = name.trim();
    if (!std::isfinite(time) || time < 0 || time > project().duration) { return juce::Result::fail("Place the marker within the composition duration."); }
    if (!Marker::validName(name)) { return juce::Result::fail(juce::String::fromUTF8("Use a marker name of 1\xe2\x80\x93" "120 characters on one line.")); }
    const auto& markers = project().markers;
    const auto found = std::find_if(markers.begin(), markers.end(), [id](const auto& marker) { return marker.id == id; });
    const bool adding = id == 0;
    if (std::any_of(markers.begin(), markers.end(), [id, time](const auto& marker) { return marker.id != id && std::abs(marker.time - time) < Marker::minimumSpacing; })) { return juce::Result::fail("A marker already exists at this position."); }
    if (!adding && found == markers.end()) { return juce::Result::fail("The marker no longer exists."); }
    if (!adding && found->time == time && found->name == name) { return juce::Result::ok(); }
    if (adding) {
        const auto highest = highestId();
        if (highest >= maximumId) { return noIdentitiesLeft(); }
        id = highest + 1;
        lastId = id;
    }
    edit(adding ? "Add marker" : "Edit marker", [id, time, name, adding](Project& project) {
        if (adding) {
            project.markers.push_back({id, time, name});
        } else {
            for (auto& marker : project.markers) { if (marker.id == id) { marker.time = time; marker.name = name; } }
        }
        sortMarkers(project.markers);
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
    scopeId = id; ++scopeChanges; ++stateRevision;
    refreshScope();
    if (onChanged) { onChanged(); }
    sendChangeMessage();
    return juce::Result::ok();
}

// Snapshots taken before a slider bake landed (undo history, gesture
// previews) carry the installed bake forward; the baker re-checks its key.
// A clip keeps its installed slider bake across edits while it plays the same
// source; a replaced source (or a removed clip) leaves the bake behind.
static void carryLuaBakes(Project& next, const Project& current) {
    const auto sourceOf = [](const Project& project, Id asset) -> const Asset* {
        return findAsset(project.assets, asset).get();
    };
    std::map<Id, std::pair<std::shared_ptr<const LuaClipBake>, const Asset*>> installed;
    const auto collect = [&](const Composition& composition) {
        for (const auto& track : composition.tracks) {
            for (const auto& clip : track.clips) {
                if (clip.luaBake != nullptr) { installed.emplace(clip.id, std::make_pair(clip.luaBake, sourceOf(current, clip.asset))); }
            }
        }
    };
    forEachComposition(current, collect);
    if (installed.empty()) { return; }
    const auto carried = [&](const Clip& clip) -> std::shared_ptr<const LuaClipBake> {
        const auto found = installed.find(clip.id);
        const auto same = clip.luaBake == nullptr && found != installed.end() && found->second.second == sourceOf(next, clip.asset);
        return same ? found->second.first : nullptr;
    };
    const auto trackNeedsCarry = [&](const Track& track) {
        return std::any_of(track.clips.begin(), track.clips.end(), [&](const auto& clip) { return carried(clip) != nullptr; });
    };
    const auto needsCarry = [&](const Composition& composition) {
        return std::any_of(composition.tracks.begin(), composition.tracks.end(), trackNeedsCarry);
    };
    const auto carry = [&](Composition& composition) {
        composition.tracks.changeEach(trackNeedsCarry, [&](Track& track) {
            for (auto& clip : track.clips) {
                auto bake = carried(clip);
                if (bake != nullptr) { clip.luaBake = std::move(bake); }
            }
        });
    };
    changeEachComposition(next, needsCarry, carry);
}

// Time display, snapping and the loop switch are view options: undo and redo
// keep the current ones (like track heights), except those the step itself
// changed (`other` is the step's opposite snapshot).
static void carryViewOptions(Project& next, const Project& current, const Project& other) {
    const auto carry = [](Composition& to, const Composition& from, const Composition& opposite) {
        if (to.timeDisplay == opposite.timeDisplay) { to.timeDisplay = from.timeDisplay; }
        if (to.snapBeats == opposite.snapBeats) { to.snapBeats = from.snapBeats; }
        if (to.gridSnap == opposite.gridSnap) { to.gridSnap = from.gridSnap; }
        if (to.looping == opposite.looping) { to.looping = from.looping && to.hasLoop(); }
    };
    const auto find = [](const Project& project, Id id) -> const Composition* {
        return findDefinition(project, id).get();
    };
    carry(next, current, other);
    for (auto& definition : next.definitions) {
        if (definition == nullptr) { continue; }
        const auto* from = find(current, definition->id);
        const auto* opposite = find(other, definition->id);
        if (from == nullptr || opposite == nullptr) { continue; }
        // Work out the carried options first; copy the definition only if one differs.
        Composition options;
        options.timeDisplay = definition->timeDisplay;
        options.snapBeats = definition->snapBeats;
        options.gridSnap = definition->gridSnap;
        options.looping = definition->looping;
        options.loopStart = definition->loopStart;
        options.loopEnd = definition->loopEnd;
        carry(options, *from, *opposite);
        const auto same = options.timeDisplay == definition->timeDisplay && options.snapBeats == definition->snapBeats && options.gridSnap == definition->gridSnap && options.looping == definition->looping;
        if (same) { continue; }
        auto updated = std::make_shared<CompositionDefinition>(*definition);
        updated->timeDisplay = options.timeDisplay;
        updated->snapBeats = options.snapBeats;
        updated->gridSnap = options.gridSnap;
        updated->looping = options.looping;
        definition = std::move(updated);
    }
}

// Track heights are view state: every snapshot shows the current heights, so
// undo and redo never resize rows. A new document starts from its own.
static void carryTrackHeights(Project& next, const Project& current) {
    std::map<Id, int> heights;
    const auto collect = [&](const Composition& composition) { for (const auto& track : composition.tracks) { heights[track.id] = track.height; } };
    forEachComposition(current, collect);
    const auto trackDiffers = [&](const Track& track) {
        const auto found = heights.find(track.id);
        return found != heights.end() && found->second != track.height;
    };
    const auto differs = [&](const Composition& composition) {
        return std::any_of(composition.tracks.begin(), composition.tracks.end(), trackDiffers);
    };
    const auto carry = [&](Composition& composition) {
        composition.tracks.changeEach(trackDiffers, [&](Track& track) { track.height = heights.at(track.id); });
    };
    changeEachComposition(next, differs, carry);
}

void Document::apply(Project value, bool revises) {
    if (revises) { ++stateRevision; }
    ++contentRevision;
    if (carryView) {
        carryTrackHeights(value, state);
        carryLuaBakes(value, state);
        if (carryOptionsFrom != nullptr) { carryViewOptions(value, state, *carryOptionsFrom); }
    }
    state = std::move(value);
    refreshScope();
    if (onChanged) {
        onChanged();
    }
    sendChangeMessage();
}

Project Document::finished(Project view) const {
    pruneReferences(view);
    view.clampLoop();
    return mergeScope(std::move(view));
}

void Document::record(const juce::String& label, Project view, bool joinPrevious) {
    if (!joinPrevious) {
        undo.beginNewTransaction(label);
        if (onUndoStep) { onUndoStep(); }
    }
    // An edit made while a gesture previews starts from before the preview:
    // what was shown was never an undo step of its own.
    auto from = previewBase.has_value() ? std::move(*previewBase) : state;
    const auto before = onEdited ? project() : Project();
    const auto scope = scopeChanges;
    undo.perform(new Change(*this, std::move(from), finished(std::move(view))));
    if (onEdited && scope == scopeChanges) { onEdited(before); }
}

void Document::preview(Project project) {
    if (!previewBase.has_value()) {
        previewBase = state;
        previewOrigin = state;
    }
    // A gesture builds its previews from where it started: a view option
    // changed since (L for loop mid-drag) stays as it now is.
    auto shown = finished(std::move(project));
    carryViewOptions(shown, state, *previewOrigin);
    apply(std::move(shown));
}

void Document::edit(juce::String label, std::function<void(Project&)> operation) {
    auto after = project();
    operation(after);
    record(label, std::move(after));
}

void Document::amend(std::function<void(Project&)> operation) {
    auto after = project();
    operation(after);
    record(undo.getUndoDescription(), std::move(after), true);
}

void Document::editProject(juce::String label, std::function<void(Project&)> operation) {
    auto after = state;
    operation(after);
    after.clampLoop();
    undo.beginNewTransaction(label);
    if (onUndoStep) { onUndoStep(); }
    auto from = previewBase.has_value() ? std::move(*previewBase) : state;
    undo.perform(new Change(*this, std::move(from), std::move(after)));
}

void Document::editCoalesced(juce::String label, const juce::String& control, std::function<void(Project&)> operation) {
    tryEditCoalesced(std::move(label), control, [&operation](Project& project) {
        operation(project);
        return true;
    });
}

bool Document::tryEditCoalesced(juce::String label, const juce::String& control, std::function<bool(Project&)> operation) {
    const auto now = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    // Joins only while the newest undo step is still this control's: a
    // parameter changed since (it shares the undo manager) has its own.
    const auto holding = held.has_value() && control == *held;
    const bool joins = control.isNotEmpty() && control == coalescingControl && revision() == coalescingRevision && (holding || now - coalescingTime < 1.0) && undo.getUndoDescription() == label;
    auto after = project();
    if (!operation(after)) { return false; }
    record(label, std::move(after), joins);
    coalescingControl = control;
    coalescingRevision = revision();
    coalescingTime = now;
    if (holding) { heldRecorded = true; }
    return true;
}

void Document::beginHeld(const juce::String& control) {
    held = control;
    heldRecorded = false;
    // The drag starts a step of its own, not one a moment ago's.
    coalescingControl.clear();
}

void Document::endHeld() {
    held.reset();
    heldRecorded = false;
}

void Document::cancelHeld() {
    // Only while its step is still the newest.
    if (held.has_value() && heldRecorded && coalescingControl == *held && revision() == coalescingRevision) { undo.undo(); }
    endHeld();
}

bool Document::tryEdit(juce::String label, std::function<bool(Project&)> operation) {
    auto after = project();
    if (!operation(after)) { return false; }
    record(label, std::move(after));
    return true;
}

void Document::changeView(std::function<void(Composition&)> change) {
    auto next = project();
    change(next);
    next.clampLoop();
    // A gesture under way carries on, and an edit it ends in starts from
    // before it with the option changed too.
    if (previewBase.has_value()) {
        if (scopeId == 0) {
            change(*previewBase);
            previewBase->clampLoop();
        } else {
            for (auto& definition : previewBase->definitions) {
                if (definition == nullptr || definition->id != scopeId) { continue; }
                auto copy = std::make_shared<CompositionDefinition>(*definition);
                change(*copy);
                copy->clampLoop();
                definition = std::move(copy);
            }
        }
    }
    const ViewChange view(*this);
    apply(mergeScope(std::move(next)), false);
}

void Document::commit(juce::String label, Project before) {
    undo.beginNewTransaction(label);
    if (onUndoStep) { onUndoStep(); }
    const auto edited = onEdited ? before : Project();
    const auto scope = scopeChanges;
    // Undo puts back the gesture, not a view option changed during it.
    auto from = mergeScope(std::move(before));
    if (previewOrigin.has_value()) { carryViewOptions(from, state, *previewOrigin); }
    undo.perform(new Change(*this, std::move(from), state));
    if (onEdited && scope == scopeChanges) { onEdited(edited); }
}

void Document::reset(Project project) {
    previewBase.reset();
    previewOrigin.reset();
    scopeId = 0;
    ++projectGeneration;
    ++scopeChanges;
    undo.clearUndoHistory();
    lastId = highestProjectIdentity(project, lastId);
    // A different document: nothing carries over from the previous one.
    carryView = false;
    apply(std::move(project));
    carryView = true;
    markSaved();
}

// In musical time, project-time items keep their beat when the tempo map
// changes: markers, cuts, effect ranges and project-level keys (with slopes
// scaled by the local tempo ratio), and the composition's length in bars.
// The Scope's keys belong to the main timeline: they keep their beats only
// when it is the main timeline's tempo that changes (`withBeam`).
static void keepBeats(Project& next, const Tempo& before, const Tempo& after, bool withBeam) {
    const auto remap = [&](double time) { return after.seconds(before.beats(time)); };
    const auto stretch = [&](double time) { return before.bpmAt(time) / after.bpmAt(remap(time)); };
    const auto scaleCurve = [&](Curve& curve) {
        auto keys = curve.keyframes();
        for (const auto& key : keys) { curve.removeKey(key.time); }
        for (const auto& key : keys) {
            const auto ratio = stretch(key.time);
            auto moved = key;
            moved.time = remap(key.time);
            moved.incomingSlope /= ratio;
            moved.outgoingSlope /= ratio;
            // A key the map can't carry (a non-finite time or slope) stays as it is.
            curve.setKey(moved.valid() ? moved : key);
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
    if (withBeam) {
        for (auto& [name, curve] : next.beam.properties) { scaleCurve(curve); }
    }
    for (auto& group : next.groups) {
        for (auto& [name, curve] : group.properties) { scaleCurve(curve); }
        scaleEffects(group.effects);
    }
    next.tracks.changeEach([](const Track& track) { return !track.effects.empty(); }, [&](Track& track) { scaleEffects(track.effects); });
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
    if (!Tempo(current.bpm, shared).valid()) { return juce::Result::fail(juce::String::fromUTF8("A tempo change needs a position after the start and 1\xe2\x80\x93" "1000 BPM.")); }
    return retempo(std::move(shared), replacing.has_value() ? "Edit tempo change" : "Add tempo change");
}

juce::Result Document::removeTempoChange(double beat) {
    const auto& current = project();
    if (current.tempoChanges == nullptr) { return juce::Result::fail("There is no tempo change there."); }
    auto changes = *current.tempoChanges;
    const auto before = changes.size();
    std::erase_if(changes, [beat](const auto& change) { return change.beat == beat; });
    if (changes.size() == before) { return juce::Result::fail("There is no tempo change there."); }
    return retempo(changes.empty() ? nullptr : std::make_shared<const std::vector<TempoChange>>(std::move(changes)), "Delete tempo change");
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
// `main`: the composition retimed is the main timeline, not one inside it.
static juce::Result retimed(const Project& state, double initialBpm, std::shared_ptr<const std::vector<TempoChange>> changes, Project& next, bool main) {
    if (changes != nullptr && changes->empty()) { changes.reset(); }
    next = state;
    next.bpm = initialBpm;
    next.tempoChanges = std::move(changes);
    const auto before = state.tempo();
    const auto after = next.tempo();
    if (!after.valid()) { return juce::Result::fail("Tempo must be between 1 and 1000 BPM, with changes after the start at increasing beats."); }
    // In musical time, everything placed in project time keeps its bar
    // position, like beat-anchored clips. Seconds projects keep seconds.
    if (state.timeDisplay == TimeDisplay::beats) { keepBeats(next, before, after, main); }
    for (std::size_t index = 0; index < next.tracks.size(); ++index) {
        const auto end = next.tracks.change(index).sortClips(after);
        if (!end.has_value()) {
            return juce::Result::fail("That tempo would overlap clips on " + juce::String(next.tracks[index].name) + ". Move them apart or onto separate tracks first.");
        }
        next.duration = std::max(next.duration, *end);
    }
    return juce::Result::ok();
}

juce::Result Document::setTempoMap(double initialBpm, std::shared_ptr<const std::vector<TempoChange>> changes, juce::String label) {
    Project next;
    const auto result = retimed(project(), initialBpm, std::move(changes), next, scopeId == 0);
    if (result.failed()) { return result; }
    edit(label, [next = std::move(next)](Project& project) { project = next; });
    return juce::Result::ok();
}

juce::Result Document::setTempoFromAudio(Id clipId, double bpm, double downbeat, double& moved) {
    moved = 0;
    Project next;
    const auto result = retimed(project(), bpm, nullptr, next, scopeId == 0);
    if (result.failed()) { return result; }
    const auto tempo = next.tempo();
    const auto* track = findClipTrack(next, clipId);
    if (track == nullptr) { return juce::Result::fail("The audio clip no longer exists."); }
    if (track->locked) { return juce::Result::fail("Unlock the audio track first."); }
    auto clip = *findClip(next, clipId);
    // Move the clip later (never cutting audio) until its first downbeat sits
    // on a bar line; a pickup lands in the bar before.
    auto timing = clip.timing(tempo);
    const auto bar = 60 / bpm * std::max(1, next.beatsPerBar);
    const auto shift = std::fmod(timing.projectTime(downbeat), bar);
    if (shift > 0.005 && bar - shift > 0.005) {
        moved = bar - shift;
        timing.moveTo(timing.start + moved);
        if (!clip.setTiming(timing, tempo) || !track->canPlace(clip, clip.id, tempo)) {
            return juce::Result::fail("There is no room to move the audio onto the bar grid.");
        }
        *changeClip(next, clipId) = clip;
        next.duration = std::max(next.duration, timing.end());
    }
    // A tempo taken from the music is for working in bars.
    next.timeDisplay = TimeDisplay::beats;
    edit("Set tempo from audio", [next = std::move(next)](Project& project) { project = next; });
    return juce::Result::ok();
}

// Every identity comes from this document's counter (reset() raises it past
// a loaded project's), so the counter is the highest in use.
Id Document::highestId() const {
    jassert(highestProjectIdentity(state, lastId) == lastId);
    return lastId;
}
}

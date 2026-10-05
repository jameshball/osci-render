#include "Document.h"
#include "CompositionGraph.h"
#include "LuaClipBake.h"
#include "ModulationGraph.h"
#include "PropertySchema.h"
#include "../import/SourceDecoding.h"
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
    // Redo and undo keep the current view options; the first perform sets them.
    bool perform() override {
        const juce::ScopedValueSetter<const Project*> carry(owner.carryOptionsFrom, performed ? &before : nullptr);
        owner.apply(after);
        performed = true;
        return true;
    }
    bool undo() override {
        const juce::ScopedValueSetter<const Project*> carry(owner.carryOptionsFrom, &after);
        owner.apply(before);
        return true;
    }
    bool performed = false;
    // KiB, so the undo manager can bound history by memory as well as count.
    int getSizeInUnits() override {
        auto bytes = compositionBytes(before) + compositionBytes(after) + propertyBytes(before.beam.properties) + propertyBytes(after.beam.properties);
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
        if (adding) {
            project.markers.push_back({id, time, name});
        } else {
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
// A clip keeps its installed slider bake across edits while it plays the same
// source; a replaced source (or a removed clip) leaves the bake behind.
static void carryLuaBakes(Project& next, const Project& current) {
    const auto sourceOf = [](const Project& project, Id asset) -> const Asset* {
        const auto found = std::find_if(project.assets.begin(), project.assets.end(), [asset](const auto& item) { return item->id == asset; });
        return found == project.assets.end() ? nullptr : found->get();
    };
    std::map<Id, std::pair<std::shared_ptr<const LuaClipBake>, const Asset*>> installed;
    const auto collect = [&](const Composition& composition) {
        for (const auto& track : composition.tracks) {
            for (const auto& clip : track.clips) {
                if (clip.luaBake != nullptr) { installed.emplace(clip.id, std::make_pair(clip.luaBake, sourceOf(current, clip.asset))); }
            }
        }
    };
    collect(current);
    for (const auto& definition : current.definitions) { collect(*definition); }
    if (installed.empty()) { return; }
    const auto carried = [&](const Clip& clip) -> std::shared_ptr<const LuaClipBake> {
        const auto found = installed.find(clip.id);
        const auto same = clip.luaBake == nullptr && found != installed.end() && found->second.second == sourceOf(next, clip.asset);
        return same ? found->second.first : nullptr;
    };
    const auto needsCarry = [&](const Composition& composition) {
        return std::any_of(composition.tracks.begin(), composition.tracks.end(), [&](const auto& track) {
            return std::any_of(track.clips.begin(), track.clips.end(), [&](const auto& clip) { return carried(clip) != nullptr; });
        });
    };
    const auto carry = [&](Composition& composition) {
        for (auto& track : composition.tracks) {
            for (auto& clip : track.clips) {
                auto bake = carried(clip);
                if (bake != nullptr) { clip.luaBake = std::move(bake); }
            }
        }
    };
    carry(next);
    for (auto& definition : next.definitions) {
        if (!needsCarry(*definition)) { continue; }
        auto copy = std::make_shared<CompositionDefinition>(*definition);
        carry(*copy);
        definition = std::move(copy);
    }
}

// A loop never reaches past the composition; one that no longer fits is dropped.
static void clampLoop(Project& project) {
    if (!project.hasLoop()) { return; }
    project.loopEnd = std::min(project.loopEnd, project.duration);
    if (!project.hasLoop()) { project.loopStart = project.loopEnd = 0; project.looping = false; }
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
        const auto found = std::find_if(project.definitions.begin(), project.definitions.end(), [id](const auto& item) { return item != nullptr && item->id == id; });
        return found == project.definitions.end() ? nullptr : found->get();
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
        if (carryOptionsFrom != nullptr) { carryViewOptions(value, state, *carryOptionsFrom); }
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

void Document::changeView(std::function<void(Composition&)> change) {
    auto next = project();
    change(next);
    clampLoop(next);
    const ViewChange view(*this);
    apply(mergeScope(std::move(next)));
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
    for (auto& [name, curve] : next.beam.properties) { scaleCurve(curve); }
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
                if (interval.end() <= timing.start) {
                    displacement += interval.duration();
                } else if (interval.start < timing.end() && timing.start < interval.end()) {
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
    clip.properties = defaultProperties(objectPropertySpecs);
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
    const auto* track = findClipTrack(candidate, clipId);
    if (track != nullptr && track->locked) { return juce::Result::fail("Unlock the track before making its composition unique."); }
    auto* target = findClip(candidate, clipId);
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
    instance.properties = defaultProperties(objectPropertySpecs);
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

// A cut at `time` ends the cut it falls in and lasts until the next cut.
static void insertCut(Project& updated, Id id, Id camera, double time) {
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
}

juce::Result Document::cutToCamera(Id camera, double time, Id& cutId) {
    const auto& current = project();
    if (!hasCamera(current, camera)) { return juce::Result::fail("Choose a camera to cut to."); }
    if (!std::isfinite(time) || time < 0 || time >= current.duration) { return juce::Result::fail("Cuts must start inside the project."); }
    cutId = newId();
    const auto id = cutId;
    tryEdit("Cut to camera", [time, camera, id](Project& updated) {
        insertCut(updated, id, camera, time);
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

juce::Result Document::addCamera(double time, Id& cameraId) {
    const auto& current = project();
    if (!std::isfinite(time) || time < 0 || time > current.duration) { return juce::Result::fail("Cameras are added inside the project."); }
    Camera camera;
    camera.id = cameraId = newId();
    // The camera on the output at `time` is the first camera unless a cut shows another.
    const Camera* showing = current.cameras.empty() ? nullptr : &current.cameras.front();
    for (const auto& cut : current.cameraCuts) {
        if (cut.contains(time)) {
            for (const auto& item : current.cameras) {
                if (item.id == cut.camera) { showing = &item; }
            }
        }
    }
    if (showing != nullptr) {
        for (const auto& [name, curve] : showing->properties) { camera.properties[name] = Curve(curve.evaluateBase(time)); }
        camera.target = showing->target;
        camera.parent = showing->parent;
    }
    for (int number = static_cast<int>(current.cameras.size()) + 1;; ++number) {
        camera.name = "Camera " + std::to_string(number);
        const auto taken = std::any_of(current.cameras.begin(), current.cameras.end(), [&camera](const auto& item) { return item.name == camera.name; });
        if (!taken) { break; }
    }
    const auto cutId = current.cameras.empty() || time >= current.duration ? Id(0) : newId();
    tryEdit("Add camera", [camera, cutId, time](Project& updated) {
        updated.cameras.push_back(camera);
        if (cutId != 0) { insertCut(updated, cutId, camera.id, time); }
        return true;
    });
    return juce::Result::ok();
}

juce::Result Document::removeCamera(Id camera) {
    const auto removed = tryEdit("Delete camera", [camera](Project& updated) {
        const auto before = updated.cameras.size();
        std::erase_if(updated.cameras, [camera](const auto& item) { return item.id == camera; });
        std::erase_if(updated.cameraCuts, [camera](const auto& cut) { return cut.camera == camera; });
        return updated.cameras.size() != before;
    });
    return removed ? juce::Result::ok() : juce::Result::fail("The camera no longer exists.");
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
    if (beamOutsideMain(route.target)) { return juce::Result::fail("The Scope is modulated from the main composition."); }
    id = route.id;
    edit("Route modulator", [route](Project& project) { project.routes.push_back(route); });
    return juce::Result::ok();
}

// A quarter of a Scope property's range; otherwise as defaultRouteAmount.
double Document::routeAmount(const Project& project, Id target, const std::string& property) {
    const auto found = findPropertyTarget(project, target);
    const auto* spec = found.has_value() && found->beam ? findPropertySpec(beamPropertySpecs, property) : nullptr;
    return spec != nullptr ? 0.25 * (spec->maximum - spec->minimum) : defaultRouteAmount(property);
}

double Document::defaultRouteAmount(const std::string& property) {
    // A quarter unit keeps a moved or scaled object on the canvas; rotations
    // swing 45 degrees; colours move halfway.
    if (property.starts_with("rotation.")) { return 45.0; }
    if (property == "red" || property == "green" || property == "blue") { return 0.5; }
    return 0.25;
}

juce::Result Document::routeModulator(Id modulator, Id target, const std::vector<std::string>& properties) {
    const auto& current = project();
    const auto hasModulator = std::any_of(current.modulators.begin(), current.modulators.end(), [&](const auto& item) { return item.id == modulator; });
    if (!hasModulator) { return juce::Result::fail("The modulator no longer exists."); }
    if (beamOutsideMain(target)) { return juce::Result::fail("The Scope is modulated from the main composition."); }
    std::vector<ModulationRoute> added;
    for (const auto& property : properties) {
        const auto routed = std::any_of(current.routes.begin(), current.routes.end(), [&](const auto& route) { return route.modulator == modulator && route.target == target && route.property == property; });
        if (routed || !drivableProperty(current, target, property)) { continue; }
        added.push_back({newId(), modulator, target, property, routeAmount(current, target, property), ModulationMode::add});
    }
    if (added.empty()) { return juce::Result::fail("It already drives that, or that cannot be modulated."); }
    edit("Route modulator", [added](Project& project) { project.routes.insert(project.routes.end(), added.begin(), added.end()); });
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
    if (beamOutsideMain(route.target)) { return juce::Result::fail("The Scope is modulated from the main composition."); }
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
        // The Scope follows the main composition; nothing follows the Scope.
        if (link->source == current.beam.id) { return juce::Result::fail("Scope properties cannot be linked to."); }
        if (beamOutsideMain(target)) { return juce::Result::fail("The Scope is linked from the main composition."); }
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
    const auto found = findAsset(state.assets, id);
    if (found == nullptr || !found->extension.equalsIgnoreCase(".blender")) { return juce::Result::fail("The Blender source no longer exists."); }
    if (!settings.valid() || name.trim().isEmpty()) { return juce::Result::fail("Enter a source name and a port from 51600 to 51699."); }
    if (found->name == name.trim() && found->blenderSettings == settings) { return juce::Result::ok(); }
    auto replacement = std::make_shared<Asset>(*found);
    replacement->name = name.trim(); replacement->blenderSettings = settings;
    if (found->blenderSettings.port != settings.port) { replacement->liveIdentity = std::make_shared<const LiveSourceIdentity>(); }
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
        clip.properties = defaultProperties(audioPropertySpecs);
        return clip;
    }
    if (asset.source != nullptr && (asset.source->frameCount() > 1 || asset.extension.equalsIgnoreCase(".lua") || asset.extension.equalsIgnoreCase(".blender-capture") || osci::files::isVideo(asset.extension))) {
        clip.duration = asset.source->duration();
    }
    clip.properties = defaultProperties(objectPropertySpecs);
    // A source without its own colours starts in the phosphor green.
    if (asset.source == nullptr || !asset.source->hasExplicitColour()) {
        clip.properties["red"] = Curve(0.2);
        clip.properties["blue"] = Curve(0.35);
    }
    return clip;
}

}

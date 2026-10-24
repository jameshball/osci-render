// The Document's edits of clips, track heights and compositions, and of
// clips' timings.

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

static const char* const limitsMessage = "A clip ends within 24 hours, its offset is within 24 hours, and it plays at most 1000 times as fast.";

juce::Result Document::duplicateClip(Id sourceId, Id& duplicateId) {
    duplicateId = 0;
    std::vector<Id> copies;
    const auto result = duplicateClips({sourceId}, copies);
    if (result.wasOk()) { duplicateId = copies.front(); }
    return result;
}

bool Document::setTrackHeight(Id trackId, int height) {
    return setTrackHeights({{trackId, height}});
}

bool Document::setTrackHeights(const std::vector<std::pair<Id, int>>& heights) {
    std::map<Id, int> wanted;
    for (const auto& [id, height] : heights) { wanted[id] = Track::rowHeight(height); }
    std::set<Id> found;
    forEachComposition(state, [&](const Composition& composition) {
        for (const auto& track : composition.tracks) { if (wanted.contains(track.id)) { found.insert(track.id); } }
        for (const auto& group : composition.groups) { if (wanted.contains(group.id)) { found.insert(group.id); } }
    });
    // Tracks and groups both have rows.
    const auto rowDiffers = [&](const auto& row) {
        const auto height = wanted.find(row.id);
        return height != wanted.end() && row.height != height->second;
    };
    const auto trackDiffers = [&](const Track& track) { return rowDiffers(track); };
    const auto differs = [&](const Composition& composition) {
        return std::any_of(composition.tracks.begin(), composition.tracks.end(), trackDiffers)
            || std::any_of(composition.groups.begin(), composition.groups.end(), [&](const Group& group) { return rowDiffers(group); });
    };
    // View state: no undo step, no revision bump and no change broadcast (it
    // affects neither playback nor any other view); the timeline relayouts
    // itself and the next save writes it.
    bool changed = false;
    auto next = state;
    changeEachComposition(next, differs, [&](Composition& composition) {
        changed = true;
        composition.tracks.changeEach(trackDiffers, [&](Track& track) { track.height = wanted.at(track.id); });
        for (auto& group : composition.groups) {
            if (rowDiffers(group)) { group.height = wanted.at(group.id); }
        }
    });
    if (changed) {
        state = std::move(next);
        refreshScope();
    }
    return found.size() == wanted.size();
}

juce::Result Document::pasteClips(const std::vector<CopiedClip>& clips, double time, std::vector<Id>& pastedIds) {
    pastedIds.clear();
    const auto& state = project();
    if (clips.empty()) { return juce::Result::fail("The clipboard has no clips."); }
    if (!std::isfinite(time) || time < 0) { return juce::Result::fail("Paste at a valid position."); }
    double first = std::numeric_limits<double>::infinity();
    for (const auto& copied : clips) {
        if (!copied.clip.valid()) { return juce::Result::fail("A copied clip is no longer valid."); }
        if (copied.clip.asset != 0 && findAsset(state.assets, copied.clip.asset) == nullptr) {
            return juce::Result::fail("A copied clip's source is not in this composition.");
        }
        if (copied.clip.composition != 0 && !canReferenceComposition(copied.clip.composition)) {
            return juce::Result::fail("A copied composition can't be placed here.");
        }
        first = std::min(first, copied.clip.timing(state.tempo()).start);
    }
    auto highest = highestId();
    auto candidate = state;
    std::map<Id, Id> overflowTracks, owners;
    for (const auto& copied : clips) {
        auto clip = copied.clip;
        if (static_cast<Id>(clip.effects.size() + state.routes.size()) + 2 > maximumId - highest) { return noIdentitiesLeft(); }
        auto timing = clip.timing(state.tempo());
        timing.moveTo(timing.start - first + time);
        if (!clip.setTiming(timing, state.tempo())) { return juce::Result::fail("The pasted selection has invalid timing."); }
        renumber(clip, highest, owners);
        renumber(clip.effects, highest, owners);
        const auto original = std::find_if(candidate.tracks.begin(), candidate.tracks.end(), [&](const auto& track) { return track.id == copied.track; });
        const bool fits = original != candidate.tracks.end() && !original->locked && original->kind == copied.kind && original->canPlace(clip, 0, state.tempo());
        if (fits) {
            candidate.tracks.change(original).insert(clip, state.tempo());
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
            candidate.tracks.change(overflow).insert(clip, state.tempo());
        }
        candidate.duration = std::max(candidate.duration, clip.timing(state.tempo()).end());
        pastedIds.push_back(clip.id);
    }
    // Pasting while the originals exist copies their routes too.
    cloneDrivers(candidate, owners, [&highest] { return ++highest; });
    // Instances pasted may take the project past what compositions may hold.
    const auto graph = validateCompositionGraph(mergeScope(candidate));
    if (!graph) {
        pastedIds.clear();
        return juce::Result::fail(graph.error);
    }
    lastId = highest;
    edit(clips.size() > 1 ? "Paste clips" : "Paste clip", [&candidate](Project& project) { project = candidate; });
    return juce::Result::ok();
}

juce::Result Document::pasteKeys(Id clipId, const std::vector<CopiedKey>& keys, double time) {
    if (keys.empty()) { return juce::Result::fail("The clipboard has no keys."); }
    const auto target = findPropertyTarget(project(), clipId);
    if (!target.has_value() || target->isEffect) { return juce::Result::fail("Select a clip, group or camera to paste keys onto."); }
    if (target->locked) { return juce::Result::fail("Unlock the track before pasting keys."); }
    const auto changed = tryEdit(keys.size() > 1 ? "Paste keys" : "Paste key", [&](Project& updated) {
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
        if (required > maximumId - highest) { return noIdentitiesLeft(); }
        auto timing = copy.timing(state.tempo());
        timing.moveTo(timing.start + (last - first));
        if (copies.size() == 1) {
            copy.start = copy.end();
        } else if (!copy.setTiming(timing, state.tempo())) {
            return juce::Result::fail("The duplicated selection has invalid timing.");
        }
        renumber(copy, highest, owners);
        renumber(copy.effects, highest, owners);
        if (!candidate.tracks.change(index).insert(copy, state.tempo())) {
            return juce::Result::fail("There is not enough free space after the selection. Move the following clips first.");
        }
        candidate.duration = std::max(candidate.duration, copy.timing(state.tempo()).end());
        ids.push_back(copy.id);
    }
    // A duplicate keeps its routed modulators, like its own keys.
    cloneDrivers(candidate, owners, [&highest] { return ++highest; });
    // Instances duplicated may take the project past what compositions may hold.
    const auto graph = validateCompositionGraph(mergeScope(candidate));
    if (!graph) { return juce::Result::fail(graph.error); }
    lastId = highest;
    edit(copies.size() == 1 ? "Duplicate clip" : "Duplicate clips", [candidate = std::move(candidate)](Project& project) { project = candidate; });
    duplicateIds = std::move(ids);
    return juce::Result::ok();
}

juce::Result Document::extractParts(const std::vector<PartSplit>& splits, std::vector<Id>& partClips) {
    partClips.clear();
    if (splits.empty()) { return juce::Result::fail("Pick some parts first."); }
    const auto& state = project();
    auto candidate = state;
    auto highest = highestId();
    std::map<Id, Id> owners;
    // Sources every one of whose clips is split leave the library first, so
    // the rest can take their names.
    std::map<Id, std::size_t> splitUses;
    for (const auto& split : splits) {
        const auto* clip = findClip(state, split.clip);
        if (clip == nullptr) { return juce::Result::fail("The clip no longer exists."); }
        ++splitUses[clip->asset];
    }
    std::map<Id, std::shared_ptr<const Asset>> originals;
    for (const auto& [asset, uses] : splitUses) {
        originals[asset] = findAsset(state.assets, asset);
        if (assetUses(asset) == uses) { std::erase_if(candidate.assets, [asset = asset](const auto& item) { return item != nullptr && item->id == asset; }); }
    }
    std::vector<Id> created;
    for (const auto& split : splits) {
        const auto& [clipId, part, rest, name] = split;
        if (part == nullptr || rest == nullptr || part->source == nullptr || rest->source == nullptr) { return juce::Result::fail("Couldn't prepare the parts."); }
        const auto partName = name.trim();
        if (partName.isEmpty() || partName.length() > 200 || partName.containsAnyOf("\r\n")) { return juce::Result::fail(juce::String::fromUTF8("Name the part with 1\xe2\x80\x93" "200 characters on one line.")); }
        const auto trackAt = std::find_if(candidate.tracks.begin(), candidate.tracks.end(), [clipId = clipId](const auto& track) { return holdsClip(track, clipId); });
        if (trackAt == candidate.tracks.end()) { return juce::Result::fail("The clip no longer exists."); }
        if (trackAt->locked) { return juce::Result::fail("Unlock the track before extracting parts."); }
        const auto clip = *std::find_if(trackAt->clips.begin(), trackAt->clips.end(), [clipId = clipId](const auto& item) { return item.id == clipId; });
        const auto original = originals[clip.asset];
        if (original == nullptr) { return juce::Result::fail("The clip's source no longer exists."); }
        const auto required = static_cast<Id>(clip.effects.size() + trackAt->effects.size() + state.routes.size()) + 4;
        if (required > maximumId - highest) { return noIdentitiesLeft(); }
        const auto stem = original->name.containsChar('.') ? original->name.upToLastOccurrenceOf(".", false, false) : original->name;
        const auto partStem = partName.endsWithIgnoreCase(part->extension) ? partName.dropLastCharacters(part->extension.length()) : partName;
        // The document keeps its own copies of the halves, named and numbered here.
        const auto add = [&](const Asset& half, const juce::String& named) {
            auto made = std::make_shared<Asset>(half);
            made->id = ++highest;
            made->name = uniqueAssetName(candidate, named + made->extension);
            candidate.assets.push_back(made);
            return made;
        };
        const auto madePart = add(*part, partStem), madeRest = add(*rest, stem);
        auto copy = clip;
        renumber(copy, highest, owners);
        renumber(copy.effects, highest, owners);
        copy.asset = madePart->id;
        copy.name = madePart->name.toStdString();
        // The clip keeps its name, as its source does.
        changeClip(candidate, clipId)->asset = madeRest->id;
        Track track;
        track.id = ++highest;
        track.kind = trackAt->kind;
        track.name = copy.name;
        track.group = trackAt->group;
        track.muted = trackAt->muted;
        track.solo = trackAt->solo;
        track.label = trackAt->label;
        track.effects = trackAt->effects;
        renumber(track.effects, highest, owners);
        if (!track.insert(copy, state.tempo())) { return juce::Result::fail("The clip's timing is not valid."); }
        const auto position = trackAt - candidate.tracks.begin();
        candidate.tracks.insert(candidate.tracks.begin() + position, std::move(track));
        created.push_back(copy.id);
    }
    cloneDrivers(candidate, owners, [&highest] { return ++highest; });
    lastId = highest;
    partClips = std::move(created);
    edit("Extract parts", [candidate = std::move(candidate)](Project& project) { project = candidate; });
    return juce::Result::ok();
}

juce::Result Document::removeClips(const std::vector<Id>& clipIds, bool ripple) {
    const std::set<Id> requested(clipIds.begin(), clipIds.end());
    if (requested.empty() || requested.contains(0) || requested.size() != clipIds.size()) {
        return juce::Result::fail("Select distinct clips to delete.");
    }
    auto candidate = project();
    std::size_t found = 0;
    for (std::size_t index = 0; index < candidate.tracks.size(); ++index) {
        std::vector<ClipTiming> removed;
        for (const auto& clip : candidate.tracks[index].clips) {
            if (!requested.contains(clip.id)) { continue; }
            if (candidate.tracks[index].locked) { return juce::Result::fail("Unlock selected tracks before deleting clips."); }
            const auto timing = clip.timing(candidate.tempo());
            if (!timing.valid()) { return juce::Result::fail("A selected clip has invalid timing."); }
            removed.push_back(timing);
            ++found;
        }
        if (removed.empty()) { continue; }
        auto& track = candidate.tracks.change(index);
        if (ripple && !track.sortClips(candidate.tempo()).has_value()) {
            return juce::Result::fail("Ripple delete can't close up clips that overlap.");
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
                    return juce::Result::fail("Ripple delete can't close an overlapping clip interval.");
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
                    if (first < previousEnd && previousEnd - first <= timeTolerance(first, previousEnd, displacement)) {
                        auto aligned = clip.timing(candidate.tempo());
                        aligned.moveTo(previousEnd);
                        if (!clip.setTiming(aligned, candidate.tempo())) { return juce::Result::fail("Ripple delete produced invalid clip timing."); }
                        clip.nudgeStartPast(previousEnd, candidate.tempo());
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
    return definition == 0 ? 0 : countClips(state, [definition](const auto& clip) { return clip.composition == definition; });
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
        const auto found = findDefinition(state, id);
        if (found == nullptr) { return false; }
        for (const auto& track : found->tracks) {
            for (const auto& clip : track.clips) {
                if (clip.composition != 0 && !self(self, clip.composition)) { return false; }
            }
        }
        return true;
    };
    return definition != 0 && visit(visit, definition);
}

Clip Document::makeCompositionClip(Id id, const CompositionDefinition& definition, double time) {
    Clip clip;
    clip.id = id;
    clip.composition = definition.id;
    clip.name = definition.name.toStdString();
    double first = definition.duration, last = 0;
    for (const auto& track : definition.tracks) {
        for (const auto& source : track.clips) {
            const auto timing = source.timing(definition.tempo());
            if (!timing.valid() || timing.start >= definition.duration) { continue; }
            first = std::min(first, timing.start);
            last = std::max(last, std::min(timing.end(), definition.duration));
        }
    }
    if (last <= first) { first = 0; last = definition.duration; }
    clip.start = time;
    clip.offset = first;
    clip.duration = last - first;
    clip.properties = defaultProperties(objectPropertySpecs);
    return clip;
}

juce::Result Document::insertComposition(Id definition, double time, Id trackId, Id groupId, Id& clipId) {
    clipId = 0;
    if (!std::isfinite(time) || time < 0 || !canReferenceComposition(definition)) {
        return juce::Result::fail("That composition contains the one you're editing, so it can't go inside it.");
    }
    auto candidate = project();
    const auto source = findDefinition(candidate, definition);
    if (source == nullptr) { return juce::Result::fail("The composition no longer exists."); }
    auto highest = highestId();
    const auto required = trackId == 0 ? 2u : 1u;
    if (required > maximumId - highest) { return noIdentitiesLeft(); }
    auto clip = makeCompositionClip(++highest, *source, time);
    if (!clip.valid() || !clip.timing(candidate.tempo()).valid()) { return juce::Result::fail("The composition has invalid timing."); }
    if (trackId != 0) {
        const auto track = std::find_if(candidate.tracks.begin(), candidate.tracks.end(), [trackId](const auto& value) { return value.id == trackId; });
        if (track == candidate.tracks.end() || track->locked || track->kind != TrackKind::visual || !candidate.tracks.change(track).insert(clip, candidate.tempo())) {
            return juce::Result::fail("Use an unlocked visual track with room for the composition.");
        }
    } else {
        if (groupId != 0 && findGroup(candidate, groupId) == nullptr) { return juce::Result::fail("The destination group no longer exists."); }
        Track track;
        track.id = ++highest;
        track.name = clip.name;
        track.group = groupId;
        track.clips = {clip};
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
    auto* target = changeClip(candidate, clipId);
    if (target == nullptr || target->composition == 0) { return juce::Result::fail("Select a composition instance."); }
    const auto found = findDefinition(candidate, target->composition);
    if (found == nullptr) { return juce::Result::fail("The referenced composition no longer exists."); }
    auto copy = std::make_shared<CompositionDefinition>(*found);
    std::size_t required = 1 + copy->groups.size() + copy->tracks.size() + copy->cameras.size() + copy->cameraCuts.size() + copy->effects.size() + copy->markers.size()
        + copy->modulators.size() + copy->routes.size();
    for (const auto& group : copy->groups) { required += group.effects.size(); }
    for (const auto& track : copy->tracks) {
        required += track.effects.size() + track.clips.size();
        for (const auto& clip : track.clips) { required += clip.effects.size(); }
    }
    auto highest = highestId();
    if (required > maximumId - highest) { return noIdentitiesLeft(); }
    // Every renumbered property owner, so routes and links can follow.
    std::map<Id, Id> owners;
    copy->id = ++highest;
    copy->name += " copy";
    std::map<Id, Id> groups, cameras;
    for (auto& group : copy->groups) {
        const auto old = group.id;
        renumber(group, highest, owners);
        groups.emplace(old, group.id);
        renumber(group.effects, highest, owners);
    }
    for (auto& group : copy->groups) {
        if (group.parent != 0) {
            if (!groups.contains(group.parent)) { return juce::Result::fail("Invalid composition group hierarchy."); }
            group.parent = groups.at(group.parent);
        }
    }
    for (const auto& track : copy->tracks) {
        if (track.group != 0 && !groups.contains(track.group)) { return juce::Result::fail("Invalid composition group reference."); }
    }
    copy->tracks.changeAll([&](Track& track) {
        track.id = ++highest;
        renumber(track.effects, highest, owners);
        if (track.group != 0) { track.group = groups.at(track.group); }
        for (auto& clip : track.clips) {
            renumber(clip, highest, owners);
            renumber(clip.effects, highest, owners);
        }
    });
    for (auto& camera : copy->cameras) {
        const auto old = camera.id;
        renumber(camera, highest, owners);
        cameras.emplace(old, camera.id);
    }
    for (auto& cut : copy->cameraCuts) {
        if (!cameras.contains(cut.camera)) { return juce::Result::fail("Invalid composition camera reference."); }
        cut.id = ++highest;
        cut.camera = cameras.at(cut.camera);
    }
    for (auto& marker : copy->markers) { marker.id = ++highest; }
    renumber(copy->effects, highest, owners);
    const auto remap = [&](Id id) { const auto found = owners.find(id); return found != owners.end() ? found->second : Id(0); };
    std::map<Id, Id> modulators;
    for (auto& modulator : copy->modulators) {
        const auto old = modulator.id;
        modulator.id = ++highest;
        modulators.emplace(old, modulator.id);
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

juce::Result Document::groupTracks(const std::vector<Id>& ids, Id& made) {
    if (ids.empty()) { return juce::Result::fail("Pick tracks to group."); }
    const auto& current = project();
    // The new group sits where its members did when they shared a parent.
    std::optional<Id> shared;
    bool same = true;
    for (const auto id : ids) {
        const auto* group = findGroup(current, id);
        const auto track = std::find_if(current.tracks.begin(), current.tracks.end(), [id](const auto& item) { return item.id == id; });
        if (group == nullptr && track == current.tracks.end()) { return juce::Result::fail("A track to group no longer exists."); }
        const auto parent = group != nullptr ? group->parent : track->group;
        if (shared.has_value() && *shared != parent) { same = false; }
        shared = parent;
    }
    Group group;
    group.id = newId();
    group.name = "Group " + std::to_string(current.groups.size() + 1);
    group.parent = same && shared.has_value() ? *shared : 0;
    const auto grouped = tryEdit(ids.size() > 1 ? "Group tracks" : "Group track", [&ids, &group](Project& updated) {
        updated.groups.push_back(group);
        for (const auto id : ids) {
            auto* member = findGroup(updated, id);
            if (member != nullptr) { member->parent = group.id; }
            auto* track = updated.tracks.changeById(id);
            if (track != nullptr) { track->group = group.id; }
        }
        return validGroupHierarchy(updated);
    });
    if (!grouped) { return juce::Result::fail("Those tracks can't share a group."); }
    made = group.id;
    return juce::Result::ok();
}

juce::Result Document::ungroup(const std::vector<Id>& groups, juce::String& lost) {
    const auto& current = project();
    std::vector<Id> dissolved;
    for (const auto id : groups) {
        if (findGroup(current, id) != nullptr) { dissolved.push_back(id); }
    }
    if (dissolved.empty()) { return juce::Result::fail("Select a group to ungroup."); }
    // Whether a group's own transform or look was doing anything.
    bool styled = false;
    for (const auto id : dissolved) {
        const auto* group = findGroup(current, id);
        for (const auto& spec : objectPropertySpecs) {
            const auto curve = curveOrDefault(group->properties, spec);
            if (curve.animated() || curve.link.has_value() || std::abs(curve.base - spec.defaultValue) > 1.0e-9) { styled = true; }
        }
        if (std::any_of(current.routes.begin(), current.routes.end(), [id](const auto& route) { return route.target == id; })) { styled = true; }
    }
    lost = styled ? (dissolved.size() > 1 ? "The groups' own transforms and looks no longer apply." : "The group's own transform and look no longer apply.") : juce::String();
    const auto ungrouped = tryEdit(dissolved.size() > 1 ? "Ungroup " + juce::String(static_cast<int>(dissolved.size())) + " groups" : juce::String("Ungroup"), [&](Project& updated) {
        for (const auto id : dissolved) {
            const auto* found = findGroup(updated, id);
            if (found == nullptr) { continue; }
            const auto group = *found;
            // Its tracks and groups, up a level, playing as they did.
            const auto copyEffects = [&](std::vector<EffectInstance>& effects) {
                std::map<Id, Id> owners;
                for (auto effect : group.effects) {
                    const auto clone = newId();
                    owners.emplace(effect.id, clone);
                    effect.id = clone;
                    effects.push_back(std::move(effect));
                }
                cloneDrivers(updated, owners, [this] { return newId(); });
            };
            for (std::size_t index = 0; index < updated.tracks.size(); ++index) {
                if (updated.tracks[index].group != id) { continue; }
                auto* track = updated.tracks.changeById(updated.tracks[index].id);
                track->group = group.parent;
                track->muted = track->muted || group.muted;
                track->solo = track->solo || group.solo;
                copyEffects(track->effects);
            }
            for (auto& child : updated.groups) {
                if (child.parent != id || child.id == id) { continue; }
                child.parent = group.parent;
                child.muted = child.muted || group.muted;
                child.solo = child.solo || group.solo;
                copyEffects(child.effects);
            }
            for (auto& camera : updated.cameras) {
                if (camera.parent == id) { camera.parent = group.parent; }
                if (camera.target == id) { camera.target = group.parent; }
            }
            std::erase_if(updated.groups, [id](const auto& item) { return item.id == id; });
        }
        return validGroupHierarchy(updated);
    });
    if (!ungrouped) { return juce::Result::fail("Those groups can't be ungrouped."); }
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
    const auto graph = validateCompositionGraph(mergeScope(state));
    if (!graph || !validGroupHierarchy(state)) { return juce::Result::fail("The existing composition structure is invalid."); }
    auto definition = std::make_shared<CompositionDefinition>();
    // Keep the owning time coordinate: track/group curves and modulation phase
    // must not restart when only part of a group is moved into a composition.
    // The new instance trims this source to the selected interval.
    definition->name = name;
    definition->duration = state.duration;
    definition->bpm = state.bpm;
    definition->tempoChanges = state.tempoChanges;
    definition->frameRate = state.frameRate;
    definition->timeDisplay = state.timeDisplay;
    definition->beatsPerBar = state.beatsPerBar;
    definition->snapBeats = state.snapBeats;
    definition->gridSnap = state.gridSnap;
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
            first = std::min(first, timing.start);
            last = std::max(last, timing.end());
            ++count;
        }
        // Capture effective visibility across the new scope boundary. Solo is
        // scoped independently inside the definition after this operation.
        copy.muted = !trackIsAudible(state, track);
        copy.solo = false;
        // Ancestors already collected end the walk (their own were too).
        for (auto group = track.group; group != 0 && requiredGroups.insert(group).second;) {
            const auto* found = findGroup(state, group);
            group = found != nullptr ? found->parent : 0;
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
    if (required > maximumId - highest) { return noIdentitiesLeft(); }
    definition->id = ++highest;
    // copies: every root owner copied (not moved) into the definition.
    std::map<Id, Id> groupIds, copies;
    for (const auto& group : state.groups) {
        if (!requiredGroups.contains(group.id)) { continue; }
        auto copy = group;
        copy.solo = false;
        renumber(copy, highest, copies);
        groupIds.emplace(group.id, copy.id);
        renumber(copy.effects, highest, copies);
        definition->groups.push_back(std::move(copy));
    }
    for (auto& group : definition->groups) { if (group.parent != 0) { group.parent = groupIds.at(group.parent); } }
    definition->tracks.changeAll([&](Track& track) {
        track.id = ++highest;
        if (track.group != 0) { track.group = groupIds.at(track.group); }
        renumber(track.effects, highest, copies);
    });
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
    instance.id = ++highest;
    instance.composition = definition->id;
    instance.name = name.toStdString();
    instance.start = first;
    instance.duration = last - first;
    instance.offset = first;
    instance.properties = defaultProperties(objectPropertySpecs);
    Track replacement;
    replacement.id = ++highest;
    replacement.name = name.toStdString();
    replacement.clips = {instance};
    replacement.solo = std::any_of(state.tracks.begin(), state.tracks.end(), [](const auto& track) { return track.solo; })
        || std::any_of(state.groups.begin(), state.groups.end(), [](const auto& group) { return group.solo; });
    const auto isSelected = [&](const Clip& clip) { return selected.contains(clip.id); };
    std::set<Id> emptiedTracks;
    candidate.tracks.changeEach([&](const Track& track) { return std::any_of(track.clips.begin(), track.clips.end(), isSelected); }, [&](Track& track) {
        std::erase_if(track.clips, isSelected);
        if (track.clips.empty()) { emptiedTracks.insert(track.id); }
    });
    candidate.tracks.eraseIf([&](const Track& track) { return emptiedTracks.contains(track.id); });
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
    const auto expanded = validateCompositionGraph(mergeScope(candidate));
    if (!expanded) { return juce::Result::fail(expanded.error); }
    // Publish identities and one undo snapshot only after every check succeeds.
    lastId = highest;
    edit("Create composition", [candidate = std::move(candidate)](Project& project) { project = candidate; });
    instanceId = instance.id;
    return juce::Result::ok();
}

juce::Result Document::setClipTimings(const std::vector<std::pair<Id, ClipTiming>>& timings) {
    if (timings.size() == 1) { return setClipTiming(timings.front().first, timings.front().second); }
    return applyClipTimings(timings, "Change clip timing", {});
}

juce::Result Document::nudgeClips(const std::vector<Id>& clips, double seconds) {
    const auto& state = project();
    const auto tempo = state.tempo();
    std::vector<std::pair<Id, ClipTiming>> timings;
    for (const auto& track : state.tracks) {
        for (const auto& clip : track.clips) {
            if (std::find(clips.begin(), clips.end(), clip.id) == clips.end()) { continue; }
            auto timing = clip.timing(tempo);
            if (timing.start + seconds < -1.0e-9) { return juce::Result::fail("Clips can't move before the start."); }
            timing.moveTo(std::max(0.0, timing.start + seconds));
            timings.emplace_back(clip.id, timing);
        }
    }
    if (timings.empty()) { return juce::Result::ok(); }
    return applyClipTimings(timings, timings.size() > 1 ? "Nudge clips" : "Nudge clip", "timeline.nudge.clips");
}

juce::Result Document::applyClipTimings(const std::vector<std::pair<Id, ClipTiming>>& timings, const juce::String& label, const juce::String& control) {
    const auto& state = project();
    const auto tempo = state.tempo();
    auto tracks = state.tracks;
    bool changed = false;
    std::set<std::size_t> touched;
    for (const auto& [clipId, timing] : timings) {
        if (!timing.valid()) { return juce::Result::fail("Enter a start of 0 or more, and a duration and speed above 0."); }
        if (!timing.withinLimits()) { return juce::Result::fail(limitsMessage); }
        const auto track = std::find_if(tracks.begin(), tracks.end(), [clipId](const Track& item) { return holdsClip(item, clipId); });
        if (track == tracks.end()) { return juce::Result::fail("A selected clip no longer exists."); }
        if (track->locked) { return juce::Result::fail("Unlock the track before changing clip timing."); }
        const auto clip = std::find_if(track->clips.begin(), track->clips.end(), [clipId](const Clip& item) { return item.id == clipId; });
        auto next = *clip;
        if (!next.setTiming(timing, tempo)) { return juce::Result::fail("The requested timing is invalid."); }
        changed = changed || !next.sameTiming(*clip);
        const auto position = clip - track->clips.begin();
        touched.insert(static_cast<std::size_t>(track - tracks.begin()));
        tracks.change(track).clips[static_cast<std::size_t>(position)] = std::move(next);
    }
    for (const auto& track : tracks) {
        for (const auto& clip : track.clips) {
            if (!track.canPlace(clip, clip.id, tempo)) { return juce::Result::fail("The requested timing overlaps another clip on the same track."); }
        }
    }
    if (!changed) { return juce::Result::ok(); }
    auto apply = [tracks = std::move(tracks), touched = std::move(touched)](Project& project) mutable {
        const auto tempo = project.tempo();
        for (const auto index : touched) {
            auto& track = tracks.change(index);
            for (const auto& clip : track.clips) { project.duration = std::max(project.duration, clip.timing(tempo).end()); }
            track.sortClips(tempo);
        }
        project.tracks = std::move(tracks);
    };
    if (control.isEmpty()) {
        edit(label, std::move(apply));
    } else {
        editCoalesced(label, control, std::move(apply));
    }
    return juce::Result::ok();
}

juce::Result Document::setClipTiming(Id clipId, ClipTiming resolvedSeconds) {
    const auto& state = project();
    if (!resolvedSeconds.valid()) { return juce::Result::fail("Enter a start of 0 or more, and a duration and speed above 0."); }
    if (!resolvedSeconds.withinLimits()) { return juce::Result::fail(limitsMessage); }
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
            if (changed.sameTiming(original)) {
                return juce::Result::ok();
            }
            edit("Change clip timing", [trackIndex, clipIndex, changed = std::move(changed)](Project& project) {
                auto& track = project.tracks.change(trackIndex);
                project.duration = std::max(project.duration, changed.timing(project.tempo()).end());
                track.clips[clipIndex] = changed;
                track.sortClips(project.tempo());
            });
            return juce::Result::ok();
        }
    }
    return juce::Result::fail("The selected clip no longer exists.");
}
}

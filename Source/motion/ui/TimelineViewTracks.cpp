#include "TimelineView.h"
#include "MenuIcons.h"
#include "../model/LightPresets.h"

// Menus for clips, groups and tracks, and what they do: grouping, locking,
// muting and soloing, and dragging tracks and groups into place.

void MotionTimelineView::showClipMenu(motion::Id id) {
    juce::PopupMenu menu;
    const auto many = selectedClips.size() > 1;
    motion::Id asset = 0, definition = 0;
    bool locked = false;
    for (const auto& track : processor.document.project().tracks) {
        for (const auto& clip : track.clips) { if (clip.id == id) { asset = clip.asset; definition = clip.composition; locked = track.locked; } }
    }
    // The editor's own commands, so the menu and shortcuts agree.
    const std::array<std::pair<const char*, const char*>, 4> edits {{{"Cut", "Cmd+X"}, {"Copy", "Cmd+C"}, {"Paste", "Cmd+V"}, {"Split at playhead", "Cmd+K"}}};
    for (int index = 0; index < static_cast<int>(edits.size()); ++index) {
        const auto pasting = index == 2;
        const auto enabled = (index == 1 || !locked) && (!pasting || (canPaste && canPaste()));
        menu.addItem(motion::style::menuItem(edits[static_cast<std::size_t>(index)].first, 20 + index, edits[static_cast<std::size_t>(index)].second).setEnabled(enabled));
    }
    menu.addItem(motion::style::menuItem(many ? "Duplicate clips" : "Duplicate clip", 1, "Cmd+D").setEnabled(!locked));
    menu.addSeparator();
    menu.addItem(2, "Edit clip timing");
    menu.addItem(motion::style::menuItem(many ? "Loop selected clips" : "Loop this clip", 9, "Shift+L"));
    if (asset != 0) { menu.addItem(10, "Show in Sources"); }
    menu.addSeparator();
    menu.addItem(motion::style::menuItem(many ? "Delete clips" : "Delete clip", 7, "Delete").setEnabled(!locked));
    menu.addItem(motion::style::menuItem(many ? "Ripple delete clips" : "Ripple delete clip", 8, "Shift+Delete").setEnabled(!locked));
    const auto references = processor.document.assetUses(asset);
    if (definition == 0) { menu.addItem(3, "Make this clip's source unique", !locked && asset != 0 && references > 1); }
    // A light's presets: ways it moves on its own.
    const auto* generator = motion::generatorFor(processor.document.project(), id);
    if (generator != nullptr && generator->category == motion::GeneratorCategory::light) {
        const auto hasPreset = motion::hasLightPreset(processor.document.project(), id);
        juce::PopupMenu presets;
        for (std::size_t index = 0; index < motion::lightPresets.size(); ++index) {
            const auto& definition = motion::lightPresetDefinition(motion::lightPresets[index]);
            presets.addItem(40 + static_cast<int>(index), juce::String(definition.name.data(), definition.name.size()), !locked);
        }
        presets.addSeparator();
        presets.addItem(39, "No preset", !locked && hasPreset);
        menu.addSeparator();
        menu.addSubMenu("Light preset", presets);
    }
    menu.addSeparator();
    menu.addItem(motion::style::menuItem("Make composition", 4, "Cmd+Shift+C").setEnabled(!locked && !selectedClips.empty()));
    if (definition != 0) { menu.addItem(5, "Open composition"); menu.addItem(6, "Make composition unique", !locked); }
    motion::ui::showDocumentMenu(menu, *this, processor.document, juce::PopupMenu::Options().withTargetComponent(this).withMousePosition(), [this, id](int result) {
        if (result >= 39 && result < 40 + static_cast<int>(motion::lightPresets.size())) {
            applyLightPreset(id, result == 39 ? std::nullopt : std::optional(motion::lightPresets[static_cast<std::size_t>(result - 40)]));
        } else if (result >= 20 && result < 24 && onCommand) {
            const std::array<const char*, 4> names {"Cut", "Copy", "Paste", "Split at playhead"};
            onCommand(names[static_cast<std::size_t>(result - 20)]);
        } else if (result == 1) {
            duplicateClip(id);
        } else if (result == 2 && onTimingRequested) {
            onTimingRequested(id);
        } else if (result == 3 && onMakeUnique) {
            onMakeUnique(id);
        } else if (result == 4) {
            createCompositionFromSelection();
        } else if (result == 5 && onEnterComposition) {
            onEnterComposition(id);
        } else if (result == 7 || result == 8) {
            deleteSelectedClips(result == 8);
        } else if (result == 9 && onLoopSelection) {
            onLoopSelection();
        } else if (result == 10 && onRevealSource) {
            for (const auto& track : processor.document.project().tracks) {
                for (const auto& clip : track.clips) { if (clip.id == id) { onRevealSource(clip.asset); } }
            }
        } else if (result == 6) {
            motion::Id definitionId = 0;
            const auto copied = processor.document.makeCompositionUnique(id, definitionId);
            if (copied.failed() && onError) { onError(copied.getErrorMessage()); }
            refreshTracks();
        }
    });
}

void MotionTimelineView::applyLightPreset(motion::Id id, std::optional<motion::LightPreset> preset) {
    motion::editLightPreset(processor.document, id, preset);
    refreshTracks();
}

void MotionTimelineView::createGroup(motion::Id trackId, motion::Id parent) {
    motion::Group group;
    group.id = processor.document.newId();
    group.name = "Group " + std::to_string(processor.document.project().groups.size() + 1);
    group.parent = parent;
    auto candidate = processor.document.project();
    candidate.groups.push_back(group);
    if (!motion::validGroupHierarchy(candidate)) { return; }
    processor.document.edit("Create group", [group, trackId](motion::Project& project) {
        project.groups.push_back(group);
        auto* track = project.tracks.changeById(trackId);
        if (track != nullptr) { track->group = group.id; }
    });
    refreshTracks();
    selectClip(group.id);
}

void MotionTimelineView::showGroupMenu(motion::Id id) {
    const auto* group = motion::findGroup(processor.document.project(), id);
    if (group == nullptr) { return; }
    juce::PopupMenu menu;
    menu.addItem(1, "Edit group");
    menu.addItem(2, "Add track to group");
    menu.addItem(3, "Add nested group");
    menu.addSeparator();
    menu.addItem(4, "Delete group and its tracks");
    motion::ui::showDocumentMenu(menu, *this, processor.document, juce::PopupMenu::Options().withTargetComponent(this).withMousePosition(), [this, id](int result) {
        if (motion::findGroup(processor.document.project(), id) == nullptr) { return; }
        cancelGesture();
        if (result == 1) { selectClip(id); return; }
        if (result == 3) { createGroup(0, id); return; }
        if (result == 2) {
            motion::Track track;
            track.id = processor.document.newId();
            track.group = id;
            track.name = "New track";
            processor.document.edit("Add track", [track](motion::Project& project) { project.tracks.push_back(track); });
        } else if (result == 4) {
            processor.document.edit("Delete group", [id](motion::Project& project) {
                std::set<motion::Id> removed { id };
                for (std::size_t depth = 0; depth < motion::maximumGroupDepth; ++depth) {
                    for (const auto& group : project.groups) { if (removed.contains(group.parent)) { removed.insert(group.id); } }
                }
                project.tracks.eraseIf([&](const auto& track) { return removed.contains(track.group); });
                std::erase_if(project.groups, [&](const auto& group) { return removed.contains(group.id); });
            });
        }
        refreshTracks();
    });
}

void MotionTimelineView::showTrackMenu(motion::Id id) {
    // Right-clicking one of several picked headers acts on them all.
    if (selectedTracks.size() > 1 && selectedTracks.contains(id)) {
        juce::PopupMenu menu;
        const auto count = juce::String(static_cast<int>(selectedTracks.size()));
        menu.addItem(motion::style::menuItem("Group " + count + " tracks", 1, "Cmd+G"));
        menu.addSeparator();
        menu.addItem(motion::style::menuItem("Delete " + count + " tracks", 2, "Delete"));
        motion::ui::showDocumentMenu(menu, *this, processor.document, juce::PopupMenu::Options().withTargetComponent(this).withMousePosition(), [this](int result) {
            if (result == 1) { groupTracks({selectedTracks.begin(), selectedTracks.end()}); }
            if (result == 2) { deleteTracks(pickedTracks()); }
        });
        return;
    }
    if (motion::findGroup(processor.document.project(), id) != nullptr) { showGroupMenu(id); return; }
    const auto& tracks = processor.document.project().tracks;
    const auto found = std::find_if(tracks.begin(), tracks.end(), [id](const auto& track) { return track.id == id; });
    if (found == tracks.end()) { return; }
    juce::PopupMenu menu;
    menu.addItem(1, "Move track up", std::any_of(tracks.begin(), found, [&](const auto& track) { return track.group == found->group; }));
    menu.addItem(2, "Move track down", std::any_of(found + 1, tracks.end(), [&](const auto& track) { return track.group == found->group; }));
    menu.addItem(motion::style::menuItem("Group track", 4, "Cmd+G"));
    std::vector<motion::Id> groups { 0 };
    juce::PopupMenu destinations;
    destinations.addItem(100, "Root", true, found->group == 0);
    for (const auto& group : processor.document.project().groups) {
        groups.push_back(group.id);
        destinations.addItem(100 + static_cast<int>(groups.size()) - 1, juce::String(group.name), true, found->group == group.id);
    }
    menu.addSubMenu("Move to group", destinations);
    juce::PopupMenu labels;
    for (std::size_t index = 0; index < motion::style::trackLabels().size(); ++index) {
        const auto& label = motion::style::trackLabels()[index];
        juce::PopupMenu::Item item(label.name);
        item.itemID = 300 + static_cast<int>(index);
        item.isTicked = found->label == static_cast<int>(index);
        // Brighter than a clip's muted fill, so the colours tell apart this small.
        item.image = motion::ui::menu_icons::swatch(label.argb == 0 ? std::nullopt : std::optional<juce::Colour>(juce::Colour(label.argb).brighter(.4f)));
        labels.addItem(std::move(item));
    }
    menu.addSubMenu("Label colour", labels);
    menu.addSeparator();
    menu.addItem(motion::style::menuItem("Delete track", 3, "Delete"));
    motion::ui::showDocumentMenu(menu, *this, processor.document, juce::PopupMenu::Options().withTargetComponent(this).withMousePosition(), [this, id, groups](int result) {
        cancelGesture();
        const auto& current = processor.document.project().tracks;
        const auto track = std::find_if(current.begin(), current.end(), [id](const auto& item) { return item.id == id; });
        if (track == current.end()) { return; }
        const auto index = static_cast<int>(track - current.begin());
        if (result == 4) {
            selectedTracks.clear();
            groupTracks({id});
            return;
        }
        if (result >= 300 && result < 300 + static_cast<int>(motion::style::trackLabels().size())) {
            const auto label = result - 300;
            processor.document.tryEdit("Change label colour", [id, label](motion::Project& project) {
                auto* track = project.tracks.changeById(id);
                if (track == nullptr || track->label == label) { return false; }
                track->label = label;
                return true;
            });
            return;
        }
        if (result >= 100 && result - 100 < static_cast<int>(groups.size())) {
            placeTrack(id, index, groups[result - 100]);
        } else if (result == 3) {
            deleteTracks({id});
        } else {
            const auto direction = result == 1 ? -1 : 1;
            for (int candidate = index + direction; candidate >= 0 && candidate < static_cast<int>(current.size()); candidate += direction) {
                if (current[candidate].group == track->group) {
                    placeTrack(id, candidate + (direction > 0 ? 1 : 0), track->group);
                    break;
                }
            }
        }
    });
}

void MotionTimelineView::toggleLock(motion::Id id) {
    cancelGesture();
    const auto* current = processor.document.project().tracks.findById(id);
    processor.document.edit(current != nullptr && current->locked ? "Unlock track" : "Lock track", [id](motion::Project& project) {
        auto* track = project.tracks.changeById(id);
        if (track != nullptr) { track->locked = !track->locked; }
    });
}

void MotionTimelineView::toggleTrack(motion::Id id, bool solo) {
    cancelGesture();
    // Named for what it does: "Mute track", "Unsolo group".
    const auto& project = processor.document.project();
    const auto* group = motion::findGroup(project, id);
    const auto* track = project.tracks.findById(id);
    const auto on = group != nullptr ? (solo ? group->solo : group->muted) : track != nullptr && (solo ? track->solo : track->muted);
    const juce::String what = group != nullptr ? "group" : "track";
    processor.document.edit((on ? (solo ? "Unsolo " : "Unmute ") : (solo ? "Solo " : "Mute ")) + what, [id, solo](motion::Project& project) {
        auto* group = motion::findGroup(project, id);
        if (group != nullptr) {
            if (solo) { group->solo = !group->solo; } else { group->muted = !group->muted; }
        }
        auto* track = project.tracks.changeById(id);
        if (track != nullptr) {
            if (solo) { track->solo = !track->solo; } else { track->muted = !track->muted; }
        }
    });
}

void MotionTimelineView::placeTrack(motion::Id id, int boundary, motion::Id group) {
    cancelGesture();
    const auto& project = processor.document.project();
    if (group != 0 && motion::findGroup(project, group) == nullptr) { return; }
    const auto& tracks = project.tracks;
    const auto found = std::find_if(tracks.begin(), tracks.end(), [id](const auto& track) { return track.id == id; });
    if (found == tracks.end()) { return; }
    const auto source = static_cast<int>(found - tracks.begin());
    auto destination = std::clamp(boundary, 0, static_cast<int>(tracks.size()));
    if (destination > source) { --destination; }
    if (source == destination && found->group == group) { return; }
    processor.document.edit("Reorder track", [source, destination, group](motion::Project& project) {
        auto track = std::move(project.tracks[source]);
        track.group = group;
        project.tracks.erase(project.tracks.begin() + source);
        project.tracks.insert(project.tracks.begin() + destination, std::move(track));
    });
    collapsedGroups.erase(group);
    refreshTracks();
}

// Moves a group, and every track it holds as one block, before the track at
// `boundary`, into `parent`.
void MotionTimelineView::placeGroup(motion::Id id, int boundary, motion::Id parent) {
    cancelGesture();
    const auto& project = processor.document.project();
    const auto* group = motion::findGroup(project, id);
    if (group == nullptr || (parent != 0 && motion::findGroup(project, parent) == nullptr) || motion::groupHolds(project, id, parent)) { return; }
    const auto& tracks = project.tracks;
    std::vector<motion::Id> order, members;
    const auto destination = std::clamp(boundary, 0, static_cast<int>(tracks.size()));
    for (int index = 0; index <= static_cast<int>(tracks.size()); ++index) {
        if (index == destination) { order.push_back(0); }
        if (index == static_cast<int>(tracks.size())) { break; }
        const auto& track = tracks[static_cast<std::size_t>(index)];
        (motion::groupHolds(project, id, track.group) ? members : order).push_back(track.id);
    }
    const auto place = std::find(order.begin(), order.end(), motion::Id(0));
    order.insert(order.erase(place), members.begin(), members.end());
    auto unchanged = parent == group->parent;
    for (std::size_t index = 0; unchanged && index < order.size(); ++index) { unchanged = tracks[index].id == order[index]; }
    if (unchanged) { return; }
    processor.document.tryEdit("Move group", [&](motion::Project& project) {
        for (std::size_t position = 0; position < order.size(); ++position) {
            for (auto index = position; index < project.tracks.size(); ++index) {
                if (project.tracks[index].id == order[position]) {
                    if (index != position) { project.tracks.move(index, position); }
                    break;
                }
            }
        }
        motion::findGroup(project, id)->parent = parent;
        return motion::validGroupHierarchy(project);
    });
    refreshTracks();
}

bool MotionTimelineView::inLiftedBlock(int row) const {
    const auto* trackDrag = active<TrackDrag>();
    return trackDrag != nullptr && row >= trackDrag->first && row < trackDrag->last;
}

void MotionTimelineView::validateTrackDrag() {
    const auto* trackDrag = active<TrackDrag>();
    if (trackDrag == nullptr) { return; }
    const auto stale = processor.document.revision() != trackDrag->revision || trackDrag->last > static_cast<int>(rows.size())
        || rows[static_cast<std::size_t>(trackDrag->first)].id != trackDrag->id;
    if (stale) {
        gesture = {};
        rowShift.clear();
    }
}

void MotionTimelineView::dragTrack(motion::Id id, int y, bool finished) {
    ensureTrackRows();
    if (active<TrackDrag>() == nullptr) {
        if (finished) { return; }
        const auto found = std::find_if(rows.begin(), rows.end(), [id](const auto& row) { return row.id == id && !row.isLane(); });
        if (found == rows.end()) { return; }
        cancelGesture();
        const auto first = static_cast<int>(found - rows.begin());
        // A track lifts with its lanes; a group with everything under it.
        auto last = first + 1;
        while (last < static_cast<int>(rows.size()) && (found->group() ? rows[static_cast<std::size_t>(last)].depth > found->depth : rows[static_cast<std::size_t>(last)].isLane())) { ++last; }
        gesture = TrackDrag {id, first, last, rowY(last) - rowY(first), y - rowY(first), y, processor.document.revision(), first};
        rowShift.assign(rows.size(), 0.0f);
        rowAnimation.startTimerHz(60);
    }
    auto* trackDrag = active<TrackDrag>();
    // Another edit while dragging ends the drag where it began.
    if (processor.document.revision() != trackDrag->revision) {
        gesture = {};
        return;
    }
    trackDrag->pointer = y;
    trackDrag->boundary = dropBoundary(trackDrag->group);
    if (!finished) { stepRows(); return; }
    const auto drag = *trackDrag;
    const auto top = liftedTop();
    gesture = {};
    // Let go where it was, it stays put: nothing to place.
    const auto inPlace = drag.group == 0 && drag.boundary >= drag.first && drag.boundary <= drag.last;
    if (!inPlace) {
        const auto [boundary, group] = dropTarget(drag);
        if (motion::findGroup(processor.document.project(), drag.id) != nullptr) {
            placeGroup(drag.id, boundary, group);
        } else {
            placeTrack(drag.id, boundary, group);
        }
    }
    // The block glides from where it was let go into its new place.
    ensureTrackRows();
    rowShift.assign(rows.size(), 0.0f);
    const auto landed = std::find_if(rows.begin(), rows.end(), [&](const auto& row) { return row.id == drag.id && !row.isLane(); });
    if (landed != rows.end()) {
        const auto index = static_cast<int>(landed - rows.begin());
        const auto& head = *landed;
        const auto inBlock = [&](int row) { return row == index || (head.group() ? rows[static_cast<std::size_t>(row)].depth > head.depth : rows[static_cast<std::size_t>(row)].isLane()); };
        for (auto row = index; row < static_cast<int>(rows.size()) && inBlock(row); ++row) {
            rowShift[static_cast<std::size_t>(row)] = static_cast<float>(top - rowY(index));
        }
    }
    rowAnimation.startTimerHz(60);
}

int MotionTimelineView::dropBoundary(motion::Id& group) const {
    const auto* trackDrag = active<TrackDrag>();
    group = 0;
    const auto count = static_cast<int>(rows.size());
    const auto centre = liftedTop() + trackDrag->height / 2;
    for (int start = 0; start < count;) {
        if (start == trackDrag->first) {
            start = trackDrag->last;
            continue;
        }
        const auto& row = rows[static_cast<std::size_t>(start)];
        auto end = start + 1;
        if (!row.group()) {
            while (end < count && rows[static_cast<std::size_t>(end)].isLane()) { ++end; }
        }
        // Tracks go below the composition's effects, never above them.
        if (row.composition()) {
            start = end;
            continue;
        }
        const auto top = rowY(start) - (start >= trackDrag->last ? trackDrag->height : 0);
        const auto height = rowY(end) - rowY(start);
        if (row.group() && centre >= top + height / 4 && centre < top + height * 3 / 4) {
            group = row.id;
            auto subtree = start + 1;
            while (subtree < count && rows[static_cast<std::size_t>(subtree)].depth > row.depth) { ++subtree; }
            return subtree;
        }
        if (centre < top + height / 2) { return start; }
        start = end;
    }
    return count;
}

std::pair<int, motion::Id> MotionTimelineView::dropTarget(const TrackDrag& drag) const {
    const auto& project = processor.document.project();
    const auto& tracks = project.tracks;
    const auto count = static_cast<int>(rows.size());
    // (Let go where it was, nothing is placed: see dragTrack.)
    if (drag.group != 0) { return {static_cast<int>(tracks.size()), drag.group}; }
    if (drag.boundary >= count) { return {static_cast<int>(tracks.size()), 0}; }
    const auto& row = rows[static_cast<std::size_t>(drag.boundary)];
    if (!row.group()) { return {row.track, tracks[static_cast<std::size_t>(row.track)].group}; }
    // Before a group: its parent, ahead of the first track under it.
    const auto* folder = motion::findGroup(project, row.id);
    auto index = static_cast<int>(tracks.size());
    for (auto next = drag.boundary + 1; next < count; ++next) {
        const auto& below = rows[static_cast<std::size_t>(next)];
        if (!below.group() && !below.isLane() && (next < drag.first || next >= drag.last)) { index = below.track; break; }
    }
    return {index, folder != nullptr ? folder->parent : 0};
}

float MotionTimelineView::targetShift(int row) const {
    const auto* trackDrag = active<TrackDrag>();
    if (trackDrag == nullptr) { return 0.0f; }
    auto boundary = trackDrag->boundary;
    if (boundary > trackDrag->first && boundary <= trackDrag->last) { boundary = trackDrag->first; }
    return static_cast<float>((row >= trackDrag->last ? -trackDrag->height : 0) + (row >= boundary ? trackDrag->height : 0));
}

void MotionTimelineView::stepRows() {
    ensureTrackRows();
    validateTrackDrag();
    auto* trackDrag = active<TrackDrag>();
    if (trackDrag != nullptr) { trackDrag->boundary = dropBoundary(trackDrag->group); }
    if (rowShift.size() != rows.size()) { rowShift.assign(rows.size(), 0.0f); }
    bool moving = false;
    for (std::size_t row = 0; row < rowShift.size(); ++row) {
        const auto target = targetShift(static_cast<int>(row));
        auto& shift = rowShift[row];
        shift += (target - shift) * .3f;
        if (std::abs(target - shift) < .5f) { shift = target; } else { moving = true; }
    }
    // Near the top or bottom edge the list scrolls to reach further rows.
    if (trackDrag != nullptr) {
        const auto edge = trackDrag->pointer < rulerHeight + 16 ? -8 : trackDrag->pointer > rulerHeight + viewHeight() - 16 ? 8 : 0;
        if (edge != 0) {
            const auto before = scrollY;
            scrollY = std::clamp(scrollY + edge, 0, maximumScrollY());
            if (scrollY != before) { trackDrag->boundary = dropBoundary(trackDrag->group); }
        }
    }
    if (trackDrag == nullptr && !moving) {
        rowShift.clear();
        rowAnimation.stopTimer();
    }
    resized();
    repaint();
}

motion::Id MotionTimelineView::effectOwnerAt(juce::Point<int> position) const {
    if (position.y < rulerHeight) { return 0; }
    int row = 0;
    const auto* clip = clipAt(position, row);
    const auto& tracks = processor.document.project().tracks;
    if (position.x < namesWidth) { row = trackAtY(position.y); }
    const auto group = groupAtY(position.y);
    if (group != 0) { return group; }
    if (row < 0 || row >= static_cast<int>(tracks.size()) || tracks[static_cast<std::size_t>(row)].kind != motion::TrackKind::visual || (clip == nullptr && position.x >= namesWidth)) { return 0; }
    const auto owner = clip != nullptr ? clip->id : tracks[static_cast<std::size_t>(row)].id;
    const auto* effects = motion::findEffectOwner(processor.document.project(), owner);
    return effects != nullptr && effects->size() < motion::maximumEffectsPerOwner ? owner : 0;
}

void MotionTimelineView::insertEffect(const std::string& type, juce::Point<int> position) {
    const auto* definition = motion::effectDefinition(type);
    const auto owner = effectOwnerAt(position);
    if (definition == nullptr || owner == 0) { return; }
    int row = 0;
    const auto* clip = position.x >= namesWidth ? clipAt(position, row) : nullptr;
    if (clip != nullptr) { selectClip(clip->id); }
    const auto effect = motion::makeEffect(processor.document.newId(), *definition);
    processor.document.edit("Add " + juce::String(definition->name), [owner, effect](motion::Project& project) {
        auto* destination = motion::findEffectOwner(project, owner);
        if (destination != nullptr) { destination->push_back(effect); }
    });
    if (onEffectAdded) { onEffectAdded(owner, effect.id); }
}

void MotionTimelineView::showSpaceMenu() {
    juce::PopupMenu menu;
    menu.addItem(motion::style::menuItem("Paste", 1, "Cmd+V").setEnabled(canPaste && canPaste()));
    menu.addItem(2, "Add track");
    menu.addItem(3, "Add camera here");
    const auto time = std::clamp(scrollTime + (getMouseXYRelative().x - namesWidth) / pixelsPerSecond, 0.0, processor.document.project().duration);
    motion::ui::showDocumentMenu(menu, *this, processor.document, juce::PopupMenu::Options().withTargetComponent(this).withMousePosition(), [this, time](int result) {
        cancelGesture();
        if (result == 1 && onCommand) { onCommand("Paste"); }
        if (result == 2) { addTrack.triggerClick(); }
        if (result == 3 && onAddCamera) {
            onAddCamera(time);
        }
    });
}

void MotionTimelineView::fitProject() {
    cancelGesture();
    const auto& project = processor.document.project();
    auto end = project.duration;
    for (const auto& track : project.tracks) {
        for (const auto& clip : track.clips) {
            end = std::max(end, clip.timing(project.tempo()).end());
        }
    }
    const auto duration = std::isfinite(end) ? std::max(0.001, end) : 1.0;
    scrollY = 0;
    animateView(std::clamp(std::max(1, getWidth() - namesWidth - 20) / duration, 0.000001, 500.0), 0.0);
    resized();
    repaint();
}

void MotionTimelineView::createCompositionFromSelection() {
    if (selectedClips.empty()) {
        if (onError) { onError("Select clips to make a composition of them."); }
        return;
    }
    motion::Id instance = 0;
    auto& document = processor.document;
    const auto name = "Composition " + juce::String(document.mainProject().definitions.size() + 1);
    const auto created = document.createComposition({selectedClips.begin(), selectedClips.end()}, name, instance);
    if (created.failed()) { if (onError) { onError(created.getErrorMessage()); } return; }
    selectClip(instance);
    refreshTracks();
}

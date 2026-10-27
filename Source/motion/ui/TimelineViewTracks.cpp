#include "TimelineView.h"
#include "MenuIcons.h"

// Menus for groups and tracks, and what they do: grouping, locking, muting
// and soloing, and dragging tracks and groups into place.

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
    menu.addItem(motion::style::menuItem("Ungroup", 5, "Cmd+Shift+G"));
    menu.addItem(4, "Delete group and its tracks");
    motion::ui::showDocumentMenu(menu, *this, processor.document, juce::PopupMenu::Options().withTargetComponent(this).withMousePosition(), [this, id](int result) {
        if (motion::findGroup(processor.document.project(), id) == nullptr) { return; }
        cancelClipGesture();
        if (result == 1) { selectClip(id); return; }
        if (result == 3) { createGroup(0, id); return; }
        if (result == 2) {
            motion::Track track;
            track.id = processor.document.newId();
            track.group = id;
            track.name = "New track";
            processor.document.edit("Add track", [track](motion::Project& project) { project.tracks.push_back(track); });
        } else if (result == 4) {
            deleteTracks({id});
            return;
        } else if (result == 5) {
            juce::String lost;
            if (!report(processor.document.ungroup({id}, lost))) { return; }
            collapsedGroups.erase(id);
            if (lost.isNotEmpty() && onNotice) { onNotice(lost + " Undo to keep them."); }
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
        cancelClipGesture();
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
    cancelClipGesture();
    const auto* current = processor.document.project().tracks.findById(id);
    processor.document.edit(current != nullptr && current->locked ? "Unlock track" : "Lock track", [id](motion::Project& project) {
        auto* track = project.tracks.changeById(id);
        if (track != nullptr) { track->locked = !track->locked; }
    });
}

void MotionTimelineView::toggleTrack(motion::Id id, bool solo) {
    cancelClipGesture();
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
    cancelClipGesture();
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
    cancelClipGesture();
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
        const auto first = headerRow(id);
        if (first < 0) { return; }
        cancelClipGesture();
        const auto& head = rows[static_cast<std::size_t>(first)];
        // A track lifts with its lanes; a group with everything under it.
        auto last = first + 1;
        while (last < static_cast<int>(rows.size()) && (head.group() ? rows[static_cast<std::size_t>(last)].depth > head.depth : rows[static_cast<std::size_t>(last)].isLane())) { ++last; }
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
        const auto [boundary, group] = trackDropPlace(drag);
        if (motion::findGroup(processor.document.project(), drag.id) != nullptr) {
            placeGroup(drag.id, boundary, group);
        } else {
            placeTrack(drag.id, boundary, group);
        }
    }
    // The block glides from where it was let go into its new place.
    ensureTrackRows();
    rowShift.assign(rows.size(), 0.0f);
    const auto index = headerRow(drag.id);
    if (index >= 0) {
        const auto& head = rows[static_cast<std::size_t>(index)];
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

std::pair<int, motion::Id> MotionTimelineView::trackDropPlace(const TrackDrag& drag) const {
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

void MotionTimelineView::showSpaceMenu() {
    juce::PopupMenu menu;
    menu.addItem(motion::style::menuItem("Paste", 1, "Cmd+V").setEnabled(canPaste && canPaste()));
    menu.addItem(2, "Add track");
    menu.addItem(3, "Add camera here");
    const auto time = std::clamp(timeAt(getMouseXYRelative().x), 0.0, processor.document.project().duration);
    motion::ui::showDocumentMenu(menu, *this, processor.document, juce::PopupMenu::Options().withTargetComponent(this).withMousePosition(), [this, time](int result) {
        cancelClipGesture();
        if (result == 1 && onCommand) { onCommand("Paste"); }
        if (result == 2) { addTrack.triggerClick(); }
        if (result == 3 && onAddCamera) {
            onAddCamera(time);
        }
    });
}

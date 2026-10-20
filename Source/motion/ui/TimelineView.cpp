#include "TimelineView.h"
#include "TimeRuler.h"

MotionTimelineView::MotionTimelineView(MotionProcessor& ownerProcessor) : processor(ownerProcessor) {
    setName("Composition timeline");
    addTrack.onClick = [this] {
        motion::Track track;
        track.id = processor.document.newId();
        track.name = "Track " + std::to_string(processor.document.project().tracks.size() + 1);
        processor.document.edit("Add track", [track](motion::Project& project) { project.tracks.push_back(track); });
    };
    snapButton.setName("Snapping");
    snapButton.setTitle("Snapping");
    snapButton.setTooltip("Snapping: hold Alt while dragging to bypass it");
    snapButton.onClick = [this] {
        processor.document.changeView([](motion::Composition& state) { state.gridSnap = !state.gridSnap; });
    };
    snapButton.setClickingTogglesState(false);
    addAndMakeVisible(snapButton);
    // The editing tools, as in Premiere's tool strip.
    for (auto [button, value, tip] : {std::tuple {&selectTool, Tool::move, "Select, move and trim (V)"}, std::tuple {&slipTool, Tool::slip, "Slip: move the content inside a clip (S)"},
                                      std::tuple {&stretchTool, Tool::stretch, "Stretch: change a clip's length and speed together (R)"}, std::tuple {&rippleTool, Tool::ripple, "Ripple trim: trimming moves the clips after it (B)"}}) {
        button->setTooltip(tip);
        button->iconSize = 16.0f;
        button->onClick = [this, value] {
            setTool(value);
        };
        addAndMakeVisible(button);
    }
    snapButton.iconSize = 16.0f;
    headerArea.setInterceptsMouseClicks(false, true);
    addAndMakeVisible(headerArea);
    // Add track sits under the last track and scrolls with the list. Its
    // plus lines up with the grips above, its text with the track names.
    addTrack.leading = 18;
    addTrack.gap = 3;
    headerArea.addAndMakeVisible(addTrack);
    setWantsKeyboardFocus(true);
    // Shortcuts are listed in Help > Keyboard shortcuts rather than a hover wall.
}

juce::Rectangle<int> MotionTimelineView::rulerAnchor(double seconds) const {
    const auto x = std::clamp(timeX(seconds), namesWidth + 4, std::max(namesWidth + 4, getWidth() - 4));
    const auto top = showsMarkerBand() ? markerBandTop() : 0;
    return {x - 1, top, 2, showsMarkerBand() ? markerBandHeight : toolsHeight};
}

void MotionTimelineView::setSelection(motion::Id id) {
    ensureTrackRows();
    selectedMarker = 0;
    // A camera key or cut stays selected only with its camera.
    if (selectedCameraKey.has_value() && selectedCameraKey->first != id) { selectedCameraKey.reset(); }
    const auto* cut = selectedCut != 0 ? findCut(selectedCut) : nullptr;
    if (cut == nullptr || cut->camera != id) { selectedCut = 0; }
    selected = id;
    if (!notifyingSelection) {
        // Picked from elsewhere (the Scene, the library): keys selected here
        // would otherwise go on being what Delete or Cut acts on.
        selectedKeys.clear();
        selectedClips.clear();
        if (isClip(id)) { selectedClips.insert(id); }
    }
    // Picking anything else ends a selection of track headers.
    if (!selectedTracks.contains(id)) { selectedTracks.clear(); }
    repaint();
}

void MotionTimelineView::pickTrack(motion::Id id, juce::ModifierKeys mods) {
    auto picked = selectedTracks;
    if (picked.empty() && selected != 0 && std::any_of(rows.begin(), rows.end(), [this](const auto& row) { return !row.isLane() && row.id == selected; })) { picked.insert(selected); }
    if (mods.isCommandDown()) {
        if (picked.contains(id) && picked.size() > 1) { picked.erase(id); } else { picked.insert(id); }
    } else if (mods.isShiftDown() && trackAnchor != 0) {
        // Every header between the first picked and this one, as listed.
        picked.clear();
        bool inside = false;
        for (const auto& row : rows) {
            if (row.isLane()) { continue; }
            const auto end = row.id == id || row.id == trackAnchor;
            if (end || inside) { picked.insert(row.id); }
            if (end && id != trackAnchor) { inside = !inside; }
        }
    } else {
        picked = {id};
    }
    if (!mods.isShiftDown()) { trackAnchor = id; }
    selectedTracks = picked;
    selectClip(picked.contains(id) ? id : *picked.begin());
    selectedTracks = picked.size() > 1 ? picked : std::set<motion::Id>();
    repaint();
}

std::vector<motion::Id> MotionTimelineView::tracksToGroup() const {
    if (selectedTracks.size() > 1) { return {selectedTracks.begin(), selectedTracks.end()}; }
    // Otherwise the tracks of the selected clips, or the selected track.
    std::vector<motion::Id> result;
    for (const auto& track : processor.document.project().tracks) {
        const auto holds = std::any_of(track.clips.begin(), track.clips.end(), [this](const auto& clip) { return selectedClips.contains(clip.id); });
        if (holds || track.id == selected) { result.push_back(track.id); }
    }
    if (result.empty() && motion::findGroup(processor.document.project(), selected) != nullptr) { result.push_back(selected); }
    return result;
}

std::vector<motion::Id> MotionTimelineView::pickedTracks() const {
    std::vector<motion::Id> result;
    for (const auto& track : processor.document.project().tracks) {
        if (selectedTracks.size() > 1 ? selectedTracks.contains(track.id) : track.id == selected) { result.push_back(track.id); }
    }
    return result;
}

void MotionTimelineView::deleteTracks(const std::vector<motion::Id>& ids) {
    if (ids.empty()) { return; }
    const std::set<motion::Id> doomed(ids.begin(), ids.end());
    processor.document.edit(ids.size() == 1 ? juce::String("Delete track") : "Delete " + juce::String(static_cast<int>(ids.size())) + " tracks", [&doomed](motion::Project& project) {
        project.tracks.eraseIf([&doomed](const auto& item) { return doomed.contains(item.id); });
    });
    selectedTracks.clear();
    refreshTracks();
}

void MotionTimelineView::groupSelectedTracks() { groupTracks(tracksToGroup()); }

void MotionTimelineView::groupTracks(const std::vector<motion::Id>& ids) {
    if (ids.empty()) {
        if (onError) { onError("Select tracks to group: " + motion::style::shortcutText("Cmd-click") + " their names."); }
        return;
    }
    motion::Id made = 0;
    const auto grouped = processor.document.groupTracks(ids, made);
    if (grouped.failed()) {
        if (onError) { onError(grouped.getErrorMessage()); }
        return;
    }
    selectedTracks.clear();
    refreshTracks();
    selectClip(made);
}

MotionTimelineView::ViewState MotionTimelineView::viewState() const {
    ensureTrackRows();
    return {pixelsPerSecond, scrollTime, scrollY, selected, selectedClips, collapsedGroups, expandedTracks};
}

void MotionTimelineView::restoreView(const ViewState& state) {
    ensureTrackRows();
    pixelsPerSecond = std::isfinite(state.zoom) ? std::clamp(state.zoom, 0.000001, 500.0) : 70.0;
    scrollTime = std::isfinite(state.scroll) ? std::max(0.0, state.scroll) : 0.0;
    scrollY = state.scrollY;
    selectedMarker = 0;
    selected = state.primary;
    selectedClips = state.selected;
    collapsedGroups = state.collapsed;
    expandedTracks = state.expanded;
    std::erase_if(collapsedGroups, [this](auto id) { return motion::findGroup(processor.document.project(), id) == nullptr; });
    if (!isClip(selected) && motion::findGroup(processor.document.project(), selected) == nullptr) { selected = 0; }
    layoutRevision.reset();
    refreshTracks();
}

int MotionTimelineView::rowEdgeAt(juce::Point<int> point) const {
    ensureTrackRows();
    if (point.x >= namesWidth - 5 || point.y < rulerHeight) { return -1; }
    for (int row = firstVisibleRow(); row < static_cast<int>(rows.size()); ++row) {
        const auto bottom = rowY(row) + heightAt(row);
        if (bottom > getHeight() + resizeStrip) { break; }
        // Tracks and groups resize; lanes keep their height.
        if (!rows[static_cast<std::size_t>(row)].isLane() && point.y >= bottom - resizeStrip - 1 && point.y <= bottom + 1) { return row; }
    }
    return -1;
}

int MotionTimelineView::storedHeight(motion::Id id) const {
    const auto& project = processor.document.project();
    const auto* group = motion::findGroup(project, id);
    if (group != nullptr) { return group->height; }
    const auto track = std::find_if(project.tracks.begin(), project.tracks.end(), [id](const auto& item) { return item.id == id; });
    return track != project.tracks.end() ? track->height : 0;
}

void MotionTimelineView::revealSelection() {
    // Do not move rows under the pointer during a clip gesture.
    if (clipGesture()) { return; }
    ensureTrackRows();
    const auto& project = processor.document.project();
    motion::Id trackId = 0, parent = 0;
    for (const auto& track : project.tracks) {
        if (std::any_of(track.clips.begin(), track.clips.end(), [this](const auto& clip) { return clip.id == selected; })) {
            trackId = track.id;
            parent = track.group;
            break;
        }
    }
    const auto* selectedGroup = motion::findGroup(project, selected);
    if (selectedGroup != nullptr) { trackId = selectedGroup->id; parent = selectedGroup->parent; }
    bool expanded = false;
    for (std::size_t depth = 0; parent != 0 && depth < motion::maximumGroupDepth; ++depth) {
        expanded = collapsedGroups.erase(parent) != 0 || expanded;
        const auto* group = motion::findGroup(project, parent);
        parent = group != nullptr ? group->parent : 0;
    }
    if (expanded) { layoutRevision.reset(); ensureTrackRows(); }
    const auto found = std::find_if(rows.begin(), rows.end(), [trackId](const auto& row) { return row.id == trackId && !row.isLane(); });
    if (found == rows.end()) { return; }
    const auto row = static_cast<std::size_t>(found - rows.begin());
    const auto top = rowTops[row], bottom = top + heightOf(row), view = viewHeight();
    if (top < scrollY) {
        scrollY = top;
    } else if (bottom > scrollY + view) {
        scrollY = std::min(top, bottom - view);
    }
    if (expanded) { refreshTracks(); } else { resized(); repaint(); }
}

std::vector<motion::Document::CopiedKey> MotionTimelineView::copiedKeys() const {
    std::vector<motion::Document::CopiedKey> result;
    const auto& project = processor.document.project();
    double earliest = std::numeric_limits<double>::infinity();
    std::vector<std::pair<double, motion::Document::CopiedKey>> timed;
    for (const auto& key : selectedKeys) {
        const auto* clip = motion::findClip(project, key.clip);
        if (clip == nullptr) { continue; }
        const auto found = clip->properties.find(key.property);
        if (found == clip->properties.end()) { continue; }
        const auto timing = clip->timing(project.tempo());
        for (const auto& keyframe : found->second.keyframes()) {
            if (!motion::sameTime(keyframe.time, key.time) || timing.rate == 0) { continue; }
            const auto time = timing.projectTime(keyframe.time);
            earliest = std::min(earliest, time);
            timed.push_back({time, {key.property, 0, keyframe}});
        }
    }
    for (auto& [time, copied] : timed) { copied.offset = time - earliest; result.push_back(std::move(copied)); }
    return result;
}

std::vector<motion::Document::CopiedClip> MotionTimelineView::copiedClips() const {
    std::vector<motion::Document::CopiedClip> result;
    for (const auto& track : processor.document.project().tracks) {
        for (const auto& clip : track.clips) {
            if (selectedClips.contains(clip.id)) { result.push_back({track.id, track.kind, track.name, clip}); }
        }
    }
    return result;
}

void MotionTimelineView::deleteSelection() {
    if (!selectedKeys.empty()) { deleteSelectedKeys(); return; }
    if (!selectedClips.empty()) { deleteSelectedClips(false); return; }
    deleteTracks(pickedTracks());
}

std::optional<std::pair<double, motion::Id>> MotionTimelineView::dropTarget(juce::Point<int> point) const {
    if (point.x < namesWidth || point.y < rulerHeight || point.y >= rulerHeight + viewHeight() || point.x >= getWidth()) { return std::nullopt; }
    const auto time = snapTime(std::max(0.0, scrollTime + (point.x - namesWidth) / pixelsPerSecond), juce::ModifierKeys::getCurrentModifiers());
    const auto row = trackAtY(point.y);
    const auto& tracks = processor.document.project().tracks;
    return std::make_pair(time, row >= 0 && row < static_cast<int>(tracks.size()) ? tracks[static_cast<std::size_t>(row)].id : motion::Id(0));
}

void MotionTimelineView::selectClips(const std::vector<motion::Id>& ids) {
    clearCameraSelection();
    selectedKeys.clear();
    selectedClips = {ids.begin(), ids.end()};
    notifySelection(ids.empty() ? 0 : ids.front());
    revealSelection();
}

void MotionTimelineView::selectAll() {
    ensureTrackRows();
    if (!selectedKeys.empty()) {
        selectedKeys = allLaneKeys();
        if (!selectedKeys.empty()) { repaint(); return; }
    }
    std::vector<motion::Id> ids;
    for (const auto& track : processor.document.project().tracks) {
        for (const auto& clip : track.clips) { ids.push_back(clip.id); }
    }
    selectClips(ids);
}

void MotionTimelineView::toggleLanesForSelection() {
    for (const auto& track : processor.document.project().tracks) {
        if (std::none_of(track.clips.begin(), track.clips.end(), [this](const auto& clip) { return selectedClips.contains(clip.id); })) { continue; }
        if (expandedTracks.contains(track.id)) { expandedTracks.erase(track.id); } else { expandedTracks.insert(track.id); }
    }
    layoutRevision.reset();
    refreshTracks();
}

void MotionTimelineView::zoomBy(double factor) {
    // Around the playhead when it is in view, like Premiere; else the centre.
    const auto playheadX = timeX(processor.position.load()) - namesWidth;
    const auto centreX = playheadX >= 0 && playheadX <= getWidth() - namesWidth ? playheadX : std::max(0, (getWidth() - namesWidth) / 2);
    const auto anchor = scrollTime + centreX / pixelsPerSecond;
    const auto zoom = clampZoom(pixelsPerSecond * factor);
    animateView(zoom, std::max(0.0, anchor - centreX / zoom));
}

void MotionTimelineView::animateView(double zoom, double scroll) {
    animationFrom = {pixelsPerSecond, scrollTime};
    animationTo = {zoom, scroll};
    animationStart = juce::Time::getMillisecondCounterHiRes();
    viewAnimation.startTimerHz(60);
}

void MotionTimelineView::revealTime(double seconds) {
    cancelGesture();
    if (!std::isfinite(seconds) || seconds < 0) { return; }
    // A zoom or Fit gliding on would undo the reveal.
    viewAnimation.stopTimer();
    const auto visible = std::max(1, getWidth() - namesWidth - 20) / pixelsPerSecond;
    if (seconds < scrollTime || seconds > scrollTime + visible) {
        scrollTime = std::max(0.0, seconds - visible * .5);
        repaint();
    }
}

void MotionTimelineView::setTool(Tool next) {
    cancelGesture();
    tool = next;
    updateToolButtons();
    repaint();
}

// The tool strip shows the tool in use and whether snapping is on.
void MotionTimelineView::updateToolButtons() {
    snapButton.setToggleState(processor.document.project().gridSnap, juce::dontSendNotification);
    selectTool.setToggleState(tool == Tool::move, juce::dontSendNotification);
    slipTool.setToggleState(tool == Tool::slip, juce::dontSendNotification);
    stretchTool.setToggleState(tool == Tool::stretch, juce::dontSendNotification);
    rippleTool.setToggleState(tool == Tool::ripple, juce::dontSendNotification);
}

void MotionTimelineView::refreshTracks() {
    const auto& project = processor.document.project();
    updateToolButtons();
    // Another project (or composition): the view is kept, within its length.
    if (viewGeneration != processor.document.scopeGeneration()) {
        viewGeneration = processor.document.scopeGeneration();
        pixelsPerSecond = clampZoom(pixelsPerSecond);
        scrollTime = std::clamp(scrollTime, 0.0, std::max(0.0, contentEnd() - std::max(1, getWidth() - namesWidth) / pixelsPerSecond));
    }
    ensureTrackRows();
    validateTrackDrag();
    // A drag whose source went away never says it left.
    auto* container = juce::DragAndDropContainer::findParentDragContainerFor(this);
    if (dropPosition.has_value() && (container == nullptr || !container->isDragAndDropActive())) {
        dropPosition.reset();
        addTrack.setVisible(true);
    }
    std::erase_if(headers, [&](const auto& header) {
        if (header->id == compositionRow) { return project.effects.empty(); }
        return motion::findGroup(project, header->id) == nullptr && std::none_of(project.tracks.begin(), project.tracks.end(), [&](const auto& track) { return track.id == header->id; });
    });
    const auto updateHeader = [&](const motion::Track& track, bool group) {
        auto found = std::find_if(headers.begin(), headers.end(), [&](const auto& header) { return header->id == track.id; });
        if (found == headers.end()) {
            auto header = std::make_unique<MotionTrackHeader>(track.id);
            header->onRename = [this](motion::Id id, std::string name) {
                if (name.empty()) { refreshTracks(); return; }
                // The same name again changes nothing, and records nothing.
                const auto renamed = processor.document.tryEdit(motion::findGroup(processor.document.project(), id) != nullptr ? "Rename group" : "Rename track", [id, name](motion::Project& project) {
                    bool changed = false;
                    auto* group = motion::findGroup(project, id);
                    if (group != nullptr && group->name != name) { group->name = name; changed = true; }
                    const auto& tracks = project.tracks;
                    const auto current = std::find_if(tracks.begin(), tracks.end(), [id](const auto& item) { return item.id == id; });
                    if (current != tracks.end() && current->name != name) {
                        project.tracks.changeById(id)->name = name;
                        changed = true;
                    }
                    return changed;
                });
                if (!renamed) { refreshTracks(); }
            };
            header->onMenu = [this](motion::Id id) { showTrackMenu(id); };
            // A track or group name selects it, to show its effects.
            header->onSelect = [this](motion::Id id, juce::ModifierKeys mods) { pickTrack(id, mods); };
            header->onCollapse = [this](motion::Id id) {
                if (collapsedGroups.contains(id)) { collapsedGroups.erase(id); } else { collapsedGroups.insert(id); }
                refreshTracks();
                selectClip(id);
            };
            header->onReorder = [this](motion::Id id, juce::Point<int> screen, bool finished) { dragTrack(id, getLocalPoint(nullptr, screen).y, finished); };
            header->onLanes = [this](motion::Id id) {
                if (expandedTracks.contains(id)) { expandedTracks.erase(id); } else { expandedTracks.insert(id); }
                layoutRevision.reset();
                refreshTracks();
            };
            header->onLock = [this](motion::Id id) { toggleLock(id); };
            header->onMute = [this](motion::Id id) { toggleTrack(id, false); };
            header->onSolo = [this](motion::Id id) { toggleTrack(id, true); };
            headerArea.addAndMakeVisible(*header);
            headers.push_back(std::move(header));
            found = headers.end() - 1;
        }
        const auto lanes = group ? false : !trackLanes(project, track).empty();
        (*found)->update(track, group, collapsedGroups.contains(track.id), lanes, expandedTracks.contains(track.id));
    };
    if (!project.effects.empty()) {
        auto found = std::find_if(headers.begin(), headers.end(), [](const auto& header) { return header->id == compositionRow; });
        if (found == headers.end()) {
            auto header = std::make_unique<MotionTrackHeader>(compositionRow);
            header->onSelect = [this](motion::Id, juce::ModifierKeys) { showCompositionEffects(); };
            header->onMenu = [this](motion::Id) { showCompositionEffects(); };
            header->onLanes = [this](motion::Id id) {
                if (expandedTracks.contains(id)) { expandedTracks.erase(id); } else { expandedTracks.insert(id); }
                layoutRevision.reset();
                refreshTracks();
            };
            headerArea.addAndMakeVisible(*header);
            headers.push_back(std::move(header));
            found = headers.end() - 1;
        }
        (*found)->updateComposition(!compositionLanes(project).empty(), expandedTracks.contains(compositionRow));
    }
    for (const auto& track : project.tracks) { updateHeader(track, false); }
    for (const auto& group : project.groups) {
        motion::Track display;
        display.id = group.id;
        display.name = group.name;
        display.muted = group.muted;
        display.solo = group.solo;
        updateHeader(display, true);
    }
    rebuildRows();
    layoutRevision = processor.document.revision();
    resized();
    repaint();
}

void MotionTimelineView::resized() {
    ensureTrackRows();
    validateTrackDrag();
    // The tools, then snapping beside them after a divider.
    auto tools = juce::Rectangle<int>(cardInset - 1, 2, 4 * 22, 22);
    for (auto* button : {&selectTool, &slipTool, &stretchTool, &rippleTool}) { button->setBounds(tools.removeFromLeft(22)); }
    snapButton.setBounds(tools.getX() + 9, 2, 22, 22);
    scrollY = std::clamp(scrollY, 0, maximumScrollY());
    // Headers live in a container clipped below the ruler, so rows scroll
    // smoothly under it.
    headerArea.setBounds(0, rulerHeight, namesWidth - 5, viewHeight());
    const auto* trackDrag = active<TrackDrag>();
    for (auto& header : headers) {
        const auto found = std::find_if(rows.begin(), rows.end(), [&](const auto& row) { return row.id == header->id && !row.isLane(); });
        if (found == rows.end()) { header->setVisible(false); continue; }
        const auto row = static_cast<std::size_t>(found - rows.begin());
        const auto y = rowY(static_cast<int>(row)) - rulerHeight;
        const auto height = heightOf(row);
        const auto shown = y + shiftOf(static_cast<int>(row));
        header->setVisible(shown + height > 0 && shown < headerArea.getHeight());
        // Centred on the card; the rest of the row's foot still resizes it.
        // A dragged track's header (or a dragged group's headers) rides with
        // it; the others slide.
        const auto indent = cardInset + std::min(64, found->depth * 16);
        const auto lifted = inLiftedBlock(static_cast<int>(row));
        const auto top = lifted ? liftedTop() - rulerHeight + rowY(static_cast<int>(row)) - rowY(trackDrag->first) : y + shiftOf(static_cast<int>(row));
        header->setBounds(indent, top + (resizeStrip - bandGap) / 2, namesWidth - 5 - indent, height - resizeStrip);
        header->setLifted(lifted);
        if (lifted) { header->setVisible(true); header->toFront(false); }
    }
    // While an asset is dragged below the tracks, the new track's preview
    // takes this place.
    addTrack.setVisible(!dropPosition.has_value());
    addTrack.setBounds(cardInset, rowY(static_cast<int>(rows.size())) - rulerHeight + addTrackGap, addTrack.idealWidth(), addTrackHeight);
}

void MotionTimelineView::paintDropPreview(juce::Graphics& g) {
    if (!dropPosition.has_value()) { return; }
    const auto& tracks = processor.document.project().tracks;
    // Effects, from the library or a card, outline what they would land on.
    if (!dropEffect.empty() || dropInstance != 0) {
        int row = 0;
        const auto* clip = clipAt(*dropPosition, row);
        if (dropPosition->x < namesWidth && dropPosition->y >= rulerHeight) { row = trackAtY(dropPosition->y); }
        const auto group = groupAtY(dropPosition->y);
        if (group != 0) {
            g.setColour(osci::Colours::accentColor());
            g.drawRect(0, rowY(visualRowAt(dropPosition->y)), getWidth(), heightAt(visualRowAt(dropPosition->y)), 2);
        } else if (row >= 0 && row < static_cast<int>(tracks.size()) && tracks[row].kind == motion::TrackKind::visual && (clip != nullptr || dropPosition->x < namesWidth)) {
            const auto bounds = clip != nullptr ? clipBounds(*clip, row) : juce::Rectangle<int>(0, trackY(row), namesWidth, trackHeight(row));
            g.setColour(osci::Colours::accentColor());
            g.drawRoundedRectangle(bounds.toFloat().reduced(2), 4, 2);
        }
    } else {
        const auto row = trackAtY(dropPosition->y);
        const auto time = dropPosition->x < namesWidth ? processor.position.load() : std::max(0.0, scrollTime + (dropPosition->x - namesWidth) / pixelsPerSecond);
        motion::Clip candidate;
        candidate.id = std::numeric_limits<motion::Id>::max();
        candidate.start = snapTime(time, juce::ModifierKeys::getCurrentModifiers());
        const auto& assets = processor.document.project().assets;
        const auto asset = std::find_if(assets.begin(), assets.end(), [&](const auto& item) { return item->id == dropAssetId; });
        const auto& definitions = processor.document.project().definitions;
        const auto definition = std::find_if(definitions.begin(), definitions.end(), [&](const auto& item) { return item->id == dropAssetId; });
        if (asset != assets.end()) {
            candidate = motion::Document::makeClip(candidate.id, **asset, candidate.start);
        } else if (definition != definitions.end()) {
            candidate = motion::Document::makeCompositionClip(candidate.id, **definition, candidate.start);
        }
        const bool recursive = definition != definitions.end() && !processor.document.canReferenceComposition(dropAssetId);
        const bool audio = asset != assets.end() && (*asset)->audio != nullptr;
        const auto kind = audio ? motion::TrackKind::audio : motion::TrackKind::visual;
        const bool correctKind = row < 0 || row >= static_cast<int>(tracks.size()) || tracks[row].kind == kind;
        const auto allowed = (asset != assets.end() || definition != definitions.end()) && !recursive && correctKind && (row < 0 || row >= static_cast<int>(tracks.size()) || !tracks[row].locked && tracks[row].canPlace(candidate, 0, processor.document.project().tempo()));
        // Below the tracks, the new track always lands in the first free slot.
        const auto group = groupAtY(dropPosition->y);
        const auto slotY = row < 0 && group == 0 ? rowY(static_cast<int>(rows.size())) : rowY(std::max(0, visualRowAt(dropPosition->y)));
        const auto bounds = (row >= 0 ? clipBounds(candidate, row) : juce::Rectangle<int>(timeX(candidate.start), slotY, std::max(2, boundedPixel(candidate.duration * pixelsPerSecond)), defaultTrackHeight)).toFloat().reduced(1, 4);
        if (row < 0 && allowed) {
            // The header column shows that a track will be created.
            auto header = juce::Rectangle<float>(4.0f, static_cast<float>(slotY) + 3.0f, static_cast<float>(namesWidth) - 9.0f, static_cast<float>(defaultTrackHeight) - 6.0f);
            g.setColour(osci::Colours::accentColor().withAlpha(.12f));
            g.fillRoundedRectangle(header, motion::style::radius);
            g.setColour(osci::Colours::accentColor().withAlpha(.7f));
            g.drawRoundedRectangle(header.reduced(.5f), motion::style::radius, 1.0f);
            motion::icons::draw(g, motion::icons::Icon::add, header.removeFromLeft(26.0f), osci::Colours::accentColor().brighter(.3f), 14.0f);
            g.setFont(motion::style::body());
            g.setColour(osci::Colours::text());
            g.drawText(audio ? "New audio track" : "New track", header, juce::Justification::centredLeft, true);
        }
        juce::Graphics::ScopedSaveState scope(g);
        g.reduceClipRegion(namesWidth, rulerHeight, getWidth() - namesWidth, getHeight() - rulerHeight);
        g.setColour((allowed ? motion::style::accent() : motion::style::error()).withAlpha(0.2f));
        g.fillRoundedRectangle(bounds, 4);
        g.setColour(allowed ? motion::style::accent() : motion::style::error());
        g.drawRoundedRectangle(bounds, 4, 1);
        g.drawText(allowed ? (audio ? "Add audio" : definition != definitions.end() ? "Add composition" : "Add object") : (recursive ? "Cannot contain itself" : correctKind ? "Clips cannot overlap" : "Use a matching or empty lane"), bounds.reduced(8, 0), juce::Justification::centredLeft);
    }
}

void MotionTimelineView::paintOverChildren(juce::Graphics& g) {
    // JUCE can skip paint() under opaque children; rows must still be current.
    ensureTrackRows();
    validateTrackDrag();
    if (autoKey) {
        g.setColour(motion::style::error().withAlpha(.9f));
        g.drawRect(getLocalBounds(), 2);
    }
    const auto* trackDrag = active<TrackDrag>();
    if (trackDrag == nullptr) { return; }
    juce::Graphics::ScopedSaveState view(g);
    g.reduceClipRegion(0, rulerHeight, getWidth(), viewHeight());
    const auto top = liftedTop();
    const auto block = juce::Rectangle<int>(0, top, getWidth(), trackDrag->height - bandGap);
    const auto shape = [&block](float inset) {
        juce::Path path;
        path.addRoundedRectangle(block.toFloat().reduced(inset), cardRadius);
        return path;
    };
    // A wide soft shadow and a tight one, so the block reads as lifted
    // above and below even over the dark rows.
    juce::DropShadow(juce::Colours::black.withAlpha(.9f), 22, {0, 3}).drawForPath(g, shape(0.0f));
    juce::DropShadow(juce::Colours::black.withAlpha(.6f), 6, {0, 1}).drawForPath(g, shape(0.0f));
    g.setColour(osci::Colours::veryDark());
    g.fillPath(shape(0.0f));
    {
        juce::Graphics::ScopedSaveState lifted(g);
        g.reduceClipRegion(shape(0.0f));
        g.addTransform(juce::AffineTransform::translation(0.0f, static_cast<float>(top - rowY(trackDrag->first))));
        for (int visible = trackDrag->first; visible < trackDrag->last; ++visible) { paintRow(g, visible); }
        // Lifted blocks are a shade lighter, nearer the light.
        g.setColour(juce::Colours::white.withAlpha(.035f));
        g.fillAll();
    }
    for (auto& header : headers) {
        const auto found = std::find_if(rows.begin(), rows.end(), [&](const auto& row) { return row.id == header->id && !row.isLane(); });
        if (found == rows.end() || !inLiftedBlock(static_cast<int>(found - rows.begin()))) { continue; }
        juce::Graphics::ScopedSaveState placed(g);
        g.setOrigin(headerArea.getPosition() + header->getPosition());
        header->paintEntireComponent(g, true);
    }
    g.setColour(osci::Colours::accentColor().withAlpha(.55f));
    g.strokePath(shape(.75f), juce::PathStrokeType(1.5f));
    // The scroll bars and playhead stay on top, as over any other row.
    paintScrollBars(g);
    paintPlayhead(g);
}

std::optional<int> MotionTimelineView::playheadX() const {
    const auto x = timeX(processor.position.load());
    if (x < namesWidth || x > getWidth()) { return std::nullopt; }
    return x;
}

void MotionTimelineView::paintPlayhead(juce::Graphics& g) {
    const auto x = playheadX();
    playheadStrip.drawn(x);
    if (!x.has_value()) { return; }
    const auto playhead = *x;
    g.setColour(motion::style::playhead());
    g.drawVerticalLine(playhead, 0, static_cast<float>(getHeight()));
    juce::Path head;
    head.addTriangle(playhead - 5, 0, playhead + 5, 0, playhead, 8);
    g.fillPath(head);
}

bool MotionTimelineView::isInterestedInDragSource(const SourceDetails& details) {
    const auto description = details.description.toString();
    return description.startsWith("motion-asset:") || description.startsWith("motion-effect:") || description.startsWith("motion-effect-instance:");
}

void MotionTimelineView::itemDragMove(const SourceDetails& details) {
    dropPosition = details.localPosition;
    addTrack.setVisible(false);
    const auto description = details.description.toString();
    dropEffect = description.startsWith("motion-effect:") ? description.fromFirstOccurrenceOf(":", false, false).toStdString() : std::string();
    dropInstance = description.startsWith("motion-effect-instance:") ? static_cast<motion::Id>(description.fromFirstOccurrenceOf(":", false, false).getLargeIntValue()) : motion::Id(0);
    dropAssetId = description.startsWith("motion-asset:") ? static_cast<motion::Id>(description.fromFirstOccurrenceOf(":", false, false).getLargeIntValue()) : motion::Id(0);
    previewEffect(dropEffect.empty() && dropInstance == 0 ? 0 : effectOwnerAt(details.localPosition));
    repaint();
}

void MotionTimelineView::itemDragExit(const SourceDetails&) {
    dropPosition.reset();
    addTrack.setVisible(true);
    previewEffect(0);
    repaint();
}

void MotionTimelineView::previewEffect(motion::Id owner) {
    if (owner == previewedOwner && processor.document.revision() == previewedRevision) { return; }
    previewedOwner = owner;
    previewedRevision = processor.document.revision();
    if (!onPreview) { return; }
    // A moved card plays where it would land.
    if (dropInstance != 0 && owner != 0) {
        auto project = processor.document.project();
        const auto moved = motion::moveEffect(project, dropInstance, owner, juce::ModifierKeys::getCurrentModifiers().isAltDown(), std::numeric_limits<motion::Id>::max());
        onPreview(moved ? &project : nullptr);
        return;
    }
    const auto* definition = motion::effectDefinition(dropEffect);
    if (owner == 0 || definition == nullptr) {
        onPreview(nullptr);
        return;
    }
    auto project = processor.document.project();
    auto* effects = motion::findEffectOwner(project, owner);
    if (effects == nullptr) { onPreview(nullptr); return; }
    effects->push_back(motion::makeEffect(std::numeric_limits<motion::Id>::max(), *definition));
    onPreview(&project);
}

void MotionTimelineView::setEffectDragActive(bool active) {
    effectDragActive = active;
    repaint();
}

void MotionTimelineView::itemDropped(const SourceDetails& details) {
    dropPosition.reset();
    addTrack.setVisible(true);
    previewEffect(0);
    if (details.description.toString().startsWith("motion-effect:")) {
        insertEffect(details.description.toString().fromFirstOccurrenceOf(":", false, false).toStdString(), details.localPosition);
        repaint();
        return;
    }
    if (dropInstance != 0) {
        const auto owner = effectOwnerAt(details.localPosition);
        const auto effect = dropInstance;
        dropInstance = 0;
        if (owner != 0 && onEffectMoved) { onEffectMoved(effect, owner, juce::ModifierKeys::getCurrentModifiers().isAltDown()); }
        repaint();
        return;
    }
    insertAsset(static_cast<motion::Id>(details.description.toString().fromFirstOccurrenceOf(":", false, false).getLargeIntValue()), details.localPosition.x, details.localPosition.y);
    repaint();
}

bool MotionTimelineView::insertAsset(motion::Id assetId, int x, int y) {
    const auto& project = processor.document.project();
    const auto asset = std::find_if(project.assets.begin(), project.assets.end(), [assetId](const auto& item) { return item->id == assetId; });
    if (asset == project.assets.end()) {
        const auto time = x < namesWidth ? processor.position.load() : std::max(0.0, scrollTime + (x - namesWidth) / pixelsPerSecond);
        const auto row = trackAtY(y);
        const auto track = row >= 0 && row < static_cast<int>(project.tracks.size()) ? project.tracks[row].id : 0;
        motion::Id inserted = 0;
        const auto result = processor.document.insertComposition(assetId, snapTime(time, juce::ModifierKeys::getCurrentModifiers()), track, groupAtY(y), inserted);
        if (result.failed()) {
            if (onError) { onError(result.getErrorMessage()); }
            return false;
        }
        selectClip(inserted);
        refreshTracks();
        return true;
    }
    const auto time = x < namesWidth ? processor.position.load() : std::max(0.0, scrollTime + (x - namesWidth) / pixelsPerSecond);
    const auto snapped = snapTime(time, juce::ModifierKeys::getCurrentModifiers());
    const auto row = trackAtY(y);
    const auto group = groupAtY(y);
    const auto kind = (*asset)->audio != nullptr ? motion::TrackKind::audio : motion::TrackKind::visual;
    auto clip = motion::Document::makeClip(processor.document.newId(), **asset, snapped);
    const auto id = clip.id;
    if (row >= 0 && row < static_cast<int>(project.tracks.size()) && (project.tracks[row].locked || project.tracks[row].kind != kind || !project.tracks[row].canPlace(clip, 0, project.tempo()))) {
        return false;
    }
    const auto trackId = processor.document.newId();
    processor.document.edit(kind == motion::TrackKind::audio ? "Add audio clip" : "Add object clip", [&](motion::Project& updated) {
        updated.duration = std::max(updated.duration, clip.timing(updated.tempo()).end());
        if (row >= 0 && row < static_cast<int>(updated.tracks.size())) {
            updated.tracks.change(static_cast<std::size_t>(row)).insert(std::move(clip), updated.tempo());
        } else {
            motion::Track track;
            track.id = trackId;
            track.group = group;
            track.name = clip.name;
            track.kind = kind;
            track.insert(std::move(clip), updated.tempo());
            updated.tracks.push_back(std::move(track));
        }
    });
    selectClip(id);
    return true;
}

void MotionTimelineView::updateBake() {
    const auto bake = processor.hiddenLineBake();
    const auto now = juce::Time::getMillisecondCounterHiRes();
    const auto finished = bake == nullptr || bake->complete();
    const auto wasShown = bakeShown;
    if (bake.get() != bakeSeen) {
        // A new bake goes on from what the strip showed of the last one.
        bakeShown = bakeShown && !finished && bakeFinishedAt == 0;
        bakeSeen = bake.get();
        bakeSeenAt = now;
        bakeFinishedAt = 0;
    }
    if (!finished && now - bakeSeenAt >= bakeShowAfterMs) { bakeShown = true; }
    if (finished && bakeFinishedAt == 0) { bakeFinishedAt = now; }
    // Faded out: nothing more to paint.
    if (bakeShown && bakeFinishedAt > 0 && now - bakeFinishedAt > bakeFadeMs + 100) { bakeShown = false; }
    if (bakeShown || wasShown) { repaint(bakeStrip().expanded(0, 1)); }
}

void MotionTimelineView::paintBake(juce::Graphics& g) const {
    const auto bake = processor.hiddenLineBake();
    // Its frames are the main composition's, on its time.
    if (bake == nullptr || processor.document.editingComposition() != 0) { return; }
    if (!bakeShown || bake.get() != bakeSeen) { return; }
    const auto opacity = bakeFinishedAt > 0 ? 1.0 - (juce::Time::getMillisecondCounterHiRes() - bakeFinishedAt) / bakeFadeMs : 1.0;
    if (opacity <= 0) { return; }
    const auto strip = bakeStrip();
    // Over the loop's brace, the strip cuts through it, so the progress
    // reads the same along the whole ruler.
    const auto loop = loopBounds();
    if (!loop.isEmpty()) {
        g.setColour(osci::Colours::surfaceRaised().withMultipliedAlpha(static_cast<float>(opacity)));
        g.fillRect(juce::Rectangle<int>(loop.getX(), strip.getY() - 1, loop.getWidth(), strip.getHeight() + 2).getIntersection(strip.expanded(0, 1)));
    }
    const auto first = std::max(strip.getX(), timeX(0));
    const auto last = std::min(strip.getRight(), timeX(bake->timeOf(bake->frameCount())));
    using State = motion::hidden::Bake::State;
    const auto colour = [opacity](State state) {
        const auto base = state == State::done ? motion::style::accent().withAlpha(.75f) : osci::Colours::text().withAlpha(state == State::baking ? .55f : .16f);
        return base.withMultipliedAlpha(static_cast<float>(opacity));
    };
    // Each column shows the least done of its frames; runs of one state are
    // one rectangle.
    std::optional<State> run;
    auto runStart = first;
    for (auto x = first; x <= last; ++x) {
        std::optional<State> here;
        if (x < last) {
            const auto from = bake->frameOf(scrollTime + (x - namesWidth) / pixelsPerSecond);
            const auto next = bake->frameOf(scrollTime + (x + 1 - namesWidth) / pixelsPerSecond);
            const auto to = next > from ? next - 1 : from;
            here = State::done;
            for (auto frame = from; frame <= to; ++frame) {
                const auto state = bake->state(frame);
                if (state == State::baking) { here = state; break; }
                if (state == State::waiting) { here = state; }
            }
        }
        if (here != run) {
            if (run.has_value()) {
                g.setColour(colour(*run));
                g.fillRect(runStart, strip.getY(), x - runStart, strip.getHeight());
            }
            run = here;
            runStart = x;
        }
    }
}

void MotionTimelineView::paint(juce::Graphics& g) {
    beamLabel.reset();
    ensureTrackRows();
    validateTrackDrag();
    g.fillAll(osci::Colours::veryDark());
    auto area = getLocalBounds();
    g.setColour(osci::Colours::surfaceRaised());
    g.fillRect(area.removeFromTop(toolsHeight));
    if (showsMarkerBand()) { fillCard(g, markerBandTop(), markerBandHeight, osci::Colours::surfaceRaised(), osci::Colours::surfaceRaised().darker(.15f)); }
    const auto viewEnd = scrollTime + std::max(0, getWidth() - namesWidth) / pixelsPerSecond;
    const auto grid = processor.document.project().timeGrid();
    const auto step = grid.tickStep(pixelsPerSecond);
    const auto minorStep = grid.snapStep(pixelsPerSecond, step);
    if (minorStep.has_value()) {
        g.setColour(motion::style::gridMinor());
        grid.forEachTick(scrollTime, viewEnd, *minorStep, [&](double time) { g.drawVerticalLine(timeX(time), rulerHeight, static_cast<float>(getHeight())); });
    }
    g.setColour(motion::style::gridMajor());
    grid.forEachTick(scrollTime, viewEnd, step, [&](double time) { g.drawVerticalLine(timeX(time), rulerHeight, static_cast<float>(getHeight())); });
    // As the Graph's ruler: a mark at the foot of each labelled time, the line
    // below carrying on from it.
    {
        juce::Graphics::ScopedSaveState state(g);
        g.reduceClipRegion(namesWidth, 0, getWidth() - namesWidth, toolsHeight);
        g.setColour(juce::Colours::white.withAlpha(.3f));
        motion::ui::paintRulerTicks(g, grid, scrollTime, viewEnd, step, static_cast<float>(toolsHeight), 6.0f, [this](double time) { return timeX(time); });
    }
    motion::ui::paintRulerLabels(g, grid, scrollTime, viewEnd, step, {namesWidth, 0, getWidth() - namesWidth, toolsHeight - 4}, [this](double time) { return timeX(time); });
    paintLoop(g);
    paintBake(g);
    g.setColour(osci::Colours::surfaceRaised());
    g.fillRect(0, 0, namesWidth, toolsHeight);
    g.setColour(motion::style::hairline());
    g.fillRect(snapButton.getX() - 5, 7, 1, toolsHeight - 14);
    const auto& tracks = processor.document.project().tracks;
    g.saveState();
    g.reduceClipRegion(0, rulerHeight, getWidth(), viewHeight());
    // A dragged track lifts out of the list and the other rows slide by
    // their eased offsets; paintOverChildren draws the lifted block.
    const auto* trackDrag = active<TrackDrag>();
    const auto dragging = trackDrag != nullptr;
    // While rows slide (dragging or settling) some come from off screen.
    const auto sliding = dragging || !rowShift.empty();
    const auto reach = sliding ? (dragging ? trackDrag->height : getHeight()) : 0;
    for (int visible = sliding ? 0 : firstVisibleRow(); visible < static_cast<int>(rows.size()); ++visible) {
        if (rowY(visible) - reach >= getHeight()) { break; }
        if (dragging && visible >= trackDrag->first && visible < trackDrag->last) { continue; }
        juce::Graphics::ScopedSaveState shifted(g);
        g.addTransform(juce::AffineTransform::translation(0.0f, static_cast<float>(shiftOf(visible))));
        paintRow(g, visible);
        if (dragging && trackDrag->group != 0 && rows[static_cast<std::size_t>(visible)].group() && rows[static_cast<std::size_t>(visible)].id == trackDrag->group) {
            // The group it will join.
            g.setColour(osci::Colours::accentColor().withAlpha(.7f));
            g.drawRoundedRectangle(juce::Rectangle<float>(0.0f, static_cast<float>(rowY(visible)), static_cast<float>(getWidth()), static_cast<float>(heightOf(static_cast<std::size_t>(visible)) - bandGap)).reduced(.75f), cardRadius, 1.5f);
        }
    }
    g.restoreState();
    paintBeamLabel(g);
    if (blocked.has_value()) {
        juce::Graphics::ScopedSaveState ghost(g);
        g.reduceClipRegion(namesWidth, rulerHeight, getWidth() - namesWidth, getHeight() - rulerHeight);
        const auto area = blocked->toFloat().reduced(1, 3);
        g.setColour(motion::style::error().withAlpha(.2f));
        g.fillRoundedRectangle(area, 4);
        g.setColour(motion::style::error());
        g.drawRoundedRectangle(area, 4, 1.2f);
        g.setFont(motion::style::caption());
        g.drawText("Overlaps", area.reduced(8, 0), juce::Justification::centredLeft, true);
    }
    // While an effect is dragged, every clip that can take it is outlined.
    if (effectDragActive) {
        juce::Graphics::ScopedSaveState outlines(g);
        g.reduceClipRegion(namesWidth, rulerHeight, getWidth() - namesWidth, getHeight() - rulerHeight);
        g.setColour(osci::Colours::accentColor().withAlpha(.35f));
        for (int index = 0; index < static_cast<int>(tracks.size()); ++index) {
            if (tracks[static_cast<std::size_t>(index)].kind != motion::TrackKind::visual) { continue; }
            for (const auto& clip : tracks[static_cast<std::size_t>(index)].clips) { g.drawRoundedRectangle(clipBounds(clip, index).toFloat().reduced(1.5f), 4, 1.0f); }
        }
    }
    paintDropPreview(g);
    if (showsMarkerBand()) {
        g.setColour(osci::Colours::textMuted());
        g.setFont(motion::style::caption());
        g.drawText(processor.document.project().tempoChanges != nullptr ? "Markers / tempo" : "Markers", cardInset + 8, markerBandTop(), namesWidth - 24, markerBandHeight, juce::Justification::centredLeft);
        const auto& project = processor.document.project();
        if (project.tempoChanges != nullptr) {
            const auto tempo = project.tempo();
            double previousBeat = 0, previousBpm = project.bpm;
            for (const auto& change : *project.tempoChanges) {
                const auto x = timeX(tempo.seconds(change.beat));
                const auto colour = motion::style::tempo();
                // A ramp draws as a slope from the previous tempo point.
                if (change.ramp) {
                    const auto from = static_cast<float>(std::max(namesWidth, timeX(tempo.seconds(previousBeat))));
                    const auto rising = change.bpm >= previousBpm;
                    g.setColour(colour.withAlpha(0.55f));
                    const auto high = static_cast<float>(markerBandTop() + 3), low = static_cast<float>(markerBandTop() + markerBandHeight - 3);
                    g.drawLine(from, rising ? low : high, static_cast<float>(std::min(x, getWidth())), rising ? high : low, 1.5f);
                }
                previousBeat = change.beat;
                previousBpm = change.bpm;
                if (x < namesWidth || x >= getWidth()) { continue; }
                g.setColour(colour.withAlpha(0.14f));
                g.drawVerticalLine(x, rulerHeight, static_cast<float>(getHeight()));
                g.setColour(colour);
                g.fillRect(x, markerBandTop() + 1, 2, markerBandHeight - 2);
                g.drawText(juce::String(juce::CharPointer_UTF8("\xe2\x99\xa9 ")) + juce::String(change.bpm, change.bpm == std::round(change.bpm) ? 0 : 2), x + 4, markerBandTop() + 1, 70, markerBandHeight - 2, juce::Justification::centredLeft, true);
            }
        }
        for (const auto& marker : processor.document.project().markers) {
            const auto bounds = markerBounds(marker);
            if (bounds.isEmpty()) { continue; }
            const auto colour = motion::style::marker();
            // The staff runs through the tracks, so clips can be lined up to it.
            g.setColour(colour.withAlpha(0.28f));
            g.drawVerticalLine(bounds.getX(), rulerHeight, static_cast<float>(getHeight()));
            // The staff, then the name on a small tag beside it.
            const auto tag = bounds.toFloat().withTrimmedLeft(2).reduced(0, 3);
            juce::Path flag;
            flag.addRoundedRectangle(tag.getX(), tag.getY(), tag.getWidth(), tag.getHeight(), 3.0f, 3.0f, false, true, false, true);
            g.setColour(colour.withAlpha(marker.id == selectedMarker ? 0.34f : 0.16f));
            g.fillPath(flag);
            g.setColour(colour);
            g.fillRect(bounds.getX(), bounds.getY(), 2, bounds.getHeight());
            g.setFont(motion::style::caption());
            g.setColour(osci::Colours::text().withAlpha(.9f));
            g.drawText(marker.name, tag.reduced(6, 0).toNearestInt(), juce::Justification::centredLeft, true);
        }
    }
    paintCameraBand(g);
    if (snapGuide.has_value()) {
        const auto x = timeX(*snapGuide);
        if (x >= namesWidth && x <= getWidth()) {
            g.setColour(osci::Colours::accentColor().withAlpha(.75f));
            const float dashes[] {4.0f, 3.0f};
            g.drawDashedLine(juce::Line<float>(static_cast<float>(x), static_cast<float>(rulerHeight), static_cast<float>(x), static_cast<float>(getHeight())), dashes, 2, 1.0f);
        }
    }
    // The row edge under the pointer (or being dragged) shows where it resizes.
    const auto* heightDrag = active<HeightDrag>();
    if (hoveredEdge >= 0 || heightDrag != nullptr) {
        auto row = hoveredEdge;
        if (heightDrag != nullptr) {
            const auto found = std::find_if(rows.begin(), rows.end(), [&](const auto& item) { return item.id == heightDrag->track && !item.isLane(); });
            row = found != rows.end() ? static_cast<int>(found - rows.begin()) : -1;
        }
        if (row >= 0 && row < static_cast<int>(rows.size())) {
            g.setColour(osci::Colours::accentColor().withAlpha(.8f));
            g.fillRect(0, rowY(row) + heightOf(static_cast<std::size_t>(row)) - 2, getWidth(), 2);
        }
    }
    g.setColour(osci::Colours::veryDark().darker(.25f));
    g.fillRect(0, getHeight() - scrollStrip, getWidth(), scrollStrip);
    paintScrollBars(g);
    // A box being dragged to pick clips or keys, over every row it crosses.
    const auto* clipMarquee = active<ClipMarquee>();
    const auto* keyMarquee = active<KeyMarquee>();
    for (const auto* box : {clipMarquee != nullptr ? &clipMarquee->area : nullptr, keyMarquee != nullptr ? &keyMarquee->area : nullptr}) {
        if (box == nullptr) { continue; }
        g.setColour(osci::Colours::accentColor().withAlpha(.1f));
        g.fillRect(*box);
        g.setColour(osci::Colours::accentColor().withAlpha(.6f));
        g.drawRect(*box);
    }
    paintPlayhead(g);
    if (tracks.empty()) {
        g.setColour(osci::Colours::text().withAlpha(0.55f));
        g.drawText("Drop sources here", getLocalBounds().withTrimmedTop(rulerHeight), juce::Justification::centred);
    }
}

void MotionTimelineView::mouseDown(const juce::MouseEvent& event) {
    grabKeyboardFocus();
    ensureTrackRows();
    cancelGesture();
    if (onNamesEdge(event) && event.mods.isLeftButtonDown()) {
        gesture = NamesResize {};
        return;
    }
    const auto edge = rowEdgeAt(event.getPosition());
    if (edge >= 0 && event.mods.isLeftButtonDown()) {
        const auto id = rows[static_cast<std::size_t>(edge)].id;
        if (event.getNumberOfClicks() > 1) {
            // Double-click an edge: back to the default height.
            processor.document.setTrackHeight(id, 0);
            layoutRows();
            resized();
            repaint();
            return;
        }
        gesture = HeightDrag {id, heightOf(static_cast<std::size_t>(edge)), event.y, storedHeight(id)};
        return;
    }
    if (inCameraBand(event.y)) {
        cameraBandDown(event);
        return;
    }
    if (event.mods.isLeftButtonDown() && event.mods.isCommandDown() && event.x >= namesWidth && event.y < rulerHeight && moveNearestLoopEdge(event)) { return; }
    if (event.mods.isLeftButtonDown() && loopDown(event)) { return; }
    if (barDown(event)) { return; }
    const auto* marker = markerAt(event.getPosition());
    if (event.mods.isPopupMenu() && event.x >= namesWidth && event.y < rulerHeight) {
        showMarkerMenu(marker != nullptr ? marker->id : 0, snapTime(scrollTime + (event.x - namesWidth) / pixelsPerSecond, event.mods));
        return;
    }
    if (marker != nullptr && event.mods.isLeftButtonDown()) {
        const auto id = marker->id;
        const auto time = marker->time;
        selectMarker(id);
        processor.seek(time);
        MarkerDrag drag;
        drag.marker = id;
        drag.originalTime = time;
        drag.downX = event.x;
        drag.before = processor.document.project();
        drag.revision = processor.document.revision();
        gesture = std::move(drag);
        repaint();
        return;
    }
    selectedMarker = 0;
    if (showsMarkerBand() && event.y >= markerBandTop() && event.y < markerBandTop() + markerBandHeight && event.x < namesWidth && onEditMarker) { onEditMarker(0, processor.position.load()); return; }
    if (event.y < rulerHeight && event.x < namesWidth) { return; }
    if (event.y >= rulerHeight && laneAtY(event.y) != nullptr) {
        if (event.x < namesWidth) { return; }
        // Keys in a lane are what Delete acts on now, not a camera's.
        clearCameraSelection();
        const auto key = keyAt(event.getPosition());
        if (event.mods.isPopupMenu()) {
            if (key.has_value() && !isKeySelected(key->clip, key->property, key->time)) { selectedKeys = {*key}; }
            repaint();
            showKeyMenu();
            return;
        }
        if (!event.mods.isLeftButtonDown()) { return; }
        if (key.has_value()) {
            const bool chosen = isKeySelected(key->clip, key->property, key->time);
            if (event.mods.isShiftDown()) {
                if (chosen) {
                    std::erase_if(selectedKeys, [&](const auto& item) { return item.clip == key->clip && item.property == key->property && motion::sameTime(item.time, key->time); });
                } else {
                    selectedKeys.push_back(*key);
                }
            } else if (!chosen) {
                selectedKeys = {*key};
            }
            if (isKeySelected(key->clip, key->property, key->time)) { beginKeyDrag(*key, event.x); }
        } else {
            const auto start = event.getPosition();
            auto base = event.mods.isShiftDown() ? selectedKeys : std::vector<KeyRef>{};
            gesture = KeyMarquee {juce::Rectangle<int>(start, start), start, base, selectedKeys};
            selectedKeys = std::move(base);
        }
        repaint();
        return;
    }
    if (event.mods.isPopupMenu()) {
        int row = 0;
        const auto* clip = clipAt(event.getPosition(), row);
        if (clip != nullptr) {
            const auto id = clip->id;
            if (!selectedClips.contains(id)) {
                selectClip(id);
            } else {
                notifySelection(id);
            }
            showClipMenu(id);
        } else { showSpaceMenu(); }
        return;
    }
    if (!event.mods.isLeftButtonDown()) {
        return;
    }
    if (event.y < rulerHeight && event.x >= namesWidth) {
        gesture = Scrub {};
        seek(event.x, event.mods);
        return;
    }
    if (beamDown(event)) { return; }
    const auto group = groupAtY(event.y);
    if (group != 0) { selectClip(group); return; }
    const auto visible = visualRowAt(event.y);
    if (visible >= 0 && visible < static_cast<int>(rows.size()) && rows[static_cast<std::size_t>(visible)].composition() && event.x >= namesWidth) { showCompositionEffects(); return; }
    int row = 0;
    const auto* clip = clipAt(event.getPosition(), row);
    if (clip == nullptr) {
        // Empty space: drag a box to select clips (Shift or Cmd adds).
        auto base = event.mods.isShiftDown() || event.mods.isCommandDown() ? selectedClips : std::set<motion::Id>{};
        if (base.empty()) { selectClip(0); }
        if (event.x >= namesWidth) { gesture = ClipMarquee {juce::Rectangle<int>(event.getPosition(), event.getPosition()), event.getPosition(), std::move(base)}; }
        return;
    }
    // Shift takes the run from the clip clicked last to this one: every clip
    // on the tracks between them, within their times (with Cmd too, added to
    // the selection). Cmd alone adds or removes one.
    if (event.mods.isShiftDown() && clipAnchor != 0 && clipAnchor != clip->id && selectRange(clipAnchor, clip->id, event.mods.isCommandDown())) { return; }
    if (event.mods.isShiftDown() || event.mods.isCommandDown()) {
        if (selectedClips.contains(clip->id)) {
            selectedClips.erase(clip->id);
        } else {
            selectedClips.insert(clip->id);
        }
        clipAnchor = clip->id;
        notifySelection(selectedClips.contains(clip->id) ? clip->id : (selectedClips.empty() ? 0 : *selectedClips.begin()));
        return;
    }
    clipAnchor = clip->id;
    if (processor.document.project().tracks[row].locked) { selectClip(clip->id); return; }
    const auto id = clip->id;
    const auto bounds = clipBounds(*clip, row);
    if (tool == Tool::ripple && event.x >= bounds.getX() + 8 && event.x <= bounds.getRight() - 8) {
        selectClip(id);
        return;
    }
    ClipDrag drag;
    drag.original = *clip;
    drag.originalRow = row;
    drag.downX = event.x;
    drag.mode = tool == Tool::slip ? Mode::slip : (tool == Tool::stretch ? Mode::stretch
        : (event.x < bounds.getX() + 8 ? Mode::left : (event.x > bounds.getRight() - 8 ? Mode::right : Mode::move)));
    if (tool == Tool::ripple) { drag.mode = drag.mode == Mode::left ? Mode::rippleLeft : Mode::rippleRight; }
    drag.before = processor.document.project();
    drag.revision = processor.document.revision();
    // Moving, trimming, slipping and stretching take every selected clip
    // along; a ripple trim shifts what follows, so takes one.
    const auto together = drag.mode != Mode::rippleLeft && drag.mode != Mode::rippleRight;
    gesture = std::move(drag);
    if (!selectedClips.contains(id) || !together) {
        selectClip(id);
    } else {
        notifySelection(id);
    }
}

void MotionTimelineView::mouseMove(const juce::MouseEvent& event) {
    if (onNamesEdge(event)) {
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
        return;
    }
    const auto overAdd = addCameraBounds().contains(event.getPosition());
    if (overAdd != addHover) { addHover = overAdd; repaint(addCameraBounds()); }
    if (overAdd) {
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
        setTooltip("Add a camera at the playhead");
        return;
    }
    const auto bar = barAt(event.getPosition());
    if (bar != hoveredBar) { hoveredBar = bar; repaint(); }
    if (bar != 0) {
        const auto thumb = horizontalThumb();
        setMouseCursor(bar == 1 && (std::abs(event.x - thumb.getX()) <= 5 || std::abs(event.x - thumb.getRight()) <= 5) ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
        setTooltip(bar == 1 ? "Drag either end to zoom" : juce::String());
        return;
    }
    const auto edgeRow = rowEdgeAt(event.getPosition());
    if (edgeRow != hoveredEdge) { hoveredEdge = edgeRow; repaint(); }
    const auto loop = loopBounds();
    if (!loop.isEmpty() && event.y >= loopTop - 4 && event.y < loopTop + loopHeight && event.x >= loop.getX() - 5 && event.x <= loop.getRight() + 5) {
        const auto edge = std::abs(event.x - loop.getX()) <= 5 || std::abs(event.x - loop.getRight()) <= 5;
        setMouseCursor(edge ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::DraggingHandCursor);
        setTooltip("Double-click to switch looping on or off");
        return;
    }
    if (edgeRow >= 0) {
        setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
        setTooltip("Double-click for the default height; Alt+wheel resizes every track");
        return;
    }
    const auto beamTarget = beamEdgeAt(event.getPosition());
    const auto beamId = beamTarget.has_value() ? beamTarget->id : 0;
    if (beamId != beamHover || (beamId != 0 && event.x != beamPointer)) {
        beamHover = beamId;
        beamPointer = event.x;
        repaint();
    }
    if (beamTarget.has_value()) {
        setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
        setTooltip("Beam: drag up to give this object more of the beam's time; " + motion::style::shortcutText("Cmd-click") + " adds a key");
        return;
    }
    int row = 0;
    const auto* clip = clipAt(event.getPosition(), row);
    juce::String tip;
    if (clip != nullptr) {
        const auto timing = clip->timing(processor.document.project().tempo());
        const auto grid = processor.document.project().timeGrid();
        tip = juce::String(clip->name) + "\n" + grid.positionLabel(timing.start) + " - " + grid.positionLabel(timing.end()) + "  (" + grid.durationLabel(timing.start, timing.end()) + ")";
    }
    if (getTooltip() != tip) { setTooltip(tip); }
    const auto hovered = clip != nullptr ? clip->id : 0;
    if (hovered != hoveredClip) { hoveredClip = hovered; repaint(); }
    const auto inBand = inCameraBand(event.y) && event.x >= namesWidth;
    const auto* hoveredCut = inBand ? cutAt(event.getPosition()) : nullptr;
    const auto* hoveredMarker = markerAt(event.getPosition());
    if (event.y < rulerHeight && event.x < namesWidth) {
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    } else if (inBand && cameraKeyAt(event.x).has_value()) {
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
    } else if (hoveredCut != nullptr) {
        // As cameraBandDown takes it: an end resizes the shot, the middle moves it.
        const auto bounds = cutBounds(*hoveredCut);
        const auto edge = std::abs(event.x - bounds.getX()) <= 5 || std::abs(event.x - bounds.getRight()) <= 5;
        setMouseCursor(edge ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::DraggingHandCursor);
        setTooltip(cameraName(hoveredCut->camera));
    } else if (hoveredMarker != nullptr) {
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
        setTooltip(hoveredMarker->name + ": double-click to edit");
    } else if (clip != nullptr) {
        const auto bounds = clipBounds(*clip, row);
        const bool edge = event.x < bounds.getX() + 8 || event.x > bounds.getRight() - 8;
        setMouseCursor((tool == Tool::slip || tool == Tool::stretch || edge) ? juce::MouseCursor::LeftRightResizeCursor : (tool == Tool::ripple ? juce::MouseCursor::NormalCursor : juce::MouseCursor::DraggingHandCursor));
    } else {
        setMouseCursor(juce::MouseCursor::NormalCursor);
    }
}

double MotionTimelineView::contentEnd() const {
    const auto& project = processor.document.project();
    auto end = project.duration;
    for (const auto& track : project.tracks) {
        for (const auto& clip : track.clips) { end = std::max(end, clip.timing(project.tempo()).end()); }
    }
    return end;
}

double MotionTimelineView::timelineSpan() const {
    const auto visible = std::max(1, getWidth() - namesWidth) / pixelsPerSecond;
    return std::max({contentEnd() * 1.05, scrollTime + visible, 0.001});
}

juce::Rectangle<int> MotionTimelineView::horizontalThumb() const {
    const auto bar = horizontalBar();
    const auto span = timelineSpan();
    const auto visible = std::max(1, getWidth() - namesWidth) / pixelsPerSecond;
    const auto width = std::max(24, juce::roundToInt(bar.getWidth() * std::min(1.0, visible / span)));
    const auto x = bar.getX() + juce::roundToInt((bar.getWidth() - width) * std::clamp(scrollTime / std::max(1e-9, span - visible), 0.0, 1.0));
    return {x, bar.getY(), std::min(width, bar.getWidth()), bar.getHeight()};
}

juce::Rectangle<int> MotionTimelineView::verticalThumb() const {
    const auto bar = verticalBar();
    const auto total = std::max(1, contentHeight + bottomRoom);
    const auto height = std::max(24, juce::roundToInt(bar.getHeight() * std::min(1.0, static_cast<double>(viewHeight()) / total)));
    const auto y = bar.getY() + juce::roundToInt((bar.getHeight() - height) * std::clamp(static_cast<double>(scrollY) / std::max(1, maximumScrollY()), 0.0, 1.0));
    return {bar.getX(), y, bar.getWidth(), std::min(height, bar.getHeight())};
}

void MotionTimelineView::paintScrollBars(juce::Graphics& g) const {
    const auto draw = [&](juce::Rectangle<int> bar, juce::Rectangle<int> thumb, bool hot) {
        g.setColour(juce::Colours::white.withAlpha(.04f));
        g.fillRoundedRectangle(bar.toFloat(), barSize * .5f);
        g.setColour(juce::Colours::white.withAlpha(hot ? .4f : .2f));
        g.fillRoundedRectangle(thumb.toFloat(), barSize * .5f);
    };
    const auto grabbed = active<BarDrag>() != nullptr;
    if (horizontalBarShown()) { draw(horizontalBar(), horizontalThumb(), grabbed || hoveredBar == 1); }
    if (verticalBarShown()) { draw(verticalBar(), verticalThumb(), grabbed || hoveredBar == 2); }
}

int MotionTimelineView::barAt(juce::Point<int> point) const {
    if (horizontalBarShown() && horizontalBar().expanded(0, 3).contains(point)) { return 1; }
    if (verticalBarShown() && verticalBar().expanded(3, 0).contains(point)) { return 2; }
    return 0;
}

bool MotionTimelineView::barDown(const juce::MouseEvent& event) {
    const auto bar = barAt(event.getPosition());
    if (bar == 0 || !event.mods.isLeftButtonDown()) { return false; }
    if (bar == 1) {
        auto thumb = horizontalThumb();
        if (!thumb.contains(event.x, thumb.getCentreY())) {
            // Click beside the thumb: centre the view there.
            const auto span = timelineSpan();
            const auto visible = std::max(1, getWidth() - namesWidth) / pixelsPerSecond;
            scrollTime = std::max(0.0, (event.x - horizontalBar().getX()) / static_cast<double>(horizontalBar().getWidth()) * span - visible / 2);
            userScrolled();
            thumb = horizontalThumb();
        }
        const auto mode = event.x <= thumb.getX() + 5 ? BarDrag::Mode::zoomLeft : event.x >= thumb.getRight() - 5 ? BarDrag::Mode::zoomRight : BarDrag::Mode::scrollTime;
        gesture = BarDrag {mode, event.x, scrollTime, pixelsPerSecond, scrollY};
    } else {
        if (!verticalThumb().contains(verticalThumb().getCentreX(), event.y)) {
            scrollY = std::clamp(juce::roundToInt((event.y - verticalBar().getY()) / static_cast<double>(std::max(1, verticalBar().getHeight())) * maximumScrollY()), 0, maximumScrollY());
        }
        gesture = BarDrag {BarDrag::Mode::scrollRows, event.y, scrollTime, pixelsPerSecond, scrollY};
    }
    resized();
    repaint();
    return true;
}

void MotionTimelineView::barDragged(const juce::MouseEvent& event) {
    const auto& drag = std::get<BarDrag>(gesture);
    if (drag.mode == BarDrag::Mode::scrollRows) {
        const auto bar = verticalBar(), thumb = verticalThumb();
        const auto travel = std::max(1, bar.getHeight() - thumb.getHeight());
        scrollY = std::clamp(drag.scrollY + juce::roundToInt((event.y - drag.down) / static_cast<double>(travel) * maximumScrollY()), 0, maximumScrollY());
        resized();
        repaint();
        return;
    }
    const auto bar = horizontalBar();
    const auto span = timelineSpan();
    const auto secondsPerPixel = span / std::max(1, bar.getWidth());
    const auto delta = (event.x - drag.down) * secondsPerPixel;
    const auto width = std::max(1, getWidth() - namesWidth);
    const auto visible = width / drag.pixelsPerSecond;
    if (drag.mode == BarDrag::Mode::scrollTime) {
        scrollTime = std::max(0.0, drag.scrollTime + delta);
    } else if (drag.mode == BarDrag::Mode::zoomRight) {
        pixelsPerSecond = clampZoom(width / std::max(1.0 / 120, visible + delta));
        scrollTime = drag.scrollTime;
    } else {
        const auto end = drag.scrollTime + visible;
        const auto start = std::clamp(drag.scrollTime + delta, 0.0, std::max(0.0, end - 1.0 / 120));
        pixelsPerSecond = clampZoom(width / (end - start));
        scrollTime = start;
    }
    userScrolled();
    repaint();
}

juce::Rectangle<int> MotionTimelineView::loopBounds() const {
    const auto& project = processor.document.project();
    if (!project.hasLoop()) { return {}; }
    const auto left = std::max(namesWidth, timeX(project.loopStart)), right = std::min(getWidth(), timeX(project.loopEnd));
    return right > left ? juce::Rectangle<int>(left, loopTop, right - left, loopHeight) : juce::Rectangle<int>();
}

void MotionTimelineView::paintLoop(juce::Graphics& g) const {
    const auto bounds = loopBounds();
    if (bounds.isEmpty()) { return; }
    const auto on = processor.document.project().looping;
    const auto colour = on ? osci::Colours::accentColor() : osci::Colours::textMuted();
    if (on) {
        g.setColour(colour.withAlpha(.05f));
        g.fillRect(bounds.getX(), rulerHeight, bounds.getWidth(), getHeight() - rulerHeight);
    }
    // One opaque shape (bar and end brackets), so nothing overlaps.
    juce::Path brace;
    brace.addRoundedRectangle(bounds.toFloat(), 2.0f);
    brace.addRectangle(static_cast<float>(bounds.getX()), static_cast<float>(loopTop - 4), 2.0f, static_cast<float>(loopHeight + 4));
    brace.addRectangle(static_cast<float>(bounds.getRight() - 2), static_cast<float>(loopTop - 4), 2.0f, static_cast<float>(loopHeight + 4));
    brace.setUsingNonZeroWinding(true);
    g.setColour(osci::Colours::surfaceRaised().interpolatedWith(colour, on ? .8f : .55f));
    g.fillPath(brace);
}

bool MotionTimelineView::loopDown(const juce::MouseEvent& event) {
    const auto bounds = loopBounds();
    if (bounds.isEmpty() || event.y < loopTop - 4 || event.y >= loopTop + loopHeight || event.x < bounds.getX() - 5 || event.x > bounds.getRight() + 5) { return false; }
    const auto& project = processor.document.project();
    if (event.getNumberOfClicks() > 1) {
        processor.document.changeView([](motion::Composition& state) { state.looping = !state.looping; });
        return true;
    }
    const auto mode = std::abs(event.x - bounds.getX()) <= 5 ? LoopDrag::Mode::left : std::abs(event.x - bounds.getRight()) <= 5 ? LoopDrag::Mode::right : LoopDrag::Mode::move;
    gesture = LoopDrag {mode, project.loopStart, project.loopEnd, event.x, project, processor.document.revision(), false};
    return true;
}

bool MotionTimelineView::moveNearestLoopEdge(const juce::MouseEvent& event) {
    const auto& project = processor.document.project();
    if (project.duration <= 0) { return false; }
    const auto frame = project.frameRate > 0 ? 1 / project.frameRate : 1.0 / 30;
    const auto time = std::clamp(snapTime(scrollTime + (event.x - namesWidth) / pixelsPerSecond, event.mods), 0.0, project.duration);
    auto start = project.loopStart, end = project.loopEnd;
    juce::String label;
    if (!project.hasLoop()) {
        const auto playhead = std::clamp(processor.position.load(), 0.0, project.duration);
        start = std::min(time, playhead);
        end = std::abs(time - playhead) >= frame ? std::max(time, playhead) : project.duration;
        label = "Set loop";
    } else if (std::abs(time - start) <= std::abs(time - end)) {
        start = std::min(time, end - frame);
        label = "Move loop start";
    } else {
        end = std::max(time, start + frame);
        label = "Move loop end";
    }
    if (end - start < frame || (start == project.loopStart && end == project.loopEnd)) { return true; }
    const motion::Document::ViewChange view(processor.document);
    processor.document.edit(label, [start, end](motion::Project& state) {
        state.loopStart = start;
        state.loopEnd = end;
    });
    repaint();
    return true;
}

void MotionTimelineView::loopDragged(const juce::MouseEvent& event) {
    auto& drag = std::get<LoopDrag>(gesture);
    if (processor.document.revision() != drag.revision) { gesture = {}; return; }
    const auto delta = (event.x - drag.downX) / pixelsPerSecond;
    auto updated = drag.before;
    const auto length = drag.end - drag.start;
    const auto frame = updated.frameRate > 0 ? 1 / updated.frameRate : 1.0 / 30;
    if (drag.mode == LoopDrag::Mode::move) {
        updated.loopStart = std::clamp(snapEdge(drag.start + delta, event.mods, drag.before, {}), 0.0, std::max(0.0, updated.duration - length));
        updated.loopEnd = updated.loopStart + length;
    } else if (drag.mode == LoopDrag::Mode::left) {
        updated.loopStart = std::clamp(snapEdge(drag.start + delta, event.mods, drag.before, {}), 0.0, std::max(0.0, drag.end - frame));
    } else {
        updated.loopEnd = std::clamp(snapEdge(drag.end + delta, event.mods, drag.before, {}), std::min(drag.start + frame, updated.duration), updated.duration);
    }
    drag.changed = updated.loopStart != drag.before.loopStart || updated.loopEnd != drag.before.loopEnd;
    const motion::Document::ViewChange view(processor.document);
    processor.document.preview(std::move(updated));
    drag.revision = processor.document.revision();
    repaint();
}

void MotionTimelineView::mouseExit(const juce::MouseEvent&) {
    if (hoveredBar != 0) { hoveredBar = 0; repaint(); }
    if (hoveredEdge >= 0) { hoveredEdge = -1; repaint(); }
    if (hoveredClip != 0) { hoveredClip = 0; repaint(); }
    if (beamHover != 0) { beamHover = 0; repaint(); }
    if (addHover) { addHover = false; repaint(); }
}

std::vector<double> MotionTimelineView::editPoints() const {
    std::vector<double> times;
    const auto& project = processor.document.project();
    for (const auto& track : project.tracks) {
        for (const auto& clip : track.clips) {
            const auto timing = clip.timing(project.tempo());
            times.push_back(timing.start);
            times.push_back(timing.end());
        }
    }
    std::sort(times.begin(), times.end());
    return times;
}

double MotionTimelineView::edgeSpeed(int x) const {
    constexpr int zone = 36;
    const auto left = namesWidth + zone - x;
    const auto right = x - (getWidth() - zone);
    const auto depth = std::max(left, right);
    if (depth <= 0) { return 0; }
    // Gentle at the zone's inner edge, quicker at the edge, and quicker
    // still past it.
    const auto inside = std::min(1.0, depth / static_cast<double>(zone));
    const auto speed = std::min(40.0, 1.0 + 11.0 * inside * inside + std::max(0, depth - zone) * 0.4);
    return left > right ? -speed : speed;
}

int* MotionTimelineView::dragOrigin() {
    auto* project = projectDrag();
    auto* key = active<KeyDrag>();
    auto* cameraKey = active<CameraKeyDrag>();
    auto* cut = active<CutDrag>();
    auto* clips = active<ClipMarquee>();
    auto* keys = active<KeyMarquee>();
    return project != nullptr ? &project->downX : key != nullptr ? &key->downX : cameraKey != nullptr ? &cameraKey->downX
         : cut != nullptr ? &cut->downX : clips != nullptr ? &clips->start.x : keys != nullptr ? &keys->start.x : nullptr;
}

void MotionTimelineView::followEdge(const juce::MouseEvent& event) {
    edgeEvent.reset();
    if (!scrollsAtEdge() || edgeSpeed(event.x) == 0) {
        edgeScroll.stopTimer();
        return;
    }
    edgeEvent.emplace(event);
    if (!edgeScroll.isTimerRunning()) { edgeScroll.startTimerHz(60); }
}

void MotionTimelineView::scrollAtEdge() {
    if (!edgeEvent.has_value() || !scrollsAtEdge() || !juce::Desktop::getInstance().getMainMouseSource().isDragging()) {
        edgeScroll.stopTimer();
        return;
    }
    const auto speed = edgeSpeed(edgeEvent->x);
    // Whole pixels, so the drag's origin moves exactly with the tracks; to
    // the right no further than just past the end.
    const auto limit = std::max(scrollTime, timelineSpan() - (getWidth() - namesWidth) / pixelsPerSecond * 0.5);
    const auto target = std::clamp(scrollTime + speed / pixelsPerSecond, 0.0, limit);
    const auto pixels = juce::roundToInt((target - scrollTime) * pixelsPerSecond);
    if (pixels == 0) { return; }
    viewAnimation.stopTimer();
    scrollTime += pixels / pixelsPerSecond;
    auto* origin = dragOrigin();
    if (origin != nullptr) { *origin -= pixels; }
    const auto event = *edgeEvent;
    resized();
    repaint();
    mouseDrag(event);
}

void MotionTimelineView::mouseDrag(const juce::MouseEvent& event) {
    followEdge(event);
    if (active<BeamDrag>() != nullptr) { beamDragged(event); return; }
    if (active<BarDrag>() != nullptr) { barDragged(event); return; }
    if (active<LoopDrag>() != nullptr) { loopDragged(event); return; }
    const auto* heightDrag = active<HeightDrag>();
    if (heightDrag != nullptr) {
        processor.document.setTrackHeight(heightDrag->track, std::clamp(heightDrag->startHeight + event.y - heightDrag->downY, motion::Track::minimumHeight, motion::Track::maximumHeight));
        layoutRows();
        resized();
        repaint();
        return;
    }
    if (active<NamesResize>() != nullptr) {
        namesWidth = std::clamp(event.x, narrowestNames, std::max(narrowestNames, std::min(widestNames, getWidth() / 2)));
        refreshTracks();
        repaint();
        return;
    }
    if (active<CameraKeyDrag>() != nullptr) {
        dragCameraKey(event);
        return;
    }
    if (active<CutDrag>() != nullptr) {
        cameraBandDrag(event);
        return;
    }
    if (active<KeyDrag>() != nullptr) { dragKeys(event.x, event.mods); return; }
    auto* clipMarquee = active<ClipMarquee>();
    if (clipMarquee != nullptr) {
        clipMarquee->area = juce::Rectangle<int>(clipMarquee->start, event.getPosition());
        auto chosen = clipMarquee->base;
        motion::Id first = 0;
        const auto& tracks = processor.document.project().tracks;
        for (int row = firstVisibleRow(); row < static_cast<int>(rows.size()) && rowY(row) < getHeight(); ++row) {
            const auto& item = rows[static_cast<std::size_t>(row)];
            if (item.isLane() || item.track < 0) { continue; }
            for (const auto& clip : tracks[static_cast<std::size_t>(item.track)].clips) {
                if (!clipBounds(clip, item.track).intersects(clipMarquee->area)) { continue; }
                chosen.insert(clip.id);
                if (first == 0) { first = clip.id; }
            }
        }
        if (chosen != selectedClips) {
            selectedKeys.clear();
            selectedClips = chosen;
            // The inspector keeps its clip while it stays in the box.
            notifySelection(chosen.contains(selected) ? selected : first != 0 ? first : (chosen.empty() ? 0 : *chosen.begin()));
        }
        repaint();
        return;
    }
    auto* keyMarquee = active<KeyMarquee>();
    if (keyMarquee != nullptr) {
        keyMarquee->area = juce::Rectangle<int>(keyMarquee->start, event.getPosition());
        selectedKeys = keyMarquee->base;
        for (auto& key : keysInside(keyMarquee->area)) {
            if (!isKeySelected(key.clip, key.property, key.time)) { selectedKeys.push_back(std::move(key)); }
        }
        repaint();
        return;
    }
    if (active<Scrub>() != nullptr) {
        seek(event.x, event.mods);
        return;
    }
    if (!gestureIsCurrent()) {
        return;
    }
    auto* markerDrag = active<MarkerDrag>();
    if (markerDrag != nullptr) {
        const auto id = markerDrag->marker;
        auto updated = markerDrag->before;
        const auto delta = (event.x - markerDrag->downX) / pixelsPerSecond;
        const auto time = delta == 0 ? markerDrag->originalTime : std::clamp(snapEdge(markerDrag->originalTime + delta, event.mods, markerDrag->before, {}, nullptr, id), 0.0, updated.duration);
        if (std::any_of(updated.markers.begin(), updated.markers.end(), [id, time](const auto& marker) { return marker.id != id && motion::sameTime(marker.time, time); })) { return; }
        for (auto& marker : updated.markers) { if (marker.id == id) { marker.time = time; } }
        std::sort(updated.markers.begin(), updated.markers.end(), [](const auto& a, const auto& b) { return a.time != b.time ? a.time < b.time : a.id < b.id; });
        markerDrag->changed = time != markerDrag->originalTime;
        processor.document.preview(std::move(updated));
        markerDrag->revision = processor.document.revision();
        repaint();
        return;
    }
    auto& drag = std::get<ClipDrag>(gesture);
    auto candidate = drag.original;
    const auto timing = drag.original.timing(drag.before.tempo());
    auto edited = timing;
    auto delta = (event.x - drag.downX) / pixelsPerSecond;
    if (delta != 0.0 && !event.mods.isAltDown()) {
        std::set<motion::Id> excluded(selectedClips.begin(), selectedClips.end());
        excluded.insert(drag.original.id);
        if (drag.mode == Mode::slip) {
            const auto anchor = timing.offset / timing.rate;
            delta = drag.before.timeGrid().snap(anchor + delta) - anchor;
        } else if (drag.mode == Mode::move) {
            // Either edge of a moving clip can catch a magnet.
            const auto startTarget = magnet(timing.start + delta, drag.before, excluded);
            const auto endTarget = magnet(timing.end() + delta, drag.before, excluded);
            const auto startDistance = startTarget.has_value() ? std::abs(*startTarget - (timing.start + delta)) : 1.0e300;
            const auto endDistance = endTarget.has_value() ? std::abs(*endTarget - (timing.end() + delta)) : 1.0e300;
            if (startTarget.has_value() && startDistance <= endDistance) {
                delta = *startTarget - timing.start;
                snapGuide = startTarget;
            } else if (endTarget.has_value()) {
                delta = *endTarget - timing.end();
                snapGuide = endTarget;
            } else {
                delta = drag.before.timeGrid().snap(timing.start + delta) - timing.start;
                snapGuide.reset();
            }
        } else {
            const auto anchor = drag.mode == Mode::right || drag.mode == Mode::stretch || drag.mode == Mode::rippleRight ? timing.end() : timing.start;
            delta = snapEdge(anchor + delta, event.mods, drag.before, excluded) - anchor;
        }
    } else {
        snapGuide.reset();
    }
    if (drag.mode == Mode::rippleLeft || drag.mode == Mode::rippleRight) {
        auto updated = drag.before;
        if (!motion::rippleTrim(updated.tracks.change(drag.originalRow), drag.original.id, drag.mode == Mode::rippleLeft, delta, updated.tempo())) { return; }
        for (const auto& item : updated.tracks[drag.originalRow].clips) { updated.duration = std::max(updated.duration, item.timing(updated.tempo()).end()); }
        drag.changed = delta != 0;
        processor.document.preview(std::move(updated));
        drag.revision = processor.document.revision();
        repaint();
        return;
    }
    if (drag.mode == Mode::move && selectedClips.size() > 1) {
        auto updated = drag.before;
        double earliest = timing.start;
        for (const auto& track : updated.tracks) {
            for (const auto& item : track.clips) {
                if (selectedClips.contains(item.id)) { earliest = std::min(earliest, item.timing(updated.tempo()).start); }
            }
        }
        delta = std::max(delta, -earliest);
        const auto row = trackAtY(event.y);
        const auto rows = row >= 0 ? row - drag.originalRow : 0;
        if (rows != 0) {
            const auto sourceY = trackY(drag.originalRow);
            const auto visibleDelta = trackY(row) - sourceY;
            for (int index = 0; index < static_cast<int>(updated.tracks.size()); ++index) {
                const auto& track = updated.tracks[index];
                if (!std::any_of(track.clips.begin(), track.clips.end(), [&](const auto& item) { return selectedClips.contains(item.id); })) { continue; }
                const auto y = trackY(index);
                if (y < rulerHeight || trackAtY(y + visibleDelta) != index + rows) { return; }
            }
        }
        if (!motion::moveClips(updated.tracks, {selectedClips.begin(), selectedClips.end()}, delta, rows, updated.tempo())) { return; }
        for (const auto& track : updated.tracks) {
            for (const auto& item : track.clips) { updated.duration = std::max(updated.duration, item.timing(updated.tempo()).end()); }
        }
        drag.changed = delta != 0 || rows != 0;
        processor.document.preview(std::move(updated));
        drag.revision = processor.document.revision();
        repaint();
        return;
    }
    // A clip's timing with the drag applied to the edge (or contents) held.
    const auto dragged = [&](const auto& from) {
        auto to = from;
        if (drag.mode == Mode::move) {
            to.moveTo(std::max(0.0, from.start + delta));
        } else if (drag.mode == Mode::left) {
            to.setStart(std::max(0.0, from.start + delta));
            to.offset = from.localTime(to.start);
        } else if (drag.mode == Mode::right) {
            to.setEnd(to.end() + delta);
        } else if (drag.mode == Mode::slip) {
            to.offset += delta * from.rate;
        } else {
            to.setEnd(to.end() + delta);
            to.rate *= from.duration() / to.duration();
        }
        return to;
    };
    if (drag.mode != Mode::move && selectedClips.size() > 1 && selectedClips.contains(drag.original.id)) {
        // Every selected clip on an unlocked track by the same time, as the
        // one held: all their start or end edges, contents or speeds.
        auto updated = drag.before;
        std::vector<std::pair<int, motion::Clip>> edits;
        for (int index = 0; index < static_cast<int>(updated.tracks.size()); ++index) {
            const auto& track = updated.tracks[index];
            if (track.locked) { continue; }
            for (const auto& item : track.clips) {
                if (!selectedClips.contains(item.id)) { continue; }
                auto clip = item;
                if (delta != 0.0 && !clip.setTiming(dragged(item.timing(updated.tempo())), updated.tempo())) { return; }
                edits.emplace_back(index, std::move(clip));
            }
        }
        for (const auto& [index, clip] : edits) {
            std::erase_if(updated.tracks.change(index).clips, [id = clip.id](const auto& item) { return item.id == id; });
        }
        for (const auto& [index, clip] : edits) {
            if (!updated.tracks.change(index).insert(clip, updated.tempo())) {
                // Where one would overlap another shows in red; all stay put.
                blocked = clipBounds(clip, index);
                repaint();
                return;
            }
            updated.duration = std::max(updated.duration, clip.timing(updated.tempo()).end());
        }
        blocked.reset();
        drag.changed = delta != 0.0;
        processor.document.preview(std::move(updated));
        drag.revision = processor.document.revision();
        repaint();
        return;
    }
    if (delta != 0.0) {
        edited = dragged(timing);
        if (!candidate.setTiming(edited, drag.before.tempo())) { return; }
    }
    const auto target = drag.mode == Mode::move
        ? (trackAtY(event.y) >= 0 ? trackAtY(event.y) : drag.originalRow) : drag.originalRow;
    if (drag.before.tracks[target].locked || drag.before.tracks[target].kind != drag.before.tracks[drag.originalRow].kind) { return; }
    const bool candidateChanged = candidate.start != drag.original.start || candidate.duration != drag.original.duration
        || candidate.offset != drag.original.offset || candidate.rate != drag.original.rate || target != drag.originalRow;
    if (!candidateChanged) {
        if (drag.changed) {
            processor.document.preview(drag.before);
            drag.revision = processor.document.revision();
            drag.changed = false;
            repaint();
        }
        return;
    }
    auto updated = drag.before;
    std::erase_if(updated.tracks.change(drag.originalRow).clips, [&](const auto& clip) { return clip.id == drag.original.id; });
    if (updated.tracks.change(target).insert(candidate, updated.tempo())) {
        updated.duration = std::max(updated.duration, candidate.timing(updated.tempo()).end());
        drag.changed = true;
        blocked.reset();
        processor.document.preview(std::move(updated));
        drag.revision = processor.document.revision();
        repaint();
    } else {
        // Where the clip would land shows in red; it stays at the last free spot.
        blocked = clipBounds(candidate, target);
        repaint();
    }
}

void MotionTimelineView::mouseUp(const juce::MouseEvent&) {
    edgeScroll.stopTimer();
    edgeEvent.reset();
    snapGuide.reset();
    if (blocked.has_value()) {
        blocked.reset();
        repaint();
    }
    if (active<HeightDrag>() != nullptr) { gesture = {}; return; }
    auto* beamDrag = active<BeamDrag>();
    if (beamDrag != nullptr) {
        if (beamDrag->changed && processor.document.revision() == beamDrag->revision) {
            processor.document.commit("Change Beam", std::move(beamDrag->before));
        }
        gesture = {};
        repaint();
        return;
    }
    if (active<BarDrag>() != nullptr) { gesture = {}; repaint(); return; }
    auto* loopDrag = active<LoopDrag>();
    if (loopDrag != nullptr) {
        if (loopDrag->changed && processor.document.revision() == loopDrag->revision) {
            const motion::Document::ViewChange view(processor.document);
            processor.document.commit("Move loop", std::move(loopDrag->before));
        }
        gesture = {};
        return;
    }
    if (active<NamesResize>() != nullptr) {
        gesture = {};
        return;
    }
    auto* cameraKeyDrag = active<CameraKeyDrag>();
    if (cameraKeyDrag != nullptr) {
        if (cameraKeyDrag->moved && processor.document.revision() == cameraKeyDrag->revision) {
            processor.document.commit("Move camera key", std::move(cameraKeyDrag->before));
        }
        gesture = {};
        repaint();
        return;
    }
    auto* cutDrag = active<CutDrag>();
    if (cutDrag != nullptr) {
        if (cutDrag->changed && processor.document.revision() == cutDrag->revision) {
            processor.document.commit(cutDrag->mode == CutDrag::Mode::move ? "Move camera cut" : "Trim camera cut", std::move(cutDrag->before));
        }
        gesture = {};
        repaint();
        return;
    }
    if (active<KeyDrag>() != nullptr) { endKeyDrag(); repaint(); return; }
    if (active<KeyMarquee>() != nullptr || active<ClipMarquee>() != nullptr) { gesture = {}; repaint(); return; }
    if (active<Scrub>() != nullptr) { gesture = {}; return; }
    if (!gestureIsCurrent()) {
        return;
    }
    auto& drag = *projectDrag();
    auto originalProject = std::move(drag.before);
    const auto changed = drag.changed;
    const auto* clipDrag = active<ClipDrag>();
    const auto marker = clipDrag == nullptr;
    const auto mode = marker ? Mode::move : clipDrag->mode;
    gesture = {};
    if (changed) {
        const auto several = selectedClips.size() > 1 && mode != Mode::rippleLeft && mode != Mode::rippleRight;
        const juce::String action = mode == Mode::move ? "Move clip" : mode == Mode::slip ? "Slip clip" : mode == Mode::stretch ? "Stretch clip" : (mode == Mode::rippleLeft || mode == Mode::rippleRight) ? "Ripple trim clip" : "Trim clip";
        const auto label = marker ? juce::String("Move marker") : action + (several ? "s" : "");
        processor.document.commit(label, std::move(originalProject));
    }
}

void MotionTimelineView::mouseDoubleClick(const juce::MouseEvent& event) {
    cancelGesture();
    const auto edge = rowEdgeAt(event.getPosition());
    if (edge >= 0) {
        // Double-click a row's bottom edge: back to the default height.
        if (active<HeightDrag>() != nullptr) { gesture = {}; }
        processor.document.setTrackHeight(rows[static_cast<std::size_t>(edge)].id, 0);
        layoutRows();
        resized();
        repaint();
        return;
    }
    const auto* lane = event.y >= rulerHeight ? laneAtY(event.y) : nullptr;
    if (lane != nullptr) {
        if (event.x >= namesWidth && !keyAt(event.getPosition()).has_value()) { addKeyAt(*lane, event.x, event.mods); }
        return;
    }
    const auto* marker = markerAt(event.getPosition());
    if (marker != nullptr && onEditMarker) { onEditMarker(marker->id, marker->time); return; }
    int row = 0;
    const auto* clip = clipAt(event.getPosition(), row);
    if (clip != nullptr && clip->composition != 0 && onEnterComposition) { onEnterComposition(clip->id); return; }
    // Other clips open their source's editor (text, Lua or a drawing).
    if (clip != nullptr) {
        if (onOpenSource) { onOpenSource(clip->id); }
        return;
    }
    // Empty track space: the source chosen in Sources goes there, on that
    // track (below the tracks, on a new one).
    if (!dropTarget(event.getPosition()).has_value() || !chosenSource) { return; }
    const auto source = chosenSource();
    if (source == 0) {
        if (onHint) { onHint("Choose a source in Sources, then double-click here to add it."); }
        return;
    }
    if (!insertAsset(source, event.x, event.y) && onHint) { onHint("It doesn't fit there: another clip is in the way, or the track is locked or for audio."); }
}

// Whether a drag of any kind is under way: zooming or scrolling then would
// move what the drag measures from.
bool MotionTimelineView::dragging() const {
    return !std::holds_alternative<std::monostate>(gesture);
}

void MotionTimelineView::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) {
    ensureTrackRows();
    if (dragging()) { return; }
    using Gesture = motion::ui::WheelGesture;
    const Gesture gesture(event.mods, wheel);
    if (gesture.kind == Gesture::Kind::zoomTime) {
        zoomAround(event.x, gesture.factor);
    } else if (gesture.kind == Gesture::Kind::scaleOther) {
        scaleTrackHeights(event.y, gesture.factor);
    } else {
        if (gesture.dx != 0) {
            scrollTime = std::max(0.0, scrollTime - gesture.dx * Gesture::pixelsPerUnit / pixelsPerSecond);
            userScrolled();
        }
        if (gesture.dy != 0) { scrollY = std::clamp(scrollY - juce::roundToInt(gesture.dy * Gesture::pixelsPerUnit), 0, maximumScrollY()); }
    }
    resized();
    repaint();
}

void MotionTimelineView::mouseMagnify(const juce::MouseEvent& event, float scale) {
    if (dragging() || !(scale > 0)) { return; }
    zoomAround(event.x, scale);
}

void MotionTimelineView::zoomAround(int x, double factor) {
    const auto anchorX = std::clamp(x - namesWidth, 0, std::max(0, getWidth() - namesWidth));
    const auto anchorTime = scrollTime + anchorX / pixelsPerSecond;
    pixelsPerSecond = clampZoom(pixelsPerSecond * factor);
    scrollTime = std::max(0.0, anchorTime - anchorX / pixelsPerSecond);
    userScrolled();
    repaint();
}

void MotionTimelineView::userScrolled() {
    viewAnimation.stopTimer();
    follow.userMoved();
}

void MotionTimelineView::scaleTrackHeights(int y, double factor) {
    const auto row = visualRowAt(y);
    const auto offset = row >= 0 && row < static_cast<int>(rows.size()) ? rowY(row) : 0;
    const auto next = std::clamp(juce::roundToInt(defaultTrackHeight * factor), motion::Track::minimumHeight, 120);
    if (next == defaultTrackHeight) { return; }
    // Tracks with their own height scale with the rest (collected first:
    // setting heights replaces the project being read).
    const auto ratio = static_cast<double>(next) / defaultTrackHeight;
    std::vector<std::pair<motion::Id, int>> heights;
    for (const auto& track : processor.document.project().tracks) {
        if (track.height > 0) { heights.emplace_back(track.id, juce::roundToInt(track.height * ratio)); }
    }
    for (const auto& group : processor.document.project().groups) {
        if (group.height > 0) { heights.emplace_back(group.id, juce::roundToInt(group.height * ratio)); }
    }
    processor.document.setTrackHeights(heights);
    setDefaultTrackHeight(next);
    if (row >= 0 && row < static_cast<int>(rows.size())) { scrollY = std::clamp(scrollY + rowY(row) - offset, 0, maximumScrollY()); }
}

void MotionTimelineView::setLayout(const Layout& next) {
    namesWidth = std::clamp(next.namesWidth, narrowestNames, widestNames);
    defaultTrackHeight = std::clamp(next.trackHeight, motion::Track::minimumHeight, 120);
    followEnabled = next.follow;
    layoutRows();
    resized();
    repaint();
}

void MotionTimelineView::setDefaultTrackHeight(int height) {
    defaultTrackHeight = std::clamp(height, motion::Track::minimumHeight, 120);
    layoutRows();
    if (onDefaultTrackHeight) { onDefaultTrackHeight(defaultTrackHeight); }
    resized();
    repaint();
}

void MotionTimelineView::followPlayhead(double time, bool playing) {
    const auto x = timeX(time);
    const auto visible = x >= namesWidth && x <= getWidth() - 16;
    if (!follow.update(playing, visible) || !followEnabled || clipGesture() || getWidth() <= namesWidth) { return; }
    scrollTime = std::max(0.0, time - 16 / pixelsPerSecond);
    repaint();
}

bool MotionTimelineView::keyPressed(const juce::KeyPress& key) {
    ensureTrackRows();
    // Space, Cmd+D and the rest are the editor's commands, whichever panel
    // has focus: only what is the timeline's own is handled here.
    const auto escape = key == juce::KeyPress::escapeKey;
    if (escape && (active<KeyDrag>() != nullptr || active<KeyMarquee>() != nullptr)) {
        cancelKeyDrag();
        return true;
    }
    auto* cutDrag = active<CutDrag>();
    if (escape && cutDrag != nullptr) {
        if (cutDrag->changed && processor.document.revision() == cutDrag->revision) { processor.document.preview(std::move(cutDrag->before)); }
        gesture = {};
        repaint();
        return true;
    }
    // Escape puts back what a beam share or camera key drag changed.
    auto* beamDrag = active<BeamDrag>();
    if (escape && beamDrag != nullptr) {
        if (beamDrag->changed && processor.document.revision() == beamDrag->revision) { processor.document.preview(std::move(beamDrag->before)); }
        gesture = {};
        repaint();
        return true;
    }
    auto* cameraKeyDrag = active<CameraKeyDrag>();
    if (escape && cameraKeyDrag != nullptr) {
        if (processor.document.revision() == cameraKeyDrag->revision && std::abs(cameraKeyDrag->to - cameraKeyDrag->from) > 1.0e-9) {
            processor.document.preview(std::move(cameraKeyDrag->before));
            selectedCameraKey = std::make_pair(cameraKeyDrag->camera, cameraKeyDrag->from);
        }
        gesture = {};
        repaint();
        return true;
    }
    auto* loopDrag = active<LoopDrag>();
    if (escape && loopDrag != nullptr) {
        if (loopDrag->changed && processor.document.revision() == loopDrag->revision) { processor.document.preview(std::move(loopDrag->before)); }
        gesture = {};
        return true;
    }
    const auto* heightDrag = active<HeightDrag>();
    if (escape && heightDrag != nullptr) {
        processor.document.setTrackHeight(heightDrag->track, heightDrag->original);
        gesture = {};
        layoutRows();
        resized();
        repaint();
        return true;
    }
    auto* clipMarquee = active<ClipMarquee>();
    if (escape && clipMarquee != nullptr) {
        selectedClips = std::move(clipMarquee->base);
        gesture = {};
        notifySelection(selectedClips.empty() ? 0 : *selectedClips.begin());
        return true;
    }
    if ((key.getKeyCode() == juce::KeyPress::deleteKey || key.getKeyCode() == juce::KeyPress::backspaceKey) && selectedCameraKey.has_value()) {
        deleteCameraKey(selectedCameraKey->first, selectedCameraKey->second);
        selectedCameraKey.reset();
        repaint();
        return true;
    }
    if ((key.getKeyCode() == juce::KeyPress::deleteKey || key.getKeyCode() == juce::KeyPress::backspaceKey) && selectedCut != 0) {
        report(processor.document.removeCut(selectedCut));
        selectedCut = 0;
        repaint();
        return true;
    }
    if ((key.getKeyCode() == juce::KeyPress::deleteKey || key.getKeyCode() == juce::KeyPress::backspaceKey) && !selectedKeys.empty()) {
        deleteSelectedKeys();
        return true;
    }
    const auto arrow = key.getKeyCode() == juce::KeyPress::leftKey ? -1 : key.getKeyCode() == juce::KeyPress::rightKey ? 1 : 0;
    if (arrow != 0 && key.getModifiers().isAltDown() && !key.getModifiers().isCommandDown()) {
        const auto frames = arrow * (key.getModifiers().isShiftDown() ? 10 : 1);
        if (!selectedKeys.empty()) { return nudgeSelectedKeys(frames); }
        if (!selectedClips.empty()) { return nudgeSelectedClips(frames); }
    }
    if (escape && clipGesture()) {
        cancelGesture();
        return true;
    }
    if (!key.getModifiers().isCommandDown() && !key.getModifiers().isCtrlDown() && !key.getModifiers().isAltDown() && !key.getModifiers().isShiftDown()) {
        const auto character = juce::CharacterFunctions::toLowerCase(key.getTextCharacter());
        if (character == 'm' && onEditMarker) {
            cancelGesture();
            onEditMarker(0, std::clamp(processor.position.load(), 0.0, processor.document.project().duration));
            return true;
        }
        if (character == 'v' || character == 's' || character == 'r' || character == 'b') {
            setTool(character == 'b' ? Tool::ripple : character == 'v' ? Tool::move : (character == 's' ? Tool::slip : Tool::stretch));
            return true;
        }
        if (character == 'f') {
            fitProject();
            return true;
        }
    }
    if ((key.getKeyCode() == juce::KeyPress::deleteKey || key.getKeyCode() == juce::KeyPress::backspaceKey)
        && !key.getModifiers().isCommandDown() && !key.getModifiers().isCtrlDown() && !key.getModifiers().isAltDown()) {
        cancelGesture();
        if (selectedMarker != 0) {
            const auto result = processor.document.removeMarker(selectedMarker);
            if (result.failed() && onError) { onError(result.getErrorMessage()); }
            selectedMarker = 0;
            return true;
        }
        if (selectedClips.empty()) {
            // Picked track headers go, with their clips.
            const auto tracks = pickedTracks();
            if (tracks.empty()) { return false; }
            deleteTracks(tracks);
            return true;
        }
        deleteSelectedClips(key.getModifiers().isShiftDown());
        return true;
    }
    return false;
}

std::optional<juce::Colour> MotionTimelineView::labelColour(const motion::Track& track) {
    if (track.label <= 0 || track.label >= static_cast<int>(motion::style::trackLabels().size())) { return std::nullopt; }
    return juce::Colour(motion::style::trackLabels()[static_cast<std::size_t>(track.label)].argb);
}

juce::Colour MotionTimelineView::clipColour(const motion::Clip& clip, const motion::Track& track) const {
    const auto label = labelColour(track);
    if (label.has_value()) { return *label; }
    if (track.kind == motion::TrackKind::audio) { return motion::style::audioClip(); }
    if (clip.composition != 0) { return motion::style::compositionClip(); }
    return motion::style::visualClip();
}

std::vector<double> MotionTimelineView::clipKeyTimes(const motion::Clip& clip) const {
    std::vector<double> times;
    const auto timing = clip.timing(processor.document.project().tempo());
    if (!(timing.rate != 0)) { return times; }
    // Its own keys and its effects' alike: each is the clip changing.
    const auto add = [&](const motion::PropertyMap& properties) {
        for (const auto& [name, curve] : properties) {
            for (const auto& key : curve.keyframes()) {
                const auto time = timing.projectTime(key.time);
                if (time >= timing.start - 1.0e-9 && time <= timing.end() + 1.0e-9) { times.push_back(time); }
            }
        }
    };
    add(clip.properties);
    for (const auto& effect : clip.effects) { add(effect.properties); }
    std::sort(times.begin(), times.end());
    times.erase(std::unique(times.begin(), times.end(), [](double a, double b) { return std::abs(a - b) < 1.0e-9; }), times.end());
    return times;
}

void MotionTimelineView::paintClip(juce::Graphics& g, const motion::Clip& clip, const motion::Track& track, int index, float opacity) const {
    const auto& project = processor.document.project();
    const auto bounds = clipBounds(clip, index).toFloat().reduced(1, 3);
    const auto active = selectedClips.contains(clip.id);
    const bool audio = track.kind == motion::TrackKind::audio;
    const auto base = clipColour(clip, track);
    // Selection keeps the clip's colour inside a crisp accent stroke.
    // Hovering lifts it, brightens its edge and shows the trim grips.
    const auto hovered = clip.id == hoveredClip && !active;
    g.setColour((hovered ? base.brighter(.1f) : base).withAlpha(opacity));
    g.fillRoundedRectangle(bounds, motion::style::radius);
    if (active) {
        g.setColour(motion::style::accent().withAlpha(opacity));
        g.drawRoundedRectangle(bounds.reduced(.75f), motion::style::radius, 1.5f);
    } else {
        g.setColour(base.brighter(.6f).withAlpha(opacity * (hovered ? .75f : .35f)));
        g.drawRoundedRectangle(bounds.reduced(.5f), motion::style::radius, 1.0f);
    }
    // Too narrow for anything but its colour.
    if (bounds.getWidth() < 14) { return; }
    if ((hovered || (active && tool == Tool::ripple)) && bounds.getWidth() > 24) {
        g.setColour(active ? motion::style::accent().withAlpha(opacity) : juce::Colours::white.withAlpha(.35f * opacity));
        g.fillRoundedRectangle(bounds.getX() + 3, bounds.getY() + 6, 2, bounds.getHeight() - 12, 1);
        g.fillRoundedRectangle(bounds.getRight() - 5, bounds.getY() + 6, 2, bounds.getHeight() - 12, 1);
    }
    if (!audio) { paintBeamShare(g, clip.id, beamArea(clip, index), bounds, opacity); }
    // Every clip's name sits in the same band at its top, clear of the key
    // ticks along its foot and the waveform below, so names line up along a
    // row.
    // An audio clip's name is a caption on a narrower band, leaving the
    // waveform room below it.
    const auto nameBand = std::min(bounds.getHeight() - 6.0f, audio ? 15.0f : 20.0f);
    const auto keyTimes = clipKeyTimes(clip);
    if (audio) {
        // The waveform fills the clip below its name, as in a DAW's
        // regions; a clip too short for both draws it behind the name.
        const auto& assets = project.assets;
        const auto asset = std::find_if(assets.begin(), assets.end(), [&](const auto& item) { return item->id == clip.asset; });
        if (asset != assets.end() && (*asset)->audio != nullptr) {
            const auto left = std::max(namesWidth, static_cast<int>(bounds.getX()) + 2);
            const auto right = std::min(getWidth(), static_cast<int>(bounds.getRight()) - 2);
            auto wave = bounds.withTrimmedTop(nameBand).withTrimmedBottom(2.0f);
            if (wave.getHeight() < 6.0f) { wave = bounds.reduced(0, 3.0f); }
            const auto centre = wave.getCentreY();
            const auto reach = wave.getHeight() * .5f;
            g.setColour(motion::style::waveform().withAlpha(opacity * .38f));
            for (int x = left; x < right; ++x) {
                const auto time = scrollTime + (x - namesWidth) / pixelsPerSecond;
                const auto end = time + 1.0 / pixelsPerSecond;
                const auto a = (*asset)->audio->querySeconds(0, clip.localTime(time, project.tempo()), clip.localTime(end, project.tempo()));
                const auto b = (*asset)->audio->querySeconds(1, clip.localTime(time, project.tempo()), clip.localTime(end, project.tempo()));
                const auto low = std::clamp(std::min(a.minimum, b.minimum), -1.0f, 1.0f);
                const auto high = std::clamp(std::max(a.maximum, b.maximum), -1.0f, 1.0f);
                g.drawVerticalLine(x, centre - high * reach, centre - low * reach + 0.5f);
            }
        }
    }
    auto label = juce::String(clip.name);
    if (active && tool == Tool::slip) {
        label += "  offset " + juce::String(clip.timing(project.tempo()).offset, 2) + "s";
    } else if (active && tool == Tool::stretch) {
        label += "  " + juce::String(clip.timing(project.tempo()).rate, 2) + "x";
    }
    const auto effects = !clip.effects.empty() && bounds.getWidth() > 90;
    // A Beam other than the even 1x shows beside the name, as its effects do.
    juce::String beamBadge;
    if (!audio) {
        const auto found = clip.properties.find("beam");
        if (found != clip.properties.end() && !found->second.animated() && std::abs(found->second.base - 1) > .005) {
            const auto value = found->second.base;
            const auto places = std::abs(value - std::round(value)) < .005 ? 0 : std::abs(value * 10 - std::round(value * 10)) < .05 ? 1 : 2;
            beamBadge = juce::String(value, places) + juce::String(juce::CharPointer_UTF8("\xc3\x97"));
        }
    }
    const auto badges = bounds.getWidth() > 90 ? beamBadge : juce::String();
    const auto badgeWidth = badges.isEmpty() ? 0.0f : juce::TextLayout::getStringWidth(motion::style::caption(), badges) + 10.0f;
    auto labelBounds = bounds.reduced(8, 0).withTrimmedRight((effects ? 32.0f : 0.0f) + badgeWidth).withHeight(nameBand);
    // A clip starting off to the left keeps its name in view.
    labelBounds.setLeft(std::min(labelBounds.getRight(), std::max(labelBounds.getX(), static_cast<float>(namesWidth) + 8.0f)));
    g.setFont(motion::style::caption());
    if (effects) {
        g.setColour(juce::Colours::white.withAlpha(0.55f * opacity));
        g.drawText(juce::String(static_cast<int>(clip.effects.size())) + " fx", bounds.withLeft(bounds.getRight() - 38).withHeight(labelBounds.getHeight()).withY(labelBounds.getY()), juce::Justification::centred);
    }
    if (badges.isNotEmpty()) {
        g.setColour(juce::Colours::white.withAlpha(0.55f * opacity));
        const auto right = bounds.getRight() - (effects ? 38.0f : 6.0f);
        g.drawText(badges, juce::Rectangle<float>(right - badgeWidth, labelBounds.getY(), badgeWidth, labelBounds.getHeight()), juce::Justification::centred);
    }
    // A name longer than the clip fades out at its edge instead of an ellipsis.
    {
        juce::Graphics::ScopedSaveState clipLabel(g);
        g.reduceClipRegion(labelBounds.toNearestInt());
        g.setFont(audio ? motion::style::caption() : motion::style::body());
        const auto font = g.getCurrentFont();
        const auto textWidth = juce::TextLayout::getStringWidth(font, label);
        const auto fill = (hovered ? base.brighter(.1f) : base).withAlpha(opacity);
        // The name sits on the clip's own colour, so the beam share's edge
        // below never runs through it.
        if (!audio) {
            g.setColour(fill);
            g.fillRoundedRectangle(labelBounds.withX(labelBounds.getX() - 3).withWidth(std::min(textWidth + 6, labelBounds.getWidth() + 3)).withSizeKeepingCentre(std::min(textWidth + 6, labelBounds.getWidth() + 3), font.getHeight() + 2), 3);
        }
        // A muted track's names stay legible (3:1 or better) while its fills dim.
        g.setColour(juce::Colours::white.withAlpha((active ? 1.0f : .88f) * std::max(opacity, .6f)));
        g.drawText(label, labelBounds.withWidth(std::max(labelBounds.getWidth(), 2000.0f)), juce::Justification::centredLeft, false);
        if (textWidth > labelBounds.getWidth()) {
            const auto fade = labelBounds.withLeft(labelBounds.getRight() - 14);
            g.setGradientFill(juce::ColourGradient(fill.withAlpha(0.0f), fade.getX(), 0, fill, fade.getRight(), 0, false));
            g.fillRect(fade);
        }
    }
    // Key summary ticks along the bottom edge, neutral like keys elsewhere.
    g.setColour(motion::style::keyTick().withMultipliedAlpha(opacity));
    double lastX = -10;
    juce::Path ticks;
    for (const auto time : keyTimes) {
        const auto x = static_cast<float>(timeX(time));
        if (x - lastX < 3 || x < bounds.getX() - 2 || x > bounds.getRight() + 2) { continue; }
        // Keys on a clip edge are inset so they are never cut in half.
        motion::style::addDiamond(ticks, {std::clamp(x, bounds.getX() + 6.0f, std::max(bounds.getX() + 6.0f, bounds.getRight() - 6.0f)), bounds.getBottom() - 5.0f}, 3.5f);
        lastX = x;
    }
    g.fillPath(ticks);
}

void MotionTimelineView::paintLane(juce::Graphics& g, const Row& row, int y, int height) const {
    const auto& project = processor.document.project();
    // A lane reads like a channel in the Graph: under its track's header,
    // as light as it, with the axis colour as a dot and on its keys.
    fillCard(g, y, height - bandGap, osci::Colours::surface(), osci::Colours::surfaceSunken());
    const auto colour = row.colour;
    const auto indent = std::min(64, row.depth * 16) + 30;
    g.setColour(colour.withAlpha(.85f));
    g.fillEllipse(juce::Rectangle<float>(6, 6).withCentre({static_cast<float>(indent) + 3.0f, y + (height - bandGap) * .5f}));
    g.setFont(motion::style::caption());
    g.setColour(osci::Colours::textMuted());
    g.drawText(row.label, indent + 12, y, namesWidth - indent - 18, height - bandGap, juce::Justification::centredLeft);
    juce::Graphics::ScopedSaveState scope(g);
    g.reduceClipRegion(namesWidth, y, getWidth() - namesWidth, height);
    const auto centre = y + height * .5f;
    for (const auto owner : row.owners) {
        const auto target = motion::findPropertyTarget(project, owner);
        const auto* curve = target.has_value() ? target->curve(row.property) : nullptr;
        const auto timing = target.has_value() ? target->clock() : motion::ClipTiming();
        if (curve == nullptr || !curve->animated() || timing.rate == 0) { continue; }
        // Tinted as what holds it: its clip, or the composition's slate.
        const auto* clip = laneClip(project, owner);
        const auto* track = clip != nullptr ? motion::findClipTrack(project, clip->id) : nullptr;
        const auto tint = clip != nullptr && track != nullptr ? clipColour(*clip, *track) : motion::style::compositionClip();
        const auto left = timeX(timing.start), right = timeX(timing.end());
        g.setColour(tint.withAlpha(.18f));
        g.fillRect(left, y + 2, std::max(1, right - left), height - 4);
        const auto& keys = curve->keyframes();
        // A faint value curve normalised to the lane shows each move's shape.
        if (keys.size() > 1) {
            const auto first = std::max(left, namesWidth), last = std::min(right, getWidth());
            double low = std::numeric_limits<double>::max(), high = std::numeric_limits<double>::lowest();
            for (const auto& key : keys) { low = std::min(low, key.value); high = std::max(high, key.value); }
            const auto localAt = [&](int x) { return timing.localTime(scrollTime + (x - namesWidth) / pixelsPerSecond); };
            for (int x = first; x < last; x += 3) {
                const auto value = curve->evaluateBase(localAt(x));
                low = std::min(low, value);
                high = std::max(high, value);
            }
            if (high > low && last > first) {
                juce::Path shape;
                const auto yOf = [&](double value) { return static_cast<float>(y + height - 4 - (value - low) / (high - low) * (height - 8)); };
                shape.startNewSubPath(static_cast<float>(first), yOf(curve->evaluateBase(localAt(first))));
                for (int x = first + 2; x < last; x += 2) { shape.lineTo(static_cast<float>(x), yOf(curve->evaluateBase(localAt(x)))); }
                g.setColour(colour.withAlpha(.4f));
                g.strokePath(shape, juce::PathStrokeType(1.0f));
            }
        }
        // Key shape encodes its outgoing interpolation, as in the Graph.
        const auto behind = osci::Colours::surfaceSunken().interpolatedWith(tint, .18f);
        for (std::size_t k = 0; k < keys.size(); ++k) {
            const auto time = timing.projectTime(keys[k].time);
            // A key at the very start stays clear of the name column.
            const auto x = std::max(static_cast<float>(timeX(time)), static_cast<float>(namesWidth) + 4.5f);
            motion::style::drawKey(g, {x, centre}, 4.5f, keys[k], colour, isKeySelected(owner, row.property, keys[k].time), false, behind);
        }
    }
}

const motion::Clip* MotionTimelineView::laneClip(const motion::Project& project, motion::Id owner) {
    for (const auto& track : project.tracks) {
        for (const auto& clip : track.clips) {
            if (clip.id == owner || std::any_of(clip.effects.begin(), clip.effects.end(), [owner](const auto& effect) { return effect.id == owner; })) { return &clip; }
        }
    }
    return nullptr;
}

void MotionTimelineView::deleteSelectedClips(bool ripple) {
    cancelGesture();
    const auto result = processor.document.removeClips({selectedClips.begin(), selectedClips.end()}, ripple);
    if (result.failed()) { if (onError) { onError(result.getErrorMessage()); } return; }
    selectClip(0);
    refreshTracks();
}

void MotionTimelineView::duplicateClip(motion::Id id) {
    cancelGesture();
    std::vector<motion::Id> duplicates;
    const auto sources = selectedClips.contains(id) ? std::vector<motion::Id>(selectedClips.begin(), selectedClips.end()) : std::vector<motion::Id>{id};
    const auto result = processor.document.duplicateClips(sources, duplicates);
    if (result.failed()) { if (onError) { onError(result.getErrorMessage()); } return; }
    selectedClips = {duplicates.begin(), duplicates.end()};
    const auto duplicate = duplicates.front();
    notifySelection(duplicate);
    revealSelection();
    for (const auto& track : processor.document.project().tracks) {
        for (const auto& clip : track.clips) {
            if (clip.id == duplicate) { revealTime(clip.timing(processor.document.project().tempo()).start); return; }
        }
    }
}

void MotionTimelineView::paintRow(juce::Graphics& g, int visible) {
    const auto& project = processor.document.project();
    const auto& tracks = project.tracks;
    const auto y = rowY(visible);
    const auto& row = rows[static_cast<std::size_t>(visible)];
    const auto height = heightOf(static_cast<std::size_t>(visible));
    if (row.isLane()) {
        paintLane(g, row, y, height);
        return;
    }
    if (row.composition()) {
        paintCompositionRow(g, y, height);
        return;
    }
    // Rows are flush, like the Graph's channel list: a hairline gap between
    // them, alternate lanes faintly lighter, and a label colour tinting the
    // track. A selected row lifts to a neutral fill with an accent bar; the
    // track of a selected clip shows a fainter bar.
    const auto selectedRow = row.id == selected || selectedTracks.contains(row.id);
    const auto cardHeight = height - bandGap;
    const auto index = row.track;
    const auto owning = index >= 0 && std::any_of(tracks[static_cast<std::size_t>(index)].clips.begin(), tracks[static_cast<std::size_t>(index)].clips.end(), [this](const auto& clip) { return selectedClips.contains(clip.id); });
    const auto label = index >= 0 ? labelColour(tracks[static_cast<std::size_t>(index)]) : std::nullopt;
    // A group's own row is a raised slate band, so groups read as containers.
    auto card = index < 0 ? osci::Colours::surfaceRaised().interpolatedWith(motion::style::compositionClip(), .55f) : osci::Colours::surface();
    if (label.has_value()) { card = card.interpolatedWith(*label, .3f); }
    if (selectedRow || owning) { card = osci::Colours::surfaceRaised(); }
    auto lane = visible % 2 == 1 ? std::optional<juce::Colour>(juce::Colours::white.withAlpha(.018f)) : std::nullopt;
    if (label.has_value()) { lane = label->withAlpha(.12f); }
    fillCard(g, y, cardHeight, card, lane);
    if (selectedRow || owning) {
        g.setColour(motion::style::accent().withAlpha(selectedRow ? 1.0f : .45f));
        g.fillRect(0, y, 2, cardHeight);
    }
    // Members of a group hang from a rail in the group's slate, one per level,
    // running through the gaps so a group's rows read as one block.
    for (int level = 0; level < std::min(row.depth, 4); ++level) {
        g.setColour(motion::style::compositionClip().brighter(.7f).withAlpha(.75f));
        // Under the group's fold triangle.
        g.fillRect(cardInset + 10 + level * 16, y - bandGap, 2, height);
    }
    if (index < 0) {
        g.setColour(motion::style::compositionClip().withAlpha(0.45f));
        g.fillRect(namesWidth, y, getWidth() - namesWidth, cardHeight);
        juce::Graphics::ScopedSaveState summaryScope(g);
        g.reduceClipRegion(namesWidth, y, getWidth() - namesWidth, height);
        // Its share of the beam: everything it holds.
        const auto area = juce::Rectangle<float>(static_cast<float>(namesWidth), static_cast<float>(y), static_cast<float>(getWidth() - namesWidth), static_cast<float>(cardHeight)).reduced(0, 4);
        paintBeamShare(g, row.id, area, area.expanded(0, 4), 1.0f);
        // Its own keys along its foot, as a clip's are.
        const auto* folder = motion::findGroup(project, row.id);
        if (folder != nullptr) {
            std::set<double> times;
            const auto add = [&times](const motion::PropertyMap& properties) {
                for (const auto& [name, curve] : properties) {
                    for (const auto& key : curve.keyframes()) { times.insert(key.time); }
                }
            };
            add(folder->properties);
            for (const auto& effect : folder->effects) { add(effect.properties); }
            juce::Path ticks;
            double lastX = -10;
            for (const auto time : times) {
                const auto x = static_cast<double>(timeX(time));
                if (x - lastX < 3 || x < namesWidth - 4 || x > getWidth() + 4) { continue; }
                motion::style::addDiamond(ticks, {static_cast<float>(x), static_cast<float>(y + cardHeight) - 5.0f}, 3.5f);
                lastX = x;
            }
            g.setColour(motion::style::keyTick());
            g.fillPath(ticks);
        }
        for (const auto& track : tracks) {
            auto parent = track.group;
            for (std::size_t depth = 0; parent != 0 && depth < motion::maximumGroupDepth; ++depth) {
                if (parent == row.id) {
                    // Members' spans as brackets in the group's slate, so they
                    // never read as a scrollbar.
                    g.setColour(motion::style::compositionClip().brighter(.6f).withAlpha(motion::trackIsAudible(project, track) ? .9f : .35f));
                    const auto middle = y + cardHeight * 0.5f;
                    for (const auto& clip : track.clips) {
                        const auto timing = clip.timing(project.tempo());
                        const auto left = static_cast<float>(timeX(timing.start)), width = static_cast<float>(std::max(3, boundedPixel(timing.duration() * pixelsPerSecond)));
                        g.fillRect(juce::Rectangle<float>(left, middle - 1.5f, width, 3.0f));
                        g.fillRect(juce::Rectangle<float>(left, middle - 5.0f, 1.5f, 10.0f));
                        g.fillRect(juce::Rectangle<float>(left + width - 1.5f, middle - 5.0f, 1.5f, 10.0f));
                    }
                    break;
                }
                const auto* group = motion::findGroup(project, parent);
                parent = group != nullptr ? group->parent : 0;
            }
        }
        return;
    }
    juce::Graphics::ScopedSaveState scope(g);
    g.reduceClipRegion(namesWidth, y, getWidth() - namesWidth, height);
    const auto opacity = !motion::trackIsAudible(project, tracks[static_cast<std::size_t>(index)]) ? 0.38f : 1.0f;
    for (const auto& clip : tracks[static_cast<std::size_t>(index)].clips) {
        // Only clips in the repainted region: during playback, the playhead's strip.
        if (!g.clipRegionIntersects(clipBounds(clip, index))) { continue; }
        paintClip(g, clip, tracks[static_cast<std::size_t>(index)], index, opacity);
    }
}

void MotionTimelineView::fillCard(juce::Graphics& g, int y, int height, juce::Colour header, std::optional<juce::Colour> lane) const {
    g.setColour(header);
    g.fillRect(0, y, namesWidth - 1, height);
    if (lane.has_value()) {
        g.setColour(*lane);
        g.fillRect(namesWidth, y, getWidth() - namesWidth, height);
    }
}

bool MotionTimelineView::gestureIsCurrent() {
    const auto* drag = projectDrag();
    if (drag == nullptr) {
        return false;
    }
    if (processor.document.revision() != drag->revision) {
        gesture = {};
        repaint();
        return false;
    }
    return true;
}

void MotionTimelineView::cancelGesture() {
    if (gestureIsCurrent()) {
        auto& drag = *projectDrag();
        auto restore = std::move(drag.before);
        const auto changed = drag.changed;
        gesture = {};
        if (changed) {
            processor.document.preview(std::move(restore));
        }
    } else if (active<Scrub>() != nullptr) {
        gesture = {};
    }
    repaint();
}

void MotionTimelineView::notifySelection(motion::Id id) {
    selected = id;
    const juce::ScopedValueSetter<bool> notification(notifyingSelection, true);
    if (onSelection) { onSelection(id); }
    repaint();
}

void MotionTimelineView::selectClip(motion::Id id) {
    clearCameraSelection();
    selectedKeys.clear();
    selectedClips.clear();
    if (isClip(id)) { selectedClips.insert(id); }
    notifySelection(id);
}

const motion::Clip* MotionTimelineView::clipAt(juce::Point<int> position, int& row) const {
    if (position.x < namesWidth || position.y < rulerHeight) {
        return nullptr;
    }
    row = trackAtY(position.y);
    const auto& tracks = processor.document.project().tracks;
    if (row < 0 || row >= static_cast<int>(tracks.size())) {
        return nullptr;
    }
    for (const auto& clip : tracks[row].clips) {
        if (clipBounds(clip, row).contains(position)) {
            return &clip;
        }
    }
    return nullptr;
}

int MotionTimelineView::boundedPixel(double value) {
    constexpr double limit = 1000000.0;
    if (!std::isfinite(value)) {
        return std::signbit(value) ? -static_cast<int>(limit) : static_cast<int>(limit);
    }
    return juce::roundToInt(std::clamp(value, -limit, limit));
}

void MotionTimelineView::ensureTrackRows() const {
    // A new project, or another composition entered: other rows, and nothing
    // of the old selection.
    const auto generation = processor.document.scopeGeneration();
    const auto revision = processor.document.revision();
    if (layoutGeneration != generation) {
        collapsedGroups.clear();
        selectedClips.clear();
        selectedKeys.clear();
        selectedTracks.clear();
        selectedCameraKey.reset();
        selected = 0;
        selectedMarker = 0;
        selectedCut = 0;
        trackAnchor = 0;
        layoutGeneration = generation;
        layoutRevision.reset();
    }
    // Any change (an undo among them): what is selected and gone is let go.
    if (!layoutRevision.has_value() || *layoutRevision != revision) {
        const auto& project = processor.document.project();
        rulerHeight = cameraBandTop() + cameraBandHeight + bandGap;
        if (std::none_of(project.markers.begin(), project.markers.end(), [this](const auto& marker) { return marker.id == selectedMarker; })) { selectedMarker = 0; }
        if (std::none_of(project.cameraCuts.begin(), project.cameraCuts.end(), [this](const auto& cut) { return cut.id == selectedCut; })) { selectedCut = 0; }
        if (selectedCameraKey.has_value() && std::none_of(project.cameras.begin(), project.cameras.end(), [this](const auto& camera) { return camera.id == selectedCameraKey->first; })) { selectedCameraKey.reset(); }
        rebuildRows();
        std::erase_if(selectedClips, [this](auto id) { return !isClip(id); });
        std::erase_if(selectedTracks, [&project](auto id) {
            return motion::findGroup(project, id) == nullptr && std::none_of(project.tracks.begin(), project.tracks.end(), [id](const auto& track) { return track.id == id; });
        });
        std::erase_if(selectedKeys, [&project](const KeyRef& key) {
            const auto* clip = motion::findClip(project, key.clip);
            if (clip == nullptr) { return true; }
            const auto found = clip->properties.find(key.property);
            return found == clip->properties.end() || found->second.findKey(key.time) == nullptr;
        });
        layoutRevision = revision;
    }
    scrollY = std::clamp(scrollY, 0, maximumScrollY());
}

void MotionTimelineView::addEffectLanes(std::vector<Row>& lanes, const std::vector<const motion::EffectInstance*>& effects) {
    for (const auto* effect : effects) {
        const auto* definition = motion::effectDefinition(effect->type);
        if (definition == nullptr) { continue; }
        for (const auto& parameter : definition->parameters) {
            const auto found = effect->properties.find(parameter.id);
            if (found == effect->properties.end() || !found->second.animated()) { continue; }
            const auto key = "effect:" + effect->name + ":" + parameter.id;
            auto lane = std::find_if(lanes.begin(), lanes.end(), [&](const auto& item) { return item.lane == key; });
            if (lane == lanes.end()) {
                Row row;
                row.lane = key;
                row.property = parameter.id;
                // Named as Properties' effect cards name them: the effect, then the number.
                row.label = juce::String::fromUTF8(effect->name.c_str()) + motion::style::dot() + juce::String(parameter.name);
                row.colour = motion::style::key();
                lanes.push_back(std::move(row));
                lane = lanes.end() - 1;
            }
            lane->owners.push_back(effect->id);
        }
    }
}

std::vector<MotionTimelineView::Row> MotionTimelineView::trackLanes(const motion::Project& project, const motion::Track& track) {
    std::vector<Row> lanes;
    std::vector<motion::PropertySpec> specs;
    const auto base = track.kind == motion::TrackKind::audio ? std::span<const motion::PropertySpec>(motion::audioPropertySpecs) : motion::objectPropertySpecs;
    specs.assign(base.begin(), base.end());
    if (track.kind == motion::TrackKind::visual) {
        specs.insert(specs.end(), motion::luaSliderSpecs.begin(), motion::luaSliderSpecs.end());
        specs.insert(specs.end(), motion::fractalPropertySpecs.begin(), motion::fractalPropertySpecs.end());
        // Every generator's parameters, each name once.
        for (const auto& generator : motion::generatorDefinitions) {
            for (const auto& spec : generator.parameters) {
                if (motion::findPropertySpec(specs, spec.id) == nullptr) { specs.push_back(spec); }
            }
        }
    }
    for (const auto& spec : specs) {
        const std::string id(spec.id);
        // Shown while a clip keys it; every clip with it can take a key there.
        Row lane;
        std::optional<motion::Id> keyed;
        for (const auto& clip : track.clips) {
            const auto found = clip.properties.find(id);
            if (found == clip.properties.end()) { continue; }
            lane.owners.push_back(clip.id);
            if (!keyed.has_value() && found->second.animated()) { keyed = clip.id; }
        }
        if (!keyed.has_value()) { continue; }
        lane.lane = id;
        lane.property = id;
        // Named as the first clip keying it names it: a generator's numbers
        // and a script's sliders are its own.
        const auto own = motion::specFor(project, *keyed, id);
        const auto& named = own.has_value() ? *own : spec;
        lane.label = juce::String(named.label.data(), named.label.size());
        lane.colour = !named.axis.empty() ? motion::style::axisColour(named.axis) : motion::style::key();
        lanes.push_back(std::move(lane));
    }
    std::vector<const motion::EffectInstance*> effects;
    for (const auto& effect : track.effects) { effects.push_back(&effect); }
    for (const auto& clip : track.clips) {
        for (const auto& effect : clip.effects) { effects.push_back(&effect); }
    }
    addEffectLanes(lanes, effects);
    return lanes;
}

std::vector<MotionTimelineView::Row> MotionTimelineView::compositionLanes(const motion::Project& project) {
    std::vector<Row> lanes;
    std::vector<const motion::EffectInstance*> effects;
    for (const auto& effect : project.effects) { effects.push_back(&effect); }
    addEffectLanes(lanes, effects);
    return lanes;
}

void MotionTimelineView::rebuildRows() const {
    const auto& project = processor.document.project();
    rows.clear();
    std::erase_if(expandedTracks, [&](auto id) { return id != compositionRow && std::none_of(project.tracks.begin(), project.tracks.end(), [id](const auto& track) { return track.id == id; }); });
    const auto addLanes = [&](const motion::TrackRow& base, std::vector<Row> lanes) {
        for (auto& lane : lanes) {
            static_cast<motion::TrackRow&>(lane) = base;
            rows.push_back(std::move(lane));
        }
    };
    // The composition's effects first, as they act on everything below.
    if (!project.effects.empty()) {
        Row row;
        row.id = compositionRow;
        row.track = compositionTrack;
        rows.push_back(row);
        if (expandedTracks.contains(compositionRow)) { addLanes(row, compositionLanes(project)); }
    }
    for (const auto& base : motion::trackRows(project, collapsedGroups)) {
        Row row;
        static_cast<motion::TrackRow&>(row) = base;
        rows.push_back(row);
        if (base.track < 0 || !expandedTracks.contains(base.id)) { continue; }
        addLanes(base, trackLanes(project, project.tracks[static_cast<std::size_t>(base.track)]));
    }
    layoutRows();
}

int MotionTimelineView::heightOf(std::size_t row) const {
    if (rows[row].isLane()) { return laneHeight; }
    const auto& tracks = processor.document.project().tracks;
    const auto index = rows[row].track;
    const auto* group = index < 0 ? motion::findGroup(processor.document.project(), rows[row].id) : nullptr;
    const auto own = group != nullptr ? group->height : index >= 0 && index < static_cast<int>(tracks.size()) ? tracks[static_cast<std::size_t>(index)].height : 0;
    return own > 0 ? own : defaultTrackHeight;
}

int MotionTimelineView::trackIndex(motion::Id id) const {
    const auto& tracks = processor.document.project().tracks;
    const auto found = std::find_if(tracks.begin(), tracks.end(), [id](const auto& track) { return track.id == id; });
    return found == tracks.end() ? -1 : static_cast<int>(found - tracks.begin());
}

int MotionTimelineView::trackHeight(int track) const {
    const auto& tracks = processor.document.project().tracks;
    const auto own = track >= 0 && track < static_cast<int>(tracks.size()) ? tracks[static_cast<std::size_t>(track)].height : 0;
    return own > 0 ? own : defaultTrackHeight;
}

void MotionTimelineView::layoutRows() const {
    rowTops.resize(rows.size());
    int top = 0;
    for (std::size_t row = 0; row < rows.size(); ++row) { rowTops[row] = top; top += heightOf(row); }
    contentHeight = top;
}

int MotionTimelineView::visualRowAt(int y) const {
    ensureTrackRows();
    if (y < rulerHeight) { return -1; }
    const auto content = y - rulerHeight + scrollY;
    if (content >= contentHeight) { return static_cast<int>(rows.size()) + (content - contentHeight) / std::max(1, defaultTrackHeight); }
    const auto found = std::upper_bound(rowTops.begin(), rowTops.end(), content);
    return static_cast<int>(found - rowTops.begin()) - 1;
}

int MotionTimelineView::trackAtY(int y) const {
    const auto row = visualRowAt(y);
    return row >= 0 && row < static_cast<int>(rows.size()) && !rows[static_cast<std::size_t>(row)].isLane() ? std::max(-1, rows[static_cast<std::size_t>(row)].track) : -1;
}

motion::Id MotionTimelineView::groupAtY(int y) const {
    const auto row = visualRowAt(y);
    return row >= 0 && row < static_cast<int>(rows.size()) && rows[static_cast<std::size_t>(row)].group() ? rows[static_cast<std::size_t>(row)].id : 0;
}

const MotionTimelineView::Row* MotionTimelineView::laneAtY(int y) const {
    const auto row = visualRowAt(y);
    return row >= 0 && row < static_cast<int>(rows.size()) && rows[static_cast<std::size_t>(row)].isLane() ? &rows[static_cast<std::size_t>(row)] : nullptr;
}

int MotionTimelineView::trackY(int track) const {
    ensureTrackRows();
    const auto found = std::find_if(rows.begin(), rows.end(), [track](const auto& row) { return row.track == track && !row.isLane(); });
    return found == rows.end() ? -defaultTrackHeight : rowY(static_cast<int>(found - rows.begin()));
}

int MotionTimelineView::rowY(int row) const {
    if (row < 0) { return rulerHeight - scrollY; }
    const auto top = row < static_cast<int>(rowTops.size()) ? rowTops[static_cast<std::size_t>(row)] : contentHeight + (row - static_cast<int>(rowTops.size())) * defaultTrackHeight;
    return rulerHeight - scrollY + top;
}

juce::Rectangle<int> MotionTimelineView::clipBounds(const motion::Clip& clip, int row) const {
    const auto timing = clip.timing(processor.document.project().tempo());
    return { timeX(timing.start), trackY(row), std::max(2, boundedPixel(timing.duration() * pixelsPerSecond)), trackHeight(row) };
}

std::optional<double> MotionTimelineView::magnet(double time, const motion::Project& project, const std::set<motion::Id>& excludedClips, const std::vector<KeyRef>* movingKeys, bool includePlayhead, motion::Id excludedMarker) const {
    // Free mode (grid snapping off) turns magnets off too; Alt bypasses both per drag.
    if (!project.gridSnap) {
        return std::nullopt;
    }
    const auto reach = 8.0 / pixelsPerSecond;
    std::optional<double> best;
    auto bestDistance = reach;
    const auto consider = [&](double candidate) {
        const auto distance = std::abs(candidate - time);
        if (distance < bestDistance) {
            bestDistance = distance;
            best = candidate;
        }
    };
    if (includePlayhead) {
        consider(processor.position.load());
    }
    for (const auto& marker : project.markers) {
        if (marker.id != excludedMarker) {
            consider(marker.time);
        }
    }
    if (project.hasLoop()) {
        consider(project.loopStart);
        consider(project.loopEnd);
    }
    // Camera and group keys run on the composition's time, as do camera cuts.
    for (const auto& camera : project.cameras) {
        for (const auto& [name, curve] : camera.properties) {
            for (const auto& key : curve.keyframes()) { consider(key.time); }
        }
    }
    for (const auto& cut : project.cameraCuts) {
        consider(cut.start);
        consider(cut.end());
    }
    for (const auto& group : project.groups) {
        for (const auto& [name, curve] : group.properties) {
            for (const auto& key : curve.keyframes()) { consider(key.time); }
        }
    }
    for (const auto& track : project.tracks) {
        for (const auto& clip : track.clips) {
            if (excludedClips.contains(clip.id)) { continue; }
            const auto timing = clip.timing(project.tempo());
            consider(timing.start);
            consider(timing.end());
            // Keys count whether or not lanes are open: clips show them as ticks.
            if (timing.rate == 0) { continue; }
            for (const auto& [name, curve] : clip.properties) {
                for (const auto& key : curve.keyframes()) {
                    const bool moving = movingKeys != nullptr && std::any_of(movingKeys->begin(), movingKeys->end(), [&](const auto& item) {
                        return item.clip == clip.id && item.property == name && motion::sameTime(item.time, key.time);
                    });
                    if (!moving) { consider(timing.projectTime(key.time)); }
                }
            }
        }
    }
    return best;
}

double MotionTimelineView::snapEdge(double time, juce::ModifierKeys modifiers, const motion::Project& project, const std::set<motion::Id>& excludedClips, const std::vector<KeyRef>* movingKeys, motion::Id excludedMarker) {
    if (modifiers.isAltDown()) {
        snapGuide.reset();
        return time;
    }
    const auto target = magnet(time, project, excludedClips, movingKeys, true, excludedMarker);
    snapGuide = target;
    return target.value_or(project.timeGrid().snap(time));
}

void MotionTimelineView::seek(int x, juce::ModifierKeys modifiers) {
    // The playhead itself is excluded as a magnet while scrubbing it.
    const auto raw = scrollTime + (x - namesWidth) / pixelsPerSecond;
    const auto& project = processor.document.project();
    // The playhead moves freely and snaps only to things that matter
    // (clip edges, keys, markers, the loop); Alt bypasses even those.
    const auto target = modifiers.isAltDown() ? std::optional<double>() : magnet(raw, project, {}, nullptr, false);
    processor.seek(std::clamp(target.value_or(raw), 0.0, project.duration));
    repaint();
}

void MotionTimelineView::showCompositionEffects() {
    cancelGesture();
    selectClip(0);
    if (onShowCompositionEffects) { onShowCompositionEffects(); }
}

// The composition's effects as one bar across all of it, as an adjustment
// layer would be: their names, and their keys along its foot as a clip's are.
void MotionTimelineView::paintCompositionRow(juce::Graphics& g, int y, int height) const {
    const auto& project = processor.document.project();
    const auto cardHeight = height - bandGap;
    fillCard(g, y, cardHeight, osci::Colours::surfaceRaised().interpolatedWith(motion::style::compositionClip(), .55f), std::nullopt);
    juce::Graphics::ScopedSaveState scope(g);
    g.reduceClipRegion(namesWidth, y, getWidth() - namesWidth, height);
    const auto bounds = juce::Rectangle<float>(static_cast<float>(timeX(0)), static_cast<float>(y), static_cast<float>(std::max(14, timeX(project.duration) - timeX(0))), static_cast<float>(cardHeight)).reduced(1, 3);
    const auto base = motion::style::compositionClip();
    const auto anyOn = std::any_of(project.effects.begin(), project.effects.end(), [](const auto& effect) { return effect.enabled; });
    const auto opacity = anyOn ? 1.0f : .45f;
    g.setColour(base.withAlpha(opacity));
    g.fillRoundedRectangle(bounds, motion::style::radius);
    g.setColour(base.brighter(.6f).withAlpha(opacity * .35f));
    g.drawRoundedRectangle(bounds.reduced(.5f), motion::style::radius, 1.0f);
    // Named by its effects, in order; one switched off reads dimmer.
    juce::StringArray names;
    for (const auto& effect : project.effects) { names.add(juce::String::fromUTF8(effect.name.c_str()) + (effect.enabled ? juce::String() : juce::String(" (off)"))); }
    const auto labelBounds = bounds.reduced(6, 0).withHeight(std::min(bounds.getHeight(), 18.0f));
    // The name stays in view while the start scrolls away, as a clip's does.
    const auto shown = labelBounds.withLeft(std::max(labelBounds.getX(), static_cast<float>(namesWidth) + 6.0f));
    g.setFont(motion::style::body());
    g.setColour(juce::Colours::white.withAlpha(opacity < 1.0f ? .7f : 1.0f));
    g.drawText(names.joinIntoString(", "), shown, juce::Justification::centredLeft, true);
    std::set<double> times;
    for (const auto& effect : project.effects) {
        for (const auto& [name, curve] : effect.properties) {
            for (const auto& key : curve.keyframes()) { times.insert(key.time); }
        }
    }
    juce::Path ticks;
    double lastX = -10;
    for (const auto time : times) {
        const auto x = static_cast<double>(timeX(time));
        if (x - lastX < 3 || x < namesWidth - 4 || x > getWidth() + 4) { continue; }
        motion::style::addDiamond(ticks, {static_cast<float>(x), bounds.getBottom() - 4.0f}, 3.5f);
        lastX = x;
    }
    g.setColour(motion::style::keyTick().withMultipliedAlpha(opacity));
    g.fillPath(ticks);
}

bool MotionTimelineView::selectRange(motion::Id from, motion::Id to, bool adding) {
    const auto& project = processor.document.project();
    const auto tempo = project.tempo();
    // Each clip's track, as the rows list them, and its span.
    const auto locate = [&](motion::Id id) -> std::optional<std::pair<int, motion::ClipTiming>> {
        for (std::size_t row = 0; row < rows.size(); ++row) {
            if (rows[row].isLane() || rows[row].track < 0) { continue; }
            for (const auto& clip : project.tracks[static_cast<std::size_t>(rows[row].track)].clips) {
                if (clip.id == id) { return std::pair {static_cast<int>(row), clip.timing(tempo)}; }
            }
        }
        return std::nullopt;
    };
    const auto first = locate(from), last = locate(to);
    if (!first.has_value() || !last.has_value()) { return false; }
    const auto top = std::min(first->first, last->first), bottom = std::max(first->first, last->first);
    const auto start = std::min(first->second.start, last->second.start), end = std::max(first->second.end(), last->second.end());
    auto picked = adding ? selectedClips : std::set<motion::Id> {};
    for (auto row = top; row <= bottom; ++row) {
        const auto& item = rows[static_cast<std::size_t>(row)];
        if (item.isLane() || item.track < 0) { continue; }
        for (const auto& clip : project.tracks[static_cast<std::size_t>(item.track)].clips) {
            const auto timing = clip.timing(tempo);
            if (timing.start < end && timing.end() > start) { picked.insert(clip.id); }
        }
    }
    selectedClips = std::move(picked);
    notifySelection(to);
    repaint();
    return true;
}

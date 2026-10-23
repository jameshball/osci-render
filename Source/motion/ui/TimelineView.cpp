#include "TimelineView.h"
#include "GrabCursor.h"
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
    // A switch rather than a sixth tool: on, its glyph brightens with no fill
    // behind it, so a filled square only ever marks the tool in use.
    snapButton.onColour = juce::Colours::transparentBlack;
    addAndMakeVisible(snapButton);
    // The editing tools, as in Premiere's tool strip.
    for (auto [button, value, tip] : {std::tuple {&selectTool, Tool::move, "Select, move and trim (V)"}, std::tuple {&slipTool, Tool::slip, "Slip: move the content inside a clip (S)"},
                                      std::tuple {&stretchTool, Tool::stretch, "Stretch: change a clip's length and speed together (R)"}, std::tuple {&rippleTool, Tool::ripple, "Ripple trim: trimming moves the clips after it (B)"}}) {
        button->setTooltip(tip);
        button->iconSize = motion::style::iconMedium;
        button->onClick = [this, value] {
            setTool(value);
        };
        addAndMakeVisible(button);
    }
    snapButton.iconSize = motion::style::iconMedium;
    headerArea.setInterceptsMouseClicks(false, true);
    addAndMakeVisible(headerArea);
    // Add track sits under the last track and scrolls with the list. Its
    // plus lines up with the fold triangles above, its text with the track
    // names.
    addTrack.leading = MotionTrackHeader::edge + MotionTrackHeader::foldWidth / 2 - static_cast<int>(addTrack.iconSize) / 2;
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
    if (picked.empty() && selected != 0 && headerRow(selected) >= 0) { picked.insert(selected); }
    // A deleted anchor, or one hidden in a collapsed group, would never end the range.
    const auto anchorListed = trackAnchor != 0 && headerRow(trackAnchor) >= 0;
    if (mods.isCommandDown()) {
        if (picked.contains(id) && picked.size() > 1) { picked.erase(id); } else { picked.insert(id); }
    } else if (mods.isShiftDown() && anchorListed) {
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
    if (!mods.isShiftDown() || !anchorListed) { trackAnchor = id; }
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
    if (!report(grouped)) { return; }
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

void MotionTimelineView::revealSelection() {
    // Do not move rows under the pointer during a clip gesture.
    if (clipGesture()) { return; }
    ensureTrackRows();
    const auto& project = processor.document.project();
    motion::Id trackId = 0, parent = 0;
    const auto* holder = motion::findClipTrack(project, selected);
    if (holder != nullptr) {
        trackId = holder->id;
        parent = holder->group;
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
    const auto found = headerRow(trackId);
    if (found < 0) { return; }
    const auto row = static_cast<std::size_t>(found);
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
        const auto* clip = motion::findClip(project, key.owner);
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
    const auto time = snapTime(std::max(0.0, timeAt(point.x)), juce::ModifierKeys::getCurrentModifiers());
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
        toggleLanes(track.id);
    }
    layoutRevision.reset();
    refreshTracks();
}

void MotionTimelineView::setTool(Tool next) {
    cancelGesture();
    tool = next;
    updateToolButtons();
    repaint();
}

// The tool strip shows the tool in use and whether snapping is on.
void MotionTimelineView::updateToolButtons() {
    const auto snap = processor.document.project().gridSnap;
    const auto glyph = snap ? osci::Colours::text() : osci::Colours::textMuted().withAlpha(.6f);
    if (snapButton.tint != glyph) {
        snapButton.tint = glyph;
        snapButton.repaint();
    }
    snapButton.setToggleState(snap, juce::dontSendNotification);
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
        scrollTime = std::clamp(scrollTime, 0.0, std::max(0.0, contentEnd() - visibleSpan()));
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
        return motion::findGroup(project, header->id) == nullptr && project.tracks.findById(header->id) == nullptr;
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
                    const auto* current = project.tracks.findById(id);
                    if (current != nullptr && current->name != name) {
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
                toggleLanes(id);
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
                toggleLanes(id);
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
        const auto found = headerRow(header->id);
        if (found < 0) { header->setVisible(false); continue; }
        const auto row = static_cast<std::size_t>(found);
        const auto y = rowY(static_cast<int>(row)) - rulerHeight;
        const auto height = heightOf(row);
        const auto shown = y + shiftOf(static_cast<int>(row));
        header->setVisible(shown + height > 0 && shown < headerArea.getHeight());
        // Centred on the card; the rest of the row's foot still resizes it.
        // A dragged track's header (or a dragged group's headers) rides with
        // it; the others slide.
        const auto indent = cardInset + std::min(64, rows[row].depth * 16);
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
        path.addRoundedRectangle(block.toFloat().reduced(inset), motion::style::radiusControl);
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
        const auto row = headerRow(header->id);
        if (row < 0 || !inLiftedBlock(row)) { continue; }
        juce::Graphics::ScopedSaveState placed(g);
        g.setOrigin(headerArea.getPosition() + header->getPosition());
        header->paintEntireComponent(g, true);
    }
    g.setColour(motion::style::accent().withAlpha(.55f));
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
            g.setColour(motion::style::accent().withAlpha(.7f));
            g.drawRoundedRectangle(juce::Rectangle<float>(0.0f, static_cast<float>(rowY(visible)), static_cast<float>(getWidth()), static_cast<float>(heightOf(static_cast<std::size_t>(visible)) - bandGap)).reduced(.75f), motion::style::radiusControl, 1.5f);
        }
    }
    g.restoreState();
    paintBeamLabel(g);
    if (blocked.has_value()) {
        juce::Graphics::ScopedSaveState ghost(g);
        g.reduceClipRegion(namesWidth, rulerHeight, getWidth() - namesWidth, getHeight() - rulerHeight);
        const auto area = blocked->toFloat().reduced(1, 3);
        g.setColour(motion::style::error().withAlpha(.2f));
        g.fillRoundedRectangle(area, motion::style::radiusControl);
        g.setColour(motion::style::error());
        g.drawRoundedRectangle(area, motion::style::radiusControl, 1.2f);
        g.setFont(motion::style::caption());
        g.drawText("Overlaps", area.reduced(8, 0), juce::Justification::centredLeft, true);
    }
    // While an effect is dragged, every clip that can take it is outlined.
    if (effectDragActive) {
        juce::Graphics::ScopedSaveState outlines(g);
        g.reduceClipRegion(namesWidth, rulerHeight, getWidth() - namesWidth, getHeight() - rulerHeight);
        g.setColour(motion::style::accent().withAlpha(.35f));
        for (int index = 0; index < static_cast<int>(tracks.size()); ++index) {
            if (tracks[static_cast<std::size_t>(index)].kind != motion::TrackKind::visual) { continue; }
            for (const auto& clip : tracks[static_cast<std::size_t>(index)].clips) { g.drawRoundedRectangle(clipBounds(clip, index).toFloat().reduced(1.5f), motion::style::radiusControl, 1.0f); }
        }
    }
    paintDropPreview(g);
    if (showsMarkerBand()) {
        g.setColour(osci::Colours::textMuted());
        g.setFont(motion::style::caption());
        // Tempo changes label themselves on their flags.
        g.drawText("Markers", nameX, markerBandTop(), namesWidth - nameX - 12, markerBandHeight, juce::Justification::centredLeft);
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
            flag.addRoundedRectangle(tag.getX(), tag.getY(), tag.getWidth(), tag.getHeight(), motion::style::radiusControl, motion::style::radiusControl, false, true, false, true);
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
            g.setColour(motion::style::accent().withAlpha(.75f));
            const float dashes[] {4.0f, 3.0f};
            g.drawDashedLine(juce::Line<float>(static_cast<float>(x), static_cast<float>(rulerHeight), static_cast<float>(x), static_cast<float>(getHeight())), dashes, 2, 1.0f);
        }
    }
    // The row edge under the pointer (or being dragged) shows where it resizes.
    const auto* heightDrag = active<HeightDrag>();
    if (hoveredEdge >= 0 || heightDrag != nullptr) {
        auto row = hoveredEdge;
        if (heightDrag != nullptr) {
            row = headerRow(heightDrag->track);
        }
        if (row >= 0 && row < static_cast<int>(rows.size())) {
            g.setColour(motion::style::accent().withAlpha(.8f));
            g.fillRect(0, rowY(row) + heightOf(static_cast<std::size_t>(row)) - 2, getWidth(), 2);
        }
    }
    // The horizontal bar's strip, under the time area only, while it is shown.
    if (horizontalBarShown()) {
        g.setColour(osci::Colours::veryDark().darker(.25f));
        g.fillRect(namesWidth, getHeight() - scrollStrip, getWidth() - namesWidth, scrollStrip);
    }
    paintScrollBars(g);
    // A box being dragged to pick clips or keys, over every row it crosses.
    const auto* clipMarquee = active<ClipMarquee>();
    const auto* keyMarquee = active<KeyMarquee>();
    for (const auto* box : {clipMarquee != nullptr ? &clipMarquee->area : nullptr, keyMarquee != nullptr ? &keyMarquee->area : nullptr}) {
        if (box == nullptr) { continue; }
        g.setColour(motion::style::accent().withAlpha(.1f));
        g.fillRect(*box);
        g.setColour(motion::style::accent().withAlpha(.6f));
        g.drawRect(*box);
    }
    paintPlayhead(g);
    // The Scene's hint is the empty editor's one call to action; this one
    // answers a source dragged over an empty timeline.
    if (tracks.empty() && dropPosition.has_value() && dropAssetId != 0) {
        g.setColour(osci::Colours::textMuted());
        g.setFont(motion::style::caption());
        g.drawText("Drop sources here", getLocalBounds().withTrimmedTop(rulerHeight), juce::Justification::centred);
    }
}

void MotionTimelineView::mouseDown(const juce::MouseEvent& event) {
    grabKeyboardFocus();
    ensureTrackRows();
    cancelGesture();
    // The middle button grabs the view anywhere, as it moves the Scene's.
    if (event.mods.isMiddleButtonDown()) {
        gesture = Pan {event.getPosition(), scrollTime, scrollY};
        setMouseCursor(motion::grabbingCursor());
        return;
    }
    if (onNamesEdge(event) && event.mods.isLeftButtonDown()) {
        gesture = NamesResize {};
        return;
    }
    const auto edge = rowEdgeAt(event.getPosition());
    if (edge >= 0 && event.mods.isLeftButtonDown()) {
        const auto id = rows[static_cast<std::size_t>(edge)].id;
        // A double-click's second press starts one too; mouseDoubleClick
        // then puts the default height back.
        gesture = HeightDrag {id, heightOf(static_cast<std::size_t>(edge)), event.y, storedHeight(id)};
        return;
    }
    if (inCameraBand(event.y)) {
        cameraBandDown(event);
        return;
    }
    // The time ruler itself; the marker and camera bands below keep their own Cmd-clicks.
    if (event.mods.isLeftButtonDown() && event.mods.isCommandDown() && event.x >= namesWidth && event.y < toolsHeight && moveNearestLoopEdge(event)) { return; }
    if (event.mods.isLeftButtonDown() && loopDown(event)) { return; }
    if (barDown(event)) { return; }
    const auto* marker = markerAt(event.getPosition());
    if (event.mods.isPopupMenu() && event.x >= namesWidth && event.y < rulerHeight) {
        showMarkerMenu(marker != nullptr ? marker->id : 0, snapTime(timeAt(event.x), event.mods));
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
        edit.begin();
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
            if (key.has_value() && !isKeySelected(key->owner, key->property, key->time)) { selectedKeys = {*key}; }
            repaint();
            showKeyMenu();
            return;
        }
        if (!event.mods.isLeftButtonDown()) { return; }
        if (key.has_value()) {
            const bool chosen = isKeySelected(key->owner, key->property, key->time);
            if (event.mods.isShiftDown()) {
                if (chosen) {
                    std::erase_if(selectedKeys, [&](const auto& item) { return item.owner == key->owner && item.property == key->property && motion::sameTime(item.time, key->time); });
                } else {
                    selectedKeys.push_back(*key);
                }
            } else if (!chosen) {
                selectedKeys = {*key};
            }
            if (isKeySelected(key->owner, key->property, key->time)) { beginKeyDrag(*key, event.x); }
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
    // On the composition's bar; past its end the row is empty space, for a box.
    if (visible >= 0 && visible < static_cast<int>(rows.size()) && rows[static_cast<std::size_t>(visible)].composition() && event.x >= namesWidth && event.x <= timeX(processor.document.project().duration)) { showCompositionEffects(); return; }
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
    edit.begin();
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
    const auto hovered = clip != nullptr ? clip->id : event.x >= namesWidth ? groupAtY(event.y) : 0;
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

void MotionTimelineView::mouseDrag(const juce::MouseEvent& event) {
    const auto* pan = active<Pan>();
    if (pan != nullptr) {
        const auto visible = visibleSpan();
        const auto latest = std::max(0.0, contentEnd() - visible);
        scrollTime = std::clamp(pan->scrollTime - (event.x - pan->down.x) / pixelsPerSecond, 0.0, std::max(latest, pan->scrollTime));
        scrollY = std::clamp(pan->scrollY - (event.y - pan->down.y), 0, maximumScrollY());
        userScrolled();
        resized();
        repaint();
        return;
    }
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
            if (!isKeySelected(key.owner, key.property, key.time)) { selectedKeys.push_back(std::move(key)); }
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
        auto updated = edit.start();
        const auto delta = (event.x - markerDrag->downX) / pixelsPerSecond;
        const auto time = delta == 0 ? markerDrag->originalTime : std::clamp(snapEdge(markerDrag->originalTime + delta, event.mods, edit.start(), {}, nullptr, id), 0.0, updated.duration);
        if (std::any_of(updated.markers.begin(), updated.markers.end(), [id, time](const auto& marker) { return marker.id != id && motion::sameTime(marker.time, time); })) { return; }
        for (auto& marker : updated.markers) { if (marker.id == id) { marker.time = time; } }
        std::sort(updated.markers.begin(), updated.markers.end(), [](const auto& a, const auto& b) { return a.time != b.time ? a.time < b.time : a.id < b.id; });
        edit.show(std::move(updated), time != markerDrag->originalTime);
        repaint();
        return;
    }
    auto& drag = std::get<ClipDrag>(gesture);
    const auto& before = edit.start();
    // Shows the drag's edit, made from the start.
    const auto show = [&](motion::Project updated, bool changed) {
        drag.changed = changed;
        edit.show(std::move(updated), changed);
        repaint();
    };
    auto candidate = drag.original;
    const auto timing = drag.original.timing(before.tempo());
    auto edited = timing;
    auto delta = (event.x - drag.downX) / pixelsPerSecond;
    if (delta != 0.0 && !event.mods.isAltDown()) {
        std::set<motion::Id> excluded(selectedClips.begin(), selectedClips.end());
        excluded.insert(drag.original.id);
        if (drag.mode == Mode::slip) {
            const auto anchor = timing.offset / timing.rate;
            delta = before.timeGrid().snap(anchor + delta) - anchor;
        } else if (drag.mode == Mode::move) {
            // Either edge of a moving clip can catch a magnet.
            const auto startTarget = magnet(timing.start + delta, before, excluded);
            const auto endTarget = magnet(timing.end() + delta, before, excluded);
            const auto startDistance = startTarget.has_value() ? std::abs(*startTarget - (timing.start + delta)) : 1.0e300;
            const auto endDistance = endTarget.has_value() ? std::abs(*endTarget - (timing.end() + delta)) : 1.0e300;
            if (startTarget.has_value() && startDistance <= endDistance) {
                delta = *startTarget - timing.start;
                snapGuide = startTarget;
            } else if (endTarget.has_value()) {
                delta = *endTarget - timing.end();
                snapGuide = endTarget;
            } else {
                delta = before.timeGrid().snap(timing.start + delta) - timing.start;
                snapGuide.reset();
            }
        } else {
            const auto anchor = drag.mode == Mode::right || drag.mode == Mode::stretch || drag.mode == Mode::rippleRight ? timing.end() : timing.start;
            delta = snapEdge(anchor + delta, event.mods, before, excluded) - anchor;
        }
    } else {
        snapGuide.reset();
    }
    if (drag.mode == Mode::rippleLeft || drag.mode == Mode::rippleRight) {
        auto updated = before;
        if (!motion::rippleTrim(updated.tracks.change(drag.originalRow), drag.original.id, drag.mode == Mode::rippleLeft, delta, updated.tempo())) { return; }
        for (const auto& item : updated.tracks[drag.originalRow].clips) { updated.duration = std::max(updated.duration, item.timing(updated.tempo()).end()); }
        show(std::move(updated), delta != 0);
        return;
    }
    if (drag.mode == Mode::move && selectedClips.size() > 1) {
        auto updated = before;
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
        show(std::move(updated), delta != 0 || rows != 0);
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
        auto updated = before;
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
        show(std::move(updated), delta != 0.0);
        return;
    }
    if (delta != 0.0) {
        edited = dragged(timing);
        if (!candidate.setTiming(edited, before.tempo())) { return; }
    }
    const auto target = drag.mode == Mode::move
        ? (trackAtY(event.y) >= 0 ? trackAtY(event.y) : drag.originalRow) : drag.originalRow;
    if (before.tracks[target].locked || before.tracks[target].kind != before.tracks[drag.originalRow].kind) { return; }
    const bool candidateChanged = candidate.start != drag.original.start || candidate.duration != drag.original.duration
        || candidate.offset != drag.original.offset || candidate.rate != drag.original.rate || target != drag.originalRow;
    if (!candidateChanged) {
        if (drag.changed) { show(before, false); }
        return;
    }
    auto updated = before;
    std::erase_if(updated.tracks.change(drag.originalRow).clips, [&](const auto& clip) { return clip.id == drag.original.id; });
    if (updated.tracks.change(target).insert(candidate, updated.tempo())) {
        updated.duration = std::max(updated.duration, candidate.timing(updated.tempo()).end());
        blocked.reset();
        show(std::move(updated), true);
    } else {
        // Where the clip would land shows in red; it stays at the last free spot.
        blocked = clipBounds(candidate, target);
        repaint();
    }
}

void MotionTimelineView::mouseUp(const juce::MouseEvent&) {
    if (active<Pan>() != nullptr) {
        gesture = {};
        setMouseCursor(juce::MouseCursor::NormalCursor);
        return;
    }
    edgeScroll.stopTimer();
    edgeEvent.reset();
    snapGuide.reset();
    if (blocked.has_value()) {
        blocked.reset();
        repaint();
    }
    if (active<HeightDrag>() != nullptr) { gesture = {}; return; }
    if (active<BarDrag>() != nullptr) { gesture = {}; repaint(); return; }
    if (active<NamesResize>() != nullptr) {
        gesture = {};
        return;
    }
    if (active<KeyMarquee>() != nullptr || active<ClipMarquee>() != nullptr) { gesture = {}; repaint(); return; }
    if (active<Scrub>() != nullptr) { gesture = {}; return; }
    commitDrag();
}

void MotionTimelineView::commitDrag() {
    const auto* cutDrag = active<CutDrag>();
    const auto* keyDrag = active<KeyDrag>();
    if (active<BeamDrag>() != nullptr || active<CameraKeyDrag>() != nullptr || cutDrag != nullptr || keyDrag != nullptr) {
        const auto label = active<BeamDrag>() != nullptr ? juce::String("Change Beam") : active<CameraKeyDrag>() != nullptr ? juce::String("Move camera key")
            : cutDrag != nullptr ? juce::String(cutDrag->mode == CutDrag::Mode::move ? "Move camera cut" : "Trim camera cut") : juce::String(keyDrag->keys.size() > 1 ? "Move keys" : "Move key");
        edit.commit(label);
        gesture = {};
        repaint();
        return;
    }
    if (active<LoopDrag>() != nullptr) {
        {
            const motion::Document::ViewChange view(processor.document);
            edit.commit("Move loop");
        }
        gesture = {};
        return;
    }
    if (!gestureIsCurrent()) {
        return;
    }
    const auto* clipDrag = active<ClipDrag>();
    const auto marker = clipDrag == nullptr;
    const auto mode = marker ? Mode::move : clipDrag->mode;
    const auto several = selectedClips.size() > 1 && mode != Mode::rippleLeft && mode != Mode::rippleRight;
    const juce::String action = mode == Mode::move ? "Move clip" : mode == Mode::slip ? "Slip clip" : mode == Mode::stretch ? "Stretch clip" : (mode == Mode::rippleLeft || mode == Mode::rippleRight) ? "Ripple trim clip" : "Trim clip";
    const auto label = marker ? juce::String("Move marker") : action + (several ? "s" : "");
    gesture = {};
    edit.commit(label);
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

bool MotionTimelineView::keyPressed(const juce::KeyPress& key) {
    ensureTrackRows();
    // Space, Cmd+D and the rest are the editor's commands, whichever panel
    // has focus: only what is the timeline's own is handled here.
    const auto escape = key == juce::KeyPress::escapeKey;
    if (escape && cancelDrag()) { return true; }
    // A marquee given up leaves the keys selected as they were.
    const auto* keyMarquee = active<KeyMarquee>();
    if (escape && keyMarquee != nullptr) {
        selectedKeys = keyMarquee->before;
        gesture = {};
        repaint();
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
            report(processor.document.removeMarker(selectedMarker));
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

bool MotionTimelineView::gestureIsCurrent() {
    if (active<ClipDrag>() == nullptr && active<MarkerDrag>() == nullptr) {
        return false;
    }
    if (edit.stale()) {
        gesture = {};
        edit.reset();
        repaint();
        return false;
    }
    return true;
}

void MotionTimelineView::cancelGesture() {
    if (gestureIsCurrent()) {
        gesture = {};
        edit.cancel();
    } else if (active<Scrub>() != nullptr) {
        gesture = {};
    }
    repaint();
}

bool MotionTimelineView::cancelDrag() {
    const auto* keyDrag = active<KeyDrag>();
    const auto* cameraKeyDrag = active<CameraKeyDrag>();
    if (keyDrag == nullptr && cameraKeyDrag == nullptr && active<BeamDrag>() == nullptr && active<LoopDrag>() == nullptr && active<CutDrag>() == nullptr) { return false; }
    // Putting the start back (only when the drag changed it and nothing else
    // has since) moves the revision on.
    const auto revision = processor.document.revision();
    edit.cancel();
    if (processor.document.revision() != revision) {
        // The selection goes back with what was dragged.
        if (keyDrag != nullptr) { selectedKeys = keyDrag->keys; }
        if (cameraKeyDrag != nullptr) { selectedCameraKey = std::make_pair(cameraKeyDrag->camera, cameraKeyDrag->from); }
    }
    gesture = {};
    repaint();
    return true;
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
                        return item.owner == clip.id && item.property == name && motion::sameTime(item.time, key.time);
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
    const auto raw = timeAt(x);
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

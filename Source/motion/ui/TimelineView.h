#pragma once

#include "../MotionProcessor.h"
#include "TrackHeader.h"
#include "TrackLayout.h"
#include <optional>
#include <limits>
#include <set>
#include "../model/CompositionGraph.h"
#include "../model/PropertySchema.h"
#include "../model/KeyEasing.h"
#include "MotionIcons.h"

class MotionTimelineView : public juce::Component, public juce::DragAndDropTarget, public juce::SettableTooltipClient {
    struct Row : motion::TrackRow {
        std::string lane; // Property id for a keyframe lane under its track.
        bool isLane() const { return !lane.empty(); }
        bool group() const { return track < 0 && lane.empty(); }
    };
    struct KeyRef {
        motion::Id clip = 0;
        std::string property;
        double time = 0; // Content-local key time.
    };
public:
    explicit MotionTimelineView(MotionProcessor& ownerProcessor) : processor(ownerProcessor) {
        setName("Composition timeline");
        addTrack.setName("Add track");
        addTrack.setTitle("Add track");
        addTrack.setTooltip("Add an empty track");
        addTrack.onClick = [this] {
            motion::Track track;
            track.id = processor.document.newId();
            track.name = "Track " + std::to_string(processor.document.project().tracks.size() + 1);
            processor.document.edit("Add track", [track](motion::Project& project) { project.tracks.push_back(track); });
        };
        addAndMakeVisible(addTrack);
        snapButton.setName("Snapping");
        snapButton.setTitle("Snapping");
        snapButton.setTooltip("Snapping to the grid, clip edges, markers and the playhead. Hold Alt while dragging to bypass it.");
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
                cancelGesture();
                tool = value;
                repaint();
            };
            addAndMakeVisible(button);
        }
        for (auto* button : {&snapButton, &addTrack}) { button->iconSize = 16.0f; }
        headerArea.setInterceptsMouseClicks(false, true);
        addAndMakeVisible(headerArea);
        setWantsKeyboardFocus(true);
        // Shortcuts are listed in Help > Keyboard shortcuts rather than a hover wall.
    }
    // Runs one of the editor's commands by name (Cut, Copy, Paste...).
    std::function<void(const juce::String&)> onCommand;
    // Add a camera that takes over the output at the playhead.
    std::function<void()> onAddCamera;
    std::function<void(motion::Id)> onSelection, onMidiAssigned, onTimingRequested, onMakeUnique, onEnterComposition, onRevealSource;
    std::function<void()> onLoopSelection;
    std::function<void(const juce::String&)> onError;
    std::function<void(motion::Id, double)> onEditMarker;
    // (beat, current bpm, beat of the change being edited, if any)
    std::function<void(double, double, std::optional<double>)> onEditTempo;
    std::function<void(motion::Id, motion::Id)> onEffectAdded;
    mutable motion::Id selected = 0;
    void setSelection(motion::Id id) {
        ensureTrackRows();
        selectedMarker = 0;
        selected = id;
        if (!notifyingSelection) {
            selectedClips.clear();
            if (isClip(id)) { selectedClips.insert(id); }
        }
        repaint();
    }
    struct ViewState {
        double zoom = 70, scroll = 0;
        int scrollY = 0;
        motion::Id primary = 0;
        std::set<motion::Id> selected, collapsed, expanded;
    };
    ViewState viewState() const {
        ensureTrackRows();
        return {pixelsPerSecond, scrollTime, scrollY, selected, selectedClips, collapsedGroups, expandedTracks};
    }
    void restoreView(const ViewState& state) {
        ensureTrackRows();
        pixelsPerSecond = state.zoom; scrollTime = state.scroll; scrollY = state.scrollY;
        selectedMarker = 0;
        selected = state.primary; selectedClips = state.selected; collapsedGroups = state.collapsed; expandedTracks = state.expanded;
        std::erase_if(collapsedGroups, [this](auto id) { return motion::findGroup(processor.document.project(), id) == nullptr; });
        if (!isClip(selected) && motion::findGroup(processor.document.project(), selected) == nullptr) { selected = 0; }
        layoutRevision.reset();
        refreshTracks();
    }
    double pixelsPerSecond = 70;
    double scrollTime = 0;
    // Vertical scroll in pixels of row content below the ruler.
    mutable int scrollY = 0;
    // Rows without their own height use this; Alt+wheel scales it.
    int defaultTrackHeight = 32;
    std::function<void(int)> onDefaultTrackHeight;
    // Track names column; drag its edge to resize (140-420 px).
    int namesWidth = 170;
    bool resizingNames = false;
    // The resize handle is the strip just inside the names column, so clicks
    // on keys and clips at the start of the timeline are never taken.
    bool onNamesEdge(const juce::MouseEvent& event) const { return event.y >= rulerHeight && event.x >= namesWidth - 5 && event.x < namesWidth; }
    // The track whose bottom edge (in the names column) is under the pointer.
    int trackEdgeAt(juce::Point<int> point) const {
        ensureTrackRows();
        if (point.x >= namesWidth - 5 || point.y < rulerHeight) { return -1; }
        for (int row = firstVisibleRow(); row < static_cast<int>(rows.size()); ++row) {
            const auto bottom = rowY(row) + heightAt(row);
            if (bottom > getHeight() + resizeStrip) { break; }
            if (!rows[static_cast<std::size_t>(row)].isLane() && rows[static_cast<std::size_t>(row)].track >= 0 && point.y >= bottom - resizeStrip - 1 && point.y <= bottom + 1) { return rows[static_cast<std::size_t>(row)].track; }
        }
        return -1;
    }
    struct HeightDrag { motion::Id track; int startHeight, downY, original; };
    std::optional<HeightDrag> heightDrag;
    mutable int rulerHeight = 26;
    static constexpr int laneHeight = 22;
    // The bottom strip of each track row resizes it.
    static constexpr int resizeStrip = 4;

    // Selection may originate in the preview, library or an import, not only
    // from a row already on screen. Keep its layer reachable in a dense project.
    void revealSelection() {
        // Do not move rows under the pointer during a clip gesture.
        if (before.has_value() || scrubbing) { return; }
        ensureTrackRows();
        const auto& project = processor.document.project();
        motion::Id trackId = 0, parent = 0;
        for (const auto& track : project.tracks) {
            if (std::any_of(track.clips.begin(), track.clips.end(), [this](const auto& clip) { return clip.id == selected; })) {
                trackId = track.id; parent = track.group; break;
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

    bool hasSelectedKeys() const { return !selectedKeys.empty(); }
    bool hasSelectedClips() const { return !selectedClips.empty(); }
    std::vector<motion::Document::CopiedKey> copiedKeys() const {
        std::vector<motion::Document::CopiedKey> result;
        const auto& project = processor.document.project();
        double earliest = std::numeric_limits<double>::infinity();
        std::vector<std::pair<double, motion::Document::CopiedKey>> timed;
        for (const auto& key : selectedKeys) {
            const auto* clip = findClip(key.clip, project);
            if (clip == nullptr) { continue; }
            const auto found = clip->properties.find(key.property);
            if (found == clip->properties.end()) { continue; }
            const auto timing = clip->timing(project.tempo());
            for (const auto& keyframe : found->second.keyframes()) {
                if (!sameTime(keyframe.time, key.time) || timing.rate == 0) { continue; }
                const auto time = timing.projectTime(keyframe.time);
                earliest = std::min(earliest, time);
                timed.push_back({time, {key.property, 0, keyframe}});
            }
        }
        for (auto& [time, copied] : timed) { copied.offset = time - earliest; result.push_back(std::move(copied)); }
        return result;
    }
    std::vector<motion::Document::CopiedClip> copiedClips() const {
        std::vector<motion::Document::CopiedClip> result;
        for (const auto& track : processor.document.project().tracks) {
            for (const auto& clip : track.clips) {
                if (selectedClips.contains(clip.id)) { result.push_back({track.id, track.kind, track.name, clip}); }
            }
        }
        return result;
    }
    void deleteSelection() {
        if (!selectedKeys.empty()) { deleteSelectedKeys(); return; }
        if (!selectedClips.empty()) { deleteSelectedClips(false); }
    }
    const std::set<motion::Id>& selectedClipIds() const { return selectedClips; }
    // Where a dropped file lands: the snapped time under the pointer and the
    // track there (0 for empty space); nothing outside the track area.
    std::optional<std::pair<double, motion::Id>> dropTarget(juce::Point<int> point) const {
        if (point.x < namesWidth || point.y < rulerHeight || point.y >= rulerHeight + viewHeight() || point.x >= getWidth()) { return std::nullopt; }
        const auto time = snapTime(std::max(0.0, scrollTime + (point.x - namesWidth) / pixelsPerSecond), juce::ModifierKeys::getCurrentModifiers());
        const auto row = trackAtY(point.y);
        const auto& tracks = processor.document.project().tracks;
        return std::make_pair(time, row >= 0 && row < static_cast<int>(tracks.size()) ? tracks[static_cast<std::size_t>(row)].id : motion::Id(0));
    }
    void selectClips(const std::vector<motion::Id>& ids) {
        selectedKeys.clear();
        selectedClips = {ids.begin(), ids.end()};
        notifySelection(ids.empty() ? 0 : ids.front());
        revealSelection();
    }
    // Select all keys in visible lanes when a key is selected, else all clips.
    void selectAll() {
        ensureTrackRows();
        if (!selectedKeys.empty()) {
            selectedKeys = keysInside({namesWidth, 0, 1 << 29, 1 << 29});
            if (!selectedKeys.empty()) { repaint(); return; }
        }
        std::vector<motion::Id> ids;
        for (const auto& track : processor.document.project().tracks) {
            for (const auto& clip : track.clips) { ids.push_back(clip.id); }
        }
        selectClips(ids);
    }
    void toggleLanesForSelection() {
        for (const auto& track : processor.document.project().tracks) {
            if (std::none_of(track.clips.begin(), track.clips.end(), [this](const auto& clip) { return selectedClips.contains(clip.id); })) { continue; }
            if (expandedTracks.contains(track.id)) { expandedTracks.erase(track.id); } else { expandedTracks.insert(track.id); }
        }
        layoutRevision.reset();
        refreshTracks();
    }
    void zoomBy(double factor) {
        // Around the playhead when it is in view, like Premiere; else the centre.
        const auto playheadX = timeX(processor.position.load()) - namesWidth;
        const auto centreX = playheadX >= 0 && playheadX <= getWidth() - namesWidth ? playheadX : std::max(0, (getWidth() - namesWidth) / 2);
        const auto anchor = scrollTime + centreX / pixelsPerSecond;
        const auto zoom = std::clamp(pixelsPerSecond * factor, 0.000001, 500.0);
        animateView(zoom, std::max(0.0, anchor - centreX / zoom));
    }
    // Keyboard zoom and Fit glide to their view (about 140 ms, eased) rather
    // than jumping; wheel and pinch zoom stay immediate.
    void animateView(double zoom, double scroll) {
        animationFrom = {pixelsPerSecond, scrollTime};
        animationTo = {zoom, scroll};
        animationStart = juce::Time::getMillisecondCounterHiRes();
        viewAnimation.startTimerHz(60);
    }
    juce::TimedCallback viewAnimation {[this] {
        const auto t = std::clamp((juce::Time::getMillisecondCounterHiRes() - animationStart) / 140.0, 0.0, 1.0);
        const auto eased = 1 - std::pow(1 - t, 3);
        // Zoom moves in log space so it feels even at any scale.
        pixelsPerSecond = std::exp(std::lerp(std::log(animationFrom.first), std::log(animationTo.first), eased));
        scrollTime = std::lerp(animationFrom.second, animationTo.second, eased);
        if (t >= 1) { viewAnimation.stopTimer(); }
        resized();
        repaint();
    }};
    std::pair<double, double> animationFrom, animationTo;
    double animationStart = 0;
    void fitToProject() { fitProject(); }

    void revealTime(double seconds) {
        cancelGesture();
        if (!std::isfinite(seconds) || seconds < 0) { return; }
        const auto visible = std::max(1, getWidth() - namesWidth - 20) / pixelsPerSecond;
        if (seconds < scrollTime || seconds > scrollTime + visible) {
            scrollTime = std::max(0.0, seconds - visible * .5);
            repaint();
        }
    }

    void refreshTracks() {
        const auto& project = processor.document.project();
        ensureTrackRows();
        std::erase_if(headers, [&](const auto& header) {
            return motion::findGroup(project, header->id) == nullptr && std::none_of(project.tracks.begin(), project.tracks.end(), [&](const auto& track) { return track.id == header->id; });
        });
        const auto updateHeader = [&](const motion::Track& track, bool group) {
            auto found = std::find_if(headers.begin(), headers.end(), [&](const auto& header) { return header->id == track.id; });
            if (found == headers.end()) {
                auto header = std::make_unique<MotionTrackHeader>(track.id);
                header->onRename = [this](motion::Id id, std::string name) {
                    if (name.empty()) { refreshTracks(); return; }
                    processor.document.edit("Rename track", [id, name](motion::Project& project) {
                        auto* group = motion::findGroup(project, id);
                        if (group != nullptr) { group->name = name; }
                        for (auto& item : project.tracks) { if (item.id == id) { item.name = name; } }
                    });
                };
                header->onMenu = [this](motion::Id id) { showTrackMenu(id); };
                header->onSelect = [this](motion::Id id) { if (motion::findGroup(processor.document.project(), id) != nullptr) { selectClip(id); } };
                header->onCollapse = [this](motion::Id id) {
                    if (collapsedGroups.contains(id)) { collapsedGroups.erase(id); } else { collapsedGroups.insert(id); }
                    refreshTracks();
                    selectClip(id);
                };
                header->onDragRevision = [this] { return processor.document.revision(); };
                header->onLanes = [this](motion::Id id) {
                    if (expandedTracks.contains(id)) { expandedTracks.erase(id); } else { expandedTracks.insert(id); }
                    layoutRevision.reset();
                    refreshTracks();
                };
                header->onLock = [this](motion::Id id) { toggleLock(id); };
                header->onArm = [this](motion::Id id) {
                    const auto& tracks = processor.document.project().tracks;
                    const auto found = std::find_if(tracks.begin(), tracks.end(), [id](const auto& track) { return track.id == id; });
                    if (found != tracks.end()) { setMidiInput(id, found->midiInput == 0 ? motion::Track::anyMidiChannel : 0); }
                };
                header->onMute = [this](motion::Id id) { toggleTrack(id, false); };
                header->onSolo = [this](motion::Id id) { toggleTrack(id, true); };
                headerArea.addAndMakeVisible(*header);
                headers.push_back(std::move(header));
                found = headers.end() - 1;
            }
            const auto lanes = group ? false : !animatedProperties(track).empty();
            (*found)->armingAvailable = processor.document.editingComposition() == 0;
            (*found)->update(track, group, collapsedGroups.contains(track.id), lanes, expandedTracks.contains(track.id));
        };
        for (const auto& track : project.tracks) { updateHeader(track, false); }
        for (const auto& group : project.groups) {
            motion::Track display;
            display.id = group.id; display.name = group.name; display.muted = group.muted; display.solo = group.solo;
            updateHeader(display, true);
        }
        rebuildRows();
        layoutRevision = processor.document.revision();
        resized();
        repaint();
    }
    void resized() override {
        ensureTrackRows();
        addTrack.setBounds(namesWidth - 26, 2, 22, 22);
        snapButton.setBounds(namesWidth - 50, 2, 22, 22);
        auto tools = juce::Rectangle<int>(4, 2, 4 * 22, 22);
        for (auto* button : {&selectTool, &slipTool, &stretchTool, &rippleTool}) { button->setBounds(tools.removeFromLeft(22)); }
        scrollY = std::clamp(scrollY, 0, maximumScrollY());
        // Headers live in a container clipped below the ruler, so rows scroll
        // smoothly under it.
        headerArea.setBounds(0, rulerHeight, namesWidth - 5, viewHeight());
        for (auto& header : headers) {
            const auto found = std::find_if(rows.begin(), rows.end(), [&](const auto& row) { return row.id == header->id && !row.isLane(); });
            if (found == rows.end()) { header->setVisible(false); continue; }
            const auto row = static_cast<std::size_t>(found - rows.begin());
            const auto y = rowY(static_cast<int>(row)) - rulerHeight;
            const auto height = heightOf(row);
            header->setVisible(y + height > 0 && y < headerArea.getHeight());
            const auto indent = std::min(48, found->depth * 8);
            header->setBounds(indent, y, namesWidth - 5 - indent, height - resizeStrip);
        }
    }
    bool isInterestedInDragSource(const SourceDetails& details) override {
        const auto description = details.description.toString();
        return description.startsWith("motion-asset:") || description.startsWith("motion-effect:") || description.startsWith("motion-track:");
    }

    void itemDragEnter(const SourceDetails& details) override { itemDragMove(details); }
    void itemDragMove(const SourceDetails& details) override {
        dropPosition = details.localPosition;
        dropTrack = details.description.toString().startsWith("motion-track:");
        if (dropTrack && details.localPosition.y < rulerHeight) { dropPosition.reset(); repaint(); return; }
        dropEffect = details.description.toString().startsWith("motion-effect:") ? details.description.toString().fromFirstOccurrenceOf(":", false, false).toStdString() : std::string();
        dropAssetId = static_cast<motion::Id>(details.description.toString().fromFirstOccurrenceOf(":", false, false).getLargeIntValue());
        previewEffect(dropEffect.empty() ? 0 : effectOwnerAt(details.localPosition));
        repaint();
    }
    void itemDragExit(const SourceDetails&) override {
        dropPosition.reset();
        previewEffect(0);
        repaint();
    }
    // An effect dragged over a clip, track or group is heard and seen before
    // it is dropped.
    std::function<void(const motion::Project*)> onPreview;
    void previewEffect(motion::Id owner) {
        if (owner == previewedOwner) { return; }
        previewedOwner = owner;
        if (!onPreview) { return; }
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
    motion::Id previewedOwner = 0;

    void itemDropped(const SourceDetails& details) override {
        dropPosition.reset();
        previewEffect(0);
        if (details.description.toString().startsWith("motion-track:")) {
            if (details.localPosition.y < rulerHeight) { repaint(); return; }
            const auto revision = static_cast<std::uint64_t>(details.description.toString().fromLastOccurrenceOf(":", false, false).getLargeIntValue());
            if (revision != processor.document.revision()) { repaint(); return; }
            reorderTrack(static_cast<motion::Id>(details.description.toString().fromFirstOccurrenceOf(":", false, false).getLargeIntValue()), details.localPosition.y);
            return;
        }
        if (details.description.toString().startsWith("motion-effect:")) {
            insertEffect(details.description.toString().fromFirstOccurrenceOf(":", false, false).toStdString(), details.localPosition);
            repaint();
            return;
        }
        insertAsset(static_cast<motion::Id>(details.description.toString().fromFirstOccurrenceOf(":", false, false).getLargeIntValue()), details.localPosition.x, details.localPosition.y);
        repaint();
    }

    void insertAsset(motion::Id assetId, int x, int y) {
        const auto& project = processor.document.project();
        const auto asset = std::find_if(project.assets.begin(), project.assets.end(), [assetId](const auto& item) { return item->id == assetId; });
        if (asset == project.assets.end()) {
            const auto time = x < namesWidth ? processor.position.load() : std::max(0.0, scrollTime + (x - namesWidth) / pixelsPerSecond);
            const auto row = trackAtY(y);
            const auto track = row >= 0 && row < static_cast<int>(project.tracks.size()) ? project.tracks[row].id : 0;
            motion::Id inserted = 0;
            const auto result = processor.document.insertComposition(assetId, snapTime(time, juce::ModifierKeys::getCurrentModifiers()), track, groupAtY(y), inserted);
            if (result.failed()) { if (onError) { onError(result.getErrorMessage()); } return; }
            selectClip(inserted); refreshTracks();
            return;
        }
        if ((*asset)->midi != nullptr) {
            int row = -1;
            const auto* under = x >= namesWidth ? clipAt({x, y}, row) : nullptr;
            const auto target = x < 0 ? selected : (under != nullptr ? under->id : 0);
            const auto result = processor.document.assignMidi(target, assetId);
            if (result.failed()) { if (onError) { onError(result.getErrorMessage()); } }
            else if (onMidiAssigned) { onMidiAssigned(target); }
            return;
        }
        const auto time = x < namesWidth ? processor.position.load() : std::max(0.0, scrollTime + (x - namesWidth) / pixelsPerSecond);
        const auto snapped = snapTime(time, juce::ModifierKeys::getCurrentModifiers());
        const auto row = trackAtY(y);
        const auto group = groupAtY(y);
        const auto kind = (*asset)->audio != nullptr ? motion::TrackKind::audio : motion::TrackKind::visual;
        auto clip = motion::Document::makeClip(processor.document.newId(), **asset, snapped);
        const auto id = clip.id;
        if (row >= 0 && row < static_cast<int>(project.tracks.size()) && (project.tracks[row].locked || project.tracks[row].kind != kind || !project.tracks[row].canPlace(clip, 0, project.tempo()))) {
            return;
        }
        const auto trackId = processor.document.newId();
        processor.document.edit(kind == motion::TrackKind::audio ? "Add audio clip" : "Add object clip", [&](motion::Project& updated) {
            updated.duration = std::max(updated.duration, clip.timing(updated.tempo()).end());
            if (row >= 0 && row < static_cast<int>(updated.tracks.size())) {
                updated.tracks[row].insert(std::move(clip), updated.tempo());
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
    }

    void paint(juce::Graphics& g) override {
        ensureTrackRows();
        g.fillAll(osci::Colours::veryDark());
        auto area = getLocalBounds();
        g.setColour(osci::Colours::surfaceRaised());
        g.fillRect(area.removeFromTop(rulerHeight));
        g.setFont(motion::style::body());
        pixelsPerSecond = std::isfinite(pixelsPerSecond) ? std::clamp(pixelsPerSecond, 0.000001, 500.0) : 70.0;
        scrollTime = std::isfinite(scrollTime) ? std::max(0.0, scrollTime) : 0.0;
        const auto visibleSeconds = std::max(0, getWidth() - namesWidth) / pixelsPerSecond;
        const auto grid = processor.document.project().timeGrid();
        const auto step = grid.tickStep(pixelsPerSecond);
        const auto minorStep = grid.display == motion::TimeDisplay::beats ? grid.snapBeats * 60.0 / grid.bpm : 1.0 / grid.frameRate;
        // In beats, ticks step through beats so they follow tempo changes.
        const bool musical = grid.display == motion::TimeDisplay::beats && grid.tempoChanges != nullptr;
        const auto tickAt = [&](double first, double stride, int index) {
            return musical ? grid.tickTime(first + index * stride) : first + index * stride;
        };
        const auto firstOf = [&](double stride) {
            return musical ? std::floor(grid.beatAt(scrollTime) / stride) * stride : std::floor(scrollTime / stride) * stride;
        };
        const auto beatsPerSecond = grid.bpm / 60.0;
        const auto minorStride = musical ? grid.snapBeats : minorStep;
        const auto majorStride = musical ? step * beatsPerSecond : step;
        if (grid.snapping && minorStep * pixelsPerSecond >= 9 && minorStep < step) {
            const auto first = firstOf(minorStride);
            const auto count = musical ? 4000 : std::clamp(static_cast<int>(std::ceil(visibleSeconds / minorStep)) + 2, 0, 1000);
            g.setColour(juce::Colours::white.withAlpha(0.035f));
            for (int tick = 0; tick < count; ++tick) {
                const auto x = timeX(tickAt(first, minorStride, tick));
                if (x > getWidth()) { break; }
                if (x >= namesWidth) { g.drawVerticalLine(x, rulerHeight, static_cast<float>(getHeight())); }
            }
        }
        const auto firstTick = firstOf(majorStride);
        const auto tickCount = musical ? 4000 : std::clamp(static_cast<int>(std::ceil(visibleSeconds / step)) + 2, 0, 1000);
        for (int tick = 0; tick < tickCount; ++tick) {
            const auto time = tickAt(firstTick, majorStride, tick);
            if (timeX(time) > getWidth()) { break; }
            const auto x = timeX(time);
            if (x < namesWidth) {
                continue;
            }
            g.setColour(juce::Colours::white.withAlpha(0.06f));
            g.drawVerticalLine(x, rulerHeight, static_cast<float>(getHeight()));
            g.setColour(osci::Colours::text().withAlpha(0.7f));
            g.drawText(juce::String(grid.label(time, step)), x + 5, 0, 70, 26, juce::Justification::centredLeft);
        }
        paintLoop(g);
        g.setColour(osci::Colours::surfaceRaised());
        g.fillRect(0, 0, namesWidth, rulerHeight);
        snapButton.setToggleState(processor.document.project().gridSnap, juce::dontSendNotification);
        selectTool.setToggleState(tool == Tool::move, juce::dontSendNotification);
        slipTool.setToggleState(tool == Tool::slip, juce::dontSendNotification);
        stretchTool.setToggleState(tool == Tool::stretch, juce::dontSendNotification);
        rippleTool.setToggleState(tool == Tool::ripple, juce::dontSendNotification);
        const auto& tracks = processor.document.project().tracks;
        const auto& project = processor.document.project();
        scrollY = std::clamp(scrollY, 0, maximumScrollY());
        g.saveState();
        g.reduceClipRegion(0, rulerHeight, getWidth(), viewHeight());
        for (int visible = firstVisibleRow(); visible < static_cast<int>(rows.size()); ++visible) {
            const auto y = rowY(visible);
            if (y >= getHeight()) {
                break;
            }
            const auto& row = rows[static_cast<std::size_t>(visible)];
            const auto height = heightOf(static_cast<std::size_t>(visible));
            if (row.isLane()) {
                paintLane(g, row, y, height);
                continue;
            }
            g.setColour(motion::style::raised());
            g.fillRect(0, y, namesWidth - 1, height - 1);
            g.setColour(motion::style::outline().withAlpha(.35f));
            g.drawHorizontalLine(y + height - 1, static_cast<float>(namesWidth), static_cast<float>(getWidth()));
            const auto index = row.track;
            if (index < 0) {
                g.setColour(motion::style::raised().withAlpha(0.25f));
                g.fillRect(namesWidth, y, getWidth() - namesWidth, height - 1);
                juce::Graphics::ScopedSaveState summaryScope(g);
                g.reduceClipRegion(namesWidth, y, getWidth() - namesWidth, height);
                for (const auto& track : tracks) {
                    auto parent = track.group;
                    for (std::size_t depth = 0; parent != 0 && depth < motion::maximumGroupDepth; ++depth) {
                        if (parent == row.id) {
                            g.setColour(motion::style::accent().withAlpha(motion::trackIsAudible(project, track) ? 0.35f : 0.1f));
                            for (const auto& clip : track.clips) {
                                const auto timing = clip.timing(project.tempo());
                                g.fillRoundedRectangle(static_cast<float>(timeX(timing.start)), y + height * 0.5f - 3, static_cast<float>(std::max(2, boundedPixel(timing.duration() * pixelsPerSecond))), 6, 2);
                            }
                            break;
                        }
                        const auto* group = motion::findGroup(project, parent);
                        parent = group != nullptr ? group->parent : 0;
                    }
                }
                continue;
            }
            juce::Graphics::ScopedSaveState scope(g);
            g.reduceClipRegion(namesWidth, y, getWidth() - namesWidth, height);
            const auto opacity = !motion::trackIsAudible(project, tracks[static_cast<std::size_t>(index)]) ? 0.38f : 1.0f;
            for (const auto& clip : tracks[static_cast<std::size_t>(index)].clips) {
                paintClip(g, clip, tracks[static_cast<std::size_t>(index)], index, opacity);
            }
        }
        g.restoreState();
        if (dropPosition.has_value() && dropTrack) {
            g.setColour(osci::Colours::accentColor());
            const auto under = visualRowAt(dropPosition->y);
            const auto boundary = std::clamp(under >= 0 && under < static_cast<int>(rows.size()) && dropPosition->y >= rowY(under) + heightAt(under) / 2 ? under + 1 : under, 0, static_cast<int>(rows.size()));
            if (groupAtY(dropPosition->y) != 0) { g.drawRect(0, rowY(under), getWidth(), heightAt(under), 2); } else { g.fillRect(0, rowY(boundary) - 1, getWidth(), 2); }
        } else if (dropPosition.has_value() && !dropEffect.empty()) {
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
        } else if (dropPosition.has_value()) {
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
            if (asset != assets.end() && (*asset)->midi != nullptr) {
                int targetRow = -1;
                const auto* target = clipAt(*dropPosition, targetRow);
                if (target != nullptr && tracks[targetRow].kind == motion::TrackKind::visual && !tracks[targetRow].locked) {
                    g.setColour(osci::Colours::accentColor());
                    g.drawRoundedRectangle(clipBounds(*target, targetRow).toFloat().reduced(2), 3, 2);
                }
                return;
            }
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
                g.setColour(motion::style::accent().withAlpha(.12f));
                g.fillRoundedRectangle(header, motion::style::radius);
                g.setColour(motion::style::accent().withAlpha(.7f));
                g.drawRoundedRectangle(header.reduced(.5f), motion::style::radius, 1.0f);
                motion::icons::draw(g, motion::icons::Icon::add, header.removeFromLeft(26.0f), motion::style::accent().brighter(.3f), 14.0f);
                g.setFont(motion::style::body());
                g.setColour(motion::style::text());
                g.drawText(audio ? "New audio track" : "New track", header, juce::Justification::centredLeft, true);
            }
            juce::Graphics::ScopedSaveState scope(g);
            g.reduceClipRegion(namesWidth, rulerHeight, getWidth() - namesWidth, getHeight() - rulerHeight);
            g.setColour((allowed ? juce::Colour(0xff70da91) : juce::Colour(0xffe98080)).withAlpha(0.2f));
            g.fillRoundedRectangle(bounds, 4);
            g.setColour(allowed ? juce::Colour(0xff70da91) : juce::Colour(0xffe98080));
            g.drawRoundedRectangle(bounds, 4, 1);
            g.drawText(allowed ? (audio ? "Add audio" : definition != definitions.end() ? "Add composition" : "Add object") : (recursive ? "Cannot contain itself" : correctKind ? "Clips cannot overlap" : "Use a matching or empty lane"), bounds.reduced(8, 0), juce::Justification::centredLeft);
        }
        if (showsMarkerBand()) {
            g.setColour(osci::Colours::textMuted());
            g.setFont(motion::style::caption());
            g.drawText(processor.document.project().tempoChanges != nullptr ? "Markers / tempo" : "Markers", 12, 26, namesWidth - 24, 22, juce::Justification::centredLeft);
            const auto& project = processor.document.project();
            if (project.tempoChanges != nullptr) {
                const auto tempo = project.tempo();
                double previousBeat = 0, previousBpm = project.bpm;
                for (const auto& change : *project.tempoChanges) {
                    const auto x = timeX(tempo.seconds(change.beat));
                    const auto colour = juce::Colour(0xff8fb6e8);
                    // A ramp draws as a slope from the previous tempo point.
                    if (change.ramp) {
                        const auto from = static_cast<float>(std::max(namesWidth, timeX(tempo.seconds(previousBeat))));
                        const auto rising = change.bpm >= previousBpm;
                        g.setColour(colour.withAlpha(0.55f));
                        g.drawLine(from, rising ? 45.0f : 29.0f, static_cast<float>(std::min(x, getWidth())), rising ? 29.0f : 45.0f, 1.5f);
                    }
                    previousBeat = change.beat;
                    previousBpm = change.bpm;
                    if (x < namesWidth || x >= getWidth()) { continue; }
                    g.setColour(colour.withAlpha(0.14f));
                    g.drawVerticalLine(x, rulerHeight, static_cast<float>(getHeight()));
                    g.setColour(colour);
                    g.fillRect(x, 27, 2, 20);
                    g.drawText(juce::String(juce::CharPointer_UTF8("\xe2\x99\xa9 ")) + juce::String(change.bpm, change.bpm == std::round(change.bpm) ? 0 : 2), x + 4, 27, 70, 20, juce::Justification::centredLeft, true);
                }
            }
            for (const auto& marker : processor.document.project().markers) {
                const auto bounds = markerBounds(marker);
                if (bounds.isEmpty()) { continue; }
                const auto colour = juce::Colour(0xffcfb779);
                g.setColour(colour.withAlpha(0.12f));
                g.drawVerticalLine(bounds.getX(), rulerHeight, static_cast<float>(getHeight()));
                g.setColour(colour.withAlpha(marker.id == selectedMarker ? 0.3f : 0.12f));
                g.fillRoundedRectangle(bounds.toFloat(), 2.0f);
                g.setColour(colour);
                g.fillRect(bounds.getX(), bounds.getY(), 2, bounds.getHeight());
                g.setColour(osci::Colours::text());
                g.drawText(marker.name, bounds.reduced(6, 0), juce::Justification::centredLeft, true);
            }
        }
        if (showsCameraBand()) { paintCameraBand(g); }
        if (snapGuide.has_value()) {
            const auto x = timeX(*snapGuide);
            if (x >= namesWidth && x <= getWidth()) {
                g.setColour(motion::style::accent().withAlpha(.75f));
                const float dashes[] {4.0f, 3.0f};
                g.drawDashedLine(juce::Line<float>(static_cast<float>(x), static_cast<float>(rulerHeight), static_cast<float>(x), static_cast<float>(getHeight())), dashes, 2, 1.0f);
            }
        }
        // The row edge under the pointer (or being dragged) shows where it resizes.
        const auto edgeTrack = heightDrag.has_value() ? -2 : hoveredEdge;
        if (edgeTrack >= 0 || heightDrag.has_value()) {
            const auto index = heightDrag.has_value() ? trackIndex(heightDrag->track) : edgeTrack;
            if (index >= 0) {
                g.setColour(motion::style::accent().withAlpha(.8f));
                g.fillRect(0, trackY(index) + trackHeight(index) - 2, getWidth(), 2);
            }
        }
        g.setColour(osci::Colours::veryDark().darker(.25f));
        g.fillRect(0, getHeight() - scrollStrip, getWidth(), scrollStrip);
        paintScrollBars(g);
        if (clipMarquee.has_value()) {
            g.setColour(motion::style::accent().withAlpha(.1f));
            g.fillRect(*clipMarquee);
            g.setColour(motion::style::accent().withAlpha(.6f));
            g.drawRect(*clipMarquee);
        }
        const auto playhead = timeX(processor.position.load());
        if (playhead >= namesWidth && playhead <= getWidth()) {
            g.setColour(juce::Colour(0xff7de5a0));
            g.drawVerticalLine(playhead, 0, static_cast<float>(getHeight()));
            juce::Path head;
            head.addTriangle(playhead - 5, 0, playhead + 5, 0, playhead, 8);
            g.fillPath(head);
        }
        if (tracks.empty()) {
            g.setColour(osci::Colours::text().withAlpha(0.55f));
            g.drawText("Drop files here to put them on the timeline, or Add source (" + motion::style::shortcutText("Cmd+I") + ")", getLocalBounds().withTrimmedTop(rulerHeight), juce::Justification::centred);
        }
    }

    void mouseDown(const juce::MouseEvent& event) override {
        grabKeyboardFocus();
        ensureTrackRows();
        cancelGesture();
        if (onNamesEdge(event) && event.mods.isLeftButtonDown()) {
            resizingNames = true;
            return;
        }
        const auto edge = trackEdgeAt(event.getPosition());
        if (edge >= 0 && edge < static_cast<int>(processor.document.project().tracks.size()) && event.mods.isLeftButtonDown()) {
            const auto& track = processor.document.project().tracks[static_cast<std::size_t>(edge)];
            if (event.getNumberOfClicks() > 1) {
                // Double-click an edge: back to the default height.
                processor.document.setTrackHeight(track.id, 0);
                layoutRows(); resized(); repaint();
                return;
            }
            heightDrag = HeightDrag {track.id, trackHeight(edge), event.y, track.height};
            return;
        }
        if (inCameraBand(event.y)) {
            cameraBandDown(event);
            return;
        }
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
            selectMarker(id); markerDragging = id; markerOriginalTime = time;
            processor.seek(time);
            downX = event.x; before = processor.document.project();
            expectedRevision = processor.document.revision(); changed = false;
            repaint();
            return;
        }
        selectedMarker = 0;
        if (event.y >= 26 && event.y < cameraBandTop() && event.x < namesWidth && onEditMarker) { onEditMarker(0, processor.position.load()); return; }
        if (event.y < rulerHeight && event.x < namesWidth) { return; }
        if (event.y >= rulerHeight && laneAtY(event.y) != nullptr) {
            if (event.x < namesWidth) { return; }
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
                    if (chosen) { std::erase_if(selectedKeys, [&](const auto& item) { return item.clip == key->clip && item.property == key->property && sameTime(item.time, key->time); }); }
                    else { selectedKeys.push_back(*key); }
                } else if (!chosen) {
                    selectedKeys = {*key};
                }
                if (isKeySelected(key->clip, key->property, key->time)) { beginKeyDrag(*key, event.x); }
            } else {
                marqueeStart = event.getPosition();
                marqueeBase = event.mods.isShiftDown() ? selectedKeys : std::vector<KeyRef>{};
                selectedKeys = marqueeBase;
                marquee = juce::Rectangle<int>(marqueeStart, marqueeStart);
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
            scrubbing = true;
            seek(event.x, event.mods);
            return;
        }
        const auto group = groupAtY(event.y);
        if (group != 0) { selectClip(group); return; }
        int row = 0;
        const auto* clip = clipAt(event.getPosition(), row);
        if (clip == nullptr) {
            // Empty space: drag a box to select clips (Shift or Cmd adds).
            clipMarqueeBase = event.mods.isShiftDown() || event.mods.isCommandDown() ? selectedClips : std::set<motion::Id>{};
            if (clipMarqueeBase.empty()) { selectClip(0); }
            if (event.x >= namesWidth) { clipMarquee = juce::Rectangle<int>(event.getPosition(), event.getPosition()); clipMarqueeStart = event.getPosition(); }
            return;
        }
        if (event.mods.isShiftDown() || event.mods.isCommandDown()) {
            if (selectedClips.contains(clip->id)) { selectedClips.erase(clip->id); }
            else { selectedClips.insert(clip->id); }
            notifySelection(selectedClips.contains(clip->id) ? clip->id : (selectedClips.empty() ? 0 : *selectedClips.begin()));
            return;
        }
        if (processor.document.project().tracks[row].locked) { selectClip(clip->id); return; }
        original = *clip;
        originalRow = row;
        downX = event.x;
        const auto bounds = clipBounds(original, row);
        if (tool == Tool::ripple && event.x >= bounds.getX() + 8 && event.x <= bounds.getRight() - 8) {
            selectClip(original.id);
            return;
        }
        mode = tool == Tool::slip ? Mode::slip : (tool == Tool::stretch ? Mode::stretch
            : (event.x < bounds.getX() + 8 ? Mode::left : (event.x > bounds.getRight() - 8 ? Mode::right : Mode::move)));
        if (tool == Tool::ripple) { mode = mode == Mode::left ? Mode::rippleLeft : Mode::rippleRight; }
        before = processor.document.project();
        expectedRevision = processor.document.revision();
        changed = false;
        if (!selectedClips.contains(original.id) || mode != Mode::move) { selectClip(original.id); }
        else { notifySelection(original.id); }
    }

    void mouseMove(const juce::MouseEvent& event) override {
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
            setTooltip(bar == 1 ? "Drag to scroll; drag either end to zoom" : "Drag to scroll tracks");
            return;
        }
        const auto edgeTrack = trackEdgeAt(event.getPosition());
        if (edgeTrack != hoveredEdge) { hoveredEdge = edgeTrack; repaint(); }
        const auto loop = loopBounds();
        if (!loop.isEmpty() && event.y >= loopTop - 4 && event.y < loopTop + loopHeight && event.x >= loop.getX() - 5 && event.x <= loop.getRight() + 5) {
            const auto edge = std::abs(event.x - loop.getX()) <= 5 || std::abs(event.x - loop.getRight()) <= 5;
            setMouseCursor(edge ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::DraggingHandCursor);
            setTooltip("Loop: drag to move, drag an end to resize, double-click to switch looping on or off");
            return;
        }
        if (edgeTrack >= 0) {
            setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
            setTooltip("Drag to resize this track; double-click for the default height. Alt+wheel resizes every track.");
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
        if (event.y < rulerHeight && event.x < namesWidth) {
            setMouseCursor(juce::MouseCursor::PointingHandCursor);
        } else if (markerAt(event.getPosition()) != nullptr) {
            setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
        } else if (clip != nullptr) {
            const auto bounds = clipBounds(*clip, row);
            const bool edge = event.x < bounds.getX() + 8 || event.x > bounds.getRight() - 8;
            setMouseCursor((tool == Tool::slip || tool == Tool::stretch || edge) ? juce::MouseCursor::LeftRightResizeCursor : (tool == Tool::ripple ? juce::MouseCursor::NormalCursor : juce::MouseCursor::DraggingHandCursor));
        } else {
            setMouseCursor(juce::MouseCursor::NormalCursor);
        }
    }

    // Overlay scroll bars: vertical for rows, horizontal for time (Premiere
    // style: drag the thumb to scroll, its ends to zoom).
    static constexpr int barSize = 8, scrollStrip = barSize + 4;
    double timelineSpan() const {
        const auto& project = processor.document.project();
        auto end = project.duration;
        for (const auto& track : project.tracks) {
            for (const auto& clip : track.clips) { end = std::max(end, clip.timing(project.tempo()).end()); }
        }
        const auto visible = std::max(1, getWidth() - namesWidth) / pixelsPerSecond;
        return std::max({end * 1.05, scrollTime + visible, 0.001});
    }
    juce::Rectangle<int> horizontalBar() const { return {namesWidth + 2, getHeight() - barSize - 2, std::max(0, getWidth() - namesWidth - 4), barSize}; }
    juce::Rectangle<int> horizontalThumb() const {
        const auto bar = horizontalBar();
        const auto span = timelineSpan();
        const auto visible = std::max(1, getWidth() - namesWidth) / pixelsPerSecond;
        const auto width = std::max(24, juce::roundToInt(bar.getWidth() * std::min(1.0, visible / span)));
        const auto x = bar.getX() + juce::roundToInt((bar.getWidth() - width) * std::clamp(scrollTime / std::max(1e-9, span - visible), 0.0, 1.0));
        return {x, bar.getY(), std::min(width, bar.getWidth()), bar.getHeight()};
    }
    bool horizontalBarShown() const { return std::max(1, getWidth() - namesWidth) / pixelsPerSecond < timelineSpan() * 0.999 || scrollTime > 0; }
    juce::Rectangle<int> verticalBar() const { return {getWidth() - barSize - 2, rulerHeight + 2, barSize, std::max(0, viewHeight() - 4)}; }
    juce::Rectangle<int> verticalThumb() const {
        const auto bar = verticalBar();
        const auto total = std::max(1, contentHeight + bottomRoom);
        const auto height = std::max(24, juce::roundToInt(bar.getHeight() * std::min(1.0, static_cast<double>(viewHeight()) / total)));
        const auto y = bar.getY() + juce::roundToInt((bar.getHeight() - height) * std::clamp(static_cast<double>(scrollY) / std::max(1, maximumScrollY()), 0.0, 1.0));
        return {bar.getX(), y, bar.getWidth(), std::min(height, bar.getHeight())};
    }
    bool verticalBarShown() const { return maximumScrollY() > 0; }
    void paintScrollBars(juce::Graphics& g) const {
        const auto draw = [&](juce::Rectangle<int> bar, juce::Rectangle<int> thumb, bool hot) {
            g.setColour(juce::Colours::white.withAlpha(.04f));
            g.fillRoundedRectangle(bar.toFloat(), barSize * .5f);
            g.setColour(juce::Colours::white.withAlpha(hot ? .4f : .2f));
            g.fillRoundedRectangle(thumb.toFloat(), barSize * .5f);
        };
        if (horizontalBarShown()) { draw(horizontalBar(), horizontalThumb(), barDrag.has_value() || hoveredBar == 1); }
        if (verticalBarShown()) { draw(verticalBar(), verticalThumb(), barDrag.has_value() || hoveredBar == 2); }
    }
    struct BarDrag { enum class Mode { scrollTime, zoomLeft, zoomRight, scrollRows } mode; int down; double scrollTime, pixelsPerSecond; int scrollY; };
    std::optional<BarDrag> barDrag;
    int hoveredBar = 0;
    int barAt(juce::Point<int> point) const {
        if (horizontalBarShown() && horizontalBar().expanded(0, 3).contains(point)) { return 1; }
        if (verticalBarShown() && verticalBar().expanded(3, 0).contains(point)) { return 2; }
        return 0;
    }
    bool barDown(const juce::MouseEvent& event) {
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
            barDrag = BarDrag {mode, event.x, scrollTime, pixelsPerSecond, scrollY};
        } else {
            if (!verticalThumb().contains(verticalThumb().getCentreX(), event.y)) {
                scrollY = std::clamp(juce::roundToInt((event.y - verticalBar().getY()) / static_cast<double>(std::max(1, verticalBar().getHeight())) * maximumScrollY()), 0, maximumScrollY());
            }
            barDrag = BarDrag {BarDrag::Mode::scrollRows, event.y, scrollTime, pixelsPerSecond, scrollY};
        }
        resized();
        repaint();
        return true;
    }
    void barDragged(const juce::MouseEvent& event) {
        const auto& drag = *barDrag;
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
            pixelsPerSecond = std::clamp(width / std::max(1.0 / 120, visible + delta), 0.000001, 500.0);
            scrollTime = drag.scrollTime;
        } else {
            const auto end = drag.scrollTime + visible;
            const auto start = std::clamp(drag.scrollTime + delta, 0.0, end - 1.0 / 120);
            pixelsPerSecond = std::clamp(width / (end - start), 0.000001, 500.0);
            scrollTime = start;
        }
        userScrolled();
        repaint();
    }

    // The loop brace: a bar along the bottom of the time ruler.
    static constexpr int loopTop = 20, loopHeight = 6;
    juce::Rectangle<int> loopBounds() const {
        const auto& project = processor.document.project();
        if (!project.hasLoop()) { return {}; }
        const auto left = std::max(namesWidth, timeX(project.loopStart)), right = std::min(getWidth(), timeX(project.loopEnd));
        return right > left ? juce::Rectangle<int>(left, loopTop, right - left, loopHeight) : juce::Rectangle<int>();
    }
    void paintLoop(juce::Graphics& g) const {
        const auto bounds = loopBounds();
        if (bounds.isEmpty()) { return; }
        const auto on = processor.document.project().looping;
        const auto colour = on ? motion::style::accent() : osci::Colours::textMuted();
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
    struct LoopDrag { enum class Mode { move, left, right } mode; double start, end; int downX; motion::Project before; std::uint64_t revision; bool changed; };
    std::optional<LoopDrag> loopDrag;
    bool loopDown(const juce::MouseEvent& event) {
        const auto bounds = loopBounds();
        if (bounds.isEmpty() || event.y < loopTop - 4 || event.y >= loopTop + loopHeight || event.x < bounds.getX() - 5 || event.x > bounds.getRight() + 5) { return false; }
        const auto& project = processor.document.project();
        if (event.getNumberOfClicks() > 1) {
            processor.document.changeView([](motion::Composition& state) { state.looping = !state.looping; });
            return true;
        }
        const auto mode = std::abs(event.x - bounds.getX()) <= 5 ? LoopDrag::Mode::left : std::abs(event.x - bounds.getRight()) <= 5 ? LoopDrag::Mode::right : LoopDrag::Mode::move;
        loopDrag = LoopDrag {mode, project.loopStart, project.loopEnd, event.x, project, processor.document.revision(), false};
        return true;
    }
    void loopDragged(const juce::MouseEvent& event) {
        auto& drag = *loopDrag;
        if (processor.document.revision() != drag.revision) { loopDrag.reset(); return; }
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
    void mouseExit(const juce::MouseEvent&) override {
        if (hoveredBar != 0) { hoveredBar = 0; repaint(); }
        if (hoveredEdge >= 0) { hoveredEdge = -1; repaint(); }
        if (hoveredClip != 0) { hoveredClip = 0; repaint(); }
        if (addHover) { addHover = false; repaint(); }
    }
    // Project times of every clip start and end (edit points), sorted.
    std::vector<double> editPoints() const {
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
    void mouseDrag(const juce::MouseEvent& event) override {
        if (barDrag.has_value()) { barDragged(event); return; }
        if (loopDrag.has_value()) { loopDragged(event); return; }
        if (heightDrag.has_value()) {
            processor.document.setTrackHeight(heightDrag->track, std::clamp(heightDrag->startHeight + event.y - heightDrag->downY, motion::Track::minimumHeight, motion::Track::maximumHeight));
            layoutRows(); resized(); repaint();
            return;
        }
        if (resizingNames) {
            namesWidth = std::clamp(event.x, 150, std::max(150, std::min(420, getWidth() / 2)));
            refreshTracks();
            repaint();
            return;
        }
        if (cutDrag.has_value()) {
            cameraBandDrag(event);
            return;
        }
        if (keyDrag.has_value()) { dragKeys(event.x, event.mods); return; }
        if (clipMarquee.has_value()) {
            clipMarquee = juce::Rectangle<int>(clipMarqueeStart, event.getPosition());
            auto chosen = clipMarqueeBase;
            motion::Id first = 0;
            const auto& tracks = processor.document.project().tracks;
            for (int row = firstVisibleRow(); row < static_cast<int>(rows.size()) && rowY(row) < getHeight(); ++row) {
                const auto& item = rows[static_cast<std::size_t>(row)];
                if (item.isLane() || item.track < 0) { continue; }
                for (const auto& clip : tracks[static_cast<std::size_t>(item.track)].clips) {
                    if (!clipBounds(clip, item.track).intersects(*clipMarquee)) { continue; }
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
        if (marquee.has_value()) {
            marquee = juce::Rectangle<int>(marqueeStart, event.getPosition());
            selectedKeys = marqueeBase;
            for (auto& key : keysInside(*marquee)) {
                if (!isKeySelected(key.clip, key.property, key.time)) { selectedKeys.push_back(std::move(key)); }
            }
            repaint();
            return;
        }
        if (scrubbing) {
            seek(event.x, event.mods);
            return;
        }
        if (!gestureIsCurrent()) {
            return;
        }
        if (markerDragging != 0) {
            auto updated = *before;
            const auto delta = (event.x - downX) / pixelsPerSecond;
            const auto time = delta == 0 ? markerOriginalTime : std::clamp(snapEdge(markerOriginalTime + delta, event.mods, *before, {}, nullptr, markerDragging), 0.0, updated.duration);
            if (std::any_of(updated.markers.begin(), updated.markers.end(), [this, time](const auto& marker) { return marker.id != markerDragging && std::abs(marker.time - time) < 1.0e-9; })) { return; }
            for (auto& marker : updated.markers) { if (marker.id == markerDragging) { marker.time = time; } }
            std::sort(updated.markers.begin(), updated.markers.end(), [](const auto& a, const auto& b) { return a.time != b.time ? a.time < b.time : a.id < b.id; });
            changed = time != markerOriginalTime;
            processor.document.preview(std::move(updated));
            expectedRevision = processor.document.revision();
            repaint();
            return;
        }
        auto candidate = original;
        const auto timing = original.timing(before->tempo());
        auto edited = timing;
        auto delta = (event.x - downX) / pixelsPerSecond;
        if (delta != 0.0 && !event.mods.isAltDown()) {
            std::set<motion::Id> excluded(selectedClips.begin(), selectedClips.end());
            excluded.insert(original.id);
            if (mode == Mode::slip) {
                const auto anchor = timing.offset / timing.rate;
                delta = before->timeGrid().snap(anchor + delta) - anchor;
            } else if (mode == Mode::move) {
                // Either edge of a moving clip can catch a magnet.
                const auto startTarget = magnet(timing.start + delta, *before, excluded);
                const auto endTarget = magnet(timing.end() + delta, *before, excluded);
                const auto startDistance = startTarget.has_value() ? std::abs(*startTarget - (timing.start + delta)) : 1.0e300;
                const auto endDistance = endTarget.has_value() ? std::abs(*endTarget - (timing.end() + delta)) : 1.0e300;
                if (startTarget.has_value() && startDistance <= endDistance) {
                    delta = *startTarget - timing.start;
                    snapGuide = startTarget;
                } else if (endTarget.has_value()) {
                    delta = *endTarget - timing.end();
                    snapGuide = endTarget;
                } else {
                    delta = before->timeGrid().snap(timing.start + delta) - timing.start;
                    snapGuide.reset();
                }
            } else {
                const auto anchor = mode == Mode::right || mode == Mode::stretch || mode == Mode::rippleRight ? timing.end() : timing.start;
                delta = snapEdge(anchor + delta, event.mods, *before, excluded) - anchor;
            }
        } else {
            snapGuide.reset();
        }
        if (mode == Mode::rippleLeft || mode == Mode::rippleRight) {
            auto updated = *before;
            if (!motion::rippleTrim(updated.tracks[originalRow], original.id, mode == Mode::rippleLeft, delta, updated.tempo())) { return; }
            for (const auto& item : updated.tracks[originalRow].clips) { updated.duration = std::max(updated.duration, item.timing(updated.tempo()).end()); }
            changed = delta != 0;
            processor.document.preview(std::move(updated));
            expectedRevision = processor.document.revision();
            repaint();
            return;
        }
        if (mode == Mode::move && selectedClips.size() > 1) {
            auto updated = *before;
            double earliest = timing.start;
            for (const auto& track : updated.tracks) {
                for (const auto& item : track.clips) {
                    if (selectedClips.contains(item.id)) { earliest = std::min(earliest, item.timing(updated.tempo()).start); }
                }
            }
            delta = std::max(delta, -earliest);
            const auto row = trackAtY(event.y);
            const auto rows = row >= 0 ? row - originalRow : 0;
            if (rows != 0) {
                const auto sourceY = trackY(originalRow);
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
            changed = delta != 0 || rows != 0;
            processor.document.preview(std::move(updated));
            expectedRevision = processor.document.revision();
            repaint();
            return;
        }
        if (delta != 0.0) {
            if (mode == Mode::move) {
                edited.moveTo(std::max(0.0, timing.start + delta));
            } else if (mode == Mode::left) {
                edited.setStart(std::max(0.0, timing.start + delta));
                edited.offset = timing.localTime(edited.start);
            } else if (mode == Mode::right) {
                edited.setEnd(edited.end() + delta);
            } else if (mode == Mode::slip) {
                edited.offset += delta * timing.rate;
            } else {
                edited.setEnd(edited.end() + delta);
                edited.rate *= timing.duration() / edited.duration();
            }
            if (!candidate.setTiming(edited, before->tempo())) { return; }
        }
        const auto target = mode == Mode::move
            ? (trackAtY(event.y) >= 0 ? trackAtY(event.y) : originalRow) : originalRow;
        if (before->tracks[target].locked || before->tracks[target].kind != before->tracks[originalRow].kind) { return; }
        const bool candidateChanged = candidate.start != original.start || candidate.duration != original.duration
            || candidate.offset != original.offset || candidate.rate != original.rate || target != originalRow;
        if (!candidateChanged) {
            if (changed) {
                processor.document.preview(*before);
                expectedRevision = processor.document.revision();
                changed = false;
                repaint();
            }
            return;
        }
        auto updated = *before;
        auto& source = updated.tracks[originalRow].clips;
        source.erase(std::remove_if(source.begin(), source.end(), [&](const auto& clip) { return clip.id == original.id; }), source.end());
        if (updated.tracks[target].insert(candidate, updated.tempo())) {
            updated.duration = std::max(updated.duration, candidate.timing(updated.tempo()).end());
            changed = true;
            processor.document.preview(std::move(updated));
            expectedRevision = processor.document.revision();
            repaint();
        }
    }

    void mouseUp(const juce::MouseEvent&) override {
        snapGuide.reset();
        if (heightDrag.has_value()) { heightDrag.reset(); return; }
        if (barDrag.has_value()) { barDrag.reset(); repaint(); return; }
        if (loopDrag.has_value()) {
            if (loopDrag->changed && processor.document.revision() == loopDrag->revision) {
                const motion::Document::ViewChange view(processor.document);
                processor.document.commit("Move loop", std::move(loopDrag->before));
            }
            loopDrag.reset();
            return;
        }
        if (resizingNames) {
            resizingNames = false;
            return;
        }
        if (cutDrag.has_value()) {
            cutDrag.reset();
            repaint();
            return;
        }
        if (keyDrag.has_value()) { endKeyDrag(); repaint(); return; }
        if (marquee.has_value()) { marquee.reset(); repaint(); return; }
        if (clipMarquee.has_value()) { clipMarquee.reset(); repaint(); return; }
        scrubbing = false;
        if (!gestureIsCurrent()) {
            return;
        }
        auto originalProject = std::move(*before);
        before.reset();
        if (changed) {
            const auto label = markerDragging != 0 ? "Move marker" : (mode == Mode::rippleLeft || mode == Mode::rippleRight) ? "Ripple trim clip" : mode == Mode::move ? (selectedClips.size() > 1 ? "Move clips" : "Move clip") : (mode == Mode::slip ? "Slip clip" : (mode == Mode::stretch ? "Stretch clip" : "Trim clip"));
            processor.document.commit(label, std::move(originalProject));
        }
        changed = false;
        markerDragging = 0;
    }

    void mouseDoubleClick(const juce::MouseEvent& event) override {
        cancelGesture();
        const auto edge = trackEdgeAt(event.getPosition());
        if (edge >= 0 && edge < static_cast<int>(processor.document.project().tracks.size())) {
            // Double-click a row's bottom edge: back to the default height.
            heightDrag.reset();
            processor.document.setTrackHeight(processor.document.project().tracks[static_cast<std::size_t>(edge)].id, 0);
            layoutRows(); resized(); repaint();
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
        if (clip != nullptr && clip->composition != 0 && onEnterComposition) { onEnterComposition(clip->id); }
    }

    // One convention across the timeline, graph and notes: the wheel and
    // trackpad pan (Shift makes the wheel horizontal), Cmd/Ctrl+wheel or a
    // pinch zooms time around the pointer, Alt+wheel changes track height.
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override {
        ensureTrackRows();
        if (before.has_value() || scrubbing) {
            return;
        }
        if (event.mods.isCommandDown() || event.mods.isCtrlDown()) {
            zoomAround(event.x, std::exp((std::abs(wheel.deltaY) > std::abs(wheel.deltaX) ? wheel.deltaY : wheel.deltaX) * 2.5));
        } else if (event.mods.isAltDown()) {
            scaleTrackHeights(event.y, std::exp(wheel.deltaY * 2));
        } else {
            const auto dx = event.mods.isShiftDown() ? wheel.deltaY + wheel.deltaX : wheel.deltaX;
            const auto dy = event.mods.isShiftDown() ? 0.0f : wheel.deltaY;
            constexpr double pixelsPerUnit = 256;
            if (dx != 0) { scrollTime = std::max(0.0, scrollTime - dx * pixelsPerUnit / pixelsPerSecond); }
            if (dy != 0) { scrollY = std::clamp(scrollY - juce::roundToInt(dy * pixelsPerUnit), 0, maximumScrollY()); }
            if (dx != 0) { userScrolled(); }
        }
        resized();
        repaint();
    }
    void mouseMagnify(const juce::MouseEvent& event, float scale) override {
        if (before.has_value() || scrubbing || !(scale > 0)) { return; }
        zoomAround(event.x, scale);
    }
    // Keeps the time under `x` fixed while zooming.
    void zoomAround(int x, double factor) {
        const auto anchorX = std::clamp(x - namesWidth, 0, std::max(0, getWidth() - namesWidth));
        const auto anchorTime = scrollTime + anchorX / pixelsPerSecond;
        pixelsPerSecond = std::clamp(pixelsPerSecond * factor, 0.000001, 500.0);
        scrollTime = std::max(0.0, anchorTime - anchorX / pixelsPerSecond);
        userScrolled();
        repaint();
    }
    void userScrolled() {
        viewAnimation.stopTimer();
        if (following) { followPaused = true; }
        if (onUserScroll) { onUserScroll(); }
    }
    // Vertical zoom: every row without its own height, keeping the row under
    // the pointer in place.
    void scaleTrackHeights(int y, double factor) {
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
        processor.document.setTrackHeights(heights);
        setDefaultTrackHeight(next);
        if (row >= 0 && row < static_cast<int>(rows.size())) { scrollY = std::clamp(scrollY + rowY(row) - offset, 0, maximumScrollY()); }
    }
    void setDefaultTrackHeight(int height) {
        defaultTrackHeight = std::clamp(height, motion::Track::minimumHeight, 120);
        layoutRows();
        if (onDefaultTrackHeight) { onDefaultTrackHeight(defaultTrackHeight); }
        resized();
        repaint();
    }
    std::function<void()> onUserScroll;

    // Page-follows the playhead during playback, like Premiere and Ableton. A
    // manual scroll while playing pauses following until the playhead is back
    // in view (or playback restarts).
    bool followEnabled = true;
    void followPlayhead(double time, bool playing) {
        if (!playing) { following = false; followPaused = false; return; }
        if (!following) { following = true; followPaused = false; }
        const auto x = timeX(time);
        const auto right = getWidth() - 16;
        if (followPaused) {
            if (x >= namesWidth && x <= right) { followPaused = false; }
            return;
        }
        if (!followEnabled || before.has_value() || scrubbing || getWidth() <= namesWidth) { return; }
        if (x > right || x < namesWidth) {
            scrollTime = std::max(0.0, time - 16 / pixelsPerSecond);
            repaint();
        }
    }

    bool keyPressed(const juce::KeyPress& key) override {
        ensureTrackRows();
        if (selected != 0 && key.getModifiers().isCommandDown() && key.getKeyCode() == 'D') {
            duplicateClip(selected);
            return true;
        }
        if (key == juce::KeyPress::escapeKey && (keyDrag.has_value() || marquee.has_value())) {
            cancelKeyDrag();
            return true;
        }
        if (key == juce::KeyPress::escapeKey && loopDrag.has_value()) {
            if (loopDrag->changed && processor.document.revision() == loopDrag->revision) { processor.document.preview(std::move(loopDrag->before)); }
            loopDrag.reset();
            return true;
        }
        if (key == juce::KeyPress::escapeKey && heightDrag.has_value()) {
            processor.document.setTrackHeight(heightDrag->track, heightDrag->original);
            heightDrag.reset();
            layoutRows(); resized(); repaint();
            return true;
        }
        if (key == juce::KeyPress::escapeKey && clipMarquee.has_value()) {
            clipMarquee.reset();
            selectedClips = clipMarqueeBase;
            notifySelection(selectedClips.empty() ? 0 : *selectedClips.begin());
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
        if (key == juce::KeyPress::escapeKey && (before.has_value() || scrubbing)) {
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
                cancelGesture();
                tool = character == 'b' ? Tool::ripple : character == 'v' ? Tool::move : (character == 's' ? Tool::slip : Tool::stretch);
                repaint();
                return true;
            }
            if (character == 'f') {
                fitProject();
                return true;
            }
        }
        if (key == juce::KeyPress::spaceKey) {
            processor.playing.store(!processor.playing.load());
            return true;
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
            if (selectedClips.empty()) { return false; }
            deleteSelectedClips(key.getModifiers().isShiftDown());
            return true;
        }
        return false;
    }

private:
    juce::Colour clipColour(const motion::Clip& clip, const motion::Track& track) const {
        if (track.label > 0 && track.label < static_cast<int>(motion::style::trackLabels().size())) { return juce::Colour(motion::style::trackLabels()[static_cast<std::size_t>(track.label)].argb); }
        if (track.kind == motion::TrackKind::audio) { return motion::style::audioClip(); }
        if (clip.composition != 0) { return motion::style::compositionClip(); }
        if (clip.midi != nullptr) { return motion::style::midiClip(); }
        return motion::style::visualClip();
    }
    // Project times of every key on a clip, restricted to its visible interval.
    std::vector<double> clipKeyTimes(const motion::Clip& clip, const std::string& property = {}) const {
        std::vector<double> times;
        const auto timing = clip.timing(processor.document.project().tempo());
        if (!(timing.rate != 0)) { return times; }
        for (const auto& [name, curve] : clip.properties) {
            if (!property.empty() && name != property) { continue; }
            for (const auto& key : curve.keyframes()) {
                const auto time = timing.projectTime(key.time);
                if (time >= timing.start - 1.0e-9 && time <= timing.end() + 1.0e-9) { times.push_back(time); }
            }
        }
        std::sort(times.begin(), times.end());
        times.erase(std::unique(times.begin(), times.end(), [](double a, double b) { return std::abs(a - b) < 1.0e-9; }), times.end());
        return times;
    }
    void paintClip(juce::Graphics& g, const motion::Clip& clip, const motion::Track& track, int index, float opacity) const {
        const auto& project = processor.document.project();
        const auto bounds = clipBounds(clip, index).toFloat().reduced(1, 3);
        const auto active = selectedClips.contains(clip.id);
        const bool audio = track.kind == motion::TrackKind::audio;
        const auto base = clipColour(clip, track);
        g.setColour((active ? base.brighter(.35f) : clip.id == hoveredClip ? base.brighter(.15f) : base).withAlpha(opacity));
        g.fillRoundedRectangle(bounds, motion::style::radius);
        g.setColour((active ? motion::style::key() : base.brighter(.5f)).withAlpha(opacity * (active ? 1.0f : .55f)));
        g.drawRoundedRectangle(bounds, motion::style::radius, active ? 1.4f : 1.0f);
        g.setFont(motion::style::body());
        if (!clip.effects.empty() && bounds.getWidth() > 90) {
            g.setColour(juce::Colours::white.withAlpha(0.6f * opacity));
            g.setFont(motion::style::caption());
            g.drawText(juce::String(static_cast<int>(clip.effects.size())) + " fx", bounds.withLeft(bounds.getRight() - 38).withTrimmedBottom(8), juce::Justification::centred);
            g.setFont(motion::style::body());
        }
        if (active && tool == Tool::ripple && bounds.getWidth() > 18) {
            g.setColour(motion::style::key().withAlpha(opacity));
            g.fillRoundedRectangle(bounds.getX() + 3, bounds.getY() + 5, 3, bounds.getHeight() - 10, 1);
            g.fillRoundedRectangle(bounds.getRight() - 6, bounds.getY() + 5, 3, bounds.getHeight() - 10, 1);
        }
        auto label = juce::String(clip.name);
        if (active && tool == Tool::slip) {
            label += "  offset " + juce::String(clip.timing(project.tempo()).offset, 2) + "s";
        } else if (active && tool == Tool::stretch) {
            label += "  " + juce::String(clip.timing(project.tempo()).rate, 2) + "x";
        }
        auto labelBounds = bounds.reduced(8, 0).withTrimmedRight(!clip.effects.empty() && bounds.getWidth() > 90 ? 32.0f : 0.0f).withTrimmedBottom(7);
        if (audio) {
            const auto& assets = project.assets;
            const auto asset = std::find_if(assets.begin(), assets.end(), [&](const auto& item) { return item->id == clip.asset; });
            if (asset != assets.end() && (*asset)->audio != nullptr) {
                const auto left = std::max(namesWidth, static_cast<int>(bounds.getX()) + 2);
                const auto right = std::min(getWidth(), static_cast<int>(bounds.getRight()) - 2);
                const auto centre = bounds.getBottom() - 7.5f;
                g.setColour(juce::Colour(0xff97c7df).withAlpha(opacity * .8f));
                for (int x = left; x < right; ++x) {
                    const auto time = scrollTime + (x - namesWidth) / pixelsPerSecond;
                    const auto end = time + 1.0 / pixelsPerSecond;
                    const auto a = (*asset)->audio->querySeconds(0, clip.localTime(time, project.tempo()), clip.localTime(end, project.tempo()));
                    const auto b = (*asset)->audio->querySeconds(1, clip.localTime(time, project.tempo()), clip.localTime(end, project.tempo()));
                    const auto low = std::clamp(std::min(a.minimum, b.minimum), -1.0f, 1.0f);
                    const auto high = std::clamp(std::max(a.maximum, b.maximum), -1.0f, 1.0f);
                    g.drawVerticalLine(x, centre - high * 6, centre - low * 6 + 0.5f);
                }
            }
            labelBounds = labelBounds.withHeight(15);
        }
        g.setColour(juce::Colours::white.withAlpha(.92f * opacity));
        g.drawText(label, labelBounds, juce::Justification::centredLeft);
        // Key summary ticks along the bottom edge.
        g.setColour(motion::style::key().withAlpha(.8f * opacity));
        double lastX = -10;
        for (const auto time : clipKeyTimes(clip)) {
            const auto x = static_cast<float>(timeX(time));
            if (x - lastX < 3 || x < bounds.getX() - 2 || x > bounds.getRight() + 2) { continue; }
            // Keys on a clip edge are inset so they are never cut in half.
            motion::style::drawDiamond(g, {std::clamp(x, bounds.getX() + 3.0f, std::max(bounds.getX() + 3.0f, bounds.getRight() - 3.0f)), bounds.getBottom() - 5.0f}, 3.5f, true);
            lastX = x;
        }
    }
    void paintLane(juce::Graphics& g, const Row& row, int y, int height) const {
        const auto& project = processor.document.project();
        const auto& track = project.tracks[static_cast<std::size_t>(row.track)];
        g.setColour(motion::style::sunken());
        g.fillRect(0, y, getWidth(), height);
        const auto specs = track.kind == motion::TrackKind::audio ? motion::audioPropertySpecs() : motion::objectPropertySpecs();
        const auto* spec = motion::findPropertySpec(specs, row.lane);
        g.setFont(motion::style::caption());
        g.setColour(motion::style::muted());
        const auto indent = std::min(48, row.depth * 8) + 30;
        g.drawText(spec != nullptr ? juce::String(spec->label.data(), spec->label.size()) : juce::String(row.lane), indent, y, namesWidth - indent - 6, height, juce::Justification::centredLeft);
        juce::Graphics::ScopedSaveState scope(g);
        g.reduceClipRegion(namesWidth, y, getWidth() - namesWidth, height);
        const auto centre = y + height * .5f;
        for (const auto& clip : track.clips) {
            const auto timing = clip.timing(project.tempo());
            const auto left = timeX(timing.start), right = timeX(timing.end());
            g.setColour(clipColour(clip, track).withAlpha(.18f));
            g.fillRect(left, y + 2, std::max(1, right - left), height - 4);
            const auto found = clip.properties.find(row.lane);
            if (found == clip.properties.end() || timing.rate == 0) { continue; }
            const auto& keys = found->second.keyframes();
            // A faint value curve normalised to the lane shows each move's shape.
            if (keys.size() > 1) {
                const auto first = std::max(left, namesWidth), last = std::min(right, getWidth());
                double low = std::numeric_limits<double>::max(), high = std::numeric_limits<double>::lowest();
                for (const auto& key : keys) { low = std::min(low, key.value); high = std::max(high, key.value); }
                const auto localAt = [&](int x) { return timing.localTime(scrollTime + (x - namesWidth) / pixelsPerSecond); };
                for (int x = first; x < last; x += 3) {
                    const auto value = found->second.evaluateBase(localAt(x));
                    low = std::min(low, value); high = std::max(high, value);
                }
                if (high > low && last > first) {
                    juce::Path shape;
                    const auto yOf = [&](double value) { return static_cast<float>(y + height - 4 - (value - low) / (high - low) * (height - 8)); };
                    shape.startNewSubPath(static_cast<float>(first), yOf(found->second.evaluateBase(localAt(first))));
                    for (int x = first + 2; x < last; x += 2) { shape.lineTo(static_cast<float>(x), yOf(found->second.evaluateBase(localAt(x)))); }
                    g.setColour(motion::style::key().withAlpha(.35f));
                    g.strokePath(shape, juce::PathStrokeType(1.0f));
                }
            }
            // Key shape encodes its outgoing interpolation: square hold,
            // diamond linear, hourglass eased, circle auto/Bezier.
            for (std::size_t k = 0; k < keys.size(); ++k) {
                const auto time = timing.projectTime(keys[k].time);
                // A key at the very start stays clear of the name column.
                const auto x = std::max(static_cast<float>(timeX(time)), static_cast<float>(namesWidth) + 4.5f);
                const bool chosen = isKeySelected(clip.id, row.lane, keys[k].time);
                g.setColour(chosen ? juce::Colours::white : motion::style::key());
                const auto shape = keys[k].interpolation == motion::Interpolation::hold ? motion::style::KeyShape::hold
                    : keys[k].interpolation == motion::Interpolation::linear ? motion::style::KeyShape::linear
                    : motion::isEased(keys[k]) ? motion::style::KeyShape::eased : motion::style::KeyShape::smooth;
                motion::style::drawKeyShape(g, {x, centre}, 4.5f, shape);
                if (chosen) {
                    g.setColour(motion::style::key());
                    g.drawEllipse(x - 6, centre - 6, 12, 12, 1.0f);
                }
            }
        }
        if (marquee.has_value()) {
            g.setColour(motion::style::accent().withAlpha(.12f));
            g.fillRect(*marquee);
            g.setColour(motion::style::accent().withAlpha(.6f));
            g.drawRect(*marquee);
        }
    }
    static bool sameTime(double a, double b) { return std::abs(a - b) < 1.0e-6; }
    bool isKeySelected(motion::Id clip, const std::string& property, double time) const {
        return std::any_of(selectedKeys.begin(), selectedKeys.end(), [&](const auto& key) { return key.clip == clip && key.property == property && sameTime(key.time, time); });
    }
    const motion::Clip* findClip(motion::Id id, const motion::Project& project) const {
        for (const auto& track : project.tracks) {
            for (const auto& clip : track.clips) { if (clip.id == id) { return &clip; } }
        }
        return nullptr;
    }
    // Keys whose diamond lies under a point in a lane row.
    std::optional<KeyRef> keyAt(juce::Point<int> point) const {
        const auto* lane = laneAtY(point.y);
        if (lane == nullptr || point.x < namesWidth) { return std::nullopt; }
        const auto& project = processor.document.project();
        const auto& track = project.tracks[static_cast<std::size_t>(lane->track)];
        std::optional<KeyRef> best;
        auto bestDistance = 7.0;
        for (const auto& clip : track.clips) {
            const auto found = clip.properties.find(lane->lane);
            const auto timing = clip.timing(project.tempo());
            if (found == clip.properties.end() || timing.rate == 0) { continue; }
            for (const auto& key : found->second.keyframes()) {
                const auto distance = std::abs(timeX(timing.projectTime(key.time)) - point.x);
                if (distance < bestDistance) { bestDistance = distance; best = KeyRef{clip.id, lane->lane, key.time}; }
            }
        }
        return best;
    }
    std::vector<KeyRef> keysInside(juce::Rectangle<int> area) const {
        std::vector<KeyRef> result;
        const auto& project = processor.document.project();
        for (int visible = firstVisibleRow(); visible < static_cast<int>(rows.size()); ++visible) {
            const auto& row = rows[static_cast<std::size_t>(visible)];
            const auto y = rowY(visible);
            if (!row.isLane() || y + laneHeight / 2 < area.getY() || y + laneHeight / 2 > area.getBottom()) { continue; }
            for (const auto& clip : project.tracks[static_cast<std::size_t>(row.track)].clips) {
                const auto found = clip.properties.find(row.lane);
                const auto timing = clip.timing(project.tempo());
                if (found == clip.properties.end() || timing.rate == 0) { continue; }
                for (const auto& key : found->second.keyframes()) {
                    const auto x = timeX(timing.projectTime(key.time));
                    if (x >= area.getX() && x <= area.getRight()) { result.push_back({clip.id, row.lane, key.time}); }
                }
            }
        }
        return result;
    }
    // Moves keys by a project-time delta on a copy of the gesture's project.
    // Fails when a moved key would land on a key that is not moving.
    static bool moveKeys(motion::Project& project, const std::vector<KeyRef>& keys, double delta) {
        for (auto& track : project.tracks) {
            if (track.locked) {
                const bool touched = std::any_of(track.clips.begin(), track.clips.end(), [&](const auto& clip) {
                    return std::any_of(keys.begin(), keys.end(), [&](const auto& key) { return key.clip == clip.id; });
                });
                if (touched) { return false; }
                continue;
            }
            for (auto& clip : track.clips) {
                const auto timing = clip.timing(project.tempo());
                for (auto& [name, curve] : clip.properties) {
                    std::vector<motion::Keyframe> moving;
                    for (const auto& key : curve.keyframes()) {
                        const bool chosen = std::any_of(keys.begin(), keys.end(), [&](const auto& item) { return item.clip == clip.id && item.property == name && sameTime(item.time, key.time); });
                        if (chosen) { moving.push_back(key); }
                    }
                    if (moving.empty()) { continue; }
                    for (const auto& key : moving) { curve.removeKey(key.time); }
                    for (auto key : moving) {
                        key.time = timing.localTime(timing.projectTime(key.time) + delta);
                        const auto& remaining = curve.keyframes();
                        if (std::any_of(remaining.begin(), remaining.end(), [&](const auto& other) { return sameTime(other.time, key.time); })) { return false; }
                        curve.setKey(key);
                    }
                }
            }
        }
        return true;
    }
    void beginKeyDrag(const KeyRef& grabbed, int x) {
        keyDrag = KeyDrag{processor.document.project(), selectedKeys, grabbed, x, processor.document.revision(), false};
    }
    void dragKeys(int x, juce::ModifierKeys modifiers) {
        if (!keyDrag.has_value()) { return; }
        if (processor.document.revision() != keyDrag->revision) { keyDrag.reset(); return; }
        const auto* clip = findClip(keyDrag->grabbed.clip, keyDrag->before);
        if (clip == nullptr) { return; }
        const auto timing = clip->timing(keyDrag->before.tempo());
        if (timing.rate == 0) { return; }
        const auto grabbedTime = timing.projectTime(keyDrag->grabbed.time);
        auto delta = (x - keyDrag->downX) / pixelsPerSecond;
        if (delta != 0) { delta = snapEdge(grabbedTime + delta, modifiers, keyDrag->before, {}, &keyDrag->keys) - grabbedTime; }
        auto updated = keyDrag->before;
        if (delta != 0 && !moveKeys(updated, keyDrag->keys, delta)) { return; }
        keyDrag->changed = delta != 0;
        selectedKeys = keyDrag->keys;
        for (auto& key : selectedKeys) {
            const auto* owner = findClip(key.clip, keyDrag->before);
            if (owner != nullptr) { key.time += delta * owner->timing(keyDrag->before.tempo()).rate; }
        }
        processor.document.preview(std::move(updated));
        keyDrag->revision = processor.document.revision();
        repaint();
    }
    void endKeyDrag() {
        if (keyDrag.has_value() && keyDrag->changed && processor.document.revision() == keyDrag->revision) {
            processor.document.commit(keyDrag->keys.size() > 1 ? "Move keyframes" : "Move keyframe", std::move(keyDrag->before));
        }
        keyDrag.reset();
    }
    void cancelKeyDrag() {
        if (keyDrag.has_value() && keyDrag->changed && processor.document.revision() == keyDrag->revision) {
            selectedKeys = keyDrag->keys;
            processor.document.preview(std::move(keyDrag->before));
        }
        keyDrag.reset();
        marquee.reset();
        repaint();
    }
public:
    bool easeSelectedKeys(bool in, bool out) {
        if (selectedKeys.empty()) { return false; }
        const auto keys = selectedKeys;
        const auto eased = processor.document.tryEdit(in && out ? "Easy ease" : (in ? "Easy ease in" : "Easy ease out"), [&keys, in, out](motion::Project& project) {
            bool any = false;
            for (auto& track : project.tracks) {
                for (auto& clip : track.clips) {
                    for (const auto& key : keys) {
                        if (key.clip != clip.id) { continue; }
                        if (track.locked) { return false; }
                        const auto found = clip.properties.find(key.property);
                        any = (found != clip.properties.end() && motion::easeKey(found->second, key.time, in, out)) || any;
                    }
                }
            }
            return any;
        });
        repaint();
        return eased;
    }
private:
    void deleteSelectedKeys() {
        const auto keys = selectedKeys;
        const auto removed = processor.document.tryEdit(keys.size() > 1 ? "Delete keyframes" : "Delete keyframe", [&keys](motion::Project& project) {
            bool any = false;
            for (auto& track : project.tracks) {
                for (auto& clip : track.clips) {
                    for (const auto& key : keys) {
                        if (key.clip != clip.id) { continue; }
                        if (track.locked) { return false; }
                        const auto found = clip.properties.find(key.property);
                        if (found != clip.properties.end()) { any = found->second.removeKey(key.time) || any; }
                    }
                }
            }
            return any;
        });
        if (removed) { selectedKeys.clear(); }
        repaint();
    }
    void addKeyAt(const Row& lane, int x, juce::ModifierKeys modifiers) {
        const auto& project = processor.document.project();
        const auto time = snapTime(scrollTime + (x - namesWidth) / pixelsPerSecond, modifiers);
        const auto& track = project.tracks[static_cast<std::size_t>(lane.track)];
        const motion::Clip* target = nullptr;
        for (const auto& clip : track.clips) {
            const auto timing = clip.timing(project.tempo());
            if (time >= timing.start && time <= timing.end()) { target = &clip; }
        }
        if (target == nullptr || track.locked) { return; }
        const auto clipId = target->id;
        const auto local = target->timing(project.tempo()).localTime(time);
        const auto property = lane.lane;
        const auto added = processor.document.tryEdit("Add keyframe", [&](motion::Project& updated) {
            auto* curve = motion::findPropertyCurve(updated, clipId, property);
            if (curve == nullptr) { return false; }
            curve->setKeyValue(local, curve->evaluateBase(local));
            return true;
        });
        if (added) { selectedKeys = {{clipId, property, local}}; }
        repaint();
    }
    void showKeyMenu() {
        if (selectedKeys.empty()) { return; }
        juce::PopupMenu menu;
        menu.addItem(1, "Hold");
        menu.addItem(2, "Linear");
        menu.addItem(3, "Auto");
        menu.addItem(4, "Bezier");
        menu.addSeparator();
        menu.addItem(10, "Delete");
        const auto revision = processor.document.revision();
        const juce::Component::SafePointer<MotionTimelineView> owner(this);
        menu.setLookAndFeel(&getLookAndFeel());
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withMousePosition(), [owner, revision](int result) {
            if (owner == nullptr || result == 0 || owner->processor.document.revision() != revision) { return; }
            if (result == 10) { owner->deleteSelectedKeys(); return; }
            const auto shape = static_cast<motion::Interpolation>(result - 1);
            const auto keys = owner->selectedKeys;
            owner->processor.document.tryEdit("Change key interpolation", [&](motion::Project& project) {
                bool any = false;
                for (auto& track : project.tracks) {
                    for (auto& clip : track.clips) {
                        for (const auto& key : keys) {
                            if (key.clip != clip.id || track.locked) { continue; }
                            const auto found = clip.properties.find(key.property);
                            if (found == clip.properties.end()) { continue; }
                            auto& curve = found->second;
                            const auto& all = curve.keyframes();
                            for (std::size_t index = 0; index < all.size(); ++index) {
                                if (!sameTime(all[index].time, key.time) || all[index].interpolation == shape) { continue; }
                                auto updated = all[index];
                                if (shape == motion::Interpolation::cubic && index + 1 < all.size()) {
                                    auto following = all[index + 1];
                                    updated.outgoingSlope = curve.automaticSlope(index);
                                    following.incomingSlope = curve.automaticSlope(index + 1);
                                    curve.setKey(following);
                                }
                                updated.interpolation = shape;
                                curve.setKey(updated);
                                any = true;
                                break;
                            }
                        }
                    }
                }
                return any;
            });
        });
    }
    void deleteSelectedClips(bool ripple) {
        cancelGesture();
        const auto result = processor.document.removeClips({selectedClips.begin(), selectedClips.end()}, ripple);
        if (result.failed()) { if (onError) { onError(result.getErrorMessage()); } return; }
        selectClip(0);
        refreshTracks();
    }
    void duplicateClip(motion::Id id) {
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
    // The camera track: a band under the ruler (and markers) whose clips are
    // the camera cuts. Between cuts the first camera (or the default view)
    // shows.
    static constexpr int cameraBandHeight = 22;
    // The marker band also carries tempo changes.
    bool showsMarkerBand() const { return !processor.document.project().markers.empty() || processor.document.project().tempoChanges != nullptr; }
    int cameraBandTop() const { return showsMarkerBand() ? 48 : 26; }
    bool showsCameraBand() const { return true; }
    juce::Rectangle<int> addCameraBounds() const { return {namesWidth - 26, cameraBandTop() + 2, 20, cameraBandHeight - 4}; }
    bool inCameraBand(int y) const { return showsCameraBand() && y >= cameraBandTop() && y < cameraBandTop() + cameraBandHeight; }
    juce::Rectangle<int> cutBounds(const motion::CameraCut& cut) const {
        const auto left = std::max(namesWidth, timeX(cut.start));
        const auto right = std::min(getWidth(), timeX(cut.end()));
        if (right <= left) { return {}; }
        return {left, cameraBandTop() + 2, right - left, cameraBandHeight - 4};
    }
    static juce::Colour cameraColour(const motion::Project& project, motion::Id camera) {
        const auto found = std::find_if(project.cameras.begin(), project.cameras.end(), [camera](const auto& item) { return item.id == camera; });
        const auto index = found == project.cameras.end() ? 0 : static_cast<int>(found - project.cameras.begin());
        return juce::Colour::fromHSV(std::fmod(0.58f + 0.17f * static_cast<float>(index), 1.0f), 0.32f, 0.42f, 1.0f);
    }
    juce::String cameraName(motion::Id camera) const {
        for (const auto& item : processor.document.project().cameras) {
            if (item.id == camera) { return juce::String(item.name); }
        }
        return "Camera";
    }
    void paintCameraBand(juce::Graphics& g) const {
        const auto& project = processor.document.project();
        const auto top = cameraBandTop();
        g.setColour(osci::Colours::surfaceRaised().darker(.15f));
        g.fillRect(namesWidth, top, getWidth() - namesWidth, cameraBandHeight);
        g.setColour(motion::style::muted());
        g.setFont(motion::style::caption());
        g.drawText("Cameras", 12, top, namesWidth - 40, cameraBandHeight, juce::Justification::centredLeft);
        // The green plus adds a camera at the playhead.
        const auto add = addCameraBounds().toFloat();
        g.setColour(motion::style::accent().withAlpha(addHover ? .32f : .2f));
        g.fillRoundedRectangle(add, motion::style::radius);
        g.setColour(motion::style::accent().brighter(.3f));
        const auto c = add.getCentre();
        g.fillRect(juce::Rectangle<float>(9.0f, 1.5f).withCentre(c));
        g.fillRect(juce::Rectangle<float>(1.5f, 9.0f).withCentre(c));
        // Between cuts the first camera shows; label each visible gap.
        const bool defaultSelected = !project.cameras.empty() && selected == project.cameras.front().id;
        const auto label = project.cameras.empty() ? juce::String("Default view") : juce::String(project.cameras.front().name);
        double from = 0;
        const auto gap = [&](double end) {
            const auto left = std::max(namesWidth, timeX(from)), right = std::min(getWidth(), timeX(end));
            if (right <= left) { return; }
            if (defaultSelected) {
                g.setColour(motion::style::accent().withAlpha(.12f));
                g.fillRect(left, top + 2, right - left, cameraBandHeight - 4);
            }
            g.setColour(motion::style::muted().withAlpha(defaultSelected ? .9f : .55f));
            if (right - left > 60) { g.drawText(label, left + 6, top, right - left - 10, cameraBandHeight, juce::Justification::centredLeft, true); }
        };
        for (const auto& cut : project.cameraCuts) {
            if (cut.start > from) { gap(cut.start); }
            from = std::max(from, cut.end());
        }
        if (from < project.duration) { gap(project.duration); }
        for (const auto& cut : project.cameraCuts) {
            const auto bounds = cutBounds(cut);
            if (bounds.isEmpty()) { continue; }
            g.setColour(cameraColour(project, cut.camera).withAlpha(cut.id == selectedCut ? 1.0f : .85f));
            g.fillRoundedRectangle(bounds.toFloat(), 3.0f);
            if (cut.id == selectedCut || cut.camera == selected) {
                g.setColour(motion::style::accent());
                g.drawRoundedRectangle(bounds.toFloat().reduced(.5f), 3.0f, 1.2f);
            }
            g.setColour(osci::Colours::text());
            g.drawText(cameraName(cut.camera), bounds.reduced(6, 0), juce::Justification::centredLeft, true);
        }
    }
    bool addHover = false;
    const motion::CameraCut* findCut(motion::Id id) const {
        for (const auto& cut : processor.document.project().cameraCuts) {
            if (cut.id == id) { return &cut; }
        }
        return nullptr;
    }
    const motion::CameraCut* cutAt(juce::Point<int> point) const {
        for (const auto& cut : processor.document.project().cameraCuts) {
            if (cutBounds(cut).expanded(3, 0).contains(point)) { return &cut; }
        }
        return nullptr;
    }
    void cameraBandDown(const juce::MouseEvent& event) {
        if (addCameraBounds().contains(event.getPosition())) {
            if (onAddCamera) { onAddCamera(); }
            return;
        }
        if (event.x < namesWidth) { return; }
        const auto time = std::clamp(snapTime(scrollTime + (event.x - namesWidth) / pixelsPerSecond, event.mods), 0.0, processor.document.project().duration);
        const auto* cut = cutAt(event.getPosition());
        selectedCut = cut != nullptr ? cut->id : 0;
        // A cut, or the gap where the first camera shows, selects that camera.
        const auto& cameras = processor.document.project().cameras;
        const auto camera = cut != nullptr ? cut->camera : cameras.empty() ? motion::Id(0) : cameras.front().id;
        if (camera != 0 && onSelection) { onSelection(camera); }
        if (event.mods.isPopupMenu()) {
            showCutMenu(selectedCut, time);
            repaint();
            return;
        }
        if (cut == nullptr) {
            if (camera == 0) { processor.seek(time); }
            repaint();
            return;
        }
        const auto bounds = cutBounds(*cut);
        CutDrag drag;
        drag.cut = cut->id;
        drag.start = cut->start;
        drag.end = cut->end();
        drag.downX = event.x;
        drag.mode = std::abs(event.x - bounds.getX()) <= 5 ? CutDrag::Mode::left : (std::abs(event.x - bounds.getRight()) <= 5 ? CutDrag::Mode::right : CutDrag::Mode::move);
        cutDrag = drag;
        repaint();
    }
    void cameraBandDrag(const juce::MouseEvent& event) {
        auto& drag = *cutDrag;
        const auto& project = processor.document.project();
        const auto delta = (event.x - drag.downX) / pixelsPerSecond;
        if (delta == 0) { return; }
        auto start = drag.start, end = drag.end;
        // Neighbouring cuts bound every gesture; nothing overlaps.
        double lower = 0, upper = project.duration;
        for (const auto& other : project.cameraCuts) {
            if (other.id == drag.cut) { continue; }
            if (other.end() <= drag.start + 1.0e-9) { lower = std::max(lower, other.end()); }
            if (other.start >= drag.end - 1.0e-9) { upper = std::min(upper, other.start); }
        }
        const auto frame = project.frameRate > 0 ? 1.0 / project.frameRate : 1.0 / 30;
        if (drag.mode == CutDrag::Mode::move) {
            start = std::clamp(snapEdge(drag.start + delta, event.mods, project, {}), lower, upper - (drag.end - drag.start));
            end = start + (drag.end - drag.start);
        } else if (drag.mode == CutDrag::Mode::left) {
            start = std::clamp(snapEdge(drag.start + delta, event.mods, project, {}), lower, drag.end - frame);
        } else {
            end = std::clamp(snapEdge(drag.end + delta, event.mods, project, {}), drag.start + frame, upper);
        }
        report(processor.document.setCutRange(drag.cut, start, end, drag.mode == CutDrag::Mode::move ? "Move camera cut" : "Trim camera cut"));
        repaint();
    }
    void showCutMenu(motion::Id cut, double time) {
        const auto& project = processor.document.project();
        juce::PopupMenu menu, cutTo, show;
        menu.setLookAndFeel(&getLookAndFeel());
        for (std::size_t index = 0; index < project.cameras.size(); ++index) {
            cutTo.addItem(100 + static_cast<int>(index), juce::String(project.cameras[index].name));
            show.addItem(200 + static_cast<int>(index), juce::String(project.cameras[index].name));
        }
        menu.addItem(3, "Add camera here");
        menu.addSubMenu("Cut to camera here", cutTo, !project.cameras.empty());
        if (cut != 0) {
            menu.addSubMenu("Show camera", show);
            menu.addItem(1, "Delete cut");
        }
        const auto* cutValue = findCut(cut);
        const auto camera = cutValue != nullptr ? cutValue->camera : project.cameras.empty() ? motion::Id(0) : project.cameras.front().id;
        if (camera != 0) {
            menu.addSeparator();
            menu.addItem(2, "Delete " + cameraName(camera));
        }
        juce::Component::SafePointer<MotionTimelineView> safe(this);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withMousePosition(), [safe, cut, time, camera](int result) {
            if (safe == nullptr || result == 0) { return; }
            auto& document = safe->processor.document;
            const auto& cameras = document.project().cameras;
            if (result == 3) {
                safe->processor.seek(time);
                if (safe->onAddCamera) { safe->onAddCamera(); }
            } else if (result == 2) {
                safe->report(document.removeCamera(camera));
                safe->selectedCut = 0;
                if (safe->onSelection) { safe->onSelection(0); }
            } else if (result == 1) {
                safe->report(document.removeCut(cut));
                safe->selectedCut = 0;
            } else if (result >= 200 && result - 200 < static_cast<int>(cameras.size())) {
                safe->report(document.setCutCamera(cut, cameras[static_cast<std::size_t>(result - 200)].id));
            } else if (result >= 100 && result - 100 < static_cast<int>(cameras.size())) {
                motion::Id created = 0;
                safe->report(document.cutToCamera(cameras[static_cast<std::size_t>(result - 100)].id, time, created));
                safe->selectedCut = created;
            }
            safe->repaint();
        });
    }
    void report(const juce::Result& result) {
        if (result.failed() && onError) { onError(result.getErrorMessage()); }
    }
    struct CutDrag {
        enum class Mode { move, left, right };
        motion::Id cut = 0;
        double start = 0, end = 0;
        int downX = 0;
        Mode mode = Mode::move;
    };
    std::optional<CutDrag> cutDrag;
    motion::Id selectedCut = 0;

    juce::Rectangle<int> markerBounds(const motion::Marker& marker) const {
        const auto x = timeX(marker.time);
        if (x < namesWidth || x >= getWidth()) { return {}; }
        auto right = std::min(getWidth(), x + 140);
        for (const auto& next : processor.document.project().markers) {
            if (next.time > marker.time) { right = std::min(right, timeX(next.time) - 2); break; }
        }
        return {x, 27, std::max(3, right - x), 20};
    }
    const motion::Marker* markerAt(juce::Point<int> point) const {
        if (point.y < 26 || point.y >= cameraBandTop()) { return nullptr; }
        for (const auto& marker : processor.document.project().markers) {
            if (markerBounds(marker).contains(point)) { return &marker; }
        }
        return nullptr;
    }
    void selectMarker(motion::Id id) {
        selected = 0;
        selectedClips.clear();
        if (onSelection) { onSelection(0); }
        selectedMarker = id;
    }
    void jumpMarker(bool forward) {
        const auto now = processor.position.load();
        const motion::Marker* target = nullptr;
        for (const auto& marker : processor.document.project().markers) {
            if (forward && marker.time > now + 0.000001) { target = &marker; break; }
            if (!forward && marker.time < now - 0.000001) { target = &marker; }
        }
        if (target != nullptr) {
            const auto id = target->id;
            const auto time = target->time;
            selectMarker(id); processor.seek(time); revealTime(time); repaint();
        }
    }
    void showMarkerMenu(motion::Id id, double time) {
        juce::PopupMenu menu;
        if (id != 0) {
            menu.addItem(1, "Edit marker...");
            menu.addItem(2, "Delete marker");
            menu.addSeparator();
        }
        menu.addItem(3, "Add marker here...");
        menu.addItem(4, "Previous marker", !processor.document.project().markers.empty());
        menu.addItem(5, "Next marker", !processor.document.project().markers.empty());
        menu.addSeparator();
        const auto tempo = processor.document.project().tempo();
        const auto beat = std::round(tempo.beats(time) * 4) / 4;
        const auto* change = tempoChangeNear(time);
        if (change != nullptr) {
            menu.addItem(7, "Edit tempo change...");
            menu.addItem(9, "Glide into this tempo", true, change->ramp);
            menu.addItem(8, "Remove tempo change");
        } else {
            menu.addItem(6, "Add tempo change here...", beat > 0);
        }
        const auto generation = processor.document.generation();
        const auto revision = processor.document.revision();
        const juce::Component::SafePointer<MotionTimelineView> owner(this);
        menu.setLookAndFeel(&getLookAndFeel());
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withMousePosition(), [owner, id, time, generation, revision](int result) {
            if (owner == nullptr || owner->processor.document.generation() != generation || owner->processor.document.revision() != revision) { return; }
            if ((result == 1 || result == 3) && owner->onEditMarker) { owner->onEditMarker(result == 1 ? id : 0, time); }
            if (result == 2) { const auto removed = owner->processor.document.removeMarker(id); if (removed.failed() && owner->onError) { owner->onError(removed.getErrorMessage()); } }
            if (result == 4 || result == 5) { owner->jumpMarker(result == 5); }
            if (result == 6 || result == 7) { owner->editTempoChange(time); }
            if (result == 9) {
                const auto* existing = owner->tempoChangeNear(time);
                if (existing != nullptr) {
                    const auto changed = owner->processor.document.setTempoChange(existing->beat, existing->bpm, existing->beat, !existing->ramp);
                    if (changed.failed() && owner->onError) { owner->onError(changed.getErrorMessage()); }
                }
            }
            if (result == 8) {
                const auto* existing = owner->tempoChangeNear(time);
                if (existing != nullptr) {
                    const auto removed = owner->processor.document.removeTempoChange(existing->beat);
                    if (removed.failed() && owner->onError) { owner->onError(removed.getErrorMessage()); }
                }
            }
        });
    }
    // The tempo change whose flag is within a few pixels of `time`.
    const motion::TempoChange* tempoChangeNear(double time) const {
        const auto& project = processor.document.project();
        if (project.tempoChanges == nullptr) { return nullptr; }
        const auto tempo = project.tempo();
        for (const auto& change : *project.tempoChanges) {
            if (std::abs(timeX(tempo.seconds(change.beat)) - timeX(time)) <= 6) { return &change; }
        }
        return nullptr;
    }
    // Adds (or edits) a tempo change at the nearest quarter beat.
    void editTempoChange(double time) {
        const auto tempo = processor.document.project().tempo();
        const auto* existing = tempoChangeNear(time);
        const auto beat = existing != nullptr ? existing->beat : std::round(tempo.beats(time) * 4) / 4;
        const auto current = existing != nullptr ? existing->bpm : tempo.bpmAt(time);
        if (onEditTempo) { onEditTempo(beat, current, existing != nullptr ? std::optional<double>(existing->beat) : std::nullopt); }
    }

    void showClipMenu(motion::Id id) {
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
            menu.addItem(motion::style::menuItem(edits[static_cast<std::size_t>(index)].first, 20 + index, edits[static_cast<std::size_t>(index)].second).setEnabled(index != 1 ? !locked : true));
        }
        menu.addItem(motion::style::menuItem(many ? "Duplicate clips" : "Duplicate clip", 1, "Cmd+D").setEnabled(!locked));
        menu.addSeparator();
        menu.addItem(2, "Edit clip timing");
        menu.addItem(motion::style::menuItem(many ? "Loop selected clips" : "Loop this clip", 9, "Shift+L"));
        if (asset != 0) { menu.addItem(10, "Show source in Assets"); }
        menu.addSeparator();
        menu.addItem(motion::style::menuItem(many ? "Delete clips" : "Delete clip", 7, "Delete").setEnabled(!locked));
        menu.addItem(motion::style::menuItem(many ? "Ripple delete clips" : "Ripple delete clip", 8, "Shift+Delete").setEnabled(!locked));
        const auto references = motion::sourceReferenceCount(processor.document.mainProject(), asset);
        if (definition == 0) { menu.addItem(3, "Make this clip's source unique", !locked && asset != 0 && references > 1); }
        menu.addSeparator();
        menu.addItem(4, "Create composition from selection", !locked && !selectedClips.empty());
        if (definition != 0) { menu.addItem(5, "Open composition"); menu.addItem(6, "Make composition unique", !locked); }
        const auto generation = processor.document.generation();
        const auto revision = processor.document.revision();
        const juce::Component::SafePointer<MotionTimelineView> owner(this);
        menu.setLookAndFeel(&getLookAndFeel());
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withMousePosition(), [owner, id, generation, revision](int result) {
            if (owner == nullptr || result == 0 || owner->processor.document.generation() != generation || owner->processor.document.revision() != revision) { return; }
            if (result >= 20 && result < 24 && owner->onCommand) {
                const std::array<const char*, 4> names {"Cut", "Copy", "Paste", "Split at playhead"};
                owner->onCommand(names[static_cast<std::size_t>(result - 20)]);
            } else if (result == 1) {
                owner->duplicateClip(id);
            } else if (result == 2 && owner->onTimingRequested) {
                owner->onTimingRequested(id);
            } else if (result == 3 && owner->onMakeUnique) {
                owner->onMakeUnique(id);
            } else if (result == 4) {
                motion::Id instance = 0;
                auto& document = owner->processor.document;
                const auto name = "Composition " + juce::String(document.mainProject().definitions.size() + 1);
                const auto created = document.createComposition({owner->selectedClips.begin(), owner->selectedClips.end()}, name, instance);
                if (created.failed()) { if (owner->onError) { owner->onError(created.getErrorMessage()); } return; }
                owner->selectClip(instance);
                owner->refreshTracks();
            } else if (result == 5 && owner->onEnterComposition) {
                owner->onEnterComposition(id);
            } else if (result == 7 || result == 8) {
                owner->deleteSelectedClips(result == 8);
            } else if (result == 9 && owner->onLoopSelection) {
                owner->onLoopSelection();
            } else if (result == 10 && owner->onRevealSource) {
                for (const auto& track : owner->processor.document.project().tracks) {
                    for (const auto& clip : track.clips) { if (clip.id == id) { owner->onRevealSource(clip.asset); } }
                }
            } else if (result == 6) {
                motion::Id definitionId = 0;
                const auto copied = owner->processor.document.makeCompositionUnique(id, definitionId);
                if (copied.failed() && owner->onError) { owner->onError(copied.getErrorMessage()); }
                owner->refreshTracks();
            }
        });
    }
    void createGroup(motion::Id trackId, motion::Id parent) {
        motion::Group group;
        group.id = processor.document.newId();
        group.name = "Group " + std::to_string(processor.document.project().groups.size() + 1);
        group.parent = parent;
        auto candidate = processor.document.project();
        candidate.groups.push_back(group);
        if (!motion::validGroupHierarchy(candidate)) { return; }
        processor.document.edit("Create group", [group, trackId](motion::Project& project) {
            project.groups.push_back(group);
            for (auto& track : project.tracks) { if (track.id == trackId) { track.group = group.id; } }
        });
        refreshTracks();
        selectClip(group.id);
    }
    void showGroupMenu(motion::Id id) {
        const auto* group = motion::findGroup(processor.document.project(), id);
        if (group == nullptr) { return; }
        juce::PopupMenu menu;
        menu.addItem(1, "Edit group");
        menu.addItem(2, "Add track to group");
        menu.addItem(3, "Add nested group");
        menu.addSeparator();
        menu.addItem(4, "Delete group and its tracks");
        const auto generation = processor.document.generation();
        const juce::Component::SafePointer<MotionTimelineView> owner(this);
        menu.setLookAndFeel(&getLookAndFeel());
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withMousePosition(), [owner, id, generation](int result) {
            if (owner == nullptr || result == 0 || owner->processor.document.generation() != generation || motion::findGroup(owner->processor.document.project(), id) == nullptr) { return; }
            owner->cancelGesture();
            if (result == 1) { owner->selectClip(id); return; }
            if (result == 3) { owner->createGroup(0, id); return; }
            if (result == 2) {
                motion::Track track;
                track.id = owner->processor.document.newId(); track.group = id; track.name = "New track";
                owner->processor.document.edit("Add track", [track](motion::Project& project) { project.tracks.push_back(track); });
            } else if (result == 4) {
                owner->processor.document.edit("Delete group", [id](motion::Project& project) {
                    std::set<motion::Id> removed { id };
                    for (std::size_t depth = 0; depth < motion::maximumGroupDepth; ++depth) {
                        for (const auto& group : project.groups) { if (removed.contains(group.parent)) { removed.insert(group.id); } }
                    }
                    std::erase_if(project.tracks, [&](const auto& track) { return removed.contains(track.group); });
                    std::erase_if(project.groups, [&](const auto& group) { return removed.contains(group.id); });
                });
            }
            owner->refreshTracks();
        });
    }
    void showTrackMenu(motion::Id id) {
        if (motion::findGroup(processor.document.project(), id) != nullptr) { showGroupMenu(id); return; }
        const auto& tracks = processor.document.project().tracks;
        const auto found = std::find_if(tracks.begin(), tracks.end(), [id](const auto& track) { return track.id == id; });
        if (found == tracks.end()) { return; }
        juce::PopupMenu menu;
        menu.addItem(1, "Move track up", std::any_of(tracks.begin(), found, [&](const auto& track) { return track.group == found->group; }));
        menu.addItem(2, "Move track down", std::any_of(found + 1, tracks.end(), [&](const auto& track) { return track.group == found->group; }));
        menu.addItem(4, "Group track");
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
            const auto colour = label.argb == 0 ? osci::Colours::textMuted() : juce::Colour(label.argb).brighter(.6f);
            labels.addColouredItem(300 + static_cast<int>(index), label.name, colour, true, found->label == static_cast<int>(index));
        }
        menu.addSubMenu("Label colour", labels);
        if (found->kind == motion::TrackKind::visual && processor.document.editingComposition() == 0) {
            juce::PopupMenu input;
            input.addItem(200, "Off", true, found->midiInput == 0);
            input.addItem(200 + motion::Track::anyMidiChannel, "Any channel", true, found->midiInput == motion::Track::anyMidiChannel);
            for (int channel = 1; channel <= 16; ++channel) { input.addItem(200 + channel, "Channel " + juce::String(channel), true, found->midiInput == channel); }
            menu.addSubMenu("MIDI input", input);
        }
        menu.addSeparator();
        menu.addItem(3, "Delete track");
        const auto generation = processor.document.generation();
        const juce::Component::SafePointer<MotionTimelineView> owner(this);
        menu.setLookAndFeel(&getLookAndFeel());
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withMousePosition(), [owner, id, generation, groups](int result) {
            if (owner == nullptr || result == 0 || owner->processor.document.generation() != generation) { return; }
            owner->cancelGesture();
            const auto& current = owner->processor.document.project().tracks;
            const auto track = std::find_if(current.begin(), current.end(), [id](const auto& item) { return item.id == id; });
            if (track == current.end()) { return; }
            const auto index = static_cast<int>(track - current.begin());
            if (result == 4) { owner->createGroup(id, track->group); return; }
            if (result >= 200 && result <= 200 + motion::Track::anyMidiChannel) { owner->setMidiInput(id, result - 200); return; }
            if (result >= 300 && result < 300 + static_cast<int>(motion::style::trackLabels().size())) {
                const auto label = result - 300;
                owner->processor.document.tryEdit("Change track colour", [id, label](motion::Project& project) {
                    for (auto& item : project.tracks) {
                        if (item.id == id && item.label != label) { item.label = label; return true; }
                    }
                    return false;
                });
                return;
            }
            if (result >= 100 && result - 100 < static_cast<int>(groups.size())) {
                owner->placeTrack(id, index, groups[result - 100]);
            } else if (result == 3) {
                owner->processor.document.edit("Delete track", [id](motion::Project& project) {
                    std::erase_if(project.tracks, [id](const auto& item) { return item.id == id; });
                });
                owner->refreshTracks();
            } else {
                const auto direction = result == 1 ? -1 : 1;
                for (int candidate = index + direction; candidate >= 0 && candidate < static_cast<int>(current.size()); candidate += direction) {
                    if (current[candidate].group == track->group) {
                        owner->placeTrack(id, candidate + (direction > 0 ? 1 : 0), track->group);
                        break;
                    }
                }
            }
        });
    }
    void setMidiInput(motion::Id id, int input) {
        cancelGesture();
        processor.document.tryEdit(input == 0 ? "Disarm MIDI input" : "Arm MIDI input", [id, input](motion::Project& project) {
            for (auto& track : project.tracks) {
                if (track.id == id && track.kind == motion::TrackKind::visual && track.midiInput != input) {
                    track.midiInput = input;
                    return true;
                }
            }
            return false;
        });
    }
    void toggleLock(motion::Id id) {
        cancelGesture();
        processor.document.edit("Toggle track lock", [id](motion::Project& project) {
            for (auto& track : project.tracks) { if (track.id == id) { track.locked = !track.locked; } }
        });
    }
    void toggleTrack(motion::Id id, bool solo) {
        cancelGesture();
        processor.document.edit(solo ? "Toggle track solo" : "Toggle track mute", [id, solo](motion::Project& project) {
            auto* group = motion::findGroup(project, id);
            if (group != nullptr) {
                if (solo) { group->solo = !group->solo; } else { group->muted = !group->muted; }
            }
            for (auto& track : project.tracks) {
                if (track.id == id) {
                    if (solo) { track.solo = !track.solo; } else { track.muted = !track.muted; }
                }
            }
        });
    }
    void placeTrack(motion::Id id, int boundary, motion::Id group) {
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
    void reorderTrack(motion::Id id, int y) {
        const auto& tracks = processor.document.project().tracks;
        const auto row = trackAtY(y);
        const auto group = groupAtY(y);
        const auto boundary = row >= 0 ? row + (y - trackY(row) >= trackHeight(row) / 2 ? 1 : 0) : static_cast<int>(tracks.size());
        placeTrack(id, boundary, group != 0 ? group : (row >= 0 ? tracks[row].group : 0));
    }
    // What an effect dropped here applies to: the clip under the pointer, a
    // track by its header, or a group; 0 for nothing.
    motion::Id effectOwnerAt(juce::Point<int> position) const {
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
    void insertEffect(const std::string& type, juce::Point<int> position) {
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
    enum class Tool { move, slip, stretch, ripple };
    enum class Mode { move, left, right, slip, stretch, rippleLeft, rippleRight };

    // Right-clicking empty track space offers what people look for first.
    void showSpaceMenu() {
        juce::PopupMenu menu;
        menu.addItem(motion::style::menuItem("Paste", 1, "Cmd+V"));
        menu.addItem(2, "Add track");
        menu.addItem(3, "Add camera here");
        const juce::Component::SafePointer<MotionTimelineView> owner(this);
        const auto time = std::clamp(scrollTime + (getMouseXYRelative().x - namesWidth) / pixelsPerSecond, 0.0, processor.document.project().duration);
        menu.setLookAndFeel(&getLookAndFeel());
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withMousePosition(), [owner, time](int result) {
            if (owner == nullptr || result == 0) { return; }
            owner->cancelGesture();
            if (result == 1 && owner->onCommand) { owner->onCommand("Paste"); }
            if (result == 2) { owner->addTrack.triggerClick(); }
            if (result == 3 && owner->onAddCamera) {
                owner->processor.seek(time);
                owner->onAddCamera();
            }
        });
    }

    void fitProject() {
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

    bool gestureIsCurrent() {
        if (!before.has_value()) {
            return false;
        }
        if (processor.document.revision() != expectedRevision) {
            before.reset();
            changed = false;
            repaint();
            return false;
        }
        return true;
    }

    void cancelGesture() {
        scrubbing = false;
        markerDragging = 0;
        if (gestureIsCurrent()) {
            auto restore = std::move(*before);
            before.reset();
            if (changed) {
                processor.document.preview(std::move(restore));
            }
        }
        changed = false;
        repaint();
    }

    bool isClip(motion::Id id) const {
        for (const auto& track : processor.document.project().tracks) {
            for (const auto& clip : track.clips) { if (clip.id == id) { return true; } }
        }
        return false;
    }
    mutable std::set<motion::Id> selectedClips;
    bool notifyingSelection = false;
    void notifySelection(motion::Id id) {
        selected = id;
        const juce::ScopedValueSetter<bool> notification(notifyingSelection, true);
        if (onSelection) { onSelection(id); }
        repaint();
    }
    void selectClip(motion::Id id) {
        selectedKeys.clear();
        selectedClips.clear();
        if (isClip(id)) { selectedClips.insert(id); }
        notifySelection(id);
    }

    const motion::Clip* clipAt(juce::Point<int> position, int& row) const {
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
    static int boundedPixel(double value) {
        constexpr double limit = 1000000.0;
        if (!std::isfinite(value)) {
            return std::signbit(value) ? -static_cast<int>(limit) : static_cast<int>(limit);
        }
        return juce::roundToInt(std::clamp(value, -limit, limit));
    }
    // ChangeBroadcaster delivery is deferred. Undo/delete may replace the
    // document before the next paint or pointer event, so cached indices must
    // be rebuilt synchronously before any row lookup. Keep header creation and
    // destruction in refreshTracks(), outside paint and hit testing.
    void ensureTrackRows() const {
        const auto generation = processor.document.generation();
        const auto revision = processor.document.revision();
        if (layoutGeneration != generation) {
            collapsedGroups.clear();
            selectedClips.clear();
            selected = 0;
            selectedMarker = 0;
            layoutGeneration = generation;
            layoutRevision.reset();
        }
        if (!layoutRevision.has_value() || *layoutRevision != revision) {
            rulerHeight = cameraBandTop() + (showsCameraBand() ? cameraBandHeight : 0);
            if (std::none_of(processor.document.project().markers.begin(), processor.document.project().markers.end(), [this](const auto& marker) { return marker.id == selectedMarker; })) { selectedMarker = 0; }
            rebuildRows();
            std::erase_if(selectedClips, [this](auto id) { return !isClip(id); });
            layoutRevision = revision;
        }
        scrollY = std::clamp(scrollY, 0, maximumScrollY());
    }
    // Lanes list every animated property on a track's clips, in schema order.
    static std::vector<std::string> animatedProperties(const motion::Track& track) {
        std::vector<std::string> result;
        std::vector<motion::PropertySpec> specs;
        const auto base = track.kind == motion::TrackKind::audio ? motion::audioPropertySpecs() : motion::objectPropertySpecs();
        specs.assign(base.begin(), base.end());
        if (track.kind == motion::TrackKind::visual) { specs.insert(specs.end(), motion::luaSliderSpecs().begin(), motion::luaSliderSpecs().end()); }
        for (const auto& spec : specs) {
            const std::string id(spec.id);
            const bool animated = std::any_of(track.clips.begin(), track.clips.end(), [&](const auto& clip) {
                const auto found = clip.properties.find(id);
                return found != clip.properties.end() && found->second.animated();
            });
            if (animated) { result.push_back(id); }
        }
        return result;
    }
    void rebuildRows() const {
        const auto& project = processor.document.project();
        rows.clear();
        std::erase_if(expandedTracks, [&](auto id) { return std::none_of(project.tracks.begin(), project.tracks.end(), [id](const auto& track) { return track.id == id; }); });
        for (const auto& base : motion::trackRows(project, collapsedGroups)) {
            Row row;
            static_cast<motion::TrackRow&>(row) = base;
            rows.push_back(row);
            if (base.track < 0 || !expandedTracks.contains(base.id)) { continue; }
            for (auto& property : animatedProperties(project.tracks[static_cast<std::size_t>(base.track)])) {
                Row lane;
                static_cast<motion::TrackRow&>(lane) = base;
                lane.lane = std::move(property);
                rows.push_back(std::move(lane));
            }
        }
        layoutRows();
    }
    // Row heights: lanes are fixed; tracks use their own height or the
    // default; groups use the default.
    int heightOf(std::size_t row) const {
        if (rows[row].isLane()) { return laneHeight; }
        const auto& tracks = processor.document.project().tracks;
        const auto index = rows[row].track;
        const auto own = index >= 0 && index < static_cast<int>(tracks.size()) ? tracks[static_cast<std::size_t>(index)].height : 0;
        return own > 0 ? own : defaultTrackHeight;
    }
    int heightAt(int row) const { return row >= 0 && row < static_cast<int>(rows.size()) ? heightOf(static_cast<std::size_t>(row)) : defaultTrackHeight; }
    int trackIndex(motion::Id id) const {
        const auto& tracks = processor.document.project().tracks;
        const auto found = std::find_if(tracks.begin(), tracks.end(), [id](const auto& track) { return track.id == id; });
        return found == tracks.end() ? -1 : static_cast<int>(found - tracks.begin());
    }
    int trackHeight(int track) const {
        const auto& tracks = processor.document.project().tracks;
        const auto own = track >= 0 && track < static_cast<int>(tracks.size()) ? tracks[static_cast<std::size_t>(track)].height : 0;
        return own > 0 ? own : defaultTrackHeight;
    }
    void layoutRows() const {
        rowTops.resize(rows.size());
        int top = 0;
        for (std::size_t row = 0; row < rows.size(); ++row) { rowTops[row] = top; top += heightOf(row); }
        contentHeight = top;
    }
    // Rows end above the horizontal scroll strip.
    int viewHeight() const { return std::max(0, getHeight() - rulerHeight - scrollStrip); }
    // A little room below the last track to drop new ones into, without
    // pushing whole rows off the last page.
    static constexpr int bottomRoom = 12;
    int maximumScrollY() const { return std::max(0, contentHeight + bottomRoom - viewHeight()); }
    int firstVisibleRow() const { return std::max(0, visualRowAt(rulerHeight)); }
    int visualRowAt(int y) const {
        ensureTrackRows();
        if (y < rulerHeight) { return -1; }
        const auto content = y - rulerHeight + scrollY;
        if (content >= contentHeight) { return static_cast<int>(rows.size()) + (content - contentHeight) / std::max(1, defaultTrackHeight); }
        const auto found = std::upper_bound(rowTops.begin(), rowTops.end(), content);
        return static_cast<int>(found - rowTops.begin()) - 1;
    }
    int trackAtY(int y) const {
        const auto row = visualRowAt(y);
        return row >= 0 && row < static_cast<int>(rows.size()) && !rows[static_cast<std::size_t>(row)].isLane() ? rows[static_cast<std::size_t>(row)].track : -1;
    }
    motion::Id groupAtY(int y) const {
        const auto row = visualRowAt(y);
        return row >= 0 && row < static_cast<int>(rows.size()) && rows[static_cast<std::size_t>(row)].group() ? rows[static_cast<std::size_t>(row)].id : 0;
    }
    const Row* laneAtY(int y) const {
        const auto row = visualRowAt(y);
        return row >= 0 && row < static_cast<int>(rows.size()) && rows[static_cast<std::size_t>(row)].isLane() ? &rows[static_cast<std::size_t>(row)] : nullptr;
    }
    int trackY(int track) const {
        ensureTrackRows();
        const auto found = std::find_if(rows.begin(), rows.end(), [track](const auto& row) { return row.track == track && !row.isLane(); });
        return found == rows.end() ? -defaultTrackHeight : rowY(static_cast<int>(found - rows.begin()));
    }
    int timeX(double time) const { return namesWidth + boundedPixel((time - scrollTime) * pixelsPerSecond); }
    int rowY(int row) const {
        if (row < 0) { return rulerHeight - scrollY; }
        const auto top = row < static_cast<int>(rowTops.size()) ? rowTops[static_cast<std::size_t>(row)] : contentHeight + (row - static_cast<int>(rowTops.size())) * defaultTrackHeight;
        return rulerHeight - scrollY + top;
    }
    juce::Rectangle<int> clipBounds(const motion::Clip& clip, int row) const {
        const auto timing = clip.timing(processor.document.project().tempo());
        return { timeX(timing.start), trackY(row), std::max(2, boundedPixel(timing.duration() * pixelsPerSecond)), trackHeight(row) };
    }
    double snapTime(double time, juce::ModifierKeys modifiers) const {
        return modifiers.isAltDown() ? time : processor.document.project().timeGrid().snap(time);
    }
    // Magnetic targets within 8 px: the playhead, markers, other clips' edges
    // and keys shown in lanes. Keys being dragged and excluded clips are skipped.
    std::optional<double> magnet(double time, const motion::Project& project, const std::set<motion::Id>& excludedClips, const std::vector<KeyRef>* movingKeys = nullptr, bool includePlayhead = true, motion::Id excludedMarker = 0) const {
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
                            return item.clip == clip.id && item.property == name && sameTime(item.time, key.time);
                        });
                        if (!moving) { consider(timing.projectTime(key.time)); }
                    }
                }
            }
        }
        return best;
    }
    // Snaps a moving edge: magnets first, then the grid; Alt bypasses both.
    double snapEdge(double time, juce::ModifierKeys modifiers, const motion::Project& project, const std::set<motion::Id>& excludedClips, const std::vector<KeyRef>* movingKeys = nullptr, motion::Id excludedMarker = 0) {
        if (modifiers.isAltDown()) {
            snapGuide.reset();
            return time;
        }
        const auto target = magnet(time, project, excludedClips, movingKeys, true, excludedMarker);
        snapGuide = target;
        return target.value_or(project.timeGrid().snap(time));
    }
    std::optional<double> snapGuide;
    bool following = false, followPaused = false;
    void seek(int x, juce::ModifierKeys modifiers) {
        // The playhead itself is excluded as a magnet while scrubbing it.
        const auto raw = scrollTime + (x - namesWidth) / pixelsPerSecond;
        const auto& project = processor.document.project();
        // The playhead moves freely and snaps only to things that matter
        // (clip edges, keys, markers, the loop); Alt bypasses even those.
        const auto target = modifiers.isAltDown() ? std::optional<double>() : magnet(raw, project, {}, nullptr, false);
        processor.seek(std::clamp(target.value_or(raw), 0.0, project.duration));
        repaint();
    }
    motion::icons::Button addTrack {"Add track", motion::icons::Icon::add}, snapButton {"Snapping", motion::icons::Icon::magnet};
    motion::icons::Button selectTool {"Select tool", motion::icons::Icon::select}, slipTool {"Slip tool", motion::icons::Icon::slip};
    motion::icons::Button stretchTool {"Stretch tool", motion::icons::Icon::stretch}, rippleTool {"Ripple trim tool", motion::icons::Icon::ripple};
    std::vector<std::unique_ptr<MotionTrackHeader>> headers;
    bool dropTrack = false;
    mutable std::vector<Row> rows;
    mutable std::vector<int> rowTops;
    mutable int contentHeight = 0;
    juce::Component headerArea;
    mutable std::set<motion::Id> expandedTracks;
    mutable std::set<motion::Id> collapsedGroups;
    mutable std::uint64_t layoutGeneration = 0;
    mutable std::optional<std::uint64_t> layoutRevision;
    MotionProcessor& processor;
    std::optional<motion::Project> before;
    motion::Clip original;
    int originalRow = 0;
    int downX = 0;
    Tool tool = Tool::move;
    Mode mode = Mode::move;
    std::uint64_t expectedRevision = 0;
    mutable motion::Id selectedMarker = 0;
    motion::Id markerDragging = 0;
    double markerOriginalTime = 0;
    bool scrubbing = false;
    bool changed = false;
    std::optional<juce::Point<int>> dropPosition;
    motion::Id dropAssetId = 0;
    std::string dropEffect;
    std::vector<KeyRef> selectedKeys;
    struct KeyDrag {
        motion::Project before;
        std::vector<KeyRef> keys;
        KeyRef grabbed;
        int downX = 0;
        std::uint64_t revision = 0;
        bool changed = false;
    };
    std::optional<KeyDrag> keyDrag;
    std::optional<juce::Rectangle<int>> marquee, clipMarquee;
    juce::Point<int> clipMarqueeStart;
    std::set<motion::Id> clipMarqueeBase;
    motion::Id hoveredClip = 0;
    int hoveredEdge = -1;
    juce::Point<int> marqueeStart;
    std::vector<KeyRef> marqueeBase;
};

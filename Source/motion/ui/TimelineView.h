#pragma once

#include "../MotionProcessor.h"
#include "TrackHeader.h"
#include "TrackLayout.h"
#include <optional>
#include <limits>
#include <set>
#include "../model/CompositionGraph.h"
#include "../model/PropertySchema.h"
#include "MotionStyle.h"

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
        addTrack.setButtonText("+");
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
        setWantsKeyboardFocus(true);
        // Shortcuts are listed in Help > Keyboard shortcuts rather than a hover wall.
    }
    std::function<void(motion::Id)> onSelection, onMidiAssigned, onTimingRequested, onMakeUnique, onEnterComposition;
    std::function<void(const juce::String&)> onError;
    std::function<void(motion::Id, double)> onEditMarker;
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
        int row = 0;
        motion::Id primary = 0;
        std::set<motion::Id> selected, collapsed, expanded;
    };
    ViewState viewState() const {
        ensureTrackRows();
        return {pixelsPerSecond, scrollTime, scrollRows, selected, selectedClips, collapsedGroups, expandedTracks};
    }
    void restoreView(const ViewState& state) {
        ensureTrackRows();
        pixelsPerSecond = state.zoom; scrollTime = state.scroll; scrollRows = state.row;
        selectedMarker = 0;
        selected = state.primary; selectedClips = state.selected; collapsedGroups = state.collapsed; expandedTracks = state.expanded;
        std::erase_if(collapsedGroups, [this](auto id) { return motion::findGroup(processor.document.project(), id) == nullptr; });
        if (!isClip(selected) && motion::findGroup(processor.document.project(), selected) == nullptr) { selected = 0; }
        layoutRevision.reset();
        refreshTracks();
    }
    double pixelsPerSecond = 70;
    double scrollTime = 0;
    mutable int scrollRows = 0;
    static constexpr int namesWidth = 170;
    mutable int rulerHeight = 26;
    static constexpr int rowHeight = 32;
    static constexpr int laneHeight = 22;

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
        const auto row = static_cast<int>(found - rows.begin());
        if (row < scrollRows) { scrollRows = row; }
        else if (row >= scrollRows + visibleRowCount()) { scrollRows = row - visibleRowCount() + 1; }
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
            const auto timing = clip->timing(project.bpm);
            for (const auto& keyframe : found->second.keyframes()) {
                if (!sameTime(keyframe.time, key.time) || timing.rate == 0) { continue; }
                const auto time = timing.start + (keyframe.time - timing.offset) / timing.rate;
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
        const auto centreX = std::max(0, (getWidth() - namesWidth) / 2);
        const auto anchor = scrollTime + centreX / pixelsPerSecond;
        pixelsPerSecond = std::clamp(pixelsPerSecond * factor, 0.000001, 500.0);
        scrollTime = std::max(0.0, anchor - centreX / pixelsPerSecond);
        repaint();
    }
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
                header->onMute = [this](motion::Id id) { toggleTrack(id, false); };
                header->onSolo = [this](motion::Id id) { toggleTrack(id, true); };
                addAndMakeVisible(*header);
                headers.push_back(std::move(header));
                found = headers.end() - 1;
            }
            const auto lanes = group ? false : !animatedProperties(track).empty();
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
        addTrack.setBounds(namesWidth - 27, 2, 24, 22);
        scrollRows = std::clamp(scrollRows, 0, maximumScrollRow());
        for (auto& header : headers) {
            const auto found = std::find_if(rows.begin(), rows.end(), [&](const auto& row) { return row.id == header->id && !row.isLane(); });
            const auto row = static_cast<int>(found - rows.begin());
            const auto y = rowY(row);
            header->setVisible(found != rows.end() && row >= scrollRows && y < getHeight());
            const auto indent = found != rows.end() ? std::min(48, found->depth * 8) : 0;
            header->setBounds(indent, y, namesWidth - 1 - indent, rowHeight - 1);
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
        repaint();
    }
    void itemDragExit(const SourceDetails&) override {
        dropPosition.reset();
        repaint();
    }

    void itemDropped(const SourceDetails& details) override {
        dropPosition.reset();
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
        if (row >= 0 && row < static_cast<int>(project.tracks.size()) && (project.tracks[row].locked || project.tracks[row].kind != kind || !project.tracks[row].canPlace(clip, 0, project.bpm))) {
            return;
        }
        const auto trackId = processor.document.newId();
        processor.document.edit(kind == motion::TrackKind::audio ? "Add audio clip" : "Add object clip", [&](motion::Project& updated) {
            updated.duration = std::max(updated.duration, clip.timing(updated.bpm).end());
            if (row >= 0 && row < static_cast<int>(updated.tracks.size())) {
                updated.tracks[row].insert(std::move(clip), updated.bpm);
            } else {
                motion::Track track;
                track.id = trackId;
                track.group = group;
                track.name = clip.name;
                track.kind = kind;
                track.insert(std::move(clip), updated.bpm);
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
        g.setFont(12.0f);
        pixelsPerSecond = std::isfinite(pixelsPerSecond) ? std::clamp(pixelsPerSecond, 0.000001, 500.0) : 70.0;
        scrollTime = std::isfinite(scrollTime) ? std::max(0.0, scrollTime) : 0.0;
        const auto visibleSeconds = std::max(0, getWidth() - namesWidth) / pixelsPerSecond;
        const auto grid = processor.document.project().timeGrid();
        const auto step = grid.tickStep(pixelsPerSecond);
        const auto minorStep = grid.display == motion::TimeDisplay::beats ? grid.snapBeats * 60.0 / grid.bpm : 1.0 / grid.frameRate;
        if (grid.snapping && minorStep * pixelsPerSecond >= 9 && minorStep < step) {
            const auto first = std::floor(scrollTime / minorStep) * minorStep;
            const auto count = std::clamp(static_cast<int>(std::ceil(visibleSeconds / minorStep)) + 2, 0, 1000);
            g.setColour(juce::Colours::white.withAlpha(0.035f));
            for (int tick = 0; tick < count; ++tick) {
                const auto x = timeX(first + tick * minorStep);
                if (x >= namesWidth) { g.drawVerticalLine(x, rulerHeight, static_cast<float>(getHeight())); }
            }
        }
        const auto firstTick = std::floor(scrollTime / step) * step;
        const auto tickCount = std::clamp(static_cast<int>(std::ceil(visibleSeconds / step)) + 2, 0, 1000);
        for (int tick = 0; tick < tickCount; ++tick) {
            const auto time = firstTick + tick * step;
            const auto x = timeX(time);
            if (x < namesWidth) {
                continue;
            }
            g.setColour(juce::Colours::white.withAlpha(0.06f));
            g.drawVerticalLine(x, rulerHeight, static_cast<float>(getHeight()));
            g.setColour(osci::Colours::text().withAlpha(0.7f));
            g.drawText(juce::String(grid.label(time, step)), x + 5, 0, 70, 26, juce::Justification::centredLeft);
        }
        g.setColour(osci::Colours::surfaceRaised());
        g.fillRect(0, 0, namesWidth, rulerHeight);
        g.setColour(osci::Colours::text());
        g.drawText(toolName(), 12, 0, namesWidth - 54, 26, juce::Justification::centredLeft);
        juce::Path toolArrow;
        toolArrow.addTriangle(namesWidth - 47.0f, 11.0f, namesWidth - 39.0f, 11.0f, namesWidth - 43.0f, 15.0f);
        g.fillPath(toolArrow);
        const auto& tracks = processor.document.project().tracks;
        const auto& project = processor.document.project();
        scrollRows = std::clamp(scrollRows, 0, maximumScrollRow());
        for (int visible = scrollRows; visible < static_cast<int>(rows.size()); ++visible) {
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
                                const auto timing = clip.timing(project.bpm);
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
        if (dropPosition.has_value() && dropTrack) {
            g.setColour(osci::Colours::accentColor());
            const auto boundary = std::clamp(visualRowAt(dropPosition->y + rowHeight / 2), 0, static_cast<int>(rows.size()));
            if (groupAtY(dropPosition->y) != 0) { g.drawRect(0, rowY(visualRowAt(dropPosition->y)), getWidth(), rowHeight, 2); } else { g.fillRect(0, rowY(boundary) - 1, getWidth(), 2); }
        } else if (dropPosition.has_value() && !dropEffect.empty()) {
            int row = 0;
            const auto* clip = clipAt(*dropPosition, row);
            if (dropPosition->x < namesWidth && dropPosition->y >= rulerHeight) { row = trackAtY(dropPosition->y); }
            const auto group = groupAtY(dropPosition->y);
            if (group != 0) {
                g.setColour(osci::Colours::accentColor());
                g.drawRect(0, rowY(visualRowAt(dropPosition->y)), getWidth(), rowHeight, 2);
            } else if (row >= 0 && row < static_cast<int>(tracks.size()) && tracks[row].kind == motion::TrackKind::visual && (clip != nullptr || dropPosition->x < namesWidth)) {
                const auto bounds = clip != nullptr ? clipBounds(*clip, row) : juce::Rectangle<int>(0, trackY(row), namesWidth, rowHeight);
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
            const auto allowed = (asset != assets.end() || definition != definitions.end()) && !recursive && correctKind && (row < 0 || row >= static_cast<int>(tracks.size()) || !tracks[row].locked && tracks[row].canPlace(candidate, 0, processor.document.project().bpm));
            const auto bounds = (row >= 0 ? clipBounds(candidate, row) : juce::Rectangle<int>(timeX(candidate.start), rowY(std::max(0, visualRowAt(dropPosition->y))), std::max(2, boundedPixel(candidate.duration * pixelsPerSecond)), rowHeight)).toFloat().reduced(1, 4);
            juce::Graphics::ScopedSaveState scope(g);
            g.reduceClipRegion(namesWidth, rulerHeight, getWidth() - namesWidth, getHeight() - rulerHeight);
            g.setColour((allowed ? juce::Colour(0xff70da91) : juce::Colour(0xffe98080)).withAlpha(0.2f));
            g.fillRoundedRectangle(bounds, 4);
            g.setColour(allowed ? juce::Colour(0xff70da91) : juce::Colour(0xffe98080));
            g.drawRoundedRectangle(bounds, 4, 1);
            g.drawText(allowed ? (audio ? "Add audio" : definition != definitions.end() ? "Add composition" : "Add object") : (recursive ? "Cannot contain itself" : correctKind ? "Clips cannot overlap" : "Use a matching or empty lane"), bounds.reduced(8, 0), juce::Justification::centredLeft);
        }
        if (rulerHeight > 26) {
            g.setColour(osci::Colours::textMuted());
            g.setFont(11.0f);
            g.drawText("Markers", 12, 26, namesWidth - 24, 22, juce::Justification::centredLeft);
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
            g.drawText("Import media to start your composition", getLocalBounds().withTrimmedTop(rulerHeight), juce::Justification::centred);
        }
    }

    void mouseDown(const juce::MouseEvent& event) override {
        grabKeyboardFocus();
        ensureTrackRows();
        cancelGesture();
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
        if (event.y >= 26 && event.y < rulerHeight && event.x < namesWidth && onEditMarker) { onEditMarker(0, processor.position.load()); return; }
        if (event.y < rulerHeight && event.x < namesWidth) {
            showToolMenu(true);
            return;
        }
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
            } else { showToolMenu(); }
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
            selectClip(0);
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
        int row = 0;
        const auto* clip = clipAt(event.getPosition(), row);
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

    void mouseDrag(const juce::MouseEvent& event) override {
        if (keyDrag.has_value()) { dragKeys(event.x, event.mods); return; }
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
            const auto time = delta == 0 ? markerOriginalTime : std::clamp(snapTime(markerOriginalTime + delta, event.mods), 0.0, updated.duration);
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
        const auto timing = original.timing(before->bpm);
        auto edited = timing;
        auto delta = (event.x - downX) / pixelsPerSecond;
        if (delta != 0.0 && !event.mods.isAltDown()) {
            const auto anchor = mode == Mode::right || mode == Mode::stretch || mode == Mode::rippleRight ? timing.end()
                : (mode == Mode::slip ? timing.offset / timing.rate : timing.start);
            delta = before->timeGrid().snap(anchor + delta) - anchor;
        }
        if (mode == Mode::rippleLeft || mode == Mode::rippleRight) {
            auto updated = *before;
            if (!motion::rippleTrim(updated.tracks[originalRow], original.id, mode == Mode::rippleLeft, delta, updated.bpm)) { return; }
            for (const auto& item : updated.tracks[originalRow].clips) { updated.duration = std::max(updated.duration, item.timing(updated.bpm).end()); }
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
                    if (selectedClips.contains(item.id)) { earliest = std::min(earliest, item.timing(updated.bpm).start); }
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
            if (!motion::moveClips(updated.tracks, {selectedClips.begin(), selectedClips.end()}, delta, rows, updated.bpm)) { return; }
            for (const auto& track : updated.tracks) {
                for (const auto& item : track.clips) { updated.duration = std::max(updated.duration, item.timing(updated.bpm).end()); }
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
            if (!candidate.setTiming(edited, before->bpm)) { return; }
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
        if (updated.tracks[target].insert(candidate, updated.bpm)) {
            updated.duration = std::max(updated.duration, candidate.timing(updated.bpm).end());
            changed = true;
            processor.document.preview(std::move(updated));
            expectedRevision = processor.document.revision();
            repaint();
        }
    }

    void mouseUp(const juce::MouseEvent&) override {
        if (keyDrag.has_value()) { endKeyDrag(); repaint(); return; }
        if (marquee.has_value()) { marquee.reset(); repaint(); return; }
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

    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override {
        ensureTrackRows();
        if (before.has_value() || scrubbing) {
            return;
        }
        if (event.mods.isCommandDown() || event.mods.isCtrlDown()) {
            const auto anchorX = std::clamp(event.x - namesWidth, 0, std::max(0, getWidth() - namesWidth));
            const auto anchorTime = scrollTime + anchorX / pixelsPerSecond;
            pixelsPerSecond = std::clamp(pixelsPerSecond * std::exp(wheel.deltaY * 3), 0.000001, 500.0);
            scrollTime = std::max(0.0, anchorTime - anchorX / pixelsPerSecond);
        } else if (event.mods.isShiftDown() || std::abs(wheel.deltaX) > std::abs(wheel.deltaY)) {
            scrollTime = std::max(0.0, scrollTime - (wheel.deltaX + wheel.deltaY) * 8);
        } else {
            const auto last = maximumScrollRow();
            scrollRows = std::clamp(scrollRows + (wheel.deltaY < 0 ? 1 : -1), 0, last);
        }
        resized();
        repaint();
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
        if ((key.getKeyCode() == juce::KeyPress::deleteKey || key.getKeyCode() == juce::KeyPress::backspaceKey) && !selectedKeys.empty()) {
            deleteSelectedKeys();
            return true;
        }
        if (key == juce::KeyPress::escapeKey && (before.has_value() || scrubbing)) {
            cancelGesture();
            return true;
        }
        if (!key.getModifiers().isCommandDown() && !key.getModifiers().isCtrlDown() && !key.getModifiers().isAltDown()) {
            const auto character = juce::CharacterFunctions::toLowerCase(key.getTextCharacter());
            if (character == 'm' && onEditMarker) {
                cancelGesture();
                onEditMarker(0, std::clamp(processor.position.load(), 0.0, processor.document.project().duration));
                return true;
            }
            if (character == '[' || character == ']') {
                jumpMarker(character == ']');
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
        if (track.kind == motion::TrackKind::audio) { return motion::style::audioClip(); }
        if (clip.composition != 0) { return motion::style::compositionClip(); }
        if (clip.midi != nullptr) { return motion::style::midiClip(); }
        return motion::style::visualClip();
    }
    // Project times of every key on a clip, restricted to its visible interval.
    std::vector<double> clipKeyTimes(const motion::Clip& clip, const std::string& property = {}) const {
        std::vector<double> times;
        const auto timing = clip.timing(processor.document.project().bpm);
        if (!(timing.rate != 0)) { return times; }
        for (const auto& [name, curve] : clip.properties) {
            if (!property.empty() && name != property) { continue; }
            for (const auto& key : curve.keyframes()) {
                const auto time = timing.start + (key.time - timing.offset) / timing.rate;
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
        g.setColour((active ? base.brighter(.35f) : base).withAlpha(opacity));
        g.fillRoundedRectangle(bounds, motion::style::radius);
        g.setColour((active ? motion::style::key() : base.brighter(.5f)).withAlpha(opacity * (active ? 1.0f : .55f)));
        g.drawRoundedRectangle(bounds, motion::style::radius, active ? 1.4f : 1.0f);
        g.setFont(motion::style::body());
        if (!clip.effects.empty() && bounds.getWidth() > 90) {
            g.setColour(juce::Colours::white.withAlpha(0.6f * opacity));
            g.setFont(motion::style::small());
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
            label += "  offset " + juce::String(clip.timing(project.bpm).offset, 2) + "s";
        } else if (active && tool == Tool::stretch) {
            label += "  " + juce::String(clip.timing(project.bpm).rate, 2) + "x";
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
                    const auto a = (*asset)->audio->querySeconds(0, clip.localTime(time, project.bpm), clip.localTime(end, project.bpm));
                    const auto b = (*asset)->audio->querySeconds(1, clip.localTime(time, project.bpm), clip.localTime(end, project.bpm));
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
            motion::style::drawDiamond(g, {x, bounds.getBottom() - 4.0f}, 2.5f, true);
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
        g.setFont(motion::style::small());
        g.setColour(motion::style::muted());
        const auto indent = std::min(48, row.depth * 8) + 30;
        g.drawText(spec != nullptr ? juce::String(spec->label.data(), spec->label.size()) : juce::String(row.lane), indent, y, namesWidth - indent - 6, height, juce::Justification::centredLeft);
        juce::Graphics::ScopedSaveState scope(g);
        g.reduceClipRegion(namesWidth, y, getWidth() - namesWidth, height);
        const auto centre = y + height * .5f;
        for (const auto& clip : track.clips) {
            const auto timing = clip.timing(project.bpm);
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
            // diamond linear, circle auto/Bezier.
            for (std::size_t k = 0; k < keys.size(); ++k) {
                const auto time = timing.start + (keys[k].time - timing.offset) / timing.rate;
                const auto x = static_cast<float>(timeX(time));
                const bool chosen = isKeySelected(clip.id, row.lane, keys[k].time);
                g.setColour(chosen ? juce::Colours::white : motion::style::key());
                const auto shape = keys[k].interpolation;
                if (shape == motion::Interpolation::hold) {
                    g.fillRect(juce::Rectangle<float>(x - 3.5f, centre - 3.5f, 7, 7));
                } else if (shape == motion::Interpolation::linear) {
                    motion::style::drawDiamond(g, {x, centre}, 4.5f, true);
                } else {
                    g.fillEllipse(x - 4, centre - 4, 8, 8);
                }
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
            const auto timing = clip.timing(project.bpm);
            if (found == clip.properties.end() || timing.rate == 0) { continue; }
            for (const auto& key : found->second.keyframes()) {
                const auto distance = std::abs(timeX(timing.start + (key.time - timing.offset) / timing.rate) - point.x);
                if (distance < bestDistance) { bestDistance = distance; best = KeyRef{clip.id, lane->lane, key.time}; }
            }
        }
        return best;
    }
    std::vector<KeyRef> keysInside(juce::Rectangle<int> area) const {
        std::vector<KeyRef> result;
        const auto& project = processor.document.project();
        for (int visible = scrollRows; visible < static_cast<int>(rows.size()); ++visible) {
            const auto& row = rows[static_cast<std::size_t>(visible)];
            const auto y = rowY(visible);
            if (!row.isLane() || y + laneHeight / 2 < area.getY() || y + laneHeight / 2 > area.getBottom()) { continue; }
            for (const auto& clip : project.tracks[static_cast<std::size_t>(row.track)].clips) {
                const auto found = clip.properties.find(row.lane);
                const auto timing = clip.timing(project.bpm);
                if (found == clip.properties.end() || timing.rate == 0) { continue; }
                for (const auto& key : found->second.keyframes()) {
                    const auto x = timeX(timing.start + (key.time - timing.offset) / timing.rate);
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
                const auto timing = clip.timing(project.bpm);
                for (auto& [name, curve] : clip.properties) {
                    std::vector<motion::Keyframe> moving;
                    for (const auto& key : curve.keyframes()) {
                        const bool chosen = std::any_of(keys.begin(), keys.end(), [&](const auto& item) { return item.clip == clip.id && item.property == name && sameTime(item.time, key.time); });
                        if (chosen) { moving.push_back(key); }
                    }
                    if (moving.empty()) { continue; }
                    for (const auto& key : moving) { curve.removeKey(key.time); }
                    for (auto key : moving) {
                        key.time += delta * timing.rate;
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
        const auto timing = clip->timing(keyDrag->before.bpm);
        if (timing.rate == 0) { return; }
        const auto grabbedTime = timing.start + (keyDrag->grabbed.time - timing.offset) / timing.rate;
        auto delta = (x - keyDrag->downX) / pixelsPerSecond;
        if (delta != 0 && !modifiers.isAltDown()) { delta = keyDrag->before.timeGrid().snap(grabbedTime + delta) - grabbedTime; }
        auto updated = keyDrag->before;
        if (delta != 0 && !moveKeys(updated, keyDrag->keys, delta)) { return; }
        keyDrag->changed = delta != 0;
        selectedKeys = keyDrag->keys;
        for (auto& key : selectedKeys) {
            const auto* owner = findClip(key.clip, keyDrag->before);
            if (owner != nullptr) { key.time += delta * owner->timing(keyDrag->before.bpm).rate; }
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
            const auto timing = clip.timing(project.bpm);
            if (time >= timing.start && time <= timing.end()) { target = &clip; }
        }
        if (target == nullptr || track.locked) { return; }
        const auto clipId = target->id;
        const auto local = target->timing(project.bpm).localTime(time);
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
        menu.addSectionHeader(selectedKeys.size() > 1 ? juce::String(static_cast<int>(selectedKeys.size())) + " keyframes" : juce::String("Keyframe"));
        menu.addItem(1, "Hold");
        menu.addItem(2, "Linear");
        menu.addItem(3, "Auto");
        menu.addItem(4, "Bezier");
        menu.addSeparator();
        menu.addItem(10, "Delete");
        const auto revision = processor.document.revision();
        const juce::Component::SafePointer<MotionTimelineView> owner(this);
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
                if (clip.id == duplicate) { revealTime(clip.timing(processor.document.project().bpm).start); return; }
            }
        }
    }
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
        if (point.y < 26 || point.y >= rulerHeight) { return nullptr; }
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
        const auto generation = processor.document.generation();
        const auto revision = processor.document.revision();
        const juce::Component::SafePointer<MotionTimelineView> owner(this);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withMousePosition(), [owner, id, time, generation, revision](int result) {
            if (owner == nullptr || owner->processor.document.generation() != generation || owner->processor.document.revision() != revision) { return; }
            if ((result == 1 || result == 3) && owner->onEditMarker) { owner->onEditMarker(result == 1 ? id : 0, time); }
            if (result == 2) { const auto removed = owner->processor.document.removeMarker(id); if (removed.failed() && owner->onError) { owner->onError(removed.getErrorMessage()); } }
            if (result == 4 || result == 5) { owner->jumpMarker(result == 5); }
        });
    }

    void showClipMenu(motion::Id id) {
        juce::PopupMenu menu;
        menu.addItem(1, selectedClips.size() > 1 ? "Duplicate clips" : "Duplicate clip");
        menu.addItem(2, "Edit clip timing");
        motion::Id asset = 0, definition = 0;
        bool locked = false;
        for (const auto& track : processor.document.project().tracks) {
            for (const auto& clip : track.clips) { if (clip.id == id) { asset = clip.asset; definition = clip.composition; locked = track.locked; } }
        }
        menu.addSeparator();
        menu.addItem(7, "Delete clips (keep gaps)", !locked);
        menu.addItem(8, "Ripple delete on selected tracks (Shift+Delete)", !locked);
        const auto references = motion::sourceReferenceCount(processor.document.mainProject(), asset);
        if (definition == 0) { menu.addItem(3, "Make this clip's source unique", !locked && asset != 0 && references > 1); }
        menu.addSeparator();
        menu.addItem(4, "Create composition from selection", !locked && !selectedClips.empty());
        if (definition != 0) { menu.addItem(5, "Open composition"); menu.addItem(6, "Make composition unique", !locked); }
        const auto generation = processor.document.generation();
        const auto revision = processor.document.revision();
        const juce::Component::SafePointer<MotionTimelineView> owner(this);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withMousePosition(), [owner, id, generation, revision](int result) {
            if (owner == nullptr || result == 0 || owner->processor.document.generation() != generation || owner->processor.document.revision() != revision) { return; }
            if (result == 1) {
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
        menu.addSectionHeader(juce::String(group->name));
        menu.addItem(1, "Edit group");
        menu.addItem(2, "Add track to group");
        menu.addItem(3, "Add nested group");
        menu.addSeparator();
        menu.addItem(4, "Delete group and its tracks");
        const auto generation = processor.document.generation();
        const juce::Component::SafePointer<MotionTimelineView> owner(this);
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
        menu.addSectionHeader(juce::String(found->name));
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
        menu.addSeparator();
        menu.addItem(3, "Delete track");
        const auto generation = processor.document.generation();
        const juce::Component::SafePointer<MotionTimelineView> owner(this);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withMousePosition(), [owner, id, generation, groups](int result) {
            if (owner == nullptr || result == 0 || owner->processor.document.generation() != generation) { return; }
            owner->cancelGesture();
            const auto& current = owner->processor.document.project().tracks;
            const auto track = std::find_if(current.begin(), current.end(), [id](const auto& item) { return item.id == id; });
            if (track == current.end()) { return; }
            const auto index = static_cast<int>(track - current.begin());
            if (result == 4) { owner->createGroup(id, track->group); return; }
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
        const auto boundary = row >= 0 ? row + (y - trackY(row) >= rowHeight / 2 ? 1 : 0) : static_cast<int>(tracks.size());
        placeTrack(id, boundary, group != 0 ? group : (row >= 0 ? tracks[row].group : 0));
    }
    void insertEffect(const std::string& type, juce::Point<int> position) {
        const auto* definition = motion::effectDefinition(type);
        if (definition == nullptr || position.y < rulerHeight) { return; }
        int row = 0;
        const auto* clip = clipAt(position, row);
        const auto& tracks = processor.document.project().tracks;
        if (position.x < namesWidth) { row = trackAtY(position.y); }
        const auto group = groupAtY(position.y);
        if (group == 0 && (row < 0 || row >= static_cast<int>(tracks.size()) || (clip == nullptr && position.x >= namesWidth))) { return; }
        const auto owner = group != 0 ? group : (clip != nullptr ? clip->id : tracks[row].id);
        const auto* effects = motion::findEffectOwner(processor.document.project(), owner);
        if (effects == nullptr || effects->size() >= motion::maximumEffectsPerOwner) { return; }
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

    juce::String toolName() const {
        return tool == Tool::ripple ? "Ripple Trim (B)" : tool == Tool::move ? "Move / Trim (V)" : (tool == Tool::slip ? "Slip (S)" : "Stretch (R)");
    }

    void showToolMenu(bool atHeader = false) {
        juce::PopupMenu menu;
        menu.addSectionHeader("Clip editing tool");
        menu.addItem(1, "Move / trim edges (V)", true, tool == Tool::move);
        menu.addItem(2, "Slip content inside clip (S)", true, tool == Tool::slip);
        menu.addItem(3, "Stretch duration and speed (R)", true, tool == Tool::stretch);
        menu.addItem(5, "Ripple trim edges on this track (B)", true, tool == Tool::ripple);
        menu.addSeparator();
        menu.addItem(4, "Fit project (F)");
        menu.addSectionHeader("Alt: disable snapping   Escape: cancel");
        const juce::Component::SafePointer<MotionTimelineView> owner(this);
        const auto options = juce::PopupMenu::Options().withTargetComponent(this);
        const auto placement = atHeader ? options.withTargetScreenArea(localAreaToGlobal(juce::Rectangle<int>(0, 0, namesWidth, rulerHeight))) : options.withMousePosition();
        menu.showMenuAsync(placement, [owner](int result) {
            if (owner == nullptr || result == 0) {
                return;
            }
            owner->cancelGesture();
            if (result == 4) {
                owner->fitProject();
            } else {
                owner->tool = result == 5 ? Tool::ripple : result == 1 ? Tool::move : (result == 2 ? Tool::slip : Tool::stretch);
                owner->repaint();
            }
        });
    }

    void fitProject() {
        cancelGesture();
        const auto& project = processor.document.project();
        auto end = project.duration;
        for (const auto& track : project.tracks) {
            for (const auto& clip : track.clips) {
                end = std::max(end, clip.timing(project.bpm).end());
            }
        }
        const auto duration = std::isfinite(end) ? std::max(0.001, end) : 1.0;
        pixelsPerSecond = std::clamp(std::max(1, getWidth() - namesWidth - 20) / duration, 0.000001, 500.0);
        scrollTime = 0.0;
        scrollRows = 0;
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
            rulerHeight = processor.document.project().markers.empty() ? 26 : 48;
            if (std::none_of(processor.document.project().markers.begin(), processor.document.project().markers.end(), [this](const auto& marker) { return marker.id == selectedMarker; })) { selectedMarker = 0; }
            rebuildRows();
            std::erase_if(selectedClips, [this](auto id) { return !isClip(id); });
            layoutRevision = revision;
        }
        scrollRows = std::clamp(scrollRows, 0, maximumScrollRow());
    }
    // Lanes list every animated property on a track's clips, in schema order.
    static std::vector<std::string> animatedProperties(const motion::Track& track) {
        std::vector<std::string> result;
        const auto specs = track.kind == motion::TrackKind::audio ? motion::audioPropertySpecs() : motion::objectPropertySpecs();
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
    }
    int heightOf(std::size_t row) const { return rows[row].isLane() ? laneHeight : rowHeight; }
    int visibleRowCount() const {
        int count = 0, used = 0;
        for (auto row = static_cast<std::size_t>(std::max(0, scrollRows)); row < rows.size() && used + heightOf(row) <= getHeight() - rulerHeight; ++row) { used += heightOf(row); ++count; }
        return std::max(1, count);
    }
    int maximumScrollRow() const {
        int used = 0;
        for (int row = static_cast<int>(rows.size()) - 1; row >= 0; --row) {
            used += heightOf(static_cast<std::size_t>(row));
            if (used > getHeight() - rulerHeight) { return std::min(static_cast<int>(rows.size()) - 1, row + 1); }
        }
        return 0;
    }
    int visualRowAt(int y) const {
        ensureTrackRows();
        if (y < rulerHeight) { return -1; }
        auto top = rulerHeight;
        for (auto row = static_cast<std::size_t>(std::max(0, scrollRows)); row < rows.size(); ++row) {
            if (y < top + heightOf(row)) { return static_cast<int>(row); }
            top += heightOf(row);
        }
        return static_cast<int>(rows.size()) + (y - top) / rowHeight;
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
        return found == rows.end() ? -rowHeight : rowY(static_cast<int>(found - rows.begin()));
    }
    int timeX(double time) const { return namesWidth + boundedPixel((time - scrollTime) * pixelsPerSecond); }
    int rowY(int row) const {
        auto y = rulerHeight;
        if (row >= scrollRows) {
            for (auto index = static_cast<std::size_t>(std::max(0, scrollRows)); static_cast<int>(index) < row && index < rows.size(); ++index) { y += heightOf(index); }
        } else {
            for (auto index = static_cast<std::size_t>(std::max(0, row)); static_cast<int>(index) < scrollRows && index < rows.size(); ++index) { y -= heightOf(index); }
        }
        return y;
    }
    juce::Rectangle<int> clipBounds(const motion::Clip& clip, int row) const {
        const auto timing = clip.timing(processor.document.project().bpm);
        return { timeX(timing.start), trackY(row), std::max(2, boundedPixel(timing.duration() * pixelsPerSecond)), rowHeight };
    }
    double snapTime(double time, juce::ModifierKeys modifiers) const {
        return modifiers.isAltDown() ? time : processor.document.project().timeGrid().snap(time);
    }
    void seek(int x, juce::ModifierKeys modifiers) {
        processor.seek(std::clamp(snapTime(scrollTime + (x - namesWidth) / pixelsPerSecond, modifiers), 0.0, processor.document.project().duration));
        repaint();
    }
    juce::TextButton addTrack;
    std::vector<std::unique_ptr<MotionTrackHeader>> headers;
    bool dropTrack = false;
    mutable std::vector<Row> rows;
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
    std::optional<juce::Rectangle<int>> marquee;
    juce::Point<int> marqueeStart;
    std::vector<KeyRef> marqueeBase;
};

#pragma once

#include "../MotionProcessor.h"
#include "TrackHeader.h"
#include "TrackLayout.h"
#include <optional>
#include <limits>

class MotionTimelineView : public juce::Component, public juce::DragAndDropTarget, public juce::SettableTooltipClient {
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
        setTooltip("V: Move / trim. S: Slip content. R: Stretch duration. Alt: disable snapping. Command/Ctrl + wheel: zoom. F: fit project. Escape: cancel edit. Command/Ctrl+D: duplicate clip.");
    }
    std::function<void(motion::Id)> onSelection, onMidiAssigned, onTimingRequested;
    std::function<void(const juce::String&)> onError;
    std::function<void(motion::Id, motion::Id)> onEffectAdded;
    motion::Id selected = 0;
    double pixelsPerSecond = 70;
    double scrollTime = 0;
    mutable int scrollRows = 0;
    static constexpr int namesWidth = 170;
    static constexpr int rulerHeight = 26;
    static constexpr int rowHeight = 40;

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
        const auto found = std::find_if(rows.begin(), rows.end(), [trackId](const auto& row) { return row.id == trackId; });
        if (found == rows.end()) { return; }
        const auto row = static_cast<int>(found - rows.begin());
        if (row < scrollRows) { scrollRows = row; }
        else if (row >= scrollRows + visibleRowCount()) { scrollRows = row - visibleRowCount() + 1; }
        if (expanded) { refreshTracks(); } else { resized(); repaint(); }
    }

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
        if (layoutGeneration != processor.document.generation()) { collapsedGroups.clear(); layoutGeneration = processor.document.generation(); }
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
                header->onMute = [this](motion::Id id) { toggleTrack(id, false); };
                header->onSolo = [this](motion::Id id) { toggleTrack(id, true); };
                addAndMakeVisible(*header);
                headers.push_back(std::move(header));
                found = headers.end() - 1;
            }
            (*found)->update(track, group, collapsedGroups.contains(track.id));
        };
        for (const auto& track : project.tracks) { updateHeader(track, false); }
        for (const auto& group : project.groups) {
            motion::Track display;
            display.id = group.id; display.name = group.name; display.muted = group.muted; display.solo = group.solo;
            updateHeader(display, true);
        }
        rows = motion::trackRows(project, collapsedGroups);
        layoutRevision = processor.document.revision();
        resized();
        repaint();
    }
    void resized() override {
        ensureTrackRows();
        addTrack.setBounds(namesWidth - 27, 2, 24, rulerHeight - 4);
        scrollRows = std::clamp(scrollRows, 0, maximumScrollRow());
        for (auto& header : headers) {
            const auto found = std::find_if(rows.begin(), rows.end(), [&](const auto& row) { return row.id == header->id; });
            const auto row = static_cast<int>(found - rows.begin());
            const auto y = rowY(row);
            header->setVisible(found != rows.end() && y >= rulerHeight && y < getHeight());
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
        if (row >= 0 && row < static_cast<int>(project.tracks.size()) && (project.tracks[row].kind != kind || !project.tracks[row].canPlace(clip, 0, project.bpm))) {
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
        selected = id;
        if (onSelection) {
            onSelection(id);
        }
        repaint();
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
            g.drawText(juce::String(grid.label(time, step)), x + 5, 0, 70, rulerHeight, juce::Justification::centredLeft);
        }
        g.setColour(osci::Colours::surfaceRaised());
        g.fillRect(0, 0, namesWidth, rulerHeight);
        g.setColour(osci::Colours::text());
        g.drawText(toolName(), 12, 0, namesWidth - 54, rulerHeight, juce::Justification::centredLeft);
        juce::Path toolArrow;
        toolArrow.addTriangle(namesWidth - 47.0f, 11.0f, namesWidth - 39.0f, 11.0f, namesWidth - 43.0f, 15.0f);
        g.fillPath(toolArrow);
        const auto& tracks = processor.document.project().tracks;
        scrollRows = std::clamp(scrollRows, 0, maximumScrollRow());
        for (int visible = scrollRows; visible < static_cast<int>(rows.size()); ++visible) {
            const auto y = rowY(visible);
            if (y >= getHeight()) {
                break;
            }
            g.setColour(osci::Colours::surfaceRaised());
            g.fillRect(0, y, namesWidth - 1, rowHeight - 1);
            g.setColour(osci::Colours::text());

            const auto index = rows[visible].track;
            if (index < 0) {
                g.setColour(osci::Colours::surfaceRaised().withAlpha(0.25f));
                g.fillRect(namesWidth, y, getWidth() - namesWidth, rowHeight - 1);
                juce::Graphics::ScopedSaveState summaryScope(g);
                g.reduceClipRegion(namesWidth, y, getWidth() - namesWidth, rowHeight);
                for (const auto& track : tracks) {
                    auto parent = track.group;
                    for (std::size_t depth = 0; parent != 0 && depth < motion::maximumGroupDepth; ++depth) {
                        if (parent == rows[visible].id) {
                            g.setColour(osci::Colours::accentColor().withAlpha(motion::trackIsAudible(processor.document.project(), track) ? 0.35f : 0.1f));
                            for (const auto& clip : track.clips) {
                                const auto timing = clip.timing(processor.document.project().bpm);
                                g.fillRoundedRectangle(static_cast<float>(timeX(timing.start)), y + rowHeight * 0.5f - 3, static_cast<float>(std::max(2, boundedPixel(timing.duration() * pixelsPerSecond))), 6, 2);
                            }
                            break;
                        }
                        const auto* group = motion::findGroup(processor.document.project(), parent);
                        parent = group != nullptr ? group->parent : 0;
                    }
                }
                continue;
            }
            juce::Graphics::ScopedSaveState scope(g);
            g.reduceClipRegion(namesWidth, y, getWidth() - namesWidth, rowHeight);
            const auto opacity = !motion::trackIsAudible(processor.document.project(), tracks[index]) ? 0.38f : 1.0f;
            for (const auto& clip : tracks[index].clips) {
                const auto bounds = clipBounds(clip, index).toFloat().reduced(1, 4);
                const auto active = clip.id == selected;
                const bool audio = tracks[index].kind == motion::TrackKind::audio;
                g.setColour((audio ? (active ? juce::Colour(0xff365e80) : juce::Colour(0xff304451)) : (active ? juce::Colour(0xff305742) : juce::Colour(0xff354c45))).withAlpha(opacity));
                g.fillRoundedRectangle(bounds, 4);
                g.setColour((active ? juce::Colour(0xff70da91) : juce::Colour(0xff647d71)).withAlpha(opacity));
                g.drawRoundedRectangle(bounds, 4, 1);
                g.setColour(juce::Colours::white);
                if (!clip.effects.empty() && bounds.getWidth() > 90) {
                    g.setColour(juce::Colours::white.withAlpha(0.7f));
                    g.drawText(juce::String(static_cast<int>(clip.effects.size())) + " fx", bounds.withLeft(bounds.getRight() - 38), juce::Justification::centred);
                    g.setColour(juce::Colours::white);
                }
                auto label = juce::String(clip.name);
                if (active && tool == Tool::slip) {
                    label += "  offset " + juce::String(clip.timing(processor.document.project().bpm).offset, 2) + "s";
                } else if (active && tool == Tool::stretch) {
                    label += "  " + juce::String(clip.timing(processor.document.project().bpm).rate, 2) + "x";
                }
                auto labelBounds = bounds.reduced(8, 0).withTrimmedRight(!clip.effects.empty() && bounds.getWidth() > 90 ? 32.0f : 0.0f);
                if (audio) {
                    const auto& assets = processor.document.project().assets;
                    const auto asset = std::find_if(assets.begin(), assets.end(), [&](const auto& item) { return item->id == clip.asset; });
                    if (asset != assets.end() && (*asset)->audio != nullptr) {
                        const auto left = std::max(namesWidth, static_cast<int>(bounds.getX()) + 2);
                        const auto right = std::min(getWidth(), static_cast<int>(bounds.getRight()) - 2);
                        const auto centre = bounds.getBottom() - 8.5f;
                        g.setColour(juce::Colour(0xff97c7df).withAlpha(opacity));
                        for (int x = left; x < right; ++x) {
                            const auto time = scrollTime + (x - namesWidth) / pixelsPerSecond;
                            const auto end = time + 1.0 / pixelsPerSecond;
                            const auto a = (*asset)->audio->querySeconds(0, clip.localTime(time, processor.document.project().bpm), clip.localTime(end, processor.document.project().bpm));
                            const auto b = (*asset)->audio->querySeconds(1, clip.localTime(time, processor.document.project().bpm), clip.localTime(end, processor.document.project().bpm));
                            const auto low = std::clamp(std::min(a.minimum, b.minimum), -1.0f, 1.0f);
                            const auto high = std::clamp(std::max(a.maximum, b.maximum), -1.0f, 1.0f);
                            g.drawVerticalLine(x, centre - high * 8, centre - low * 8 + 0.5f);
                        }
                    }
                    labelBounds = labelBounds.withHeight(15);
                }
                g.setColour(juce::Colours::white.withAlpha(opacity));
                g.drawText(label, labelBounds, juce::Justification::centredLeft);
            }
        }
        if (dropPosition.has_value() && dropTrack) {
            g.setColour(osci::Colours::accentColor());
            const auto boundary = std::clamp((dropPosition->y - rulerHeight + rowHeight / 2) / rowHeight + scrollRows, 0, static_cast<int>(rows.size()));
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
            if (asset != assets.end()) {
                candidate = motion::Document::makeClip(candidate.id, **asset, candidate.start);
            }
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
            const auto allowed = asset != assets.end() && correctKind && (row < 0 || row >= static_cast<int>(tracks.size()) || tracks[row].canPlace(candidate, 0, processor.document.project().bpm));
            const auto bounds = (row >= 0 ? clipBounds(candidate, row) : juce::Rectangle<int>(timeX(candidate.start), rowY(std::max(0, visualRowAt(dropPosition->y))), std::max(2, boundedPixel(candidate.duration * pixelsPerSecond)), rowHeight)).toFloat().reduced(1, 4);
            juce::Graphics::ScopedSaveState scope(g);
            g.reduceClipRegion(namesWidth, rulerHeight, getWidth() - namesWidth, getHeight() - rulerHeight);
            g.setColour((allowed ? juce::Colour(0xff70da91) : juce::Colour(0xffe98080)).withAlpha(0.2f));
            g.fillRoundedRectangle(bounds, 4);
            g.setColour(allowed ? juce::Colour(0xff70da91) : juce::Colour(0xffe98080));
            g.drawRoundedRectangle(bounds, 4, 1);
            g.drawText(allowed ? (audio ? "Add audio" : "Add object") : (correctKind ? "Clips cannot overlap" : "Use a matching or empty lane"), bounds.reduced(8, 0), juce::Justification::centredLeft);
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
        cancelGesture();
        if (event.y < rulerHeight && event.x < namesWidth) {
            showToolMenu(true);
            return;
        }
        if (event.mods.isPopupMenu()) {
            int row = 0;
            const auto* clip = clipAt(event.getPosition(), row);
            if (clip != nullptr) {
                const auto id = clip->id;
                selectClip(id);
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
            return;
        }
        original = *clip;
        originalRow = row;
        downX = event.x;
        const auto bounds = clipBounds(original, row);
        mode = tool == Tool::slip ? Mode::slip : (tool == Tool::stretch ? Mode::stretch
            : (event.x < bounds.getX() + 8 ? Mode::left : (event.x > bounds.getRight() - 8 ? Mode::right : Mode::move)));
        before = processor.document.project();
        expectedRevision = processor.document.revision();
        changed = false;
        selectClip(original.id);
    }

    void mouseMove(const juce::MouseEvent& event) override {
        int row = 0;
        const auto* clip = clipAt(event.getPosition(), row);
        if (event.y < rulerHeight && event.x < namesWidth) {
            setMouseCursor(juce::MouseCursor::PointingHandCursor);
        } else if (clip != nullptr) {
            const auto bounds = clipBounds(*clip, row);
            const bool edge = event.x < bounds.getX() + 8 || event.x > bounds.getRight() - 8;
            setMouseCursor(tool != Tool::move || edge ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::DraggingHandCursor);
        } else {
            setMouseCursor(juce::MouseCursor::NormalCursor);
        }
    }

    void mouseDrag(const juce::MouseEvent& event) override {
        if (scrubbing) {
            seek(event.x, event.mods);
            return;
        }
        if (!gestureIsCurrent()) {
            return;
        }
        auto candidate = original;
        const auto timing = original.timing(before->bpm);
        auto edited = timing;
        auto delta = (event.x - downX) / pixelsPerSecond;
        if (delta != 0.0 && !event.mods.isAltDown()) {
            const auto anchor = mode == Mode::right || mode == Mode::stretch ? timing.end()
                : (mode == Mode::slip ? timing.offset / timing.rate : timing.start);
            delta = before->timeGrid().snap(anchor + delta) - anchor;
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
        if (before->tracks[target].kind != before->tracks[originalRow].kind) { return; }
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
        scrubbing = false;
        if (!gestureIsCurrent()) {
            return;
        }
        auto originalProject = std::move(*before);
        before.reset();
        if (changed) {
            const auto label = mode == Mode::move ? "Move clip" : (mode == Mode::slip ? "Slip clip" : (mode == Mode::stretch ? "Stretch clip" : "Trim clip"));
            processor.document.commit(label, std::move(originalProject));
        }
        changed = false;
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
        if (selected != 0 && key.getModifiers().isCommandDown() && key.getKeyCode() == 'D') {
            duplicateClip(selected);
            return true;
        }
        if (key == juce::KeyPress::escapeKey && (before.has_value() || scrubbing)) {
            cancelGesture();
            return true;
        }
        if (!key.getModifiers().isCommandDown() && !key.getModifiers().isCtrlDown() && !key.getModifiers().isAltDown()) {
            const auto character = juce::CharacterFunctions::toLowerCase(key.getTextCharacter());
            if (character == 'v' || character == 's' || character == 'r') {
                cancelGesture();
                tool = character == 'v' ? Tool::move : (character == 's' ? Tool::slip : Tool::stretch);
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
        if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) {
            cancelGesture();
            const auto& tracks = processor.document.project().tracks;
            const auto exists = std::any_of(tracks.begin(), tracks.end(), [&](const auto& track) {
                return std::any_of(track.clips.begin(), track.clips.end(), [&](const auto& clip) { return clip.id == selected; });
            });
            if (!exists) {
                return false;
            }
            processor.document.edit("Delete clip", [&](motion::Project& project) {
                for (auto& track : project.tracks) {
                    std::erase_if(track.clips, [&](const auto& clip) { return clip.id == selected; });
                }
            });
            selected = 0;
            if (onSelection) {
                onSelection(0);
            }
            return true;
        }
        return false;
    }

private:
    void duplicateClip(motion::Id id) {
        cancelGesture();
        motion::Id duplicate = 0;
        const auto result = processor.document.duplicateClip(id, duplicate);
        if (result.failed()) { if (onError) { onError(result.getErrorMessage()); } return; }
        selectClip(duplicate);
        revealSelection();
        for (const auto& track : processor.document.project().tracks) {
            for (const auto& clip : track.clips) {
                if (clip.id == duplicate) { revealTime(clip.timing(processor.document.project().bpm).start); return; }
            }
        }
    }
    void showClipMenu(motion::Id id) {
        juce::PopupMenu menu;
        menu.addItem(1, "Duplicate clip");
        menu.addItem(2, "Edit clip timing");
        const auto generation = processor.document.generation();
        const auto revision = processor.document.revision();
        const juce::Component::SafePointer<MotionTimelineView> owner(this);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withMousePosition(), [owner, id, generation, revision](int result) {
            if (owner == nullptr || result == 0 || owner->processor.document.generation() != generation || owner->processor.document.revision() != revision) { return; }
            if (result == 1) { owner->duplicateClip(id); }
            else if (result == 2 && owner->onTimingRequested) { owner->onTimingRequested(id); }
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
    enum class Tool { move, slip, stretch };
    enum class Mode { move, left, right, slip, stretch };

    juce::String toolName() const {
        return tool == Tool::move ? "Move / Trim (V)" : (tool == Tool::slip ? "Slip (S)" : "Stretch (R)");
    }

    void showToolMenu(bool atHeader = false) {
        juce::PopupMenu menu;
        menu.addSectionHeader("Clip editing tool");
        menu.addItem(1, "Move / trim edges (V)", true, tool == Tool::move);
        menu.addItem(2, "Slip content inside clip (S)", true, tool == Tool::slip);
        menu.addItem(3, "Stretch duration and speed (R)", true, tool == Tool::stretch);
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
                owner->tool = result == 1 ? Tool::move : (result == 2 ? Tool::slip : Tool::stretch);
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

    void selectClip(motion::Id id) {
        selected = id;
        if (onSelection) {
            onSelection(id);
        }
        repaint();
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
            layoutGeneration = generation;
            layoutRevision.reset();
        }
        if (!layoutRevision.has_value() || *layoutRevision != revision) {
            rows = motion::trackRows(processor.document.project(), collapsedGroups);
            layoutRevision = revision;
        }
        scrollRows = std::clamp(scrollRows, 0, maximumScrollRow());
    }
    int visibleRowCount() const { return std::max(1, (getHeight() - rulerHeight) / rowHeight); }
    int maximumScrollRow() const { return std::max(0, static_cast<int>(rows.size()) - visibleRowCount()); }
    int visualRowAt(int y) const {
        ensureTrackRows();
        return y < rulerHeight ? -1 : (y - rulerHeight) / rowHeight + scrollRows;
    }
    int trackAtY(int y) const {
        const auto row = visualRowAt(y);
        return row >= 0 && row < static_cast<int>(rows.size()) ? rows[row].track : -1;
    }
    motion::Id groupAtY(int y) const {
        const auto row = visualRowAt(y);
        return row >= 0 && row < static_cast<int>(rows.size()) && rows[row].group() ? rows[row].id : 0;
    }
    int trackY(int track) const {
        ensureTrackRows();
        const auto found = std::find_if(rows.begin(), rows.end(), [track](const auto& row) { return row.track == track; });
        return found == rows.end() ? -rowHeight : rowY(static_cast<int>(found - rows.begin()));
    }
    int timeX(double time) const { return namesWidth + boundedPixel((time - scrollTime) * pixelsPerSecond); }
    int rowY(int row) const { return rulerHeight + (row - scrollRows) * rowHeight; }
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
    mutable std::vector<motion::TrackRow> rows;
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
    bool scrubbing = false;
    bool changed = false;
    std::optional<juce::Point<int>> dropPosition;
    motion::Id dropAssetId = 0;
    std::string dropEffect;
};

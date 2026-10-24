#include "TimelineView.h"
#include "../model/LightPresets.h"

// Clips: where they sit, how they paint and are dragged, their menu, and
// what dragging sources and effects onto the tracks, deleting and
// duplicating do.

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

juce::Rectangle<int> MotionTimelineView::clipBounds(const motion::Clip& clip, int row) const {
    const auto timing = clip.timing(processor.document.project().tempo());
    return { timeX(timing.start), trackY(row), std::max(2, boundedPixel(timing.duration() * pixelsPerSecond)), trackHeight(row) };
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
            g.setColour(motion::style::accent());
            g.drawRect(0, rowY(visualRowAt(dropPosition->y)), getWidth(), heightAt(visualRowAt(dropPosition->y)), 2);
        } else if (row >= 0 && row < static_cast<int>(tracks.size()) && tracks[row].kind == motion::TrackKind::visual && (clip != nullptr || dropPosition->x < namesWidth)) {
            const auto bounds = clip != nullptr ? clipBounds(*clip, row) : juce::Rectangle<int>(0, trackY(row), namesWidth, trackHeight(row));
            g.setColour(motion::style::accent());
            g.drawRoundedRectangle(bounds.toFloat().reduced(2), motion::style::radiusControl, 2);
        }
    } else {
        const auto row = trackAtY(dropPosition->y);
        motion::Clip candidate;
        candidate.id = std::numeric_limits<motion::Id>::max();
        candidate.start = dropSnapped;
        const auto asset = motion::findAsset(processor.document.project().assets, dropAssetId);
        const auto definition = motion::findDefinition(processor.document.project(), dropAssetId);
        if (asset != nullptr) {
            candidate = motion::Document::makeClip(candidate.id, *asset, candidate.start);
        } else if (definition != nullptr) {
            candidate = motion::Document::makeCompositionClip(candidate.id, *definition, candidate.start);
        }
        const bool recursive = definition != nullptr && !processor.document.canReferenceComposition(dropAssetId);
        const bool audio = asset != nullptr && asset->audio != nullptr;
        const auto kind = audio ? motion::TrackKind::audio : motion::TrackKind::visual;
        const bool correctKind = row < 0 || row >= static_cast<int>(tracks.size()) || tracks[row].kind == kind;
        const auto allowed = (asset != nullptr || definition != nullptr) && !recursive && correctKind && (row < 0 || row >= static_cast<int>(tracks.size()) || !tracks[row].locked && tracks[row].canPlace(candidate, 0, processor.document.project().tempo()));
        // Below the tracks, the new track always lands in the first free slot.
        const auto group = groupAtY(dropPosition->y);
        const auto slotY = row < 0 && group == 0 ? rowY(static_cast<int>(rows.size())) : rowY(std::max(0, visualRowAt(dropPosition->y)));
        const auto bounds = (row >= 0 ? clipBounds(candidate, row) : juce::Rectangle<int>(timeX(candidate.start), slotY, std::max(2, boundedPixel(candidate.duration * pixelsPerSecond)), defaultTrackHeight)).toFloat().reduced(1, 4);
        if (row < 0 && allowed) {
            // The header column shows that a track will be created.
            auto header = juce::Rectangle<float>(4.0f, static_cast<float>(slotY) + 3.0f, static_cast<float>(namesWidth) - 9.0f, static_cast<float>(defaultTrackHeight) - 6.0f);
            g.setColour(motion::style::accent().withAlpha(.12f));
            g.fillRoundedRectangle(header, motion::style::radiusControl);
            g.setColour(motion::style::accent().withAlpha(.7f));
            g.drawRoundedRectangle(header.reduced(.5f), motion::style::radiusControl, 1.0f);
            motion::icons::draw(g, motion::icons::Icon::add, header.removeFromLeft(26.0f), motion::style::accent().brighter(.3f), motion::style::iconSmall);
            g.setFont(motion::style::body());
            g.setColour(osci::Colours::text());
            g.drawText(audio ? "New audio track" : "New track", header, juce::Justification::centredLeft, true);
        }
        juce::Graphics::ScopedSaveState scope(g);
        g.reduceClipRegion(namesWidth, rulerHeight, getWidth() - namesWidth, getHeight() - rulerHeight);
        g.setColour((allowed ? motion::style::accent() : motion::style::error()).withAlpha(0.2f));
        g.fillRoundedRectangle(bounds, motion::style::radiusControl);
        g.setColour(allowed ? motion::style::accent() : motion::style::error());
        g.drawRoundedRectangle(bounds, motion::style::radiusControl, 1);
        g.drawText(allowed ? (audio ? "Add audio" : definition != nullptr ? "Add composition" : "Add object") : (recursive ? "Can't contain itself" : correctKind ? "Clips can't overlap" : "Use a matching or empty lane"), bounds.reduced(8, 0), juce::Justification::centredLeft);
    }
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
    if (dropAssetId != 0) { dropSnapped = dropStart(details.localPosition.x, dropAssetId, juce::ModifierKeys::getCurrentModifiers()); }
    previewEffect(dropEffect.empty() && dropInstance == 0 ? 0 : effectOwnerAt(details.localPosition));
    repaint();
}

void MotionTimelineView::itemDragExit(const SourceDetails&) {
    dropPosition.reset();
    snapGuide.reset();
    addTrack.setVisible(true);
    previewEffect(0);
    repaint();
}

void MotionTimelineView::previewEffect(motion::Id owner) {
    if (owner == previewedOwner && processor.document.revision() == previewedRevision) { return; }
    previewedOwner = owner;
    previewedRevision = processor.document.revision();
    const auto target = owner != 0 ? std::optional<motion::Id>(owner) : std::nullopt;
    // A moved card plays where it would land; a new effect, at the end of the owner's stack.
    if (dropInstance != 0) {
        if (onEffectMovePreview) { onEffectMovePreview(dropInstance, target); }
    } else if (onEffectPreview) {
        onEffectPreview(dropEffect, target);
    }
}

void MotionTimelineView::setEffectDragActive(bool active) {
    effectDragActive = active;
    repaint();
}

void MotionTimelineView::itemDropped(const SourceDetails& details) {
    dropPosition.reset();
    snapGuide.reset();
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

double MotionTimelineView::dropStart(int x, motion::Id id, juce::ModifierKeys modifiers) {
    const auto time = dropTime(x);
    snapGuide.reset();
    if (modifiers.isAltDown()) { return time; }
    const auto& project = processor.document.project();
    const auto asset = motion::findAsset(project.assets, id);
    const auto definition = motion::findDefinition(project, id);
    const auto clip = asset != nullptr ? std::optional(motion::Document::makeClip(0, *asset, 0)) : definition != nullptr ? std::optional(motion::Document::makeCompositionClip(0, *definition, 0)) : std::nullopt;
    const auto length = clip.has_value() ? clip->timing(project.tempo()).end() : 0.0;
    const auto startTarget = magnet(time, project, {});
    const auto endTarget = length > 0 ? magnet(time + length, project, {}) : std::nullopt;
    const auto startDistance = startTarget.has_value() ? std::abs(*startTarget - time) : 1.0e300;
    const auto endDistance = endTarget.has_value() ? std::abs(*endTarget - (time + length)) : 1.0e300;
    if (startTarget.has_value() && startDistance <= endDistance) {
        snapGuide = startTarget;
        return *startTarget;
    }
    if (endTarget.has_value() && *endTarget - length >= 0) {
        snapGuide = endTarget;
        return *endTarget - length;
    }
    return snapTime(time, modifiers);
}

bool MotionTimelineView::insertAsset(motion::Id assetId, int x, int y) {
    const auto& project = processor.document.project();
    const auto asset = motion::findAsset(project.assets, assetId);
    const auto snapped = dropStart(x, assetId, juce::ModifierKeys::getCurrentModifiers());
    snapGuide.reset();
    const auto row = trackAtY(y);
    const auto group = groupAtY(y);
    if (asset == nullptr) {
        const auto track = row >= 0 && row < static_cast<int>(project.tracks.size()) ? project.tracks[row].id : 0;
        motion::Id inserted = 0;
        if (!report(processor.document.insertComposition(assetId, snapped, track, group, inserted))) { return false; }
        selectClip(inserted);
        refreshTracks();
        return true;
    }
    const auto kind = asset->audio != nullptr ? motion::TrackKind::audio : motion::TrackKind::visual;
    auto clip = motion::Document::makeClip(processor.document.newId(), *asset, snapped);
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

void MotionTimelineView::dragClips(const juce::MouseEvent& event) {
    // One the document changed under ends.
    if (edit.stale()) { gesture = {}; edit.reset(); repaint(); return; }
    const auto& drag = std::get<ClipDrag>(gesture);
    const auto& before = edit.start();
    // Shows the drag's edit, made from the start.
    const auto show = [this](motion::Project updated, bool changed) {
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
        const auto tracksDown = row >= 0 ? row - drag.originalRow : 0;
        if (tracksDown != 0) {
            const auto sourceY = trackY(drag.originalRow);
            const auto visibleDelta = trackY(row) - sourceY;
            for (int index = 0; index < static_cast<int>(updated.tracks.size()); ++index) {
                const auto& track = updated.tracks[index];
                if (!std::any_of(track.clips.begin(), track.clips.end(), [&](const auto& item) { return selectedClips.contains(item.id); })) { continue; }
                const auto y = trackY(index);
                if (y < rulerHeight || trackAtY(y + visibleDelta) != index + tracksDown) { return; }
            }
        }
        if (!motion::moveClips(updated.tracks, {selectedClips.begin(), selectedClips.end()}, delta, tracksDown, updated.tempo())) { return; }
        for (const auto& track : updated.tracks) {
            for (const auto& item : track.clips) { updated.duration = std::max(updated.duration, item.timing(updated.tempo()).end()); }
        }
        show(std::move(updated), delta != 0 || tracksDown != 0);
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
        // Back where it began: the start is shown once.
        if (edit.changed()) { show(before, false); }
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
    g.fillRoundedRectangle(bounds, motion::style::radiusControl);
    if (active) {
        g.setColour(motion::style::accent().withAlpha(opacity));
        g.drawRoundedRectangle(bounds.reduced(.75f), motion::style::radiusControl, 1.5f);
    } else {
        g.setColour(base.brighter(.6f).withAlpha(opacity * (hovered ? .75f : .35f)));
        g.drawRoundedRectangle(bounds.reduced(.5f), motion::style::radiusControl, 1.0f);
    }
    // Too narrow for anything but its colour.
    if (bounds.getWidth() < 14) { return; }
    if ((hovered || (active && tool == Tool::ripple)) && bounds.getWidth() > 24) {
        g.setColour(active ? motion::style::accent().withAlpha(opacity) : juce::Colours::white.withAlpha(.35f * opacity));
        g.fillRoundedRectangle(bounds.getX() + 3, bounds.getY() + 6, 2, bounds.getHeight() - 12, 1);
        g.fillRoundedRectangle(bounds.getRight() - 5, bounds.getY() + 6, 2, bounds.getHeight() - 12, 1);
    }
    const auto shareShown = !audio && paintBeamShare(g, clip.id, beamArea(clip, index), bounds, opacity);
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
        const auto asset = motion::findAsset(project.assets, clip.asset);
        if (asset != nullptr && asset->audio != nullptr) {
            const auto left = std::max(namesWidth, static_cast<int>(bounds.getX()) + 2);
            const auto right = std::min(getWidth(), static_cast<int>(bounds.getRight()) - 2);
            auto wave = bounds.withTrimmedTop(nameBand).withTrimmedBottom(2.0f);
            if (wave.getHeight() < 6.0f) { wave = bounds.reduced(0, 3.0f); }
            const auto centre = wave.getCentreY();
            const auto reach = wave.getHeight() * .5f;
            g.setColour(motion::style::waveform().withAlpha(opacity * .38f));
            for (int x = left; x < right; ++x) {
                const auto time = timeAt(x);
                const auto end = time + 1.0 / pixelsPerSecond;
                const auto a = asset->audio->querySeconds(0, clip.localTime(time, project.tempo()), clip.localTime(end, project.tempo()));
                const auto b = asset->audio->querySeconds(1, clip.localTime(time, project.tempo()), clip.localTime(end, project.tempo()));
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
        // Cut at the clip's sides only: a band shorter than the font would cut its descenders.
        g.reduceClipRegion(labelBounds.withY(bounds.getY()).withHeight(bounds.getHeight()).toNearestInt());
        g.setFont(audio ? motion::style::caption() : motion::style::body());
        const auto font = g.getCurrentFont();
        const auto textWidth = juce::TextLayout::getStringWidth(font, label);
        const auto fill = (hovered ? base.brighter(.1f) : base).withAlpha(opacity);
        // Over a beam share the name sits on the clip's own colour, so the
        // share's fill, edge and keys never run through it.
        if (shareShown) {
            const auto chipWidth = std::min(textWidth + 6, labelBounds.getWidth() + 3);
            g.setColour(fill);
            g.fillRoundedRectangle(labelBounds.withX(labelBounds.getX() - 3).withWidth(chipWidth).withSizeKeepingCentre(chipWidth, font.getHeight() + 2), 3);
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
    // Keys on a clip edge are inset so they are never cut in half.
    const auto inset = juce::Range<float>(bounds.getX() + 6.0f, std::max(bounds.getX() + 6.0f, bounds.getRight() - 6.0f));
    g.fillPath(keyTicks(keyTimes, bounds.getBottom() - 5.0f, bounds.getX() - 2, bounds.getRight() + 2, inset));
}

void MotionTimelineView::deleteSelectedClips(bool ripple) {
    cancelClipGesture();
    const auto result = processor.document.removeClips({selectedClips.begin(), selectedClips.end()}, ripple);
    if (!report(result)) { return; }
    selectClip(0);
    refreshTracks();
}

void MotionTimelineView::duplicateClip(motion::Id id) {
    cancelClipGesture();
    std::vector<motion::Id> duplicates;
    const auto sources = selectedClips.contains(id) ? std::vector<motion::Id>(selectedClips.begin(), selectedClips.end()) : std::vector<motion::Id>{id};
    const auto result = processor.document.duplicateClips(sources, duplicates);
    if (!report(result)) { return; }
    selectedClips = {duplicates.begin(), duplicates.end()};
    const auto duplicate = duplicates.front();
    notifySelection(duplicate);
    revealSelection();
    const auto* clip = motion::findClip(processor.document.project(), duplicate);
    if (clip != nullptr) { revealTime(clip->timing(processor.document.project().tempo()).start); }
}

void MotionTimelineView::showClipMenu(motion::Id id) {
    juce::PopupMenu menu;
    const auto many = selectedClips.size() > 1;
    motion::Id asset = 0, definition = 0;
    bool locked = false;
    const auto* clip = motion::findClip(processor.document.project(), id);
    if (clip != nullptr) {
        asset = clip->asset;
        definition = clip->composition;
        locked = motion::findClipTrack(processor.document.project(), id)->locked;
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
            const auto* shown = motion::findClip(processor.document.project(), id);
            if (shown != nullptr) { onRevealSource(shown->asset); }
        } else if (result == 6) {
            motion::Id definitionId = 0;
            report(processor.document.makeCompositionUnique(id, definitionId));
            refreshTracks();
        }
    });
}

void MotionTimelineView::applyLightPreset(motion::Id id, std::optional<motion::LightPreset> preset) {
    motion::editLightPreset(processor.document, id, preset);
    refreshTracks();
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
    if (!report(created)) { return; }
    selectClip(instance);
    refreshTracks();
}

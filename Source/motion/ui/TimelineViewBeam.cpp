#include "TimelineView.h"

// The beam share each clip or group takes, as bars along its row: reading,
// painting and dragging them.

const MotionTimelineView::BeamCache* MotionTimelineView::ensureBeamShares() const {
    const auto* composition = beamComposition ? beamComposition() : nullptr;
    if (composition == nullptr) { return nullptr; }
    if (beamCache.has_value() && beamCache->start == scrollTime && beamCache->pixels == pixelsPerSecond && beamCache->left == namesWidth && beamCache->width == getWidth()) {
        return &*beamCache;
    }
    const auto& project = processor.document.project();
    BeamCache cache {scrollTime, pixelsPerSecond, namesWidth, getWidth(), {}};
    const auto columns = std::max(0, (getWidth() - namesWidth) / beamStep + 1);
    const auto live = processor.liveSourcePreview();
    const auto add = [&](motion::Id id, int column, double fraction) {
        auto& values = cache.shares[id];
        if (values.empty()) { values.assign(static_cast<std::size_t>(columns), 0.0f); }
        values[static_cast<std::size_t>(column)] += static_cast<float>(fraction);
    };
    for (int column = 0; column < columns; ++column) {
        const auto time = scrollTime + column * beamStep / pixelsPerSecond;
        for (const auto& share : beamShares.at(*composition, time, live.get())) {
            add(share.clip, column, share.fraction);
            // A group's share is everything it holds.
            const auto* track = motion::findClipTrack(project, share.clip);
            auto group = track != nullptr ? track->group : motion::Id(0);
            for (std::size_t depth = 0; group != 0 && depth < motion::maximumGroupDepth; ++depth) {
                add(group, column, share.fraction);
                const auto* found = motion::findGroup(project, group);
                group = found != nullptr ? found->parent : 0;
            }
        }
    }
    beamCache = std::move(cache);
    return &*beamCache;
}

std::optional<float> MotionTimelineView::beamShareAt(motion::Id id, float x) const {
    const auto* cache = ensureBeamShares();
    if (cache == nullptr) { return std::nullopt; }
    const auto found = cache->shares.find(id);
    if (found == cache->shares.end()) { return 0.0f; }
    return interpolatedShare(found->second, x - static_cast<float>(cache->left));
}

float MotionTimelineView::interpolatedShare(const std::vector<float>& values, float offset) {
    if (values.empty()) { return 0.0f; }
    const auto position = std::clamp(offset / beamStep, 0.0f, static_cast<float>(values.size() - 1));
    const auto index = static_cast<std::size_t>(position);
    const auto next = std::min(index + 1, values.size() - 1);
    const auto a = values[index], b = values[next];
    // Across an object's start or end the share steps rather than ramps.
    if ((a > 0) != (b > 0)) { return position - static_cast<float>(index) < .5f ? a : b; }
    return a + (b - a) * (position - static_cast<float>(index));
}

juce::Rectangle<float> MotionTimelineView::beamArea(const motion::Clip& clip, int row) const {
    return clipBounds(clip, row).toFloat().reduced(1, 3).reduced(1.5f);
}

std::optional<juce::Rectangle<float>> MotionTimelineView::groupBeamArea(motion::Id group) const {
    const auto found = std::find_if(rows.begin(), rows.end(), [group](const auto& row) { return row.id == group && row.group(); });
    if (found == rows.end()) { return std::nullopt; }
    const auto index = static_cast<int>(found - rows.begin());
    const auto y = static_cast<float>(rowY(index) + shiftOf(index));
    const auto height = static_cast<float>(heightOf(static_cast<std::size_t>(index)) - bandGap);
    return juce::Rectangle<float>(static_cast<float>(namesWidth), y, static_cast<float>(getWidth() - namesWidth), height).reduced(0, 4);
}

void MotionTimelineView::paintBeamShare(juce::Graphics& g, motion::Id id, juce::Rectangle<float> area, juce::Rectangle<float> body, float opacity) const {
    const auto* beamDrag = active<BeamDrag>();
    const auto lit = beamHover == id || (beamDrag != nullptr && beamDrag->id == id);
    const auto chosen = id == selected || selectedClips.contains(id);
    // At rest a clip is a plain bar: its share shows under the pointer or on
    // the selection, and a badge by its name flags a Beam other than 1x.
    if (!lit && !chosen && hoveredClip != id) { return; }
    const auto* cache = ensureBeamShares();
    if (cache == nullptr) { return; }
    const auto left = std::max(area.getX(), static_cast<float>(namesWidth)), right = std::min(area.getRight(), static_cast<float>(getWidth()));
    if (right - left < 2 || area.getHeight() < 6) { return; }
    const auto found = cache->shares.find(id);
    if (found == cache->shares.end()) { return; }
    const auto share = [&](float x) { return interpolatedShare(found->second, x - static_cast<float>(cache->left)); };
    juce::Path fill, edge;
    fill.startNewSubPath(left, area.getBottom());
    // The edge runs only where it is drawing, so a gap reads as a gap.
    bool drawing = false;
    for (auto x = left;; x += 2) {
        const auto at = std::min(x, right);
        const auto value = share(at);
        const auto y = beamY(area, value);
        fill.lineTo(at, y);
        if (value > 0) {
            if (drawing) { edge.lineTo(at, y); } else { edge.startNewSubPath(at, y); }
        }
        drawing = value > 0;
        if (at >= right) { break; }
    }
    fill.lineTo(right, area.getBottom());
    fill.closeSubPath();
    const auto& project = processor.document.project();
    const auto target = motion::findPropertyTarget(project, id);
    const auto* curve = target.has_value() ? target->curve("beam") : nullptr;
    {
        juce::Graphics::ScopedSaveState state(g);
        juce::Path shape;
        shape.addRoundedRectangle(body, motion::style::radius);
        g.reduceClipRegion(shape);
        // Like a waveform: a quiet fill. The selected object shows its edge
        // and keys, as Premiere shows a selected clip's rubber band; hovered,
        // they light up to be dragged.
        g.setColour(juce::Colours::white.withAlpha((lit ? .13f : .075f) * opacity));
        g.fillPath(fill);
        if (!lit && !chosen) { return; }
        const auto ink = lit ? motion::style::accent().withAlpha(opacity) : juce::Colours::white.withAlpha(.5f * opacity);
        g.setColour(ink);
        g.strokePath(edge, juce::PathStrokeType(lit ? 1.5f : 1.0f));
        // Its keys sit on the edge, as on Premiere's rubber bands.
        if (curve != nullptr && curve->animated()) {
            juce::Path keys;
            for (const auto& key : curve->keyframes()) {
                const auto at = static_cast<float>(timeX(target->projectTime(key.time)));
                if (at < left - 4 || at > right + 4) { continue; }
                motion::style::addDiamond(keys, {at, beamY(area, share(std::clamp(at, left, right)))}, 4.0f);
            }
            g.setColour(osci::Colours::veryDark());
            g.strokePath(keys, juce::PathStrokeType(2.0f));
            g.setColour(ink);
            g.fillPath(keys);
        }
        if (!lit) { return; }
    }
    // What the edge means, where the pointer is.
    const auto x = std::clamp(static_cast<float>(beamPointer), left, right);
    const auto time = scrollTime + (x - namesWidth) / pixelsPerSecond;
    const auto beam = curve != nullptr ? curve->evaluate(target->localTime(time)) : 1.0;
    auto text = juce::String(juce::roundToInt(share(x) * 100)) + "% of the beam";
    if (std::abs(beam - 1) > .005) { text += motion::style::dot() + juce::String(beam, 2) + juce::String(juce::CharPointer_UTF8("\xc3\x97")); }
    const auto width = juce::TextLayout::getStringWidth(motion::style::caption(), text) + 12;
    const auto y = beamY(area, share(x));
    // Above the edge, where the pointer isn't, even past the clip's top; below
    // it on the top row, where above would be under the bands.
    const auto above = y - 22 >= static_cast<float>(rulerHeight) + 2;
    auto pill = juce::Rectangle<float>(width, 16).withCentre({x, 0}).withY(above ? y - 22 : y + 6);
    pill = pill.withX(std::clamp(pill.getX(), left, std::max(left, right - width)));
    beamLabel = std::make_pair(pill, text);
}

void MotionTimelineView::paintBeamLabel(juce::Graphics& g) const {
    if (!beamLabel.has_value()) { return; }
    juce::Graphics::ScopedSaveState state(g);
    g.reduceClipRegion(namesWidth, rulerHeight, getWidth() - namesWidth, viewHeight());
    const auto& [pill, text] = *beamLabel;
    g.setColour(osci::Colours::veryDark().withAlpha(.92f));
    g.fillRoundedRectangle(pill, 4);
    g.setColour(motion::style::accent().withAlpha(.5f));
    g.drawRoundedRectangle(pill.reduced(.5f), 4, 1.0f);
    g.setColour(osci::Colours::text());
    g.setFont(motion::style::caption());
    g.drawText(text, pill, juce::Justification::centred, false);
}

std::optional<MotionTimelineView::BeamTarget> MotionTimelineView::beamEdgeAt(juce::Point<int> point) const {
    if (point.x < namesWidth || point.y < rulerHeight) { return std::nullopt; }
    const auto nearby = [&](motion::Id id, juce::Rectangle<float> area, bool drawing) -> std::optional<BeamTarget> {
        if (area.getHeight() < 10) { return std::nullopt; }
        const auto share = beamShareAt(id, static_cast<float>(point.x));
        if (!share.has_value() || (drawing && !(*share > 0))) { return std::nullopt; }
        if (std::abs(static_cast<float>(point.y) - beamY(area, *share)) > 4) { return std::nullopt; }
        return BeamTarget {id, area};
    };
    const auto group = groupAtY(point.y);
    if (group != 0) {
        const auto area = groupBeamArea(group);
        // A group's edge is wherever what it holds is drawing.
        return area.has_value() ? nearby(group, *area, true) : std::nullopt;
    }
    int row = 0;
    const auto* clip = clipAt(point, row);
    if (clip == nullptr) { return std::nullopt; }
    const auto& track = processor.document.project().tracks[static_cast<std::size_t>(row)];
    if (track.kind != motion::TrackKind::visual || track.locked) { return std::nullopt; }
    const auto bounds = clipBounds(*clip, row);
    // Clear of the trim handles at either end.
    if (point.x < bounds.getX() + 8 || point.x > bounds.getRight() - 8) { return std::nullopt; }
    return nearby(clip->id, beamArea(*clip, row), false);
}

bool MotionTimelineView::beamDown(const juce::MouseEvent& event) {
    const auto target = beamEdgeAt(event.getPosition());
    if (!target.has_value()) { return false; }
    const auto& project = processor.document.project();
    auto time = scrollTime + (event.x - namesWidth) / pixelsPerSecond;
    const auto fraction = beamShareAt(target->id, static_cast<float>(event.x)).value_or(0.0f);
    const auto id = target->id;
    const auto group = motion::findGroup(project, id) != nullptr;
    selectClip(id);
    if (fraction > .999f) {
        if (onError) { onError(group ? "Everything drawing here is in this group, so it already has all of the beam's time" : "This is the only object drawing here, so it already has all of the beam's time"); }
        return true;
    }
    const auto found = motion::findPropertyTarget(project, id);
    if (!found.has_value()) { return true; }
    const auto* curve = found->curve("beam");
    auto local = found->localTime(time);
    // Pressed by a key on the edge, the drag edits that key.
    if (curve != nullptr && curve->animated()) {
        for (const auto& key : curve->keyframes()) {
            if (std::abs(timeX(found->projectTime(key.time)) - event.x) <= 6) { local = key.time; time = found->projectTime(key.time); break; }
        }
    }
    const auto beam = curve != nullptr ? curve->evaluate(local) : 1.0;
    auto& drag = gesture.emplace<BeamDrag>(BeamDrag {id, target->area, time, fraction, beam, project, processor.document.revision(), false, event.mods.isCommandDown(), event.y});
    beamPointer = event.x;
    if (drag.keyed) {
        // Cmd-click keys the Beam here at its current value.
        auto updated = drag.before;
        auto* keyed = motion::ensurePropertyCurve(updated, id, "beam");
        if (keyed != nullptr) {
            keyed->setKeyValue(local, keyed->evaluateBase(local));
            processor.document.preview(std::move(updated));
            drag.revision = processor.document.revision();
            drag.changed = true;
        }
    }
    repaint();
    return true;
}

void MotionTimelineView::beamDragged(const juce::MouseEvent& event) {
    auto& drag = std::get<BeamDrag>(gesture);
    if (processor.document.revision() != drag.revision) { gesture = {}; repaint(); return; }
    const auto area = drag.area;
    // The edge follows the pointer, over at least 64 px however short the
    // row, so small rows stay precise; Shift moves it a quarter as far.
    const auto travel = std::max(64.0f, area.getHeight()) * (event.mods.isShiftDown() ? 4.0f : 1.0f);
    const auto level = std::sqrt(static_cast<float>(drag.fraction)) + static_cast<float>(drag.downY - event.y) / travel;
    const auto target = std::min(static_cast<double>(juce::square(std::clamp(level, 0.0f, 1.0f))), .98);
    // The Beam that gives this share, the others' unchanged: shares are each
    // object's even time scaled by its Beam, then normalised.
    auto value = drag.fraction > 1.0e-6 && drag.beam > 0
        ? drag.beam * target * (1 - drag.fraction) / (drag.fraction * (1 - target))
        : target / (1 - target);
    // 1x, the even share, is a detent.
    if (std::abs(value - 1) < .04) { value = 1; }
    value = std::round(std::clamp(value, 0.0, motion::maximumBeam) * 100) / 100;
    auto updated = drag.before;
    auto* curve = motion::ensurePropertyCurve(updated, drag.id, "beam");
    const auto found = motion::findPropertyTarget(updated, drag.id);
    if (curve == nullptr || !found.has_value()) { return; }
    const auto local = found->localTime(drag.time);
    if (curve->animated() || drag.keyed) {
        curve->setKeyValue(local, value);
    } else {
        curve->base = value;
    }
    processor.document.preview(std::move(updated));
    drag.revision = processor.document.revision();
    drag.changed = true;
    beamPointer = event.x;
    repaint();
}

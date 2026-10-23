#include "TimelineView.h"

// The rows: tracks, groups, the composition's effects and key lanes, where
// each sits, and how each paints.

int MotionTimelineView::rowEdgeAt(juce::Point<int> point) const {
    ensureTrackRows();
    if (point.x >= namesWidth - 5 || point.y < rulerHeight) { return -1; }
    for (int row = firstVisibleRow(); row < static_cast<int>(rows.size()); ++row) {
        const auto bottom = rowY(row) + heightAt(row);
        if (bottom > getHeight() + resizeStrip) { break; }
        // Tracks and groups resize; lanes keep their height.
        if (!rows[static_cast<std::size_t>(row)].isLane() && !rows[static_cast<std::size_t>(row)].composition() && point.y >= bottom - resizeStrip - 1 && point.y <= bottom + 1) { return row; }
    }
    return -1;
}

int MotionTimelineView::storedHeight(motion::Id id) const {
    const auto& project = processor.document.project();
    const auto* group = motion::findGroup(project, id);
    if (group != nullptr) { return group->height; }
    const auto* track = project.tracks.findById(id);
    return track != nullptr ? track->height : 0;
}

void MotionTimelineView::paintLane(juce::Graphics& g, const Row& row, int y, int height) const {
    const auto& project = processor.document.project();
    // A lane reads like a channel in the Graph: under its track's header,
    // as light as it, with the axis colour as a dot and on its keys.
    fillCard(g, y, height - bandGap, osci::Colours::surface(), osci::Colours::surfaceSunken());
    const auto colour = row.colour;
    // The label starts where its track's name does, the dot just before it.
    const auto indent = std::min(64, row.depth * 16) + nameX - 12;
    g.setColour(colour.withAlpha(.85f));
    g.fillEllipse(juce::Rectangle<float>(6, 6).withCentre({static_cast<float>(indent) + 3.0f, y + (height - bandGap) * .5f}));
    g.setFont(motion::style::caption());
    g.setColour(osci::Colours::textMuted());
    g.drawText(row.label, indent + 12, y, namesWidth - indent - 18, height - bandGap, juce::Justification::centredLeft);
    juce::Graphics::ScopedSaveState scope(g);
    g.reduceClipRegion(namesWidth, y, getWidth() - namesWidth, height);
    // Only the columns being repainted (during playback, the playhead's strip).
    const auto repainted = g.getClipBounds();
    if (repainted.isEmpty()) { return; }
    const auto view = LaneRangeView {processor.document.generation(), processor.document.scopeGeneration(), processor.document.revision(), scrollTime, pixelsPerSecond, namesWidth, getWidth()};
    if (laneRangeView != view) {
        laneRangeView = view;
        laneRanges.clear();
    }
    // This lane's selected keys, looked up rather than scanned per key.
    std::vector<KeyRef> laneSelection;
    std::copy_if(selectedKeys.begin(), selectedKeys.end(), std::back_inserter(laneSelection), [&row](const auto& key) { return key.property == row.property; });
    const KeySet chosen(laneSelection);
    const auto centre = y + height * .5f;
    for (const auto owner : row.owners) {
        const auto target = motion::findPropertyTarget(project, owner);
        const auto* curve = target.has_value() ? target->curve(row.property) : nullptr;
        const auto timing = target.has_value() ? target->clock() : motion::ClipTiming();
        if (curve == nullptr || !curve->animated() || timing.rate == 0) { continue; }
        const auto left = timeX(timing.start), right = timeX(timing.end());
        // Keys at its ends reach a few pixels past them.
        if (right + 6 < repainted.getX() || left - 6 > repainted.getRight()) { continue; }
        const auto* clip = laneClip(project, owner);
        const auto* track = clip != nullptr ? motion::findClipTrack(project, clip->id) : nullptr;
        // Tinted as what holds it: its clip, the composition's slate, or (a track's own effect) neutral.
        const auto composition = std::any_of(project.effects.begin(), project.effects.end(), [owner](const auto& effect) { return effect.id == owner; });
        const auto tint = clip != nullptr && track != nullptr ? clipColour(*clip, *track) : composition ? motion::style::compositionClip() : osci::Colours::textMuted();
        g.setColour(tint.withAlpha(.18f));
        g.fillRect(left, y + 2, std::max(1, right - left), height - 4);
        const auto& keys = curve->keyframes();
        // A faint value curve normalised to the lane shows each move's shape.
        if (keys.size() > 1) {
            const auto first = std::max(left, namesWidth), last = std::min(right, getWidth());
            const auto localAt = [&](int x) { return timing.localTime(timeAt(x)); };
            // Normalised over all it shows, wherever the repaint is.
            auto range = laneRanges.find({row.lane, owner});
            if (range == laneRanges.end()) {
                double low = std::numeric_limits<double>::max(), high = std::numeric_limits<double>::lowest();
                for (const auto& key : keys) { low = std::min(low, key.value); high = std::max(high, key.value); }
                for (int x = first; x < last; x += 3) {
                    const auto value = curve->evaluateBase(localAt(x));
                    low = std::min(low, value);
                    high = std::max(high, value);
                }
                range = laneRanges.emplace(std::make_pair(row.lane, owner), std::make_pair(low, high)).first;
            }
            const auto low = range->second.first, high = range->second.second;
            // Sampled every 2 px from `first`, as a full paint does, so a
            // repainted strip meets the curve either side of it.
            const auto from = std::max(first, first + (repainted.getX() - 2 - first) / 2 * 2);
            const auto to = std::min(last, repainted.getRight() + 2);
            if (high > low && to > from) {
                juce::Path shape;
                const auto yOf = [&](double value) { return static_cast<float>(y + height - 4 - (value - low) / (high - low) * (height - 8)); };
                shape.startNewSubPath(static_cast<float>(from), yOf(curve->evaluateBase(localAt(from))));
                for (int x = from + 2; x < to; x += 2) { shape.lineTo(static_cast<float>(x), yOf(curve->evaluateBase(localAt(x)))); }
                g.setColour(colour.withAlpha(.4f));
                g.strokePath(shape, juce::PathStrokeType(1.0f));
            }
        }
        // Key shape encodes its outgoing interpolation, as in the Graph.
        const auto behind = osci::Colours::surfaceSunken().interpolatedWith(tint, .18f);
        for (const auto& key : keys) {
            // A key at the very start stays clear of the name column.
            const auto x = std::max(static_cast<float>(timeX(timing.projectTime(key.time))), static_cast<float>(namesWidth) + 4.5f);
            if (x + 6 < static_cast<float>(repainted.getX()) || x - 6 > static_cast<float>(repainted.getRight())) { continue; }
            motion::style::drawKey(g, {x, centre}, 4.5f, key, colour, chosen.contains(owner, row.property, key.time), false, behind);
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
        g.fillRect(cardInset + MotionTrackHeader::edge + MotionTrackHeader::foldWidth / 2 - 1 + level * 16, y - bandGap, 2, height);
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
            g.setColour(motion::style::keyTick());
            g.fillPath(keyTicks(times, static_cast<float>(y + cardHeight) - 5.0f, static_cast<float>(namesWidth - 4), static_cast<float>(getWidth() + 4)));
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
        if (!selectedClips.empty()) {
            std::unordered_set<motion::Id> clips;
            for (const auto& track : project.tracks) {
                for (const auto& clip : track.clips) { clips.insert(clip.id); }
            }
            std::erase_if(selectedClips, [&clips](auto id) { return !clips.contains(id); });
        }
        std::erase_if(selectedTracks, [&project](auto id) {
            return motion::findGroup(project, id) == nullptr && project.tracks.findById(id) == nullptr;
        });
        // A key stays selected while its owner (a clip or an effect) still
        // has it; each owner is looked up once.
        std::unordered_map<motion::Id, std::optional<motion::PropertyTarget>> owners;
        std::erase_if(selectedKeys, [&project, &owners](const KeyRef& key) {
            auto found = owners.find(key.owner);
            if (found == owners.end()) { found = owners.emplace(key.owner, motion::findPropertyTarget(project, key.owner)).first; }
            const auto* curve = found->second.has_value() ? found->second->curve(key.property) : nullptr;
            return curve == nullptr || curve->findKey(key.time) == nullptr;
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

const std::vector<motion::PropertySpec>& MotionTimelineView::laneSpecs(motion::TrackKind kind) {
    static const auto audio = std::vector<motion::PropertySpec>(motion::audioPropertySpecs.begin(), motion::audioPropertySpecs.end());
    static const auto visual = [] {
        std::vector<motion::PropertySpec> specs(motion::objectPropertySpecs.begin(), motion::objectPropertySpecs.end());
        specs.insert(specs.end(), motion::luaSliderSpecs.begin(), motion::luaSliderSpecs.end());
        specs.insert(specs.end(), motion::fractalPropertySpecs.begin(), motion::fractalPropertySpecs.end());
        // Every generator's parameters, each name once.
        for (const auto& generator : motion::generatorDefinitions) {
            for (const auto& spec : generator.parameters) {
                if (motion::findPropertySpec(specs, spec.id) == nullptr) { specs.push_back(spec); }
            }
        }
        return specs;
    }();
    return kind == motion::TrackKind::audio ? audio : visual;
}

bool MotionTimelineView::hasLanes(const motion::Track& track) {
    const auto& specs = laneSpecs(track.kind);
    const auto keyed = [](const motion::EffectInstance& effect) {
        const auto* definition = motion::effectDefinition(effect.type);
        return definition != nullptr && std::any_of(definition->parameters.begin(), definition->parameters.end(), [&effect](const auto& parameter) {
            const auto found = effect.properties.find(parameter.id);
            return found != effect.properties.end() && found->second.animated();
        });
    };
    for (const auto& clip : track.clips) {
        for (const auto& [name, curve] : clip.properties) {
            if (curve.animated() && motion::findPropertySpec(specs, name) != nullptr) { return true; }
        }
        if (std::any_of(clip.effects.begin(), clip.effects.end(), keyed)) { return true; }
    }
    return std::any_of(track.effects.begin(), track.effects.end(), keyed);
}

std::vector<MotionTimelineView::Row> MotionTimelineView::trackLanes(const motion::Project& project, const motion::Track& track) {
    std::vector<Row> lanes;
    for (const auto& spec : laneSpecs(track.kind)) {
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
    headerRows.clear();
    std::erase_if(expandedTracks, [&](auto id) { return id != compositionRow && project.tracks.findById(id) == nullptr; });
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
    trackRows.assign(project.tracks.size(), -1);
    for (std::size_t row = 0; row < rows.size(); ++row) {
        if (rows[row].isLane()) { continue; }
        headerRows.emplace(rows[row].id, static_cast<int>(row));
        if (rows[row].track >= 0 && rows[row].track < static_cast<int>(trackRows.size())) { trackRows[static_cast<std::size_t>(rows[row].track)] = static_cast<int>(row); }
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
    const auto row = track >= 0 && track < static_cast<int>(trackRows.size()) ? trackRows[static_cast<std::size_t>(track)] : -1;
    return row < 0 ? -defaultTrackHeight : rowY(row);
}

int MotionTimelineView::rowY(int row) const {
    if (row < 0) { return rulerHeight - scrollY; }
    const auto top = row < static_cast<int>(rowTops.size()) ? rowTops[static_cast<std::size_t>(row)] : contentHeight + (row - static_cast<int>(rowTops.size())) * defaultTrackHeight;
    return rulerHeight - scrollY + top;
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
    g.fillRoundedRectangle(bounds, motion::style::radiusControl);
    g.setColour(base.brighter(.6f).withAlpha(opacity * .35f));
    g.drawRoundedRectangle(bounds.reduced(.5f), motion::style::radiusControl, 1.0f);
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
    g.setColour(motion::style::keyTick().withMultipliedAlpha(opacity));
    g.fillPath(keyTicks(times, bounds.getBottom() - 4.0f, static_cast<float>(namesWidth - 4), static_cast<float>(getWidth() + 4)));
}

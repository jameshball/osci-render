#include "CurveEditor.h"

MotionCurveEditor::MotionCurveEditor(MotionProcessor& processor) : processor(processor) {
    setName("Animation curve editor");
    setWantsKeyboardFocus(true);
    frameButton.setClickingTogglesState(false);
    frameButton.quiet = true;
    frameButton.setTooltip("Frame every curve (F). Shift+F frames only the edited curve.");
    frameButton.onClick = [this] {
        userView = false;
        fit();
        repaint();
    };
    addChildComponent(frameButton);
}

void MotionCurveEditor::setHiddenCurves(std::set<std::string> curves) {
    if (curves == hiddenCurves) { return; }
    hiddenCurves = std::move(curves);
    repaint();
}

void MotionCurveEditor::setContextCurves(std::map<std::string, juce::Colour> curves) {
    if (curves == contextCurves) { return; }
    contextCurves = std::move(curves);
    repaint();
}

void MotionCurveEditor::followPlayhead(double time, bool playing) {
    const auto inside = time >= viewStart && time <= viewEnd;
    if (follow.update(playing, inside) && !drag.has_value()) {
        const auto span = viewEnd - viewStart;
        userView = true;
        setView(time - span * 0.02, time + span * 0.98);
        repaint();
    }
}

void MotionCurveEditor::restoreView(const ViewState& state) {
    setSelection(state.target, state.property);
    if (std::isfinite(state.start) && std::isfinite(state.end) && state.end > state.start
        && std::isfinite(state.low) && std::isfinite(state.high) && state.high > state.low) {
        viewStart = state.start; viewEnd = state.end; low = state.low; high = state.high; userView = state.user;
    }
    selectedTime = state.selected;
    companions.clear();
    refresh();
}

void MotionCurveEditor::setSelection(motion::Id id, std::string property) {
    if (targetId == id && propertyName == property) {
        refresh();
        return;
    }
    cancelDrag();
    const auto clip = motion::findPropertyTarget(processor.document.project(), targetId);
    const auto sibling = targetId == id && clip.has_value() && isSibling(*clip, property);
    targetId = id;
    if (sibling) {
        // Another axis of the same group becomes primary; the selection and view carry over.
        if (selectedTime.has_value()) {
            companions.push_back({ propertyName, *selectedTime });
        }
        propertyName = std::move(property);
        selectedTime.reset();
        const auto found = std::find_if(companions.begin(), companions.end(), [this](const KeyRef& key) { return key.property == propertyName; });
        if (found != companions.end()) {
            selectedTime = found->time;
            companions.erase(found);
        }
        refresh();
        return;
    }
    selectedTime.reset();
    companions.clear();
    propertyName = std::move(property);
    userView = false;
    fit();
    selectionChanged();
    repaint();
}

void MotionCurveEditor::refresh() {
    const auto clip = motion::findPropertyTarget(processor.document.project(), targetId);
    const auto* curve = findCurve(clip, propertyName);
    if (curve == nullptr || (drag.has_value() && !dragMatches(*clip))) {
        cancelDrag();
        selectedTime.reset();
        companions.clear();
    }
    if (!drag.has_value()) {
        if (curve != nullptr && selectedTime.has_value() && curve->findKey(*selectedTime) == nullptr) {
            selectedTime.reset();
        }
        std::erase_if(companions, [&clip](const KeyRef& key) {
            const auto* owner = findCurve(clip, key.property);
            return owner == nullptr || owner->findKey(key.time) == nullptr;
        });
        if (!userView) {
            fit();
        }
    }
    selectionChanged();
    repaint();
}

template <typename Evaluate>
juce::Path MotionCurveEditor::curvePath(double from, double to, float perPixel, const std::vector<double>& keys, Evaluate&& evaluate) const {
    const auto steps = std::max(2, juce::roundToInt(std::abs(timeX(to) - timeX(from)) * perPixel));
    std::vector<double> times;
    times.reserve(static_cast<std::size_t>(steps) + 1 + 2 * keys.size());
    for (int i = 0; i <= steps; ++i) { times.push_back(std::lerp(from, to, static_cast<double>(i) / steps)); }
    const auto before = (to - from) * 1.0e-7;
    for (const auto time : keys) {
        if (time > from && time < to) {
            times.push_back(time - before);
            times.push_back(time);
        }
    }
    std::sort(times.begin(), times.end());
    juce::Path path;
    path.startNewSubPath(timeX(times.front()), valueY(evaluate(times.front())));
    for (std::size_t i = 1; i < times.size(); ++i) { path.lineTo(timeX(times[i]), valueY(evaluate(times[i]))); }
    return path;
}

void MotionCurveEditor::paintGrid(juce::Graphics& g, double step, double minorStep) {
    const auto area = plot();
    const auto grid = processor.document.project().timeGrid();
    // Whole-pixel hairlines stay crisp; minor lines sit at half strength.
    const auto vertical = [&](double time, float alpha) {
        g.setColour(juce::Colours::white.withAlpha(alpha));
        g.fillRect(std::round(timeX(time)), area.getY(), 1.0f, area.getHeight());
    };
    grid.forEachTick(viewStart, viewEnd, minorStep, [&](double time) { vertical(time, .022f); });
    grid.forEachTick(viewStart, viewEnd, step, [&](double time) { vertical(time, .05f); });
    const auto valueTick = valueStep(high - low, std::max(3, juce::roundToInt(area.getHeight() / 30)));
    const auto decimals = std::clamp(static_cast<int>(-std::floor(std::log10(valueTick))), 0, 6);
    g.setFont(motion::style::caption());
    // An integer index (capped) so extreme values can never stall the loop.
    const auto first = std::ceil(low / (valueTick / 2));
    for (int index = 0; index < 400 && (first + index) * valueTick / 2 <= high + valueTick * 1e-6; ++index) {
        const auto value = (first + index) * valueTick / 2;
        const auto y = std::round(valueY(value));
        const auto major = static_cast<long long>(first + index) % 2 == 0;
        const auto zero = std::abs(value) < valueTick * 1e-6;
        g.setColour(juce::Colours::white.withAlpha(zero ? .12f : major ? .05f : .022f));
        g.fillRect(area.getX(), y, area.getWidth(), 1.0f);
        if (!major || y < area.getY() - 1 || y > area.getBottom() + 1) { continue; }
        g.setColour(osci::Colours::text().withAlpha(zero ? .75f : .5f));
        g.drawText(juce::String(zero ? 0.0 : value, decimals), 0, juce::roundToInt(y) - 8, gutter - 9, 16, juce::Justification::centredRight);
    }
}

// The Timeline's ruler: labels left-aligned on major ticks, short marks
// between them, scrubbed by dragging.
void MotionCurveEditor::paintRuler(juce::Graphics& g, double step, double minorStep) {
    const auto grid = processor.document.project().timeGrid();
    juce::Graphics::ScopedSaveState state(g);
    g.reduceClipRegion(juce::Rectangle<int>(gutter, 0, getWidth() - gutter, bandHeight));
    grid.forEachTick(viewStart, viewEnd, minorStep, [&](double time) {
        g.setColour(juce::Colours::white.withAlpha(.16f));
        g.fillRect(std::round(timeX(time)), bandHeight - 4.0f, 1.0f, 4.0f);
    });
    g.setFont(motion::style::body());
    grid.forEachTick(viewStart, viewEnd, step, [&](double time) {
        const auto x = std::round(timeX(time));
        g.setColour(juce::Colours::white.withAlpha(.3f));
        g.fillRect(x, bandHeight - 8.0f, 1.0f, 8.0f);
        g.setColour(osci::Colours::text().withAlpha(.7f));
        g.drawText(juce::String(grid.label(time, step)), juce::roundToInt(x) + 5, 0, 70, bandHeight - 4, juce::Justification::centredLeft);
    });
}

// A readout beside a key, above it or below when there is no room: the
// muted label (a time, "Influence") before "|", then the value.
void MotionCurveEditor::paintReadout(juce::Graphics& g, juce::Point<float> anchor, const juce::String& text) {
    const auto label = text.upToFirstOccurrenceOf("|", false, false), value = text.fromFirstOccurrenceOf("|", false, false);
    const auto labelWidth = juce::GlyphArrangement::getStringWidthInt(motion::style::caption(), label);
    const auto valueWidth = juce::GlyphArrangement::getStringWidthInt(motion::style::caption(), value);
    juce::Rectangle<float> chip(static_cast<float>(labelWidth + valueWidth + 26), 20.0f);
    chip.setCentre(anchor.x, anchor.y - 21.0f);
    if (chip.getY() < plot().getY()) { chip.setY(anchor.y + 11.0f); }
    chip.setX(std::clamp(chip.getX(), plot().getX(), std::max(plot().getX(), plot().getRight() - chip.getWidth())));
    motion::style::fillFloatingPanel(g, chip, osci::Colours::surfaceRaised());
    g.setFont(motion::style::caption());
    auto row = chip.reduced(9.0f, 0.0f);
    g.setColour(osci::Colours::textMuted());
    g.drawText(label, row.removeFromLeft(static_cast<float>(labelWidth)), juce::Justification::centredLeft, false);
    g.setColour(osci::Colours::text());
    g.drawText(value, row, juce::Justification::centredRight, false);
}

void MotionCurveEditor::paint(juce::Graphics& g) {
    g.fillAll(osci::Colours::veryDark());
    playheadStrip.drawn(std::nullopt);
    const auto clip = motion::findPropertyTarget(processor.document.project(), targetId);
    const auto* storedCurve = findCurve(clip, propertyName);
    if (storedCurve == nullptr) {
        // No ruler without a curve: just what to do next.
        const auto centre = getLocalBounds().getCentre();
        motion::icons::draw(g, motion::icons::Icon::bezier, juce::Rectangle<float>(32.0f, 32.0f).withCentre(centre.toFloat().translated(0.0f, -30.0f)), osci::Colours::textMuted().withAlpha(.5f), 28.0f);
        g.setColour(osci::Colours::text().withAlpha(.85f));
        g.setFont(motion::style::title());
        g.drawText("No curve to edit", juce::Rectangle<int>(getWidth(), 20).withCentre(centre.translated(0, 0)), juce::Justification::centred);
        g.setColour(osci::Colours::textMuted());
        g.setFont(motion::style::body());
        g.drawText("Select a clip, then pick a property in the list or key one in Properties.", juce::Rectangle<int>(getWidth(), 20).withCentre(centre.translated(0, 20)), juce::Justification::centred);
        return;
    }
    g.setColour(osci::Colours::surfaceRaised());
    g.fillRect(0, 0, getWidth(), bandHeight);
    const auto& curve = *displayed(*clip, propertyName);
    // Routed modulators and links count as modulation too: the Result
    // curve shows what the property actually does.
    const auto drivers = resultDrivers(curve);
    const auto modulated = drivers != nullptr;
    const auto area = plot();
    const auto grid = processor.document.project().timeGrid();
    const auto step = grid.tickStep(area.getWidth() / (viewEnd - viewStart));
    const auto minorStep = step / 2;
    // The clip's playing span is the stage, lit; time it never plays is shaded.
    const auto clipStart = clip->start, clipEnd = clip->end();
    const auto stageLeft = std::clamp(timeX(clipStart), area.getX(), area.getRight());
    const auto stageRight = std::clamp(timeX(clipEnd), area.getX(), area.getRight());
    g.setColour(juce::Colours::white.withAlpha(.025f));
    g.fillRect(area.withLeft(stageLeft).withRight(stageRight));
    paintGrid(g, step, minorStep);
    {
        const auto left = stageLeft, right = stageRight;
        g.setColour(juce::Colours::black.withAlpha(.26f));
        g.fillRect(area.withRight(left));
        g.fillRect(area.withLeft(right));
        g.setColour(juce::Colours::white.withAlpha(.1f));
        for (const auto edge : {timeX(clipStart), timeX(clipEnd)}) {
            if (edge >= area.getX() && edge <= area.getRight()) { g.fillRect(std::round(edge), area.getY(), 1.0f, area.getHeight()); }
        }
    }
    paintRuler(g, step, minorStep);
    // Every key time of the group, where sampling must land exactly.
    std::vector<double> keyTimes;
    for (const auto& name : groupNames(*clip)) {
        for (const auto& key : displayed(*clip, name)->keyframes()) { keyTimes.push_back(clip->projectTime(key.time)); }
    }
    // Faded where the clip never plays, full strength where it does.
    const auto strokeAcrossStage = [&](const auto& evaluate, juce::Colour colour, float width, float outside, float perPixel) {
        const juce::PathStrokeType stroke(width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
        g.setColour(colour.withMultipliedAlpha(outside));
        if (viewStart < clipStart) { g.strokePath(curvePath(viewStart, std::min(clipStart, viewEnd), perPixel, keyTimes, evaluate), stroke); }
        if (viewEnd > clipEnd) { g.strokePath(curvePath(std::max(clipEnd, viewStart), viewEnd, perPixel, keyTimes, evaluate), stroke); }
        const auto from = std::max(viewStart, clipStart), to = std::min(viewEnd, clipEnd);
        g.setColour(colour);
        if (to > from) { g.strokePath(curvePath(from, to, perPixel, keyTimes, evaluate), stroke); }
    };
    const auto baseOf = [&clip](const motion::Curve& source) { return [&clip, &source](double time) { return source.evaluateBase(clip->localTime(time)); }; };
    g.saveState();
    g.reduceClipRegion(area.toNearestInt().expanded(6).withTop(bandHeight));
    const auto colour = primaryColour(*clip);
    auto withDrivers = curve;
    withDrivers.drivers = drivers;
    const auto result = [&](double time) { return constrainedValue(*clip, withDrivers.evaluate(clip->localTime(time)), propertyName); };
    {
        // A soft fill under what plays, inside the stage, gives the curve
        // weight. It goes first so it never tints the other curves.
        const auto from = std::max(viewStart, clipStart), to = std::min(viewEnd, clipEnd);
        if (to > from) {
            auto fill = modulated ? curvePath(from, to, 1.0f, keyTimes, result) : curvePath(from, to, 1.0f, keyTimes, baseOf(curve));
            // The fade follows the curve, from its highest point on screen to a
            // little under its lowest, so a curve above the view leaves only a
            // hint at the top edge rather than tinting the whole plot.
            const auto extent = fill.getBounds();
            const auto top = std::clamp(extent.getY(), area.getY(), area.getBottom());
            const auto bottom = std::min(area.getBottom(), std::clamp(extent.getBottom(), area.getY(), area.getBottom()) + 90.0f);
            fill.lineTo(timeX(to), area.getBottom());
            fill.lineTo(timeX(from), area.getBottom());
            fill.closeSubPath();
            const auto tint = modulated ? motion::style::result() : colour;
            g.setGradientFill(juce::ColourGradient(tint.withAlpha(.15f), 0.0f, top, tint.withAlpha(0.0f), 0.0f, std::max(top + 1.0f, bottom), false));
            g.fillPath(fill);
        }
    }
    // Channels shown from the list, faint and not editable.
    for (const auto& [name, colour] : contextCurves) {
        const auto* other = clip->curve(name);
        if (other == nullptr || name == propertyName || isSibling(*clip, name)) { continue; }
        strokeAcrossStage(baseOf(*other), colour.withAlpha(.32f), 1.0f, .45f, 0.5f);
    }
    // Sibling axes are editable too, in their axis colour under the edited curve.
    for (const auto& [name, colour] : siblings(*clip)) {
        strokeAcrossStage(baseOf(*displayed(*clip, name)), colour.withAlpha(.6f), 1.5f, .4f, 1.0f);
    }
    const auto emphasis = hover.curve && !drag.has_value() ? .6f : 0.0f;
    const auto linked = storedCurve->link.has_value();
    if (linked) {
        // A link replaces the keys: they are drawn dashed, as ignored.
        juce::Path dashed;
        const float dashes[] {5.0f, 4.0f};
        juce::PathStrokeType(1.5f).createDashedStroke(dashed, curvePath(viewStart, viewEnd, 1.0f, keyTimes, baseOf(curve)), dashes, 2);
        g.setColour(colour.withAlpha(.4f));
        g.fillPath(dashed);
        strokeAcrossStage(result, motion::style::result(), 2.0f, .4f, 2.0f);
    } else if (modulated) {
        // The keys are what you edit; the Result is what plays.
        strokeAcrossStage(baseOf(curve), colour.withAlpha(.55f), 1.5f + emphasis, .5f, 2.0f);
        strokeAcrossStage(result, motion::style::result(), 2.0f, .4f, 2.0f);
    } else {
        strokeAcrossStage(baseOf(curve), colour, 2.0f + emphasis, .35f, 2.0f);
    }
    for (const auto& [name, siblingColour] : siblings(*clip)) {
        const auto* other = displayed(*clip, name);
        for (const auto& key : other->keyframes()) {
            motion::style::drawKey(g, keyPoint(*clip, key), 4.0f, key, siblingColour, isSelected(name, key.time), hover.key == KeyRef {name, key.time}, osci::Colours::veryDark());
        }
    }
    if (selectedTime.has_value()) {
        const auto* selected = curve.findKey(*selectedTime);
        if (selected != nullptr) {
            const auto keyPosition = keyPoint(*clip, *selected);
            for (const auto mode : {DragMode::incoming, DragMode::outgoing}) {
                const auto handle = tangentPoint(*clip, curve, *selected, mode);
                if (!handle.has_value()) { continue; }
                // Arms and rings in the curve's colour; a ring fills white under the pointer.
                const auto active = hover.handle == mode || (drag.has_value() && drag->mode == mode);
                g.setColour(colour.withAlpha(.6f));
                g.drawLine(keyPosition.x, keyPosition.y, handle->x, handle->y, 1.25f);
                const auto ring = juce::Rectangle<float>(8.0f, 8.0f).withCentre(*handle);
                g.setColour(active ? juce::Colours::white : osci::Colours::veryDark());
                g.fillEllipse(ring);
                g.setColour(colour);
                g.drawEllipse(ring.reduced(.75f), 1.5f);
            }
        }
    }
    for (const auto& key : curve.keyframes()) {
        motion::style::drawKey(g, keyPoint(*clip, key), 5.0f, key, colour, isSelected(propertyName, key.time), hover.key == KeyRef {propertyName, key.time}, osci::Colours::veryDark());
    }
    if (snapGuide.has_value()) {
        const auto guide = std::round(timeX(*snapGuide)) + .5f;
        g.setColour(osci::Colours::accentColor().withAlpha(.75f));
        const float dashes[] {4.0f, 3.0f};
        g.drawDashedLine(juce::Line<float>(guide, area.getY(), guide, area.getBottom()), dashes, 2, 1.0f);
    }
    const auto box = selectionBox(*clip);
    if (box.has_value()) {
        g.setColour(osci::Colours::accentColor().withAlpha(.03f));
        g.fillRoundedRectangle(box->area, 4.0f);
        g.setColour(osci::Colours::accentColor().withAlpha(.5f));
        g.drawRoundedRectangle(box->area.reduced(.5f), 4.0f, 1.0f);
        for (const auto right : {false, true}) {
            if (!hasGrips(*box)) { break; }
            const auto grip = scaleHandle(*box, right);
            const auto active = hover.grip == right || (drag.has_value() && drag->mode == (right ? DragMode::scaleRight : DragMode::scaleLeft));
            // Slim accent pills on the box's edges, white while grabbed or hovered.
            g.setColour(osci::Colours::veryDark());
            g.fillRoundedRectangle(grip.expanded(1.0f), 2.5f);
            g.setColour(active ? juce::Colours::white : osci::Colours::accentColor().brighter(.15f));
            g.fillRoundedRectangle(grip, 1.5f);
        }
    }
    if (marquee.has_value()) {
        g.setColour(osci::Colours::accentColor().withAlpha(.07f));
        g.fillRoundedRectangle(*marquee, 2.0f);
        g.setColour(osci::Colours::accentColor().withAlpha(.5f));
        g.drawRoundedRectangle(marquee->reduced(.5f), 2.0f, 1.0f);
    }
    g.restoreState();
    const auto x = playheadX();
    if (x.has_value()) {
        playheadStrip.drawn(x);
        g.setColour(motion::style::playhead());
        g.drawVerticalLine(*x, 0.0f, area.getBottom());
        juce::Path head;
        head.addTriangle(*x - 5.0f, 0.0f, *x + 5.0f, 0.0f, static_cast<float>(*x), 8.0f);
        g.fillPath(head);
    }
    if (modulated) {
        // A legend for the two lines on a quiet chip in the corner; a link
        // says its keys are ignored (Routing names its source).
        const std::array<std::pair<juce::String, juce::Colour>, 2> entries {{{linked ? "Keys, ignored while linked" : "Keys", colour.withAlpha(linked ? .4f : .55f)}, {"Result", motion::style::result()}}};
        auto width = 10;
        for (const auto& entry : entries) { width += 12 + juce::GlyphArrangement::getStringWidthInt(motion::style::caption(), entry.first) + 12; }
        auto legend = juce::Rectangle<int>(area.toNearestInt().getX() + 8, area.toNearestInt().getY() + 6, width, 20);
        g.setColour(osci::Colours::veryDark().withAlpha(.92f));
        g.fillRoundedRectangle(legend.toFloat(), 10.0f);
        g.setColour(juce::Colours::white.withAlpha(.07f));
        g.drawRoundedRectangle(legend.toFloat().reduced(.5f), 10.0f, 1.0f);
        legend.removeFromLeft(10);
        g.setFont(motion::style::caption());
        for (const auto& [label, tint] : entries) {
            g.setColour(tint);
            g.fillEllipse(juce::Rectangle<float>(6.0f, 6.0f).withCentre({legend.getX() + 3.0f, legend.getCentreY() + .5f}));
            g.setColour(osci::Colours::text().withAlpha(.8f));
            g.drawText(label, legend.withTrimmedLeft(12), juce::Justification::centredLeft, false);
            legend.removeFromLeft(12 + juce::GlyphArrangement::getStringWidthInt(motion::style::caption(), label) + 12);
        }
    }
    // What the pointer is on, or what is being dragged: its time and value.
    {
        const auto valueText = [this](double value) {
            const auto tick = valueStep(high - low, std::max(3, juce::roundToInt(plot().getHeight() / 30)));
            return juce::String(value, std::clamp(static_cast<int>(-std::floor(std::log10(tick))) + 1, 0, 6));
        };
        const auto describe = [&](const std::string& name, double time) -> std::optional<std::pair<juce::Point<float>, juce::String>> {
            const auto* owner = displayed(*clip, name);
            const auto* key = owner != nullptr ? owner->findKey(time) : nullptr;
            if (key == nullptr) { return std::nullopt; }
            return std::make_pair(keyPoint(*clip, *key), juce::String(grid.positionLabel(clip->projectTime(key->time))) + "|" + valueText(key->value));
        };
        std::optional<std::pair<juce::Point<float>, juce::String>> readout;
        if (drag.has_value() && selectedTime.has_value() && drag->mode == DragMode::key) {
            readout = describe(propertyName, *selectedTime);
        } else if (drag.has_value() && selectedTime.has_value() && (drag->mode == DragMode::incoming || drag->mode == DragMode::outgoing)) {
            const auto* key = curve.findKey(*selectedTime);
            if (key != nullptr) {
                const auto influence = drag->mode == DragMode::incoming ? key->incomingInfluence : key->outgoingInfluence;
                readout = std::make_pair(keyPoint(*clip, *key), "Influence|" + juce::String(juce::roundToInt(influence * 100)) + "%");
            }
        } else if (!drag.has_value() && hover.key.has_value()) {
            readout = describe(hover.key->property, hover.key->time);
        } else if (!drag.has_value() && hover.curve) {
            // A ghost key where a double-click would add one (snapped the same
            // way), with the curve's value there.
            const auto time = snappedTime(*clip, projectTime(pointer.x), juce::ModifierKeys::currentModifiers);
            const auto value = curve.evaluateBase(clip->localTime(time));
            const juce::Point<float> ghost(timeX(time), valueY(value));
            g.setColour(osci::Colours::veryDark());
            g.fillEllipse(juce::Rectangle<float>(9.0f, 9.0f).withCentre(ghost));
            g.setColour(colour);
            g.drawEllipse(juce::Rectangle<float>(7.0f, 7.0f).withCentre(ghost), 1.5f);
            readout = std::make_pair(ghost, juce::String(grid.positionLabel(time)) + "|" + valueText(value));
        }
        if (readout.has_value() && area.expanded(4).contains(readout->first)) { paintReadout(g, readout->first, readout->second); }
    }
}

void MotionCurveEditor::resized() {
    frameButton.setBounds(juce::Rectangle<int>(0, 0, gutter, bandHeight).withSizeKeepingCentre(26, 20));
    selectionChanged();
}

void MotionCurveEditor::selectionChanged() {
    frameButton.setVisible(findCurve(motion::findPropertyTarget(processor.document.project(), targetId), propertyName) != nullptr);
    if (onSelectionChanged) { onSelectionChanged(); }
}

MotionCurveEditor::KeySelection MotionCurveEditor::keySelection() const {
    KeySelection result;
    const auto clip = motion::findPropertyTarget(processor.document.project(), targetId);
    if (!clip.has_value()) { return result; }
    result.editable = !clip->locked;
    for (const auto& ref : selection()) {
        const auto* curve = displayed(*clip, ref.property);
        const auto* key = curve != nullptr ? curve->findKey(ref.time) : nullptr;
        if (key == nullptr) { continue; }
        ++result.count;
        result.present[static_cast<std::size_t>(key->interpolation)] = true;
    }
    return result;
}

MotionCurveEditor::Hover MotionCurveEditor::hoverAt(juce::Point<float> point) const {
    Hover next;
    const auto clip = motion::findPropertyTarget(processor.document.project(), targetId);
    const auto* curve = findCurve(clip, propertyName);
    if (curve == nullptr || point.y < bandHeight) { return next; }
    const auto* selected = selectedTime.has_value() ? curve->findKey(*selectedTime) : nullptr;
    if (selected != nullptr) {
        for (const auto mode : {DragMode::incoming, DragMode::outgoing}) {
            const auto handle = tangentPoint(*clip, *curve, *selected, mode);
            if (handle.has_value() && handle->getDistanceFrom(point) <= 8.0f && keyPoint(*clip, *selected).getDistanceFrom(point) > 7.0f) {
                next.handle = mode;
                return next;
            }
        }
    }
    const auto box = selectionBox(*clip);
    for (const auto right : {false, true}) {
        if (box.has_value() && hasGrips(*box) && scaleHandle(*box, right).expanded(3.0f, 2.0f).contains(point)) {
            next.grip = right;
            return next;
        }
    }
    next.key = hitKey(*clip, point);
    if (!next.key.has_value() && plot().contains(point)) {
        const auto value = curve->evaluateBase(clip->localTime(projectTime(point.x)));
        next.curve = std::abs(valueY(value) - point.y) < 6.0f;
    }
    return next;
}

void MotionCurveEditor::setHover(Hover next) {
    if (next == hover) { return; }
    hover = std::move(next);
    setMouseCursor(hover.grip.has_value() ? juce::MouseCursor::LeftRightResizeCursor
        : hover.handle.has_value() || hover.key.has_value() ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    repaint();
}

void MotionCurveEditor::mouseMove(const juce::MouseEvent& event) {
    pointer = event.position;
    setHover(hoverAt(event.position));
    // The ghost key follows the pointer along a hovered curve.
    if (hover.curve) { repaint(); }
}

void MotionCurveEditor::mouseExit(const juce::MouseEvent&) {
    setHover({});
}

void MotionCurveEditor::mouseDown(const juce::MouseEvent& event) {
    grabKeyboardFocus();
    cancelDrag();
    snapGuide.reset();
    const auto clip = motion::findPropertyTarget(processor.document.project(), targetId);
    const auto* curve = findCurve(clip, propertyName);
    if (curve == nullptr) {
        return;
    }
    const auto left = !event.mods.isPopupMenu() && event.mods.isLeftButtonDown();
    // The ruler scrubs the playhead, as in the Timeline.
    if (left && event.position.y < bandHeight && event.position.x >= gutter) {
        scrubbing = true;
        scrubTo(event.position.x, event.mods);
        return;
    }
    if (left && selectedTime.has_value()) {
        const auto* selected = curve->findKey(*selectedTime);
        if (selected != nullptr) {
            for (const auto mode : { DragMode::incoming, DragMode::outgoing }) {
                const auto handle = tangentPoint(*clip, *curve, *selected, mode);
                if (handle.has_value() && plot().expanded(5).contains(*handle)
                    && keyPoint(*clip, *selected).getDistanceFrom(event.position) > 7.0f
                    && handle->getDistanceFrom(event.position) <= 8.0f) {
                    beginDrag(*clip, mode, event.position);
                    drag->original = *selected;
                    drag->handle = *handle;
                    repaint();
                    return;
                }
            }
        }
    }
    if (left && !event.mods.isShiftDown()) {
        // The selection box's edge handles scale key times about the opposite edge.
        const auto box = selectionBox(*clip);
        for (const auto right : { false, true }) {
            if (!box.has_value() || !hasGrips(*box)) {
                break;
            }
            const auto handle = scaleHandle(*box, right);
            if (plot().expanded(5).contains(handle.getCentre()) && handle.expanded(3.0f, 2.0f).contains(event.position)) {
                beginDrag(*clip, right ? DragMode::scaleRight : DragMode::scaleLeft, event.position);
                drag->pivot = right ? box->first : box->last;
                drag->edge = right ? box->last : box->first;
                repaint();
                return;
            }
        }
    }
    const auto hit = hitKey(*clip, event.position);
    if (hit.has_value() && event.mods.isShiftDown() && event.mods.isLeftButtonDown()) {
        // Shift toggles a key in the selection without dragging.
        toggleKey(*hit);
        selectionChanged();
        repaint();
        return;
    }
    const auto previousPrimary = primaryKey();
    if (hit.has_value() && isSelected(hit->property, hit->time)) {
        // Grabbing a selected key keeps the group and makes it primary.
        std::erase(companions, *hit);
        if (previousPrimary.has_value() && *previousPrimary != *hit) {
            companions.push_back(*previousPrimary);
        }
    } else {
        companions.clear();
    }
    selectedTime.reset();
    if (hit.has_value()) {
        makePrimary(*hit);
    }
    if (!hit.has_value() && left && plot().contains(event.position)) {
        marqueeStart = event.position;
        marquee = juce::Rectangle<float>(event.position, event.position);
    }
    if (!hit.has_value() && event.mods.isLeftButtonDown() && onPropertyChosen) {
        // Clicking empty space near another axis's curve makes it primary.
        const auto time = viewStart + (event.position.x - plot().getX()) / plot().getWidth() * (viewEnd - viewStart);
        const auto own = std::abs(valueY(curve->evaluateBase(clip->localTime(time))) - event.position.y);
        for (const auto& [name, colour] : siblings(*clip)) {
            const auto distance = std::abs(valueY(clip->curve(name)->evaluateBase(clip->localTime(time))) - event.position.y);
            if (distance < 6.0f && distance < own) {
                marquee.reset();
                onPropertyChosen(name);
                return;
            }
        }
    }
    if (event.mods.isPopupMenu()) {
        if (!selection().empty()) {
            showKeyMenu();
        }
    } else if (selectedTime.has_value() && event.mods.isLeftButtonDown()) {
        // The primary may have switched curves: resolve it again.
        const auto current = motion::findPropertyTarget(processor.document.project(), targetId);
        const auto* primary = findCurve(current, propertyName);
        const auto* key = primary != nullptr ? primary->findKey(*selectedTime) : nullptr;
        if (key != nullptr) {
            beginDrag(*current, DragMode::key, event.position);
            drag->original = *key;
        }
    }
    setHover(hoverAt(event.position));
    selectionChanged();
    repaint();
}

void MotionCurveEditor::mouseDoubleClick(const juce::MouseEvent& event) {
    cancelDrag();
    if (event.mods.isPopupMenu() || !plot().contains(event.position)) {
        return;
    }
    const auto clip = motion::findPropertyTarget(processor.document.project(), targetId);
    const auto* curve = findCurve(clip, propertyName);
    if (curve == nullptr) {
        return;
    }
    const auto existing = hitKey(*clip, event.position);
    if (existing.has_value()) {
        std::erase(companions, *existing);
        selectedTime.reset();
        makePrimary(*existing);
        repaint();
        return;
    }
    if (clip->locked) { return; }
    motion::Keyframe key;
    key.time = clip->localTime(snappedTime(*clip, projectTime(event.position.x), event.mods));
    key.value = constrainedValue(*clip, valueAt(event.position.y), propertyName);
    // On (or near) the curve the key lands on it, so the shape doesn't change.
    const auto onCurve = curve->evaluateBase(key.time);
    if (std::isfinite(onCurve) && std::abs(valueY(onCurve) - event.position.y) < 8.0f) { key.value = onCurve; }
    if (!std::isfinite(key.time) || !std::isfinite(key.value)) { return; }
    const auto id = targetId;
    const auto property = propertyName;
    processor.document.edit("Add animation key", [id, property, key](motion::Project& project) {
        auto* target = mutableCurve(project, id, property);
        if (target != nullptr) {
            target->setKeyValue(key.time, key.value);
        }
    });
    selectedTime = key.time;
    refresh();
}

void MotionCurveEditor::scrubTo(float x, juce::ModifierKeys modifiers) {
    auto time = std::clamp(projectTime(x), 0.0, processor.document.project().duration);
    // Free scrubbing that snaps only to the keys on screen (Alt: never).
    const auto target = motion::findPropertyTarget(processor.document.project(), targetId);
    if (!modifiers.isAltDown() && target.has_value()) {
        auto nearest = 8.0f;
        for (const auto& name : groupNames(*target)) {
            for (const auto& key : target->curve(name)->keyframes()) {
                const auto keyTime = target->projectTime(key.time);
                const auto distance = std::abs(timeX(keyTime) - x);
                if (distance < nearest) {
                    nearest = distance;
                    time = keyTime;
                }
            }
        }
    }
    processor.seek(time);
    repaint();
}

void MotionCurveEditor::mouseDrag(const juce::MouseEvent& event) {
    if (scrubbing) { scrubTo(event.position.x, event.mods); return; }
    if (marquee.has_value()) {
        // The box selects across every curve of the group; the first key on
        // the primary curve becomes primary.
        marquee = juce::Rectangle<float>(marqueeStart, event.position);
        const auto clip = motion::findPropertyTarget(processor.document.project(), targetId);
        selectedTime.reset();
        companions.clear();
        if (clip.has_value()) {
            for (const auto& name : groupNames(*clip)) {
                for (const auto& key : clip->curve(name)->keyframes()) {
                    if (!marquee->contains(keyPoint(*clip, key))) { continue; }
                    if (name == propertyName && !selectedTime.has_value()) {
                        selectedTime = key.time;
                    } else {
                        companions.push_back({ name, key.time });
                    }
                }
            }
        }
        selectionChanged();
        repaint();
        return;
    }
    if (!drag.has_value()) {
        return;
    }
    const auto clip = motion::findPropertyTarget(processor.document.project(), targetId);
    if (!clip.has_value() || !dragMatches(*clip)) {
        cancelDrag();
        refresh();
        return;
    }
    const auto minimum = clip->offset, maximum = clip->localTime(clip->end());
    std::optional<motion::KeyEditResult> result;
    if (drag->mode == DragMode::scaleLeft || drag->mode == DragMode::scaleRight) {
        const auto edgeTime = clip->projectTime(drag->edge);
        const auto target = snapKeyTime(*clip, edgeTime + (event.position.x - drag->down.x) / plot().getWidth() * (viewEnd - viewStart), event.mods, {});
        result = motion::keyedit::scaleKeyTimes(drag->originals, dragSelection(), drag->pivot, drag->edge, clip->localTime(target), minimum, maximum);
    } else if (drag->mode != DragMode::key) {
        auto key = drag->original;
        const auto point = drag->handle + event.position - drag->down;
        auto localDelta = clip->localTime(projectTime(point.x)) - key.time;
        const auto minimumDelta = std::max(1.0e-12, clip->duration * clip->rate * 1.0e-9);
        localDelta = drag->mode == DragMode::incoming ? std::min(-minimumDelta, localDelta) : std::max(minimumDelta, localDelta);
        const auto slope = (valueAt(point.y) - key.value) / localDelta;
        if (!std::isfinite(slope)) {
            return;
        }
        // Horizontal handle distance is the Bezier time influence.
        const auto span = segmentSpan(drag->originals.at(propertyName), key, drag->mode);
        const auto influence = span != 0 ? std::clamp(std::abs(localDelta / span), 0.02, 1.0) : motion::Keyframe::defaultInfluence;
        if (drag->mode == DragMode::incoming) {
            key.incomingSlope = slope;
            key.incomingInfluence = event.mods.isAltDown() ? key.incomingInfluence : influence;
        } else {
            key.outgoingSlope = slope;
            key.outgoingInfluence = event.mods.isAltDown() ? key.outgoingInfluence : influence;
        }
        result = motion::KeyEditResult { drag->originals, dragSelection() };
        result->curves[propertyName].setKey(key);
    } else {
        // The whole selection moves by the grabbed key's time and value delta.
        // A drag cannot silently replace an unselected key: double-click adds
        // and explicit deletion remain the ways to change the number of keys.
        const auto& original = drag->original;
        const auto originalProjectTime = clip->projectTime(original.time);
        const auto delta = (event.position.x - drag->down.x) / plot().getWidth() * (viewEnd - viewStart);
        const auto time = clip->localTime(snapKeyTime(*clip, originalProjectTime + delta, event.mods, propertyName));
        const auto value = constrainedValue(*clip, original.value + (drag->down.y - event.position.y) / plot().getHeight() * (high - low), propertyName);
        if (!std::isfinite(time) || !std::isfinite(value)) { return; }
        const auto timeDelta = time - original.time;
        const auto from = original.time;
        // Build the candidate aside so a rejected move leaves the last good preview intact.
        result = motion::keyedit::transformKeys(drag->originals, dragSelection(), [from, time, timeDelta](double keyTime) {
            return keyTime == from ? time : keyTime + timeDelta;
        }, value - original.value, minimum, maximum, [this, &clip](const std::string& property, double keyValue) {
            return constrainedValue(*clip, keyValue, property);
        });
    }
    if (!result.has_value()) {
        return;
    }
    drag->previews = std::move(result->curves);
    adoptSelection(std::move(result->selection), drag->primary.has_value());
    // The Key panel follows the drag.
    selectionChanged();
    repaint();
    if (onPreview) {
        onPreview(&drag->previews);
    }
}

void MotionCurveEditor::mouseUp(const juce::MouseEvent&) {
    if (scrubbing) { scrubbing = false; return; }
    snapGuide.reset();
    if (marquee.has_value()) {
        marquee.reset();
        selectionChanged();
        repaint();
        return;
    }
    if (!drag.has_value()) {
        return;
    }
    const auto clip = motion::findPropertyTarget(processor.document.project(), targetId);
    const auto valid = clip.has_value() && dragMatches(*clip);
    motion::PropertyMap changed;
    for (auto& [name, preview] : drag->previews) {
        if (!sameCurve(preview, drag->originals.at(name))) {
            changed.emplace(name, std::move(preview));
        }
    }
    const auto mode = drag->mode;
    drag.reset();
    if (valid && !changed.empty()) {
        const auto id = targetId;
        const auto plural = selection().size() > 1;
        const auto* label = mode == DragMode::key ? (plural ? "Move animation keys" : "Move animation key")
            : mode == DragMode::scaleLeft || mode == DragMode::scaleRight ? "Scale animation keys" : "Edit cubic tangent";
        // One undo step for every curve the drag changed.
        processor.document.edit(label, [id, changed = std::move(changed)](motion::Project& project) {
            for (const auto& [name, curve] : changed) {
                auto* target = mutableCurve(project, id, name);
                if (target != nullptr) {
                    *target = curve;
                }
            }
        });
    }
    if (onPreview) {
        onPreview(nullptr);
    }
    refresh();
}

bool MotionCurveEditor::keyPressed(const juce::KeyPress& key) {
    if (!drag.has_value() && key.getModifiers().isCommandDown() && !key.getModifiers().isShiftDown() && (key.getKeyCode() == 'A' || key.getKeyCode() == 'a')) {
        selectAllKeys();
        return true;
    }
    if (!drag.has_value() && (key.getKeyCode() == 'F' || key.getKeyCode() == 'f')) {
        const auto primaryOnly = key.getModifiers().isShiftDown();
        // Framing the group is the automatic view, so later edits keep it framed.
        userView = primaryOnly;
        fit(primaryOnly);
        repaint();
        return true;
    }
    if (key.getModifiers().isCommandDown() && (key.getKeyCode() == 'Z' || key.getKeyCode() == 'z') && drag.has_value()) {
        restoreDragSelection();
        cancelDrag();
        refresh();
        return true;
    }
    if (key == juce::KeyPress::escapeKey && drag.has_value()) {
        restoreDragSelection();
        cancelDrag();
        refresh();
        return true;
    }
    if (key == juce::KeyPress::escapeKey && hasSelectedKeys()) {
        selectedTime.reset();
        companions.clear();
        selectionChanged();
        repaint();
        return true;
    }
    if (!drag.has_value() && key.getModifiers().isAltDown() && !key.getModifiers().isCommandDown()) {
        const auto big = key.getModifiers().isShiftDown();
        const auto tick = valueStep(high - low, std::max(3, juce::roundToInt(plot().getHeight() / 30))) / (big ? 1.0 : 10.0);
        if (key.getKeyCode() == juce::KeyPress::leftKey) { return nudgeSelected(big ? -10 : -1, 0); }
        if (key.getKeyCode() == juce::KeyPress::rightKey) { return nudgeSelected(big ? 10 : 1, 0); }
        if (key.getKeyCode() == juce::KeyPress::upKey) { return nudgeSelected(0, tick); }
        if (key.getKeyCode() == juce::KeyPress::downKey) { return nudgeSelected(0, -tick); }
    }
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) {
        if (drag.has_value()) {
            restoreDragSelection();
            cancelDrag();
        }
        return deleteSelected();
    }
    return false;
}

void MotionCurveEditor::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) {
    if (drag.has_value()) {
        return;
    }
    userView = true;
    follow.userMoved();
    using Gesture = motion::ui::WheelGesture;
    const Gesture gesture(event.mods, wheel);
    if (gesture.kind == Gesture::Kind::zoomTime) {
        zoomTime(event.position.x, gesture.factor);
    } else if (gesture.kind == Gesture::Kind::scaleOther) {
        const auto anchor = valueAt(event.position.y);
        const auto ratio = (anchor - low) / (high - low);
        const auto span = std::clamp((high - low) / gesture.factor, 0.0001, 1.0e9);
        low = anchor - span * ratio;
        high = low + span;
        normalizeValueRange();
    } else {
        const auto seconds = -gesture.dx * Gesture::pixelsPerUnit / plot().getWidth() * (viewEnd - viewStart);
        if (gesture.dx != 0) { setView(viewStart + seconds, viewEnd + seconds); }
        if (gesture.dy != 0) {
            const auto shift = gesture.dy * Gesture::pixelsPerUnit / plot().getHeight() * (high - low);
            low += shift; high += shift;
            normalizeValueRange();
        }
    }
    repaint();
}

void MotionCurveEditor::mouseMagnify(const juce::MouseEvent& event, float scale) {
    if (drag.has_value() || !(scale > 0)) { return; }
    userView = true;
    follow.userMoved();
    zoomTime(event.position.x, scale);
    repaint();
}

void MotionCurveEditor::zoomTime(float x, double factor) {
    const auto anchor = projectTime(x);
    const auto ratio = (anchor - viewStart) / (viewEnd - viewStart);
    const auto duration = processor.document.project().duration;
    const auto maximumSpan = duration >= viewLimit / 2 ? viewLimit * 2 : std::max(1.0, duration * 4.0);
    const auto span = std::clamp((viewEnd - viewStart) / factor, 1.0 / std::max(1.0, processor.document.project().frameRate), maximumSpan);
    const auto start = anchor - span * ratio;
    setView(start, start + span);
}

void MotionCurveEditor::beginDrag(const motion::PropertyTarget& clip, DragMode mode, juce::Point<float> down) {
    // A locked track's keys can be selected but not moved.
    if (clip.locked) { return; }
    drag = Drag {};
    drag->originals = groupCurves(clip);
    drag->previews = drag->originals;
    drag->down = down;
    drag->start = clip.start;
    drag->duration = clip.duration;
    drag->offset = clip.offset;
    drag->rate = clip.rate;
    drag->mode = mode;
    drag->primary = selectedTime;
    drag->companions = companions;
}

void MotionCurveEditor::cancelDrag() {
    snapGuide.reset();
    if (drag.has_value()) {
        drag.reset();
        if (onPreview) {
            onPreview(nullptr);
        }
    }
}

std::vector<MotionCurveEditor::KeyRef> MotionCurveEditor::dragSelection() const {
    auto keys = drag->companions;
    if (drag->primary.has_value()) {
        keys.push_back({ propertyName, *drag->primary });
    }
    return keys;
}

void MotionCurveEditor::restoreDragSelection() {
    selectedTime = drag->primary;
    companions = drag->companions;
}

void MotionCurveEditor::adoptSelection(std::vector<KeyRef> keys, bool hasPrimary) {
    selectedTime.reset();
    if (hasPrimary && !keys.empty()) {
        selectedTime = keys.back().time;
        keys.pop_back();
    }
    companions = std::move(keys);
}

std::vector<MotionCurveEditor::KeyRef> MotionCurveEditor::selection() const {
    auto keys = companions;
    if (selectedTime.has_value()) {
        keys.push_back({ propertyName, *selectedTime });
    }
    return keys;
}

bool MotionCurveEditor::isSelected(const std::string& property, double time) const {
    return (selectedTime.has_value() && property == propertyName && *selectedTime == time)
        || std::any_of(companions.begin(), companions.end(), [&](const KeyRef& key) { return key.property == property && key.time == time; });
}

void MotionCurveEditor::makePrimary(const KeyRef& key) {
    selectedTime = key.time;
    if (key.property != propertyName) {
        propertyName = key.property;
        if (onPropertyChosen) {
            onPropertyChosen(propertyName);
        }
    }
}

void MotionCurveEditor::toggleKey(const KeyRef& key) {
    if (std::find(companions.begin(), companions.end(), key) != companions.end()) {
        std::erase(companions, key);
        return;
    }
    if (primaryKey() == key) {
        // Deselecting the primary promotes the most recent companion.
        selectedTime.reset();
        if (!companions.empty()) {
            const auto next = companions.back();
            companions.pop_back();
            makePrimary(next);
        }
        return;
    }
    if (selectedTime.has_value() && key.property != propertyName) {
        // A key on another axis joins the selection; the primary stays.
        companions.push_back(key);
        return;
    }
    if (selectedTime.has_value()) {
        companions.push_back({ propertyName, *selectedTime });
    }
    selectedTime.reset();
    makePrimary(key);
}

std::vector<std::pair<std::string, juce::Colour>> MotionCurveEditor::siblings(const motion::PropertyTarget& target) const {
    std::vector<std::pair<std::string, juce::Colour>> result;
    const auto specs = motion::propertySpecs(target);
    const auto* current = motion::findPropertySpec(specs, propertyName);
    if (current == nullptr || target.isEffect) { return result; }
    for (const auto& spec : specs) {
        if (spec.group != current->group || spec.id == current->id || target.curve(std::string(spec.id)) == nullptr || hiddenCurves.contains(std::string(spec.id))) { continue; }
        result.emplace_back(std::string(spec.id), motion::style::axisColour(spec.axis, motion::style::axisZ()));
    }
    return result;
}

juce::Colour MotionCurveEditor::primaryColour(const motion::PropertyTarget& target) const {
    const auto* spec = target.isEffect ? nullptr : motion::findPropertySpec(motion::propertySpecs(target), propertyName);
    return motion::style::axisColour(spec != nullptr ? spec->axis : std::string_view());
}

std::shared_ptr<const motion::CurveDrivers> MotionCurveEditor::resultDrivers(const motion::Curve& curve) {
    const auto& document = processor.document;
    const auto key = std::make_tuple(document.generation(), document.revision(), targetId, propertyName);
    if (key != driversKey) {
        driversKey = key;
        auto copy = curve;
        copy.drivers.reset();
        motion::PreparedDrivers prepared(nullptr);
        prepared.drive(copy, document.project(), motion::ClipTiming {}, targetId, propertyName);
        cachedDrivers = copy.drivers;
    }
    return cachedDrivers;
}

double MotionCurveEditor::valueStep(double span, int wanted) {
    const auto raw = span / std::max(1, wanted);
    if (!(raw > 0) || !std::isfinite(raw)) { return 1; }
    const auto magnitude = std::pow(10.0, std::floor(std::log10(raw)));
    for (const auto factor : {1.0, 2.0, 5.0, 10.0}) {
        if (raw <= factor * magnitude) { return factor * magnitude; }
    }
    return 10 * magnitude;
}

bool MotionCurveEditor::isSibling(const motion::PropertyTarget& target, const std::string& property) const {
    const auto group = siblings(target);
    return std::any_of(group.begin(), group.end(), [&property](const auto& entry) { return entry.first == property; });
}

std::vector<std::string> MotionCurveEditor::groupNames(const motion::PropertyTarget& target) const {
    std::vector<std::string> names;
    if (target.curve(propertyName) == nullptr) { return names; }
    names.push_back(propertyName);
    for (const auto& [name, colour] : siblings(target)) {
        names.push_back(name);
    }
    return names;
}

motion::PropertyMap MotionCurveEditor::groupCurves(const motion::PropertyTarget& target) const {
    motion::PropertyMap curves;
    for (const auto& name : groupNames(target)) {
        curves.emplace(name, *target.curve(name));
    }
    return curves;
}

const motion::Curve* MotionCurveEditor::displayed(const motion::PropertyTarget& target, const std::string& property) const {
    if (drag.has_value()) {
        const auto found = drag->previews.find(property);
        if (found != drag->previews.end()) {
            return &found->second;
        }
    }
    return target.curve(property);
}

bool MotionCurveEditor::targetLocked() const {
    const auto target = motion::findPropertyTarget(processor.document.project(), targetId);
    return target.has_value() && target->locked;
}

bool MotionCurveEditor::dragMatches(const motion::PropertyTarget& clip) const {
    if (clip.start != drag->start || clip.duration != drag->duration || clip.offset != drag->offset || clip.rate != drag->rate) {
        return false;
    }
    return std::all_of(drag->originals.begin(), drag->originals.end(), [&clip](const auto& entry) {
        const auto* current = clip.curve(entry.first);
        return current != nullptr && sameCurve(*current, entry.second);
    });
}

std::optional<MotionCurveEditor::SelectionBox> MotionCurveEditor::selectionBox(const motion::PropertyTarget& clip) const {
    const auto keys = selection();
    if (keys.size() < 2) {
        return std::nullopt;
    }
    auto first = std::numeric_limits<double>::infinity(), last = -first;
    auto top = std::numeric_limits<float>::infinity(), bottom = -top;
    for (const auto& ref : keys) {
        const auto* curve = displayed(clip, ref.property);
        const auto* key = curve != nullptr ? curve->findKey(ref.time) : nullptr;
        if (key == nullptr) {
            continue;
        }
        first = std::min(first, key->time);
        last = std::max(last, key->time);
        const auto y = keyPoint(clip, *key).y;
        top = std::min(top, y);
        bottom = std::max(bottom, y);
    }
    if (!(last > first)) {
        return std::nullopt;
    }
    const auto left = timeX(clip.projectTime(first)), right = timeX(clip.projectTime(last));
    return SelectionBox { juce::Rectangle<float>::leftTopRightBottom(left, top, right, bottom).expanded(12.0f, 8.0f), first, last };
}

juce::Rectangle<float> MotionCurveEditor::scaleHandle(const SelectionBox& box, bool right) {
    const auto x = right ? box.area.getRight() : box.area.getX();
    return { x - 1.5f, box.area.getCentreY() - 8.0f, 3.0f, 16.0f };
}

void MotionCurveEditor::setView(double start, double end) {
    if (std::isnan(start) || std::isnan(end)) { return; }
    start = std::clamp(start, -viewLimit, viewLimit);
    end = std::clamp(end, -viewLimit, viewLimit);
    const auto minimumSpan = std::max(1.0e-9, std::abs(start) * std::numeric_limits<double>::epsilon() * 4);
    if (end - start < minimumSpan) {
        end = std::min(viewLimit, start + minimumSpan);
        start = std::max(-viewLimit, end - minimumSpan);
    }
    viewStart = start;
    viewEnd = end;
}

void MotionCurveEditor::normalizeValueRange() {
    if (!std::isfinite(low) || !std::isfinite(high) || !std::isfinite(high - low) || high <= low) {
        low = -1.0;
        high = 1.0;
    }
}

std::optional<int> MotionCurveEditor::playheadX() const {
    const auto x = juce::roundToInt(timeX(processor.position.load()));
    if (x < plot().getX() || x > plot().getRight()) { return std::nullopt; }
    return x;
}

float MotionCurveEditor::timeX(double time) const {
    auto normalized = (time - viewStart) / (viewEnd - viewStart);
    if (!std::isfinite(normalized)) { normalized = time < viewStart ? -10.0 : 11.0; }
    return plot().getX() + static_cast<float>(std::clamp(normalized, -10.0, 11.0)) * plot().getWidth();
}

float MotionCurveEditor::valueY(double value) const {
    const auto normalized = (value - low) / (high - low);
    const auto bounded = std::isfinite(normalized) ? std::clamp(normalized, -10.0, 11.0) : 0.0;
    return plot().getBottom() - static_cast<float>(bounded) * plot().getHeight();
}

double MotionCurveEditor::projectTime(float x) const {
    const auto fraction = static_cast<double>((x - plot().getX()) / plot().getWidth());
    if (!std::isfinite(fraction)) { return viewStart; }
    return std::clamp(std::lerp(viewStart, viewEnd, fraction), -viewLimit, viewLimit);
}

double MotionCurveEditor::segmentSpan(const motion::Curve& curve, const motion::Keyframe& key, DragMode mode) {
    const auto& keys = curve.keyframes();
    const auto found = std::lower_bound(keys.begin(), keys.end(), key.time, [](const auto& item, double time) { return item.time < time; });
    if (found == keys.end()) { return 0; }
    if (mode == DragMode::incoming) { return found == keys.begin() ? 0 : (found - 1)->time - key.time; }
    return found + 1 == keys.end() ? 0 : (found + 1)->time - key.time;
}

std::optional<juce::Point<float>> MotionCurveEditor::tangentPoint(const motion::PropertyTarget& clip, const motion::Curve& curve, const motion::Keyframe& key, DragMode mode) const {
    const auto& keys = curve.keyframes();
    const auto found = std::lower_bound(keys.begin(), keys.end(), key.time, [](const auto& item, double time) { return item.time < time; });
    if (found == keys.end()) {
        return std::nullopt;
    }
    if (mode == DragMode::incoming && (found == keys.begin() || (found - 1)->interpolation != motion::Interpolation::cubic)) {
        return std::nullopt;
    }
    if (mode == DragMode::outgoing && (key.interpolation != motion::Interpolation::cubic || found + 1 == keys.end())) {
        return std::nullopt;
    }
    const auto span = segmentSpan(curve, key, mode);
    const auto slope = mode == DragMode::incoming ? key.incomingSlope : key.outgoingSlope;
    const auto delta = span * (mode == DragMode::incoming ? key.incomingInfluence : key.outgoingInfluence);
    const auto xScale = (plot().getWidth() / (viewEnd - viewStart)) / clip.rate;
    const auto yScale = plot().getHeight() / (high - low);
    if (!std::isfinite(delta * xScale) || !std::isfinite(slope * delta * yScale)) { return std::nullopt; }
    const auto centre = keyPoint(clip, key);
    return juce::Point<float>(centre.x + static_cast<float>(delta * xScale), centre.y - static_cast<float>(slope * delta * yScale));
}

double MotionCurveEditor::constrainedValue(const motion::PropertyTarget& target, double value, const std::string& property) const {
    if (target.isAudio) { return property == "pan" ? std::clamp(value, -1.0, 1.0) : std::clamp(value, 0.0, 4.0); }
    if (target.beam) {
        const auto* spec = motion::findPropertySpec(motion::beamPropertySpecs, property);
        return spec != nullptr ? spec->clamp(value) : value;
    }
    if (target.isEffect) {
        const auto* effect = motion::findEffect(processor.document.project(), target.id);
        const auto* definition = effect != nullptr ? motion::effectDefinition(effect->type) : nullptr;
        if (definition != nullptr) {
            for (const auto& parameter : definition->parameters) {
                if (parameter.id == property) { return std::clamp(value, parameter.min, parameter.max); }
            }
        }
    }
    if (!target.isEffect && !target.camera && !target.beam) {
        if (property == "red" || property == "green" || property == "blue") { return std::clamp(value, 0.0, 1.0); }
        if (property == "weight") { return std::clamp(value, 0.0, motion::maximumWeight); }
    }
    return target.camera && property == "fov" ? std::clamp(value, 0.001, 179.999) : value;
}

double MotionCurveEditor::snappedTime(const motion::PropertyTarget& clip, double time, juce::ModifierKeys modifiers) const {
    if (!modifiers.isAltDown()) {
        time = processor.document.project().timeGrid().snap(time);
    }
    return std::clamp(time, clip.start, clip.end());
}

std::optional<double> MotionCurveEditor::magnet(const motion::PropertyTarget& clip, double time, const std::string& ownCurve) const {
    const auto& project = processor.document.project();
    if (!project.gridSnap || !drag.has_value()) {
        return std::nullopt;
    }
    const auto reach = 8.0 / plot().getWidth() * (viewEnd - viewStart);
    std::optional<double> best;
    auto bestDistance = reach;
    const auto consider = [&](double candidate) {
        const auto distance = std::abs(candidate - time);
        if (distance < bestDistance) {
            bestDistance = distance;
            best = candidate;
        }
    };
    consider(processor.position.load());
    for (const auto& marker : project.markers) {
        consider(marker.time);
    }
    const auto moving = dragSelection();
    for (const auto& [name, curve] : drag->originals) {
        if (name == ownCurve) { continue; }
        for (const auto& key : curve.keyframes()) {
            const auto selected = std::any_of(moving.begin(), moving.end(), [&](const KeyRef& item) { return item.property == name && item.time == key.time; });
            if (!selected) {
                consider(clip.projectTime(key.time));
            }
        }
    }
    return best;
}

double MotionCurveEditor::snapKeyTime(const motion::PropertyTarget& clip, double time, juce::ModifierKeys modifiers, const std::string& ownCurve) {
    snapGuide.reset();
    if (!modifiers.isAltDown()) {
        const auto target = magnet(clip, time, ownCurve);
        if (target.has_value() && *target >= clip.start && *target <= clip.end()) {
            snapGuide = target;
            return *target;
        }
        time = processor.document.project().timeGrid().snap(time);
    }
    return std::clamp(time, clip.start, clip.end());
}

std::optional<MotionCurveEditor::KeyRef> MotionCurveEditor::hitKey(const motion::PropertyTarget& clip, juce::Point<float> point) const {
    std::optional<KeyRef> hit;
    auto distance = 9.0f;
    for (const auto& name : groupNames(clip)) {
        const auto primary = name == propertyName;
        for (const auto& key : clip.curve(name)->keyframes()) {
            const auto position = keyPoint(clip, key);
            const auto candidate = position.getDistanceFrom(point);
            if (plot().expanded(5).contains(position) && (primary ? candidate <= distance : candidate < distance)) {
                distance = candidate;
                hit = KeyRef { name, key.time };
            }
        }
    }
    return hit;
}

void MotionCurveEditor::fit(bool primaryOnly) {
    const auto clip = motion::findPropertyTarget(processor.document.project(), targetId);
    const auto* curve = findCurve(clip, propertyName);
    if (curve == nullptr) {
        return;
    }
    std::vector<const motion::Curve*> curves { curve };
    if (!primaryOnly) {
        for (const auto& [name, colour] : siblings(*clip)) {
            curves.push_back(clip->curve(name));
        }
    }
    auto first = clip->start, last = clip->end();
    for (const auto* framed : curves) {
        for (const auto& key : framed->keyframes()) {
            const auto time = clip->projectTime(key.time);
            if (std::isfinite(time)) { first = std::min(first, time); last = std::max(last, time); }
        }
    }
    const auto padding = (last - first) * 0.04;
    setView(first - padding, last + padding);
    low = high = curve->evaluateBase(clip->localTime(first));
    // Frame what plays too: routed modulators and links drive the result.
    auto withDrivers = *curve;
    withDrivers.drivers = resultDrivers(*curve);
    // Include sampled extrema of cubic segments as well as exact key values.
    for (int i = 0; i <= 256; ++i) {
        const auto local = clip->localTime(std::lerp(first, last, i / 256.0));
        const auto base = curve->evaluateBase(local);
        low = std::min(low, base); high = std::max(high, base);
        const auto value = constrainedValue(*clip, withDrivers.evaluate(local), propertyName);
        low = std::min(low, value);
        high = std::max(high, value);
    }
    for (const auto* framed : curves) {
        for (const auto& key : framed->keyframes()) {
            low = std::min(low, key.value);
            high = std::max(high, key.value);
        }
        if (framed == curve) { continue; }
        for (int i = 0; i <= 128; ++i) {
            const auto value = framed->evaluateBase(clip->localTime(std::lerp(first, last, i / 128.0)));
            if (std::isfinite(value)) { low = std::min(low, value); high = std::max(high, value); }
        }
    }
    const auto minimumSpan = propertyName.starts_with("rotation.") ? 90.0 : 1.0;
    if (high - low < minimumSpan) {
        const auto middle = low * 0.5 + high * 0.5;
        low = middle - minimumSpan * 0.5;
        high = middle + minimumSpan * 0.5;
    }
    const auto margin = (high - low) * 0.15;
    low -= margin;
    high += margin;
    normalizeValueRange();
}

void MotionCurveEditor::selectAllKeys() {
    const auto clip = motion::findPropertyTarget(processor.document.project(), targetId);
    selectedTime.reset();
    companions.clear();
    if (clip.has_value()) {
        for (const auto& name : groupNames(*clip)) {
            for (const auto& key : clip->curve(name)->keyframes()) {
                if (name == propertyName && !selectedTime.has_value()) { selectedTime = key.time; } else { companions.push_back({name, key.time}); }
            }
        }
    }
    selectionChanged();
    repaint();
}

bool MotionCurveEditor::easeSelected(bool in, bool out) {
    const auto keys = selection();
    if (keys.empty() || targetLocked()) {
        return false;
    }
    const auto id = targetId;
    const auto eased = processor.document.tryEdit(in && out ? "Easy ease" : (in ? "Easy ease in" : "Easy ease out"), [id, keys, in, out](motion::Project& project) {
        bool any = false;
        for (const auto& key : keys) {
            auto* target = mutableCurve(project, id, key.property);
            any = (target != nullptr && motion::easeKey(*target, key.time, in, out)) || any;
        }
        return any;
    });
    refresh();
    return eased;
}

bool MotionCurveEditor::deleteSelected() {
    const auto clip = motion::findPropertyTarget(processor.document.project(), targetId);
    if (!clip.has_value() || clip->locked) { return false; }
    auto keys = selection();
    std::erase_if(keys, [&clip](const KeyRef& key) {
        const auto* curve = findCurve(clip, key.property);
        return curve == nullptr || curve->findKey(key.time) == nullptr;
    });
    if (keys.empty()) {
        return false;
    }
    const auto id = targetId;
    processor.document.edit(keys.size() > 1 ? "Delete animation keys" : "Delete animation key", [id, keys](motion::Project& project) {
        for (const auto& key : keys) {
            auto* target = mutableCurve(project, id, key.property);
            if (target != nullptr) { target->removeKey(key.time); }
        }
    });
    selectedTime.reset();
    companions.clear();
    refresh();
    return true;
}

void MotionCurveEditor::showKeyMenu() {
    const auto clip = motion::findPropertyTarget(processor.document.project(), targetId);
    // The selection is captured when the menu opens; the primary key (or
    // else the first selected) shows the current choice.
    const auto keys = selection();
    const auto reference = selectedTime.has_value() ? keys.back() : keys.front();
    const auto* curve = findCurve(clip, reference.property);
    const auto* key = curve != nullptr ? curve->findKey(reference.time) : nullptr;
    if (key == nullptr) {
        return;
    }
    const auto editable = !clip->locked;
    juce::PopupMenu menu;
    const char* labels[] = { "Hold", "Linear", "Auto", "Bezier" };
    for (int i = 0; i < 4; ++i) {
        menu.addItem(i + 1, labels[i], editable, static_cast<int>(key->interpolation) == i);
    }
    menu.addSeparator();
    menu.addItem(motion::style::menuItem("Easy ease", 11, "F9").setEnabled(editable));
    menu.addItem(motion::style::menuItem("Easy ease in", 12, "Shift+F9").setEnabled(editable));
    menu.addItem(motion::style::menuItem("Easy ease out", 13, "Cmd+Shift+F9").setEnabled(editable));
    motion::ui::showDocumentMenu(menu, *this, processor.document, juce::PopupMenu::Options().withTargetComponent(this), [this](int result) {
        if (result >= 11 && result <= 13) {
            easeSelected(result != 13, result != 12);
        } else if (result >= 1 && result <= 4) {
            setSelectedInterpolation(static_cast<motion::Interpolation>(result - 1));
        }
    });
}

void MotionCurveEditor::setSelectedInterpolation(motion::Interpolation next) {
    const auto keys = selection();
    const auto id = targetId;
    const auto current = motion::findPropertyTarget(processor.document.project(), id);
    if (!current.has_value() || current->locked) {
        return;
    }
    // No undo step when every selected key already uses the choice.
    const auto needed = std::any_of(keys.begin(), keys.end(), [&current, next](const KeyRef& ref) {
        const auto* owner = findCurve(current, ref.property);
        const auto* found = owner != nullptr ? owner->findKey(ref.time) : nullptr;
        return found != nullptr && found->interpolation != next;
    });
    if (!needed) {
        return;
    }
    processor.document.edit(keys.size() > 1 ? "Change keys interpolation" : "Change key interpolation", [id, keys, next](motion::Project& project) {
        for (const auto& ref : keys) {
            auto* target = mutableCurve(project, id, ref.property);
            if (target != nullptr) { motion::keyedit::setInterpolation(*target, ref.time, next); }
        }
    });
    refresh();
}

std::optional<MotionCurveEditor::ActiveKey> MotionCurveEditor::activeKey() const {
    const auto keys = selection();
    const auto clip = motion::findPropertyTarget(processor.document.project(), targetId);
    if (keys.size() != 1 || !clip.has_value()) { return std::nullopt; }
    const auto* curve = displayed(*clip, keys.front().property);
    const auto* key = curve != nullptr ? curve->findKey(keys.front().time) : nullptr;
    if (key == nullptr) { return std::nullopt; }
    return ActiveKey {keys.front().property, clip->projectTime(key->time), key->value, !clip->locked};
}

void MotionCurveEditor::setActiveKeyTime(double projectTime) {
    const auto keys = selection();
    const auto clip = motion::findPropertyTarget(processor.document.project(), targetId);
    if (keys.size() != 1 || !clip.has_value() || clip->locked || drag.has_value() || !std::isfinite(projectTime)) { return; }
    const auto time = clip->localTime(std::clamp(projectTime, clip->start, clip->end()));
    const auto originals = groupCurves(*clip);
    auto result = motion::keyedit::transformKeys(originals, keys, [time](double) { return time; }, 0.0, clip->offset, clip->localTime(clip->end()),
        [this, &clip](const std::string& property, double value) { return constrainedValue(*clip, value, property); });
    if (!result.has_value()) { return; }
    motion::PropertyMap changed;
    for (auto& [name, curve] : result->curves) {
        if (!sameCurve(curve, originals.at(name))) { changed.emplace(name, std::move(curve)); }
    }
    adoptSelection(std::move(result->selection), selectedTime.has_value());
    if (changed.empty()) { return; }
    const auto id = targetId;
    processor.document.editCoalesced("Move animation key", "graph.key.time", [id, changed = std::move(changed)](motion::Project& project) {
        for (const auto& [name, curve] : changed) {
            auto* target = mutableCurve(project, id, name);
            if (target != nullptr) { *target = curve; }
        }
    });
    refresh();
}

void MotionCurveEditor::setActiveKeyValue(double value) {
    const auto keys = selection();
    const auto clip = motion::findPropertyTarget(processor.document.project(), targetId);
    if (keys.size() != 1 || !clip.has_value() || clip->locked || drag.has_value() || !std::isfinite(value)) { return; }
    const auto ref = keys.front();
    const auto constrained = constrainedValue(*clip, value, ref.property);
    const auto id = targetId;
    processor.document.editCoalesced("Change animation key", "graph.key.value", [id, ref, constrained](motion::Project& project) {
        auto* curve = mutableCurve(project, id, ref.property);
        const auto* key = curve != nullptr ? curve->findKey(ref.time) : nullptr;
        if (key == nullptr) { return; }
        auto changed = *key;
        changed.value = constrained;
        curve->setKey(changed);
    });
    refresh();
}

bool MotionCurveEditor::nudgeSelected(int frames, double values) {
    const auto keys = selection();
    const auto clip = motion::findPropertyTarget(processor.document.project(), targetId);
    if (keys.empty() || !clip.has_value() || clip->locked) { return !keys.empty(); }
    const auto& project = processor.document.project();
    const auto seconds = frames / (project.frameRate > 0 ? project.frameRate : 30.0);
    const auto originals = groupCurves(*clip);
    auto result = motion::keyedit::transformKeys(originals, keys, [&clip, seconds](double time) { return clip->localTime(clip->projectTime(time) + seconds); }, values,
        clip->offset, clip->localTime(clip->end()), [this, &clip](const std::string& property, double value) { return constrainedValue(*clip, value, property); });
    // Nothing moves past the clip or onto another key.
    if (!result.has_value()) { return true; }
    motion::PropertyMap changed;
    for (auto& [name, curve] : result->curves) {
        if (!sameCurve(curve, originals.at(name))) { changed.emplace(name, std::move(curve)); }
    }
    adoptSelection(std::move(result->selection), selectedTime.has_value());
    if (!changed.empty()) {
        const auto id = targetId;
        processor.document.editCoalesced(keys.size() > 1 ? "Nudge animation keys" : "Nudge animation key", frames != 0 ? "graph.nudge.time" : "graph.nudge.value", [id, changed = std::move(changed)](motion::Project& updated) {
            for (const auto& [name, curve] : changed) {
                auto* target = mutableCurve(updated, id, name);
                if (target != nullptr) { *target = curve; }
            }
        });
    }
    refresh();
    return true;
}

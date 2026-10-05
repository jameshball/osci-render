#include "CurveEditor.h"

MotionCurveEditor::MotionCurveEditor(MotionProcessor& processor) : processor(processor) {
    setName("Animation curve editor");
    setWantsKeyboardFocus(true);
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
        if (curve != nullptr && selectedTime.has_value() && findKey(*curve, *selectedTime) == nullptr) {
            selectedTime.reset();
        }
        std::erase_if(companions, [&clip](const KeyRef& key) {
            const auto* owner = findCurve(clip, key.property);
            return owner == nullptr || findKey(*owner, key.time) == nullptr;
        });
        if (!userView) {
            fit();
        }
    }
    repaint();
}

void MotionCurveEditor::paint(juce::Graphics& g) {
    g.fillAll(osci::Colours::veryDark());
    playheadStrip.drawn(std::nullopt);
    const auto clip = motion::findPropertyTarget(processor.document.project(), targetId);
    const auto* storedCurve = findCurve(clip, propertyName);
    g.setColour(osci::Colours::text());
    g.setFont(motion::style::body());
    if (storedCurve == nullptr) {
        g.setColour(osci::Colours::textMuted());
        g.drawText("Select a property", getLocalBounds(), juce::Justification::centred);
        return;
    }
    const auto& curve = *displayed(*clip, propertyName);
    // Routed modulators and links count as modulation too: the Result
    // curve shows what the property actually does.
    const auto drivers = resultDrivers(curve);
    const auto modulated = drivers != nullptr;
    const auto area = plot();
    g.setFont(motion::style::title());
    g.drawText(motion::propertyLabel(processor.document.project(), targetId, propertyName), 12, 3, 160, 22, juce::Justification::centredLeft);
    g.setFont(motion::style::body());
    g.setColour(osci::Colours::textMuted());
    // Name the owner's kind when it isn't a clip, so a camera or effect
    // curve is never mistaken for the selected clip's.
    const juce::String owner(clip->name.data(), clip->name.size());
    g.drawText(clip->camera ? "Camera: " + owner : clip->isEffect ? "Effect: " + owner : clip->isGroup ? "Group: " + owner : owner, 150, 3, getWidth() - 330, 22, juce::Justification::centredLeft);
    g.setFont(motion::style::body());
    if (modulated) {
        g.setColour(primaryColour(*clip));
        g.drawText("Keys", getWidth() - 150, 3, 48, 22, juce::Justification::centredLeft);
        g.setColour(motion::style::result());
        g.drawText("Result", getWidth() - 90, 3, 65, 22, juce::Justification::centredLeft);
    }
    g.setFont(motion::style::caption());
    {
        const auto valueTick = valueStep(high - low, std::max(3, juce::roundToInt(area.getHeight() / 30)));
        const auto decimals = std::clamp(static_cast<int>(-std::floor(std::log10(valueTick))), 0, 6);
        // An integer index (capped) so extreme values can never stall the loop.
        const auto first = std::ceil(low / valueTick);
        for (int index = 0; index < 200 && (first + index) * valueTick <= high + valueTick * 1e-6; ++index) {
            const auto value = (first + index) * valueTick;
            const auto y = valueY(value);
            g.setColour(juce::Colours::white.withAlpha(std::abs(value) < valueTick * 1e-6 ? 0.14f : 0.07f));
            g.drawLine(area.getX(), y, area.getRight(), y);
            g.setColour(osci::Colours::text().withAlpha(0.65f));
            g.drawText(juce::String(std::abs(value) < valueTick * 1e-6 ? 0.0 : value, decimals), 2, juce::roundToInt(y) - 8, 53, 16, juce::Justification::centredRight);
        }
    }
    const auto grid = processor.document.project().timeGrid();
    const auto step = grid.tickStep(area.getWidth() / (viewEnd - viewStart));
    const auto firstTick = std::ceil(viewStart / step) * step;
    const auto rawCount = std::ceil((viewEnd - viewStart) / step) + 1.0;
    const auto count = std::isfinite(rawCount) ? static_cast<int>(std::clamp(rawCount, 0.0, 1000.0)) : 0;
    for (int index = 0; index < count; ++index) {
        const auto time = firstTick + index * step;
        if (!std::isfinite(time) || time > viewEnd) { break; }
        const auto x = timeX(time);
        g.setColour(juce::Colours::white.withAlpha(0.07f));
        g.drawLine(x, area.getY(), x, area.getBottom());
        g.setColour(osci::Colours::text().withAlpha(0.65f));
        g.drawText(juce::String(grid.label(time, step)), juce::roundToInt(x) - 34, juce::roundToInt(area.getBottom()) + 3, 68, 17, juce::Justification::centred);
    }
    {
        // Channels shown from the list, faint and not editable.
        juce::Graphics::ScopedSaveState context(g);
        g.reduceClipRegion(area.toNearestInt().expanded(5));
        const auto steps = std::max(2, juce::roundToInt(area.getWidth() / 3));
        for (const auto& [name, colour] : contextCurves) {
            const auto* other = clip->curve(name);
            if (other == nullptr || name == propertyName || isSibling(*clip, name)) { continue; }
            juce::Path shape;
            for (int i = 0; i <= steps; ++i) {
                const auto time = std::lerp(viewStart, viewEnd, static_cast<double>(i) / steps);
                const auto y = valueY(other->evaluateBase(clip->localTime(time)));
                if (i == 0) { shape.startNewSubPath(timeX(time), y); } else { shape.lineTo(timeX(time), y); }
            }
            g.setColour(colour.withAlpha(.3f));
            g.strokePath(shape, juce::PathStrokeType(1.0f));
        }
    }
    {
        // Sibling axes are editable too, drawn in their axis colour under the primary curve.
        juce::Graphics::ScopedSaveState ghosts(g);
        g.reduceClipRegion(area.toNearestInt().expanded(5));
        const auto steps = std::max(2, juce::roundToInt(area.getWidth() / 2));
        for (const auto& [name, colour] : siblings(*clip)) {
            const auto* other = displayed(*clip, name);
            juce::Path ghost;
            for (int i = 0; i <= steps; ++i) {
                const auto time = std::lerp(viewStart, viewEnd, static_cast<double>(i) / steps);
                const auto y = valueY(other->evaluateBase(clip->localTime(time)));
                if (i == 0) { ghost.startNewSubPath(timeX(time), y); } else { ghost.lineTo(timeX(time), y); }
            }
            g.setColour(colour.withAlpha(.5f));
            g.strokePath(ghost, juce::PathStrokeType(1.2f));
            for (const auto& key : other->keyframes()) {
                drawKey(g, keyPoint(*clip, key), 4.0f, isSelected(name, key.time) ? juce::Colours::white : colour, key);
            }
        }
    }
    {
        juce::Graphics::ScopedSaveState scope(g);
        g.reduceClipRegion(area.toNearestInt().expanded(5));
        juce::Path path;
        const auto steps = std::max(2, juce::roundToInt(area.getWidth()));
        for (int i = 0; i <= steps; ++i) {
            const auto time = std::lerp(viewStart, viewEnd, static_cast<double>(i) / steps);
            const auto y = valueY(curve.evaluateBase(clip->localTime(time)));
            if (i == 0) {
                path.startNewSubPath(timeX(time), y);
            } else {
                path.lineTo(timeX(time), y);
            }
        }
        const auto colour = primaryColour(*clip);
        g.setColour(colour);
        g.strokePath(path, juce::PathStrokeType(2.0f));
        if (modulated) {
            auto withDrivers = curve;
            withDrivers.drivers = drivers;
            juce::Path result;
            for (int i = 0; i <= steps; ++i) {
                const auto time = std::lerp(viewStart, viewEnd, static_cast<double>(i) / steps);
                const auto value = constrainedValue(*clip, withDrivers.evaluate(clip->localTime(time)), propertyName);
                if (i == 0) { result.startNewSubPath(timeX(time), valueY(value)); } else { result.lineTo(timeX(time), valueY(value)); }
            }
            g.setColour(motion::style::result());
            g.strokePath(result, juce::PathStrokeType(1.4f));
        }
        if (selectedTime.has_value()) {
            const auto* selected = findKey(curve, *selectedTime);
            if (selected != nullptr) {
                for (const auto mode : { DragMode::incoming, DragMode::outgoing }) {
                    const auto handle = tangentPoint(*clip, curve, *selected, mode);
                    if (handle.has_value()) {
                        const auto keyPosition = keyPoint(*clip, *selected);
                        g.setColour(motion::style::tangent().withAlpha(0.75f));
                        g.drawLine(keyPosition.x, keyPosition.y, handle->x, handle->y, 1.0f);
                        g.fillEllipse(handle->x - 4, handle->y - 4, 8, 8);
                    }
                }
            }
        }
        for (const auto& key : curve.keyframes()) {
            drawKey(g, keyPoint(*clip, key), 5.0f, isSelected(propertyName, key.time) ? juce::Colours::white : colour, key);
        }
        const auto x = playheadX();
        if (x.has_value()) {
            playheadStrip.drawn(x);
            g.setColour(motion::style::playhead());
            g.drawVerticalLine(*x, area.getY(), area.getBottom());
        }
        if (snapGuide.has_value()) {
            const auto guide = timeX(*snapGuide);
            g.setColour(osci::Colours::accentColor().withAlpha(.75f));
            const float dashes[] {4.0f, 3.0f};
            g.drawDashedLine(juce::Line<float>(guide, area.getY(), guide, area.getBottom()), dashes, 2, 1.0f);
        }
    }
    const auto box = selectionBox(*clip);
    if (box.has_value()) {
        g.setColour(osci::Colours::accentColor().withAlpha(.3f));
        g.drawRect(box->area, 1.0f);
        g.setColour(osci::Colours::accentColor().withAlpha(.85f));
        for (const auto right : { false, true }) {
            g.fillRoundedRectangle(scaleHandle(*box, right), 2.0f);
        }
    }
    if (marquee.has_value()) {
        g.setColour(osci::Colours::accentColor().withAlpha(.12f));
        g.fillRect(*marquee);
        g.setColour(osci::Colours::accentColor().withAlpha(.6f));
        g.drawRect(*marquee);
    }
    // A linked property ignores its keys; say so rather than let edits
    // appear to do nothing.
    if (storedCurve->link.has_value()) {
        const auto source = motion::findPropertyTarget(processor.document.project(), storedCurve->link->source);
        const auto name = source.has_value() ? juce::String(source->name.data(), source->name.size()) : juce::String("?");
        auto banner = area.withHeight(22).reduced(40, 0).translated(0, 4);
        g.setColour(osci::Colours::warning().withAlpha(.18f));
        g.fillRoundedRectangle(banner.toFloat(), 3.0f);
        g.setColour(osci::Colours::warning());
        g.setFont(motion::style::caption());
        g.drawText("Linked to " + name + " (" + juce::String(storedCurve->link->property) + "): keys here are ignored. Unlink in Routing.", banner.reduced(8, 0), juce::Justification::centred, true);
    }
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
    // The time axis under the plot scrubs the playhead.
    if (left && event.position.y > plot().getBottom() + 2) {
        scrubbing = true;
        scrubTo(event.position.x, event.mods);
        return;
    }
    if (left && selectedTime.has_value()) {
        const auto* selected = findKey(*curve, *selectedTime);
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
            if (!box.has_value()) {
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
        const auto* key = primary != nullptr ? findKey(*primary, *selectedTime) : nullptr;
        if (key != nullptr) {
            beginDrag(*current, DragMode::key, event.position);
            drag->original = *key;
        }
    }
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

const motion::Keyframe* MotionCurveEditor::findKey(const motion::Curve& curve, double time) {
    const auto& keys = curve.keyframes();
    const auto found = std::lower_bound(keys.begin(), keys.end(), time, [](const auto& key, double value) { return key.time < value; });
    return found != keys.end() && found->time == time ? &*found : nullptr;
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

void MotionCurveEditor::drawKey(juce::Graphics& g, juce::Point<float> point, float radius, juce::Colour colour, const motion::Keyframe& key) {
    g.setColour(colour);
    motion::style::drawKeyShape(g, point, radius, keyShape(key));
}

motion::style::KeyShape MotionCurveEditor::keyShape(const motion::Keyframe& key) {
    using Shape = motion::style::KeyShape;
    if (key.interpolation == motion::Interpolation::hold) { return Shape::hold; }
    if (key.interpolation == motion::Interpolation::linear) { return Shape::linear; }
    return motion::isEased(key) ? Shape::eased : Shape::smooth;
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
        const auto* key = curve != nullptr ? findKey(*curve, ref.time) : nullptr;
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
    return { x - 3.0f, box.area.getCentreY() - 8.0f, 6.0f, 16.0f };
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
        if (property == "weight") { return std::clamp(value, 0.0, 1000000.0); }
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
    // Include sampled extrema of cubic segments as well as exact key values.
    for (int i = 0; i <= 256; ++i) {
        const auto local = clip->localTime(std::lerp(first, last, i / 256.0));
        const auto base = curve->evaluateBase(local);
        low = std::min(low, base); high = std::max(high, base);
        const auto value = constrainedValue(*clip, curve->evaluate(local), propertyName);
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
        return curve == nullptr || findKey(*curve, key.time) == nullptr;
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
    const auto* key = curve != nullptr ? findKey(*curve, reference.time) : nullptr;
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
    const auto id = targetId;
    motion::ui::showDocumentMenu(menu, *this, processor.document, juce::PopupMenu::Options().withTargetComponent(this), [this, id, keys](int result) {
        if (result >= 11 && result <= 13) {
            easeSelected(result != 13, result != 12);
            return;
        }
        if (result > 4) {
            return;
        }
        const auto current = motion::findPropertyTarget(processor.document.project(), id);
        const auto next = static_cast<motion::Interpolation>(result - 1);
        // No undo step when every selected key already uses the choice.
        const auto needed = std::any_of(keys.begin(), keys.end(), [&current, next](const KeyRef& ref) {
            const auto* owner = findCurve(current, ref.property);
            const auto* found = owner != nullptr ? findKey(*owner, ref.time) : nullptr;
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
    });
}

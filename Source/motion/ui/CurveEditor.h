#pragma once

#include "../MotionProcessor.h"
#include "../model/PropertyTarget.h"
#include <optional>
#include <limits>

// Keys retain content-local times; the ruler and snapping use project time.
// Drags preview locally and commit through Document once on release, so an
// unrelated document edit cannot be overwritten by a stale project snapshot.
class MotionCurveEditor : public juce::Component {
public:
    explicit MotionCurveEditor(MotionProcessor& processor) : processor(processor) {
        setName("Animation curve editor");
        setWantsKeyboardFocus(true);
    }

    // Called synchronously; the curve pointer is only valid during this call.
    std::function<void(const motion::Curve*)> onPreview;

    void setSelection(motion::Id id, std::string property) {
        if (targetId == id && propertyName == property) {
            refresh();
            return;
        }
        cancelDrag();
        selectedTime.reset();
        targetId = id;
        propertyName = std::move(property);
        userView = false;
        fit();
        repaint();
    }

    void refresh() {
        const auto clip = motion::findPropertyTarget(processor.document.project(), targetId);
        const auto* curve = findCurve(clip, propertyName);
        if (curve == nullptr || (drag.has_value() && !dragMatches(*clip, *curve))) {
            cancelDrag();
            selectedTime.reset();
        }
        if (!drag.has_value()) {
            if (curve != nullptr && selectedTime.has_value() && findKey(*curve, *selectedTime) == nullptr) {
                selectedTime.reset();
            }
            if (!userView) {
                fit();
            }
        }
        repaint();
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(osci::Colours::veryDark());
        const auto clip = motion::findPropertyTarget(processor.document.project(), targetId);
        const auto* storedCurve = findCurve(clip, propertyName);
        g.setColour(osci::Colours::text());
        g.setFont(13.0f);
        if (storedCurve == nullptr) {
            g.drawText("Select an object or camera property", getLocalBounds(), juce::Justification::centred);
            return;
        }
        const auto& curve = drag.has_value() ? drag->preview : *storedCurve;
        const auto area = plot();
        g.drawText(juce::String(propertyName) + " | " + juce::String(clip->name.data(), clip->name.size()), 12, 3, getWidth() - 180, 22, juce::Justification::centredLeft);
        if (curve.modulation.enabled) {
            g.setColour(juce::Colour(0xff70da91));
            g.drawText("Keys", getWidth() - 150, 3, 48, 22, juce::Justification::centredLeft);
            g.setColour(juce::Colour(0xff80baff));
            g.drawText("Result", getWidth() - 90, 3, 65, 22, juce::Justification::centredLeft);
        }
        g.setFont(11.0f);
        for (int i = 0; i <= 4; ++i) {
            const auto fraction = i / 4.0;
            const auto value = low + fraction * (high - low);
            const auto y = valueY(value);
            g.setColour(juce::Colours::white.withAlpha(0.07f));
            g.drawLine(area.getX(), y, area.getRight(), y);
            g.setColour(osci::Colours::text().withAlpha(0.65f));
            g.drawText(juce::String(value, 2), 2, juce::roundToInt(y) - 8, 53, 16, juce::Justification::centredRight);
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
        g.setColour(osci::Colours::text().withAlpha(0.5f));
        g.drawText("Double-click: key | Drag: move | Right-click: curve | Cmd+wheel: time zoom | F: fit", 12, getHeight() - 19, getWidth() - 24, 17, juce::Justification::centredLeft);
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
            g.setColour(juce::Colour(0xff70da91));
            g.strokePath(path, juce::PathStrokeType(1.7f));
            if (curve.modulation.enabled) {
                juce::Path result;
                for (int i = 0; i <= steps; ++i) {
                    const auto time = std::lerp(viewStart, viewEnd, static_cast<double>(i) / steps);
                    const auto value = constrainedValue(*clip, curve.evaluate(clip->localTime(time), clip->curveBpm(processor.document.project().bpm)));
                    if (i == 0) { result.startNewSubPath(timeX(time), valueY(value)); } else { result.lineTo(timeX(time), valueY(value)); }
                }
                g.setColour(juce::Colour(0xff80baff));
                g.strokePath(result, juce::PathStrokeType(1.4f));
            }
            if (selectedTime.has_value()) {
                const auto* selected = findKey(curve, *selectedTime);
                if (selected != nullptr) {
                    for (const auto mode : { DragMode::incoming, DragMode::outgoing }) {
                        const auto handle = tangentPoint(*clip, curve, *selected, mode);
                        if (handle.has_value()) {
                            const auto keyPosition = keyPoint(*clip, *selected);
                            g.setColour(juce::Colour(0xffe7bc6c).withAlpha(0.75f));
                            g.drawLine(keyPosition.x, keyPosition.y, handle->x, handle->y, 1.0f);
                            g.fillEllipse(handle->x - 4, handle->y - 4, 8, 8);
                        }
                    }
                }
            }
            for (const auto& key : curve.keyframes()) {
                const auto point = keyPoint(*clip, key);
                juce::Path diamond;
                diamond.startNewSubPath(point.x, point.y - 5);
                diamond.lineTo(point.x + 5, point.y);
                diamond.lineTo(point.x, point.y + 5);
                diamond.lineTo(point.x - 5, point.y);
                diamond.closeSubPath();
                const auto active = selectedTime.has_value() && key.time == *selectedTime;
                g.setColour(active ? juce::Colours::white : juce::Colour(0xff70da91));
                g.fillPath(diamond);
            }
            const auto x = timeX(processor.position.load());
            g.setColour(juce::Colour(0xffe7bc6c));
            g.drawLine(x, area.getY(), x, area.getBottom(), 1.0f);
        }
    }

    void mouseDown(const juce::MouseEvent& event) override {
        grabKeyboardFocus();
        cancelDrag();
        const auto clip = motion::findPropertyTarget(processor.document.project(), targetId);
        const auto* curve = findCurve(clip, propertyName);
        if (curve == nullptr) {
            return;
        }
        if (!event.mods.isPopupMenu() && event.mods.isLeftButtonDown() && selectedTime.has_value()) {
            const auto* selected = findKey(*curve, *selectedTime);
            if (selected != nullptr) {
                for (const auto mode : { DragMode::incoming, DragMode::outgoing }) {
                    const auto handle = tangentPoint(*clip, *curve, *selected, mode);
                    if (handle.has_value() && plot().expanded(5).contains(*handle)
                        && keyPoint(*clip, *selected).getDistanceFrom(event.position) > 7.0f
                        && handle->getDistanceFrom(event.position) <= 8.0f) {
                        drag = Drag { *curve, *curve, *selected, event.position, clip->start, clip->duration, clip->offset, clip->rate, mode, *handle };
                        repaint();
                        return;
                    }
                }
            }
        }
        selectedTime = hitKey(*clip, *curve, event.position);
        if (event.mods.isPopupMenu()) {
            if (selectedTime.has_value()) {
                showKeyMenu();
            }
        } else if (selectedTime.has_value() && event.mods.isLeftButtonDown()) {
            const auto* key = findKey(*curve, *selectedTime);
            drag = Drag { *curve, *curve, *key, event.position, clip->start, clip->duration, clip->offset, clip->rate };
        }
        repaint();
    }

    void mouseDoubleClick(const juce::MouseEvent& event) override {
        cancelDrag();
        if (event.mods.isPopupMenu() || !plot().contains(event.position)) {
            return;
        }
        const auto clip = motion::findPropertyTarget(processor.document.project(), targetId);
        const auto* curve = findCurve(clip, propertyName);
        if (curve == nullptr) {
            return;
        }
        const auto existing = hitKey(*clip, *curve, event.position);
        if (existing.has_value()) {
            selectedTime = existing;
            repaint();
            return;
        }
        motion::Keyframe key;
        key.time = clip->localTime(snappedTime(*clip, projectTime(event.position.x), event.mods));
        key.value = constrainedValue(*clip, valueAt(event.position.y));
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

    void mouseDrag(const juce::MouseEvent& event) override {
        if (!drag.has_value()) {
            return;
        }
        const auto clip = motion::findPropertyTarget(processor.document.project(), targetId);
        const auto* curve = findCurve(clip, propertyName);
        if (curve == nullptr || !dragMatches(*clip, *curve)) {
            cancelDrag();
            refresh();
            return;
        }
        auto key = drag->original;
        if (drag->mode != DragMode::key) {
            const auto point = drag->handle + event.position - drag->down;
            auto localDelta = clip->localTime(projectTime(point.x)) - key.time;
            const auto minimumDelta = std::max(1.0e-12, clip->duration * clip->rate * 1.0e-9);
            localDelta = drag->mode == DragMode::incoming ? std::min(-minimumDelta, localDelta) : std::max(minimumDelta, localDelta);
            const auto slope = (valueAt(point.y) - key.value) / localDelta;
            if (!std::isfinite(slope)) {
                return;
            }
            if (drag->mode == DragMode::incoming) {
                key.incomingSlope = slope;
            } else {
                key.outgoingSlope = slope;
            }
        } else {
            const auto originalProjectTime = clip->start + (key.time - clip->offset) / clip->rate;
            const auto delta = (event.position.x - drag->down.x) / plot().getWidth() * (viewEnd - viewStart);
            key.time = clip->localTime(snappedTime(*clip, originalProjectTime + delta, event.mods));
            key.value += (drag->down.y - event.position.y) / plot().getHeight() * (high - low);
            // A drag cannot silently replace a neighbouring key. Double-click adds
            // and explicit deletion remain the ways to change the number of keys.
            const auto* collision = findKey(drag->originalCurve, key.time);
            if (collision != nullptr && collision->time != drag->original.time) {
                return;
            }
        }
        key.value = constrainedValue(*clip, key.value);
        if (!std::isfinite(key.time) || !std::isfinite(key.value)) { return; }
        drag->preview = drag->originalCurve;
        drag->preview.removeKey(drag->original.time);
        drag->preview.setKey(key);
        selectedTime = key.time;
        repaint();
        if (onPreview) {
            onPreview(&drag->preview);
        }
    }

    void mouseUp(const juce::MouseEvent&) override {
        if (!drag.has_value()) {
            return;
        }
        const auto clip = motion::findPropertyTarget(processor.document.project(), targetId);
        const auto* curve = findCurve(clip, propertyName);
        const auto valid = curve != nullptr && dragMatches(*clip, *curve);
        const auto changed = !sameCurve(drag->preview, drag->originalCurve);
        const auto mode = drag->mode;
        auto preview = std::move(drag->preview);
        drag.reset();
        if (valid && changed) {
            const auto id = targetId;
            const auto property = propertyName;
            processor.document.edit(mode == DragMode::key ? "Move animation key" : "Edit cubic tangent", [id, property, preview = std::move(preview)](motion::Project& project) {
                auto* target = mutableCurve(project, id, property);
                if (target != nullptr) {
                    *target = preview;
                }
            });
        }
        if (onPreview) {
            onPreview(nullptr);
        }
        refresh();
    }

    bool keyPressed(const juce::KeyPress& key) override {
        if (!drag.has_value() && (key.getKeyCode() == 'F' || key.getKeyCode() == 'f')) {
            userView = false;
            fit();
            repaint();
            return true;
        }
        if (key.getModifiers().isCommandDown() && (key.getKeyCode() == 'Z' || key.getKeyCode() == 'z') && drag.has_value()) {
            selectedTime = drag->original.time;
            cancelDrag();
            refresh();
            return true;
        }
        if (key == juce::KeyPress::escapeKey && drag.has_value()) {
            selectedTime = drag->original.time;
            cancelDrag();
            refresh();
            return true;
        }
        if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) {
            if (drag.has_value()) {
                selectedTime = drag->original.time;
                cancelDrag();
            }
            return deleteSelected();
        }
        return false;
    }

    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override {
        if (drag.has_value()) {
            return;
        }
        userView = true;
        if (event.mods.isCommandDown()) {
            const auto anchor = projectTime(event.position.x);
            const auto ratio = (anchor - viewStart) / (viewEnd - viewStart);
            const auto duration = processor.document.project().duration;
            const auto maximumSpan = duration >= viewLimit / 2 ? viewLimit * 2 : std::max(1.0, duration * 4.0);
            const auto span = std::clamp((viewEnd - viewStart) * std::exp(-wheel.deltaY * 3.0), 1.0 / std::max(1.0, processor.document.project().frameRate), maximumSpan);
            const auto start = anchor - span * ratio;
            setView(start, start + span);
        } else if (event.mods.isShiftDown() || std::abs(wheel.deltaX) > std::abs(wheel.deltaY)) {
            const auto shift = -(wheel.deltaX + wheel.deltaY) * (viewEnd - viewStart) * 0.2;
            setView(viewStart + shift, viewEnd + shift);
        } else {
            const auto anchor = valueAt(event.position.y);
            const auto ratio = (anchor - low) / (high - low);
            const auto span = std::clamp((high - low) * std::exp(-wheel.deltaY * 3.0), 0.0001, 1.0e9);
            low = anchor - span * ratio;
            high = low + span;
            normalizeValueRange();
        }
        repaint();
    }

private:
    bool userView = false;
    enum class DragMode { key, incoming, outgoing };

    struct Drag {
        motion::Curve originalCurve;
        motion::Curve preview;
        motion::Keyframe original;
        juce::Point<float> down;
        double start, duration, offset, rate;
        DragMode mode = DragMode::key;
        juce::Point<float> handle;
    };

    void cancelDrag() {
        if (drag.has_value()) {
            drag.reset();
            if (onPreview) {
                onPreview(nullptr);
            }
        }
    }

    static const motion::Curve* findCurve(const std::optional<motion::PropertyTarget>& target, const std::string& property) {
        return target.has_value() ? target->curve(property) : nullptr;
    }

    static motion::Curve* mutableCurve(motion::Project& project, motion::Id id, const std::string& property) {
        return motion::findPropertyCurve(project, id, property);
    }

    static const motion::Keyframe* findKey(const motion::Curve& curve, double time) {
        const auto& keys = curve.keyframes();
        const auto found = std::lower_bound(keys.begin(), keys.end(), time, [](const auto& key, double value) { return key.time < value; });
        return found != keys.end() && found->time == time ? &*found : nullptr;
    }

    static bool sameCurve(const motion::Curve& a, const motion::Curve& b) {
        return a.base == b.base && a.modulation == b.modulation && a.keyframes().size() == b.keyframes().size()
            && std::equal(a.keyframes().begin(), a.keyframes().end(), b.keyframes().begin(), [](const auto& x, const auto& y) {
                return x.time == y.time && x.value == y.value && x.interpolation == y.interpolation
                    && x.incomingSlope == y.incomingSlope && x.outgoingSlope == y.outgoingSlope;
            });
    }

    bool dragMatches(const motion::PropertyTarget& clip, const motion::Curve& curve) const {
        return clip.start == drag->start && clip.duration == drag->duration && clip.offset == drag->offset
            && clip.rate == drag->rate && sameCurve(curve, drag->originalCurve);
    }

    juce::Rectangle<float> plot() const {
        return { 62.0f, 34.0f, std::max(1.0f, getWidth() - 82.0f), std::max(1.0f, getHeight() - 78.0f) };
    }
    // Reserve enough headroom for subtracting both viewport endpoints. This
    // bounds only the displayed window, never the authored project or keys.
    static constexpr double viewLimit = std::numeric_limits<double>::max() / 2;
    void setView(double start, double end) {
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
    void normalizeValueRange() {
        if (!std::isfinite(low) || !std::isfinite(high) || !std::isfinite(high - low) || high <= low) {
            low = -1.0;
            high = 1.0;
        }
    }
    float timeX(double time) const {
        auto normalized = (time - viewStart) / (viewEnd - viewStart);
        if (!std::isfinite(normalized)) { normalized = time < viewStart ? -10.0 : 11.0; }
        return plot().getX() + static_cast<float>(std::clamp(normalized, -10.0, 11.0)) * plot().getWidth();
    }
    float valueY(double value) const {
        const auto normalized = (value - low) / (high - low);
        const auto bounded = std::isfinite(normalized) ? std::clamp(normalized, -10.0, 11.0) : 0.0;
        return plot().getBottom() - static_cast<float>(bounded) * plot().getHeight();
    }
    double projectTime(float x) const {
        const auto fraction = static_cast<double>((x - plot().getX()) / plot().getWidth());
        if (!std::isfinite(fraction)) { return viewStart; }
        return std::clamp(std::lerp(viewStart, viewEnd, fraction), -viewLimit, viewLimit);
    }
    double valueAt(float y) const { return low + (plot().getBottom() - y) / plot().getHeight() * (high - low); }
    juce::Point<float> keyPoint(const motion::PropertyTarget& clip, const motion::Keyframe& key) const {
        return { timeX(clip.start + (key.time - clip.offset) / clip.rate), valueY(key.value) };
    }
    std::optional<juce::Point<float>> tangentPoint(const motion::PropertyTarget& clip, const motion::Curve& curve, const motion::Keyframe& key, DragMode mode) const {
        const auto& keys = curve.keyframes();
        const auto found = std::lower_bound(keys.begin(), keys.end(), key.time, [](const auto& item, double time) { return item.time < time; });
        if (found == keys.end()) {
            return std::nullopt;
        }
        double span = 0.0;
        if (mode == DragMode::incoming) {
            if (found == keys.begin() || (found - 1)->interpolation != motion::Interpolation::cubic) {
                return std::nullopt;
            }
            span = (found - 1)->time - key.time;
        } else {
            if (key.interpolation != motion::Interpolation::cubic || found + 1 == keys.end()) {
                return std::nullopt;
            }
            span = (found + 1)->time - key.time;
        }
        // Limit handles to one third of their segment and 48 screen pixels.
        // Slopes remain value per content-local second, including stretched clips.
        const auto slope = mode == DragMode::incoming ? key.incomingSlope : key.outgoingSlope;
        auto delta = span / 3.0;
        const auto xScale = (plot().getWidth() / (viewEnd - viewStart)) / clip.rate;
        const auto yScale = plot().getHeight() / (high - low);
        const auto length = std::hypot(delta * xScale, slope * delta * yScale);
        if (!std::isfinite(length)) { return std::nullopt; }
        if (length > 48.0) {
            delta *= 48.0 / length;
        }
        const auto centre = keyPoint(clip, key);
        return juce::Point<float>(centre.x + static_cast<float>(delta * xScale), centre.y - static_cast<float>(slope * delta * yScale));
    }
    double constrainedValue(const motion::PropertyTarget& target, double value) const {
        if (target.isAudio) { return propertyName == "pan" ? std::clamp(value, -1.0, 1.0) : std::clamp(value, 0.0, 4.0); }
        if (target.isEffect) {
            const auto* effect = motion::findEffect(processor.document.project(), target.id);
            const auto* definition = effect != nullptr ? motion::effectDefinition(effect->type) : nullptr;
            if (definition != nullptr) {
                for (const auto& parameter : definition->parameters) {
                    if (parameter.id == propertyName) { return std::clamp(value, parameter.min, parameter.max); }
                }
            }
        }
        if (!target.isEffect && !target.camera) {
            if (propertyName == "red" || propertyName == "green" || propertyName == "blue") { return std::clamp(value, 0.0, 1.0); }
            if (propertyName == "weight") { return std::clamp(value, 0.0, 1000000.0); }
        }
        return target.camera && propertyName == "fov" ? std::clamp(value, 0.001, 179.999) : value;
    }

    double snappedTime(const motion::PropertyTarget& clip, double time, juce::ModifierKeys modifiers) const {
        if (!modifiers.isAltDown()) {
            time = processor.document.project().timeGrid().snap(time);
        }
        return std::clamp(time, clip.start, clip.end());
    }
    std::optional<double> hitKey(const motion::PropertyTarget& clip, const motion::Curve& curve, juce::Point<float> point) const {
        std::optional<double> hit;
        auto distance = 9.0f;
        for (const auto& key : curve.keyframes()) {
            const auto position = keyPoint(clip, key);
            const auto candidate = position.getDistanceFrom(point);
            if (plot().expanded(5).contains(position) && candidate <= distance) {
                distance = candidate;
                hit = key.time;
            }
        }
        return hit;
    }

    void fit() {
        const auto clip = motion::findPropertyTarget(processor.document.project(), targetId);
        const auto* curve = findCurve(clip, propertyName);
        if (curve == nullptr) {
            return;
        }
        const auto padding = clip->duration * 0.04;
        setView(clip->start - padding, clip->end() + padding);
        low = high = curve->evaluateBase(clip->offset);
        // Include sampled extrema of cubic segments as well as exact key values.
        for (int i = 0; i <= 256; ++i) {
            const auto local = clip->localTime(std::lerp(clip->start, clip->end(), i / 256.0));
            const auto base = curve->evaluateBase(local);
            low = std::min(low, base); high = std::max(high, base);
            const auto value = constrainedValue(*clip, curve->evaluate(local, clip->curveBpm(processor.document.project().bpm)));
            low = std::min(low, value);
            high = std::max(high, value);
        }
        for (const auto& key : curve->keyframes()) {
            if (key.time >= clip->offset && key.time <= clip->localTime(clip->end())) {
                low = std::min(low, key.value);
                high = std::max(high, key.value);
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

    bool deleteSelected() {
        const auto* curve = findCurve(motion::findPropertyTarget(processor.document.project(), targetId), propertyName);
        if (!selectedTime.has_value() || curve == nullptr || findKey(*curve, *selectedTime) == nullptr) {
            return false;
        }
        const auto id = targetId;
        const auto property = propertyName;
        const auto time = *selectedTime;
        processor.document.edit("Delete animation key", [id, property, time](motion::Project& project) {
            auto* target = mutableCurve(project, id, property);
            if (target != nullptr) {
                target->removeKey(time);
            }
        });
        selectedTime.reset();
        refresh();
        return true;
    }

    void showKeyMenu() {
        const auto* curve = findCurve(motion::findPropertyTarget(processor.document.project(), targetId), propertyName);
        const auto* key = curve != nullptr && selectedTime.has_value() ? findKey(*curve, *selectedTime) : nullptr;
        if (key == nullptr) {
            return;
        }
        juce::PopupMenu menu;
        menu.setLookAndFeel(&getLookAndFeel());
        menu.addSectionHeader("Outgoing segment");
        const char* labels[] = { "Hold", "Linear", "Smooth", "Cubic" };
        for (int i = 0; i < 4; ++i) {
            menu.addItem(i + 1, labels[i], true, static_cast<int>(key->interpolation) == i);
        }
        const auto id = targetId;
        const auto property = propertyName;
        const auto original = *key;
        juce::Component::SafePointer<MotionCurveEditor> safe(this);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [safe, id, property, original](int result) {
            if (safe == nullptr || result < 1 || result > 4) {
                return;
            }
            const auto* current = findCurve(motion::findPropertyTarget(safe->processor.document.project(), id), property);
            const auto* currentKey = current != nullptr ? findKey(*current, original.time) : nullptr;
            if (currentKey == nullptr || currentKey->interpolation == static_cast<motion::Interpolation>(result - 1)) {
                return;
            }
            safe->processor.document.edit("Change key interpolation", [id, property, time = original.time, result](motion::Project& project) {
                auto* target = mutableCurve(project, id, property);
                const auto* found = target != nullptr ? findKey(*target, time) : nullptr;
                if (found != nullptr) {
                    auto updated = *found;
                    updated.interpolation = static_cast<motion::Interpolation>(result - 1);
                    target->setKey(updated);
                }
            });
            safe->refresh();
        });
    }

    MotionProcessor& processor;
    motion::Id targetId = 0;
    std::string propertyName;
    std::optional<double> selectedTime;
    std::optional<Drag> drag;
    double viewStart = 0.0, viewEnd = 1.0;
    double low = -1.0, high = 1.0;
};

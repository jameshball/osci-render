#pragma once

#include "../MotionProcessor.h"
#include "EditorCamera.h"
#include "EditorTransformFrame.h"
#include "MotionPath.h"
#include "CompositionGizmo.h"
#include "TransformGizmo.h"
#include "../model/PropertyTarget.h"

class MotionCompositionView : public juce::Component, private juce::Timer {
public:
    explicit MotionCompositionView(MotionProcessor& processor) : processor(processor) {
        setName("Composition preview");
        setWantsKeyboardFocus(true);
    }
    ~MotionCompositionView() override {
        stopTimer();
        cancelGesture();
        if (navigating) { releaseCursor(); }
    }
    motion::Id selected = 0;
    std::function<void(bool)> onNavigationChanged;
    std::function<void(MotionTransformTool)> onToolChanged;
    void setTool(MotionTransformTool value) {
        cancelGesture();
        tool = value;
        hoverHandle = -1;
        dragHint.clear();
        if (onToolChanged) { onToolChanged(tool); }
        repaint();
    }
    std::optional<double> selectedKeyContentTime(motion::Id id) const {
        editingTime();
        return pathKey.has_value() && pathKey->selection == id ? std::optional<double>(pathKey->contentTime) : std::nullopt;
    }
    void retainSelectedKeyAfterEdit() {
        if (pathKey.has_value()) { pathKey->revision = processor.document.revision(); }
    }
    bool isMotionPathVisible() const { return showMotionPath; }
    std::function<void(bool)> onMotionPathChanged;
    void setMotionPathVisible(bool visible) {
        showMotionPath = visible;
        if (onMotionPathChanged) { onMotionPathChanged(visible); }
        repaint();
    }
    struct ViewState { motion::editor::Camera camera; MotionTransformTool tool = MotionTransformTool::move; bool motionPath = false; };
    ViewState viewState() const { return {camera, tool, showMotionPath}; }
    void restoreView(const ViewState& state) {
        setNavigating(false);
        cancelGesture();
        camera = state.camera;
        setTool(state.tool);
        setMotionPathVisible(state.motionPath);
    }
    bool isNavigating() const { return navigating; }
    void setNavigating(bool enabled) {
        if (navigating == enabled) { return; }
        cancelGesture();
        navigating = enabled;
        if (enabled) {
            savedCursor = juce::Desktop::getInstance().getMainMouseSource().getScreenPosition();
            setMouseCursor(juce::MouseCursor::NoCursor);
            grabKeyboardFocus();
            lastTick = juce::Time::getMillisecondCounterHiRes();
            centreCursor();
            startTimerHz(60);
        } else {
            stopTimer();
            releaseCursor();
        }
        if (onNavigationChanged) { onNavigationChanged(enabled); }
        repaint();
    }
    void frameSelection() {
        const auto liveFrames = processor.liveSourcePreview();
        cancelGesture();
        if (prepared == nullptr) { return; }
        const auto time = editingTime();
        const auto hasSelection = std::any_of(prepared->clips.begin(), prepared->clips.end(), [&](const auto& clip) { return clip.editorId() == selected && clip.active(time); });
        motion::editor::Vec3 minimum { 1e12, 1e12, 1e12 }, maximum { -1e12, -1e12, -1e12 };
        bool found = false;
        for (const auto& clip : prepared->clips) {
            if (!clip.active(time) || (hasSelection && clip.editorId() != selected)) { continue; }
            for (int index = 0; index < 512; ++index) {
                const auto sample = clip.sample(time, index / 512.0, 0, 0, liveFrames.get());
                if (sample.r == 0 && sample.g == 0 && sample.b == 0) { continue; }
                const auto point = worldPoint(sample, time);
                if (!point.finite()) { continue; }
                minimum = { std::min(minimum.x, point.x), std::min(minimum.y, point.y), std::min(minimum.z, point.z) };
                maximum = { std::max(maximum.x, point.x), std::max(maximum.y, point.y), std::max(maximum.z, point.z) };
                found = true;
            }
        }
        if (found) { camera.frame((minimum + maximum) * 0.5, std::max(0.05, (maximum - minimum).length() * 0.5)); }
        repaint();
    }
    void resetView() { cancelGesture(); camera = {}; repaint(); }

    std::function<void(motion::Id)> onSelection;
    void refresh() { pathDirty = true; prepared = std::make_unique<motion::PreparedComposition>(processor.document.project(), 48000, nullptr, motion::CompositionPurpose::editorGeometry); repaint(); }
    void preview(const motion::Project& project) {
        pathDirty = true;
        prepared = std::make_unique<motion::PreparedComposition>(project, 48000, nullptr, motion::CompositionPurpose::editorGeometry);
        repaint();
    }

    void paint(juce::Graphics& g) override {
        const auto liveFrames = processor.liveSourcePreview();
        g.fillAll(osci::Colours::veryDark());
        for (int line = -5; line <= 5; ++line) {
            g.setColour(osci::Colours::text().withAlpha(line == 0 ? 0.12f : 0.045f));
            drawWorldLine(g, { static_cast<double>(line), -5, 0 }, { static_cast<double>(line), 5, 0 });
            drawWorldLine(g, { -5, static_cast<double>(line), 0 }, { 5, static_cast<double>(line), 0 });
        }
        g.setColour(osci::Colours::veryDark());
        g.fillRect(getLocalBounds().removeFromBottom(30));
        g.setColour(osci::Colours::textMuted());
        g.setFont(12.0f);
        const auto help = navigating ? "WASD / arrows | Q E up/down | Shift faster | Esc finish"
            : dragHint.isNotEmpty() ? dragHint : tool == MotionTransformTool::scale ? "Drag centre to scale all axes | S scale | Esc cancel"
            : tool == MotionTransformTool::rotate ? "Drag a coloured ring | R rotate | Esc cancel"
            : "Drag arrows to move | G move | Alt-drag orbit";
        g.drawFittedText(help, getLocalBounds().removeFromBottom(30).reduced(10, 0), juce::Justification::centredLeft, 2);
        if (prepared == nullptr || prepared->clips.empty()) {
            g.setColour(osci::Colours::text().withAlpha(0.5f));
            g.setFont(14);
            g.drawText(prepared == nullptr ? juce::String("Preparing...") : emptyMessage(), getLocalBounds(), juce::Justification::centred);
            return;
        }
        juce::Graphics::ScopedSaveState sceneState(g);
        g.reduceClipRegion(getLocalBounds().withTrimmedBottom(30));
        const auto time = editingTime();
        for (const auto& clip : prepared->clips) {
            const auto sampleTime = clip.editorId() == selected && atSelectedPathEnd(time) ? std::nextafter(time, 0.0) : time;
            if (!clip.active(sampleTime)) {
                continue;
            }
            // Walk stored point frames at their native density so short lit
            // runs remain visible in the editing view. Output uses its audio rate.
            const auto* source = clip.resolveSource(liveFrames.get());
            if (source == nullptr) { continue; }
            const auto sampleCount = source->previewSampleCount();
            const auto previewSpan = source->previewPhaseSpan();
            const auto firstPoint = clip.sample(sampleTime, 0, previewSpan, 0, liveFrames.get());
            auto previous = projected(firstPoint, time);
            bool previousLit = firstPoint.r != 0 || firstPoint.g != 0 || firstPoint.b != 0;
            for (std::size_t i = 1; i <= sampleCount; ++i) {
                const auto point = clip.sample(sampleTime, static_cast<double>(i) / sampleCount, previewSpan, 0, liveFrames.get());
                const auto next = projected(point, time);
                const bool lit = point.r != 0 || point.g != 0 || point.b != 0;
                if (!previous.has_value() || !next.has_value() || !previousLit || !lit) {
                    previous = next;
                    previousLit = lit;
                    continue;
                }
                const auto distance = previous->getDistanceFrom(*next);
                const auto alpha = std::min(1.0f, 12.0f / std::max(1.0f, distance));
                const auto colour = clip.editorId() == selected ? juce::Colour(0xff9affb3) : juce::Colour::fromFloatRGBA(point.r, point.g, point.b, 1);
                g.setColour(colour.withAlpha(alpha * 0.8f));
                g.drawLine({ *previous, *next }, clip.editorId() == selected ? 1.4f : 1.0f);
                previous = next;
                previousLit = lit;
            }
        }
        paintMotionPath(g);
        currentGizmo().paint(g, before.has_value() ? dragAxis : hoverHandle);
    }

    void mouseDown(const juce::MouseEvent& event) override {
        const auto liveFrames = processor.liveSourcePreview();
        if (navigating) { setNavigating(false); return; }
        grabKeyboardFocus();
        cancelGesture();
        dragHint.clear();
        if (event.position.y >= getHeight() - 30) { repaint(); return; }
        navigationDrag = event.mods.isAltDown() || event.mods.isMiddleButtonDown();
        panDrag = event.mods.isShiftDown();
        cameraAtDown = camera;
        down = event.position;
        if (navigationDrag) { return; }
        if (!event.mods.isLeftButtonDown() || prepared == nullptr) {
            return;
        }
        if (showMotionPath && seekMotionKey(event.position)) { return; }
        const auto gizmo = currentGizmo();
        const auto handle = gizmo.hitTest(event.position);
        if (handle >= 0) {
            dragAnchor = gizmoFrame->parent.worldOrigin;
            if (beginGesture(editingTime())) {
                dragAxis = handle;
                gesture = tool == MotionTransformTool::move ? (handle == 3 ? Gesture::plane : Gesture::moveAxis)
                    : tool == MotionTransformTool::rotate ? Gesture::rotateAxis
                    : handle == 3 ? Gesture::uniformScale : Gesture::scaleAxis;
                if (handle < 3) {
                    dragDirection = gizmo.axes[handle].direction;
                    if (!gizmo.axes[handle].points.empty()) { scaleScreenAxis = gizmo.axes[handle].points.back() - gizmo.origin; }
                }
                lastRotationPointer = normalized(event.position);
                rotationDelta = 0;
            }
            repaint();
            return;
        }
        float nearest = 18;
        motion::Id hit = 0;
        const auto time = editingTime();
        for (const auto& clip : prepared->clips) {
            if (!clip.active(time)) {
                continue;
            }
            for (int i = 0; i < 256; ++i) {
                const auto sample = clip.sample(time, i / 256.0, 0, 0, liveFrames.get());
                if (sample.r == 0 && sample.g == 0 && sample.b == 0) { continue; }
                const auto point = projected(sample, time);
                if (!point.has_value()) { continue; }
                const auto distance = point->getDistanceFrom(event.position);
                if (distance < nearest) {
                    nearest = distance;
                    hit = clip.editorId();
                    dragAnchor = worldPoint(sample, time);
                }
            }
        }
        selected = hit;
        if (onSelection) {
            onSelection(hit);
        }
        if (hit != 0 && tool == MotionTransformTool::move) {
            gesture = Gesture::plane;
            dragAxis = 3;
            beginGesture(time);
        }
        repaint();
    }

    void mouseDrag(const juce::MouseEvent& event) override {
        if (navigating) { mouseMove(event); return; }
        if (navigationDrag) {
            const auto delta = event.position - down;
            camera = cameraAtDown;
            if (panDrag) { camera.pan(delta.x, delta.y, outputFrame().getHeight()); }
            else { camera.orbit(-delta.x * 0.006, delta.y * 0.006); }
            repaint();
            return;
        }
        if (!validGesture() || !gizmoAtDown.has_value()) { return; }
        if (!dragStarted) {
            if (event.getDistanceFromDragStart() < 3) { return; }
            dragStarted = true;
            processor.playing.store(false);
            if (!pathKey.has_value()) { processor.seek(editTime); }
        }
        std::array<double, 3> offsets { 0, 0, 0 };
        double scaleFactor = 1;
        if (gesture == Gesture::plane || gesture == Gesture::moveAxis) {
            std::optional<motion::editor::Vec3> worldDelta;
            if (gesture == Gesture::plane) {
                worldDelta = cameraAtDown.translationOnFacingPlane(normalized(down), normalized(event.position), dragAnchor);
            } else {
                const auto distance = motion::editor::gizmo::axisDragDistance(cameraAtDown, normalized(down), normalized(event.position), gizmoAtDown->parent.worldOrigin, dragDirection);
                if (distance.has_value()) { worldDelta = dragDirection * *distance; }
            }
            if (!worldDelta.has_value()) { return; }
            const auto delta = gizmoAtDown->parent.positionDelta(*worldDelta);
            if (!delta.has_value()) { return; }
            offsets = { delta->x, delta->y, delta->z };
            // An axis handle edits only its authored position channel, avoiding
            // numerical cross-axis keys after inverse rotated parent transforms.
            if (gesture == Gesture::moveAxis) {
                for (int axis = 0; axis < 3; ++axis) { if (axis != dragAxis) { offsets[axis] = 0; } }
            }
        } else if (gesture == Gesture::rotateAxis) {
            const auto first = cameraAtDown.ray(lastRotationPointer), last = cameraAtDown.ray(normalized(event.position));
            if (!first.has_value() || !last.has_value()) { return; }
            const auto firstLocal = gizmoAtDown->rotationRay(*first, dragAxis), lastLocal = gizmoAtDown->rotationRay(*last, dragAxis);
            if (!firstLocal.has_value() || !lastLocal.has_value()) { return; }
            const std::array<motion::editor::Vec3, 3> axes { motion::editor::Vec3 { 1, 0, 0 }, motion::editor::Vec3 { 0, 1, 0 }, motion::editor::Vec3 { 0, 0, 1 } };
            const auto delta = motion::editor::gizmo::rotationDragAngle(*firstLocal, *lastLocal, {}, axes[dragAxis]);
            if (!delta.has_value()) { return; }
            rotationDelta += *delta;
            lastRotationPointer = normalized(event.position);
            offsets[dragAxis] = rotationDelta * 180 / std::numbers::pi;
        } else {
            const auto screenDelta = event.position - down;
            const auto distance = gesture == Gesture::uniformScale ? screenDelta.x - screenDelta.y
                : (screenDelta.x * scaleScreenAxis.x + screenDelta.y * scaleScreenAxis.y) / scaleScreenAxis.getDistanceFromOrigin();
            const auto factor = motion::editor::gizmo::uniformScaleFactor(distance);
            if (!factor.has_value()) { return; }
            scaleFactor = *factor;
        }
        auto project = *before;
        const auto target = motion::findPropertyTarget(project, editSelection);
        if (!target.has_value()) { return; }
        bool anyChange = false;
        for (int axis = 0; axis < 3; ++axis) {
            const auto scaling = gesture == Gesture::uniformScale || gesture == Gesture::scaleAxis;
            if (scaling && gesture == Gesture::scaleAxis && axis != dragAxis) { continue; }
            const auto prefix = scaling ? "scale." : gesture == Gesture::rotateAxis ? "rotation." : "position.";
            const auto property = std::string(prefix) + "xyz"[axis];
            auto* curve = target->curve(property);
            if (curve == nullptr) { continue; }
            const auto localTime = pathKey.has_value() && pathKey->selection == editSelection ? pathKey->contentTime : target->localTime(editTime);
            const auto base = curve->evaluateBase(localTime);
            // A zero scale can be recovered by dragging; negative scales keep their sign.
            const auto value = scaling ? (base == 0 ? scaleFactor - 1 : base * scaleFactor) : base + offsets[axis];
            if (!std::isfinite(value)) { return; }
            if (std::abs(value - base) <= 1e-10) { continue; }
            if (curve->animated()) { curve->setKeyValue(localTime, value); }
            else { curve->base = value; }
            anyChange = true;
        }
        changed = anyChange;
        processor.document.preview(std::move(project));
        editRevision = processor.document.revision();
        if (pathKey.has_value()) { pathKey->revision = editRevision; }
    }

    void mouseUp(const juce::MouseEvent&) override {
        navigationDrag = false;
        if (validGesture()) {
            if (changed) {
                processor.document.commit(tool == MotionTransformTool::move ? "Move object" : tool == MotionTransformTool::rotate ? "Rotate object" : "Scale object", std::move(*before));
            } else if (dragStarted) {
                processor.document.preview(std::move(*before));
            }
            before.reset();
            if (pathKey.has_value()) { pathKey->revision = processor.document.revision(); }
        }
    }

    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override {
        if (navigating || before.has_value()) { return; }
        camera.dolly(-wheel.deltaY * 2.0);
        repaint();
    }
    void mouseExit(const juce::MouseEvent&) override {
        if (!navigating && hoverHandle != -1) { hoverHandle = -1; repaint(); }
    }
    void mouseMove(const juce::MouseEvent& event) override {
        if (!navigating) {
            const auto hit = event.position.y < getHeight() - 30 ? currentGizmo().hitTest(event.position) : -1;
            if (hit != hoverHandle) { hoverHandle = hit; repaint(); }
            return;
        }
        const auto delta = event.position - getLocalBounds().getCentre().toFloat();
        if (delta.getDistanceFromOrigin() < 0.5f) { return; }
        camera.look(delta.x * 0.003, -delta.y * 0.003);
        centreCursor();
        repaint();
    }
    void focusLost(FocusChangeType) override { cancelGesture(); setNavigating(false); }
    void visibilityChanged() override { if (!isShowing()) { cancelGesture(); setNavigating(false); } }
    bool keyPressed(const juce::KeyPress& key) override {
        if (key == juce::KeyPress::escapeKey) {
            if (navigating) { setNavigating(false); return true; }
            if (validGesture()) { cancelGesture(); return true; }
            if (navigationDrag) { camera = cameraAtDown; navigationDrag = false; repaint(); return true; }
        }
        if (key.getModifiers().isCommandDown() || key.getModifiers().isCtrlDown()) {
            cancelGesture();
            setNavigating(false);
            return false;
        }
        if (navigating) { return true; }
        if (key.getModifiers().isAltDown()) { return false; }
        if (key.getKeyCode() == 'P') { setMotionPathVisible(!showMotionPath); return true; }
        if (key.getKeyCode() == 'G') { setTool(MotionTransformTool::move); return true; }
        if (key.getKeyCode() == 'R') { setTool(MotionTransformTool::rotate); return true; }
        if (key.getKeyCode() == 'S') { setTool(MotionTransformTool::scale); return true; }
        if (key.getKeyCode() == 'F') { frameSelection(); return true; }
        if (key.getKeyCode() == 'N') { setNavigating(true); return true; }
        if (key.getKeyCode() == '0') { resetView(); return true; }
        return false;
    }

private:
    // Why the scene is empty at the playhead, and where the next visual starts.
    juce::String emptyMessage() const {
        const auto& project = processor.document.project();
        const auto now = processor.position.load();
        std::optional<double> next;
        bool any = false, hidden = false;
        const auto soloing = std::any_of(project.tracks.begin(), project.tracks.end(), [](const auto& track) { return track.solo; });
        for (const auto& track : project.tracks) {
            if (track.kind == motion::TrackKind::audio) { continue; }
            const auto silenced = track.muted || (soloing && !track.solo);
            for (const auto& clip : track.clips) {
                any = true;
                const auto timing = clip.timing(project.tempo());
                if (silenced && timing.start <= now && now < timing.end()) { hidden = true; }
                if (!silenced && timing.start > now && (!next.has_value() || timing.start < *next)) { next = timing.start; }
            }
        }
        if (!any) { return "Drop an object here"; }
        if (hidden) { return "Clips here are on muted or un-soloed tracks"; }
        const auto at = "Nothing on screen at " + juce::String(now, 2) + "s";
        return next.has_value() ? at + "  -  next clip at " + juce::String(*next, 2) + "s" : at;
    }
    double editingTime() const {
        if (pathKey.has_value() && (pathKey->selection != selected || pathKey->generation != processor.document.generation()
            || pathKey->scope != processor.document.editingComposition() || pathKey->seekSerial != processor.seekRevision() || pathKey->revision != processor.document.revision() || processor.playing.load())) {
            pathKey.reset();
        }
        return pathKey.has_value() ? pathKey->time : processor.position.load();
    }
    bool atSelectedPathEnd(double time) const {
        if (!pathKey.has_value() || pathKey->selection != selected || pathKey->time != time) { return false; }
        for (const auto& track : processor.document.project().tracks) {
            for (const auto& clip : track.clips) {
                if (clip.id == selected) { return clip.timing(processor.document.project().tempo()).end() == time; }
            }
        }
        return false;
    }
    void updateMotionPath() {
        if (!pathDirty && pathSelection == selected && pathRevision == processor.document.revision()) { return; }
        motionPath = motion::editor::buildMotionPath(processor.document.project(), selected);
        pathDirty = false;
        pathSelection = selected;
        pathRevision = processor.document.revision();
    }
    void paintMotionPath(juce::Graphics& g) {
        if (!showMotionPath || navigating || selected == 0) { return; }
        updateMotionPath();
        const auto colour = juce::Colour(0xffb8d4ea);
        std::optional<juce::Point<float>> previous;
        for (const auto& point : motionPath.points) {
            const auto screen = screenPoint(point.position);
            if (screen.has_value()) {
                if (previous.has_value() && !point.breakBefore) {
                    g.setColour(colour.withAlpha(0.35f));
                    g.drawLine({*previous, *screen}, 1);
                }
                if (point.dot) {
                    g.setColour(colour.withAlpha(0.65f));
                    g.fillEllipse(screen->x - 1.5f, screen->y - 1.5f, 3, 3);
                }
            }
            previous = screen;
        }
        for (const auto& point : motionPath.points) {
            if (!point.key) { continue; }
            const auto screen = screenPoint(point.position);
            if (!screen.has_value()) { continue; }
            const juce::Rectangle<float> bounds(screen->x - 4, screen->y - 4, 8, 8);
            g.setColour(osci::Colours::veryDark());
            g.fillRect(bounds);
            g.setColour(colour);
            g.drawRect(bounds, 1.5f);
        }
        g.setFont(juce::FontOptions(11));
        g.setColour(osci::Colours::textMuted());
        g.drawFittedText(motionPath.tooComplex ? "Path hidden: more than 2,048 transform keys"
            : "Position path (before effects) | Click a key to seek", getLocalBounds().removeFromTop(26).reduced(10, 0), juce::Justification::centredLeft, 2);
    }
    bool seekMotionKey(juce::Point<float> position) {
        updateMotionPath();
        const motion::editor::MotionPathPoint* closest = nullptr;
        float distance = 9;
        for (const auto& point : motionPath.points) {
            if (!point.key || point.time == editingTime()) { continue; }
            const auto screen = screenPoint(point.position);
            if (screen.has_value() && screen->getDistanceFrom(position) < distance) {
                closest = &point;
                distance = screen->getDistanceFrom(position);
            }
        }
        if (closest == nullptr) { return false; }
        const auto time = closest->time;
        const auto rate = std::max(1.0, processor.getSampleRate());
        const auto atEnd = !motionPath.points.empty() && time == motionPath.points.back().time;
        processor.playing.store(false);
        processor.seek(atEnd ? std::max(motionPath.points.front().time, time - 1.0 / rate) : time);
        pathKey = PathKey {selected, processor.document.generation(), processor.document.editingComposition(), processor.seekRevision(), processor.document.revision(), time, closest->contentTime};
        repaint();
        return true;
    }
    enum class Gesture { plane, moveAxis, rotateAxis, scaleAxis, uniformScale };
    bool dragStarted = false;
    bool editable(double time) const {
        const auto& project = processor.document.project();
        for (const auto& track : project.tracks) {
            for (const auto& clip : track.clips) {
                if (clip.id != selected) { continue; }
                if (track.locked || !motion::trackIsAudible(project, track) || (!clip.contains(time, project.tempo()) && !atSelectedPathEnd(time))) { return false; }
                const auto prefix = tool == MotionTransformTool::move ? "position." : tool == MotionTransformTool::rotate ? "rotation." : "scale.";
                for (const auto axis : std::string("xyz")) {
                    const auto found = clip.properties.find(std::string(prefix) + axis);
                    if (found != clip.properties.end() && found->second.modulation.enabled) { return false; }
                }
                return true;
            }
        }
        return false;
    }
    MotionCompositionGizmo currentGizmo() const {
        const auto time = editingTime();
        gizmoFrame = motion::editor::gizmoFrameForClip(processor.document.project(), selected, time);
        if (navigating || !editable(time) || !gizmoFrame.has_value() || gizmoFrame->parent.hasPostTransformEffects) { return {}; }
        return MotionCompositionGizmo::layout(*gizmoFrame, camera, tool, std::clamp(getWidth() * 0.2, 35.0, 72.0), outputFrame().getHeight(),
            [this](motion::editor::Vec3 point) { return screenPoint(point); });
    }
    bool beginGesture(double time) {
        for (const auto& track : processor.document.project().tracks) {
            if (track.locked && std::any_of(track.clips.begin(), track.clips.end(), [this](const auto& clip) { return clip.id == selected; })) {
                dragHint = "This object's track is locked";
                return false;
            }
        }
        gizmoAtDown = motion::editor::gizmoFrameForClip(processor.document.project(), selected, time);
        if (!editable(time) || !gizmoAtDown.has_value() || gizmoAtDown->parent.hasPostTransformEffects) {
            dragHint = "Use the inspector for modulated or effected results";
            return false;
        }
        // A plain click only selects: playback stops and the document is
        // touched only once the pointer actually drags.
        editTime = time;
        editSelection = selected;
        before = processor.document.project();
        editRevision = processor.document.revision();
        changed = false;
        dragStarted = false;
        return true;
    }
    void timerCallback() override {
        if (!navigating || !hasKeyboardFocus(true) || !isShowing() || !juce::Process::isForegroundProcess()) {
            setNavigating(false);
            return;
        }
        const auto now = juce::Time::getMillisecondCounterHiRes();
        const auto seconds = std::clamp((now - lastTick) / 1000.0, 0.0, 0.05);
        lastTick = now;
        const auto down = [](int letter, int arrow = 0) { return juce::KeyPress::isKeyCurrentlyDown(letter) || (arrow != 0 && juce::KeyPress::isKeyCurrentlyDown(arrow)); };
        const motion::editor::Vec3 direction { static_cast<double>(down('D', juce::KeyPress::rightKey) - down('A', juce::KeyPress::leftKey)),
            static_cast<double>(down('E') - down('Q')), static_cast<double>(down('W', juce::KeyPress::upKey) - down('S', juce::KeyPress::downKey)) };
        const auto modifiers = juce::ModifierKeys::getCurrentModifiersRealtime();
        if (modifiers.isCommandDown() || modifiers.isCtrlDown() || modifiers.isAltDown()) { return; }
        const auto fast = modifiers.isShiftDown();
        camera.fly(direction, std::clamp(camera.distance() * 0.5, 0.01, 1e6) * (fast ? 3 : 1), seconds);
        repaint();
    }
    void centreCursor() { juce::Desktop::getInstance().getMainMouseSource().setScreenPosition(localPointToGlobal(getLocalBounds().getCentre().toFloat())); }
    void releaseCursor() {
        setMouseCursor(juce::MouseCursor::NormalCursor);
        juce::Desktop::getInstance().getMainMouseSource().setScreenPosition(savedCursor);
    }
    void cancelGesture() {
        if (validGesture()) {
            processor.document.preview(std::move(*before));
            before.reset();
            if (pathKey.has_value()) { pathKey->revision = processor.document.revision(); }
        }
        navigationDrag = false;
    }
    bool validGesture() {
        if (before.has_value() && editRevision != processor.document.revision()) {
            before.reset();
        }
        return before.has_value();
    }
    juce::Rectangle<float> outputFrame() const {
        const auto size = std::max(10, std::min(getWidth(), getHeight()) - 48);
        return getLocalBounds().toFloat().withSizeKeepingCentre(size, size);
    }
    motion::editor::Vec2 normalized(juce::Point<float> point) const {
        const auto frame = outputFrame();
        return { (point.x - frame.getCentreX()) * 2 / frame.getWidth(), (frame.getCentreY() - point.y) * 2 / frame.getHeight() };
    }
    motion::editor::Vec3 worldPoint(osci::Point point, double time) const {
        if (prepared != nullptr) { point = prepared->applyCompositionEffects(point, time); }
        return { point.x, point.y, point.z };
    }
    std::optional<juce::Point<float>> screenPoint(motion::editor::Vec3 point) const {
        const auto projected = camera.project(point);
        if (!projected.has_value() || std::abs(projected->x) > 1000 || std::abs(projected->y) > 1000) { return std::nullopt; }
        const auto frame = outputFrame();
        return juce::Point<float> { static_cast<float>(frame.getCentreX() + projected->x * frame.getWidth() / 2),
            static_cast<float>(frame.getCentreY() - projected->y * frame.getHeight() / 2) };
    }
    std::optional<juce::Point<float>> projected(osci::Point point, double time) const { return screenPoint(worldPoint(point, time)); }
    void drawWorldLine(juce::Graphics& g, motion::editor::Vec3 start, motion::editor::Vec3 end) const {
        const auto first = screenPoint(start), last = screenPoint(end);
        if (first.has_value() && last.has_value()) { g.drawLine({ *first, *last }, 1); }
    }
    struct PathKey {
        motion::Id selection;
        std::uint64_t generation;
        motion::Id scope;
        std::uint64_t seekSerial, revision;
        double time, contentTime;
    };
    mutable std::optional<PathKey> pathKey;
    bool showMotionPath = false, pathDirty = true;
    motion::Id pathSelection = 0;
    std::uint64_t pathRevision = 0;
    motion::editor::MotionPath motionPath;
    MotionProcessor& processor;
    std::unique_ptr<motion::PreparedComposition> prepared;
    std::optional<motion::Project> before;
    juce::Point<float> down;
    motion::editor::Camera camera, cameraAtDown;
    motion::editor::Vec3 dragAnchor;
    mutable std::optional<motion::editor::EulerGizmoFrame> gizmoFrame;
    std::optional<motion::editor::EulerGizmoFrame> gizmoAtDown;
    MotionTransformTool tool = MotionTransformTool::move;
    Gesture gesture = Gesture::plane;
    int dragAxis = 3, hoverHandle = -1;
    motion::editor::Vec3 dragDirection;
    motion::editor::Vec2 lastRotationPointer;
    juce::Point<float> scaleScreenAxis;
    double rotationDelta = 0;
    juce::Point<float> savedCursor;
    juce::String dragHint;
    bool navigating = false, navigationDrag = false, panDrag = false;
    double lastTick = 0;
    bool changed = false;
    motion::Id editSelection = 0;
    double editTime = 0;
    std::uint64_t editRevision = 0;
};

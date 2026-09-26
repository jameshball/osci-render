#pragma once

#include "../MotionProcessor.h"
#include "EditorCamera.h"
#include "EditorTransformFrame.h"
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
        cancelGesture();
        if (prepared == nullptr) { return; }
        const auto time = processor.position.load();
        const auto hasSelection = std::any_of(prepared->clips.begin(), prepared->clips.end(), [&](const auto& clip) { return clip.id == selected && clip.active(time); });
        motion::editor::Vec3 minimum { 1e12, 1e12, 1e12 }, maximum { -1e12, -1e12, -1e12 };
        bool found = false;
        for (const auto& clip : prepared->clips) {
            if (!clip.active(time) || (hasSelection && clip.id != selected)) { continue; }
            for (int index = 0; index < 512; ++index) {
                const auto point = worldPoint(clip.sample(time, index / 512.0), time);
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
    void refresh() { prepared = std::make_unique<motion::PreparedComposition>(processor.document.project()); repaint(); }
    void preview(const motion::Project& project) {
        prepared = std::make_unique<motion::PreparedComposition>(project);
        repaint();
    }

    void paint(juce::Graphics& g) override {
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
            g.drawText(processor.document.project().tracks.empty() ? "Drop an object here" : "No visible objects", getLocalBounds(), juce::Justification::centred);
            return;
        }
        juce::Graphics::ScopedSaveState sceneState(g);
        g.reduceClipRegion(getLocalBounds().withTrimmedBottom(30));
        const auto time = processor.position.load();
        for (const auto& clip : prepared->clips) {
            if (!clip.active(time)) {
                continue;
            }
            auto previous = projected(clip.sample(time, 0), time);
            for (int i = 1; i <= 512; ++i) {
                const auto point = clip.sample(time, static_cast<double>(i) / 512);
                const auto next = projected(point, time);
                if (!previous.has_value() || !next.has_value()) { previous = next; continue; }
                const auto distance = previous->getDistanceFrom(*next);
                const auto alpha = std::min(1.0f, 12.0f / std::max(1.0f, distance));
                const auto colour = clip.id == selected ? juce::Colour(0xff9affb3) : juce::Colour::fromFloatRGBA(point.r, point.g, point.b, 1);
                g.setColour(colour.withAlpha(alpha * 0.8f));
                g.drawLine({ *previous, *next }, clip.id == selected ? 1.4f : 1.0f);
                previous = next;
            }
        }
        currentGizmo().paint(g, before.has_value() ? dragAxis : hoverHandle);
    }

    void mouseDown(const juce::MouseEvent& event) override {
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
        const auto gizmo = currentGizmo();
        const auto handle = gizmo.hitTest(event.position);
        if (handle >= 0) {
            dragAnchor = gizmoFrame->parent.worldOrigin;
            if (beginGesture(processor.position.load())) {
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
        const auto time = processor.position.load();
        for (const auto& clip : prepared->clips) {
            if (!clip.active(time)) {
                continue;
            }
            for (int i = 0; i < 256; ++i) {
                const auto point = projected(clip.sample(time, i / 256.0), time);
                if (!point.has_value()) { continue; }
                const auto distance = point->getDistanceFrom(event.position);
                if (distance < nearest) {
                    nearest = distance;
                    hit = clip.id;
                    dragAnchor = worldPoint(clip.sample(time, i / 256.0), time);
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
            const auto localTime = target->localTime(editTime);
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
    }

    void mouseUp(const juce::MouseEvent&) override {
        navigationDrag = false;
        if (validGesture()) {
            if (changed) {
                processor.document.commit(tool == MotionTransformTool::move ? "Move object" : tool == MotionTransformTool::rotate ? "Rotate object" : "Scale object", std::move(*before));
            } else {
                processor.document.preview(std::move(*before));
            }
            before.reset();
        }
    }

    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override {
        if (navigating || before.has_value()) { return; }
        camera.dolly(-wheel.deltaY * 2.0);
        repaint();
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
        if (key.getKeyCode() == 'G') { setTool(MotionTransformTool::move); return true; }
        if (key.getKeyCode() == 'R') { setTool(MotionTransformTool::rotate); return true; }
        if (key.getKeyCode() == 'S') { setTool(MotionTransformTool::scale); return true; }
        if (key.getKeyCode() == 'F') { frameSelection(); return true; }
        if (key.getKeyCode() == 'N') { setNavigating(true); return true; }
        if (key.getKeyCode() == '0') { resetView(); return true; }
        return false;
    }

private:
    enum class Gesture { plane, moveAxis, rotateAxis, scaleAxis, uniformScale };
    bool editable(double time) const {
        const auto& project = processor.document.project();
        for (const auto& track : project.tracks) {
            for (const auto& clip : track.clips) {
                if (clip.id != selected) { continue; }
                if (track.locked || !motion::trackIsAudible(project, track) || !clip.contains(time)) { return false; }
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
        const auto time = processor.position.load();
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
        processor.playing.store(false);
        processor.seek(time);
        editTime = time;
        editSelection = selected;
        before = processor.document.project();
        editRevision = processor.document.revision();
        changed = false;
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
        if (validGesture()) { processor.document.preview(std::move(*before)); before.reset(); }
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

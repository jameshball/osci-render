#pragma once

#include "MotionStyle.h"
#include "PreviewGesture.h"

#include "../MotionProcessor.h"
#include "EditorCamera.h"
#include "EditorTransformFrame.h"
#include "MotionPath.h"
#include "CompositionGizmo.h"
#include "TransformGizmo.h"
#include "LineBatch.h"
#include "../model/PropertyTarget.h"

class MotionCompositionView : public juce::Component, public juce::DragAndDropTarget, private juce::Timer {
public:
    explicit MotionCompositionView(MotionProcessor& processor);
    ~MotionCompositionView() override;
    // The selection the editor shows everywhere: highlighted here, with its gizmo.
    void setSelection(motion::Id id) {
        if (selected != id) {
            selected = id;
            repaint();
        }
    }
    std::function<void(bool)> onNavigationChanged;
    std::function<void()> onContextMenu;
    std::function<void(motion::Id, const std::string&)> onPropertyEdited;
    std::function<bool(motion::Id)> isSelected;
    std::function<void(MotionTransformTool)> onToolChanged;
    void setTool(MotionTransformTool value);
    std::optional<double> selectedKeyContentTime(motion::Id id) const;
    void retainSelectedKeyAfterEdit() {
        if (pathKey.has_value()) { pathKey->revision = processor.document.revision(); }
    }
    bool isMotionPathVisible() const { return showMotionPath; }
    std::function<void(bool)> onMotionPathChanged;
    void setMotionPathVisible(bool visible);
    struct ViewState { motion::editor::Camera camera; MotionTransformTool tool = MotionTransformTool::move; bool motionPath = false; };
    ViewState viewState() const { return {camera, tool, showMotionPath}; }
    void restoreView(const ViewState& state);
    bool isNavigating() const { return navigating; }
    void setNavigating(bool enabled);
    void frameSelection();
    void resetView() { cancelGesture(); camera = {}; repaint(); }

    // Looking through an output camera: the Scene shows exactly its view, and
    // orbiting, panning, zooming or flying moves the camera itself (keying the
    // whole camera at the playhead once it has keys). Rigged cameras cannot be
    // driven; driveBlocker says why.
    bool canDriveCamera(motion::Id id) const { return documentPose(id).has_value(); }
    juce::String driveBlocker(motion::Id id) const;
    // A key on every camera property at the playhead (linked ones follow
    // their source and are left alone).
    bool cameraKeyed(motion::Id id) const;
    // Keys the whole camera (position, rotation and lens) at the playhead, or
    // removes that key when it is already there.
    void toggleCameraKey(motion::Id id);
    motion::Id drivenCamera() const { return lockedCamera; }
    void setDrivenCamera(motion::Id id);
    std::function<void(motion::Id)> onDrivenCameraChanged;
    // Blender's numpad views: look along an axis at the current pivot and
    // distance.
    enum class ViewPreset { front, back, right, left, top, bottom };
    void setViewPreset(ViewPreset preset);

    std::function<void(motion::Id)> onSelection;
    void refresh() { pathDirty = true; prepared = std::make_unique<motion::PreparedComposition>(processor.document.project(), 48000, nullptr, motion::CompositionPurpose::editorGeometry); repaint(); }
    void preview(const motion::Project& project);

    void paint(juce::Graphics& g) override;
    // Output cameras as small pyramids, Blender style. The camera on the
    // output also outlines its frame where the scene's origin sits, so what
    // is in shot is clear before looking at the Scope.
    void paintCameras(juce::Graphics& g, double time) const;

    // Double-clicking an object opens what made it: the text, Lua or drawing
    // editor, or a nested composition.
    std::function<void(motion::Id)> onOpenSource;
    void mouseDoubleClick(const juce::MouseEvent& event) override;
    void mouseDown(const juce::MouseEvent& event) override;

    void mouseDrag(const juce::MouseEvent& event) override;

    void mouseUp(const juce::MouseEvent&) override;

    // The visible clip drawn nearest the point (within 18 px), and the world
    // point there.
    motion::Id pickAt(juce::Point<float> position, motion::Vec3* anchor) const;

    // Context hints only while flying or dragging (the tool strip's tooltips
    // cover the rest), like Blender's status hints, drawn over the scene.
    // Effects dropped on an object apply to it; anywhere else, to everything.
    std::function<void(const std::string&, std::optional<motion::Id>)> onEffectPreview;
    std::function<void(const std::string&, motion::Id)> onEffectDropped;
    bool isInterestedInDragSource(const SourceDetails& details) override { return details.description.toString().startsWith("motion-effect:"); }
    void itemDragMove(const SourceDetails& details) override;
    void itemDragExit(const SourceDetails& details) override;
    void itemDropped(const SourceDetails& details) override;
    void setEffectDragActive(bool active);

    void paintOverChildren(juce::Graphics& g) override;
    // Blender's conventions: a mouse wheel zooms; on a trackpad two fingers
    // orbit, Shift pans and Cmd/Ctrl zooms; a pinch zooms.
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;
    void mouseMagnify(const juce::MouseEvent&, float scale) override;
    void mouseExit(const juce::MouseEvent&) override {
        if (!navigating && hoverHandle != -1) { hoverHandle = -1; repaint(); }
    }
    void mouseMove(const juce::MouseEvent& event) override;
    void focusLost(FocusChangeType) override { cancelGesture(); setNavigating(false); }
    void visibilityChanged() override { if (!isShowing()) { cancelGesture(); setNavigating(false); } }
    bool keyPressed(const juce::KeyPress& key) override;

private:
    motion::Id selected = 0;
    // Why the scene is empty at the playhead, and where the next visual starts.
    juce::String emptyMessage() const;
    double editingTime() const;
    bool atSelectedPathEnd(double time) const;
    void updateMotionPath();
    void paintMotionPath(juce::Graphics& g);
    bool seekMotionKey(juce::Point<float> position);
    enum class Gesture { plane, moveAxis, rotateAxis, scaleAxis, uniformScale };
    bool dragStarted = false;
    bool editable(double time) const;
    MotionCompositionGizmo currentGizmo() const;
    bool beginGesture(double time);
    void timerCallback() override;
    void centreCursor() { juce::Desktop::getInstance().getMainMouseSource().setScreenPosition(localPointToGlobal(getLocalBounds().getCentre().toFloat())); }
    void releaseCursor();
    void cancelGesture();
    bool validGesture();
    juce::Rectangle<float> outputFrame() const;
    motion::editor::Vec2 normalized(juce::Point<float> point) const;
    motion::Vec3 worldPoint(osci::Point point, double time) const;
    using CameraView = std::optional<motion::editor::Camera::View>;
    // Loops over many points take the camera's view once.
    std::optional<juce::Point<float>> screenPoint(const CameraView& view, motion::Vec3 point) const;
    std::optional<juce::Point<float>> screenPoint(motion::Vec3 point) const { return screenPoint(camera.view(), point); }
    std::optional<juce::Point<float>> projected(const CameraView& view, osci::Point point, double time) const { return screenPoint(view, worldPoint(point, time)); }
    void drawWorldLine(juce::Graphics& g, motion::Vec3 start, motion::Vec3 end) const;
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
    // The preview's segments, stroked together; the selection's draw on top.
    motion::LineBatch lines, highlightedLines;
    std::optional<motion::Id> dropHover;
    bool effectDrag = false;
    motion::ui::PreviewGesture edit {processor.document};
    juce::Point<float> down;
    motion::editor::Camera camera, cameraAtDown;
    struct Pose {
        double x = 0, y = 0, z = 0, pitch = 0, yaw = 0, fov = 0;
        bool operator==(const Pose&) const = default;
    };
    motion::Id lockedCamera = 0;
    std::optional<Pose> lastPose;
    std::optional<motion::editor::Camera> viewBeforeLock;
    struct Sync final : juce::Timer {
        std::function<void()> tick;
        void timerCallback() override { if (tick) { tick(); } }
    } cameraSync;
    // Zero throughout: no roll at any key or between them.
    static bool level(const motion::Curve& curve);
    double cameraTime() const { return processor.document.project().frameTime(processor.position.load()); }
    // A free camera's pose at the playhead (rotation X as pitch, -Y as yaw).
    std::optional<Pose> documentPose(motion::Id id) const;
    Pose viewPose() const {
        return {camera.position.x, camera.position.y, camera.position.z, camera.pitch * 180 / std::numbers::pi, camera.yaw * 180 / std::numbers::pi, camera.fovDegrees};
    }
    // The view moved: write it to the camera. The document moved (playback,
    // undo, the inspector): move the view.
    void syncCamera();
    motion::Vec3 dragAnchor;
    std::string editedProperty;
    mutable std::optional<motion::editor::EulerGizmoFrame> gizmoFrame;
    std::optional<motion::editor::EulerGizmoFrame> gizmoAtDown;
    MotionTransformTool tool = MotionTransformTool::move;
    Gesture gesture = Gesture::plane;
    int dragAxis = 3, hoverHandle = -1;
    motion::Vec3 dragDirection;
    motion::editor::Vec2 lastRotationPointer;
    juce::Point<float> scaleScreenAxis;
    double rotationDelta = 0;
    juce::Point<float> savedCursor;
    juce::String dragHint;
    bool navigating = false, navigationDrag = false, panDrag = false;
    double lastTick = 0;
    motion::Id editSelection = 0;
    double editTime = 0;
};

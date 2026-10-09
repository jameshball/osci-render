#pragma once

#include "MotionStyle.h"
#include "MotionIcons.h"
#include "PreviewGesture.h"

#include "../MotionProcessor.h"
#include "EditorCamera.h"
#include "EditorTransformFrame.h"
#include "MotionPath.h"
#include "CompositionGizmo.h"
#include "TransformGizmo.h"
#include "../model/PropertyTarget.h"
#include <map>
#include <set>

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
    // Dragging over empty space selects the objects it touches (Shift adds).
    // Shift-clicking an object toggles it; a Shift drag adds.
    enum class SelectionChange { replace, add, toggle };
    std::function<void(const std::vector<motion::Id>&, SelectionChange)> onSelectClips;

    // Part mode picks the exact shapes inside still vector sources (text,
    // SVG, OBJ, drawings): click or drag over them, double-click for a whole
    // path, Shift to add. Extract splits them off as an object of their own.
    bool inPartMode() const { return partMode; }
    void setPartMode(bool enabled);
    std::function<void(bool)> onPartModeChanged;
    // The picked shapes of each clip, with the source they index.
    struct Picked { std::shared_ptr<const motion::PreparedSource> source; std::set<std::size_t> shapes; };
    std::function<void(const std::map<motion::Id, Picked>&)> onExtractParts;
    std::function<void(const juce::String&)> onStatus;
    std::size_t pickedPartCount() const;
    // Box picks the parts wholly inside; lasso those inside a drawn loop;
    // pieces picks whole separate pieces (a letter, an outline) that a click
    // or a box touches.
    enum class PartPick { box, lasso, pieces };
    void setPartPick(PartPick mode) {
        partPick = mode;
        hoverShapes.clear();
        refreshPartBar();
    }
    void extractPicked();
    void resized() override;
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
    void refresh() { pathDirty = true; prepared = std::make_unique<motion::PreparedComposition>(processor.document.project(), 48000, nullptr, motion::CompositionPurpose::editorGeometry); ++preparedSerial; prunePicked(); repaint(); }
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
        if (!navigating && (hoverHandle != -1 || hoverPart.has_value() || hoverAxis.has_value() || hoverCorner)) {
            hoverHandle = -1;
            hoverPart.reset();
            hoverShapes.clear();
            hoverAxis.reset();
            hoverCorner = false;
            repaint();
        }
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
    // A world line on screen, cut at the near plane and well outside the
    // view, so lines through or behind the eye still draw their visible part.
    std::optional<juce::Line<float>> screenLine(const CameraView& view, motion::Vec3 start, motion::Vec3 end) const;
    // One lit piece of a clip in world space; `shape` is the exact shape it
    // belongs to, or -1 for a traced source.
    struct Piece { motion::Vec3 a, b; juce::Colour colour; int shape; };
    // What the Scene shows, on screen: drawn by paint and hit-tested by
    // picking, worked out again only when the view, time or content change.
    struct ScreenPiece { juce::Line<float> line; motion::Vec3 a, b; juce::Colour colour; const motion::PreparedClip* clip; int shape; float depth; };
    struct PiecesKey {
        std::uint64_t prepared;
        const motion::LiveSourceFrames* live;
        double time, x, y, z, yaw, pitch, fov;
        motion::Id selected;
        int width, height;
        bool operator==(const PiecesKey&) const = default;
    };
    const std::vector<ScreenPiece>& screenPieces() const;
    mutable std::vector<ScreenPiece> pieceCache;
    mutable std::optional<PiecesKey> pieceKey;
    mutable std::shared_ptr<const motion::LiveSourceFrames> pieceLive;
    std::uint64_t preparedSerial = 0;
    // A clip's geometry: a vector source's exact shapes (lines straight,
    // curves divided finely enough to look smooth), otherwise its traced beam.
    void forEachPiece(const motion::PreparedClip& clip, double time, const CameraView& view, const motion::LiveSourceFrames* live, const std::function<void(const Piece&)>& visit) const;
    double clipTime(const motion::PreparedClip& clip, double time) const;
    float depthFade(double depth) const;
    // Effects anywhere above the clip may bend straight lines.
    bool warps(const motion::PreparedClip& clip) const;
    // The shapes a clip's parts are picked from: a still vector source on
    // this timeline; null otherwise.
    const motion::PreparedDrawing* partDrawing(const motion::PreparedClip& clip) const;
    juce::String partBlocker(motion::Id clip) const;
    struct PartHit { motion::Id clip; std::size_t shape; };
    std::optional<PartHit> pickPart(juce::Point<float> position) const;
    // Picks the parts inside `area` (all of each shape), or with `touching`
    // the whole pieces it touches.
    void pickParts(const juce::Path& area, bool add, bool touching);
    void pickAllParts();
    void resetMarquee();
    motion::Vec3 nearestOnSegment(juce::Point<float> position, motion::Vec3 a, motion::Vec3 b) const;
    void pickPath(PartHit hit, bool add);
    void clickPart(PartHit part, bool adding);
    std::set<std::size_t> pickedBy(PartHit part) const;
    std::set<std::size_t> hoverShapes;
    juce::Rectangle<int> partBar() const;
    static constexpr int barTextInset = 12, extractWidth = 72;
    std::size_t pickedPieceCount() const;
    bool anythingPickable() const;
    bool lookingAlong(ViewPreset preset) const;
    static ViewPreset opposite(ViewPreset preset);
    std::optional<PartHit> pressedPart;
    void prunePicked();
    void refreshPartBar();
    std::vector<motion::Id> clipsIn(juce::Rectangle<float> area) const;
    bool partMode = false;
    std::map<motion::Id, Picked> picked;
    std::optional<PartHit> hoverPart;
    std::optional<juce::Rectangle<float>> marquee;
    PartPick partPick = PartPick::box;
    juce::Path lasso;
    bool marqueeArmed = false, marqueeAdds = false;
    juce::Label partCount;
    juce::TextButton extractButton {"Extract"};
    // The view's axes in the top right, Blender style: click one to look along it.
    // Its hover disc sits 8 px in from the top right, as the tool strip does at the top left.
    juce::Point<float> orientationCentre() const { return {static_cast<float>(getWidth()) - 48.0f, 48.0f}; }
    std::optional<ViewPreset> orientationHit(juce::Point<float> position) const;
    void paintOrientation(juce::Graphics& g) const;
    std::optional<ViewPreset> hoverAxis;
    bool hoverCorner = false;
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
    osci::LineBatch lines, highlightedLines, hoveredLines;
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

#pragma once

#include "../MotionProcessor.h"
#include "ViewNavigation.h"
#include "TrackHeader.h"
#include "TrackLayout.h"
#include <optional>
#include <limits>
#include <set>
#include "../model/CompositionGraph.h"
#include "../model/PropertySchema.h"
#include "../model/KeyEasing.h"
#include "MotionIcons.h"
#include "DocumentMenu.h"

class MotionTimelineView : public juce::Component, public juce::DragAndDropTarget, public juce::SettableTooltipClient {
    struct Row : motion::TrackRow {
        std::string lane; // Property id for a keyframe lane under its track.
        bool isLane() const { return !lane.empty(); }
        bool group() const { return track < 0 && lane.empty(); }
    };
    struct KeyRef {
        motion::Id clip = 0;
        std::string property;
        double time = 0; // Content-local key time.
    };
public:
    explicit MotionTimelineView(MotionProcessor& ownerProcessor);
    // Runs one of the editor's commands by name (Cut, Copy, Paste...).
    std::function<void(const juce::String&)> onCommand;
    // Add a camera that takes over the output at the playhead.
    std::function<void(double)> onAddCamera;
    std::function<void(motion::Id)> onSelection, onTimingRequested, onMakeUnique, onEnterComposition, onRevealSource;
    std::function<void()> onLoopSelection;
    std::function<void(const juce::String&)> onError;
    std::function<void(motion::Id, double)> onEditMarker;
    // (beat, current bpm, beat of the change being edited, if any)
    std::function<void(double, double, std::optional<double>)> onEditTempo;
    // The tempo change at (or nearest a beat to) `seconds`, as the ruler's menu edits it.
    void editTempoAt(double seconds) { editTempoChange(seconds); }
    // A point on the ruler at `seconds` for a popover to aim at, kept within
    // the visible lanes.
    juce::Rectangle<int> rulerAnchor(double seconds) const;
    std::function<void(motion::Id, motion::Id)> onEffectAdded;
    void setSelection(motion::Id id);
    struct ViewState {
        double zoom = 70, scroll = 0;
        int scrollY = 0;
        motion::Id primary = 0;
        std::set<motion::Id> selected, collapsed, expanded;
    };
    ViewState viewState() const;
    void restoreView(const ViewState& state);
    std::function<void(int)> onDefaultTrackHeight;

    // Selection may originate in the preview, library or an import, not only
    // from a row already on screen. Keep its layer reachable in a dense project.
    void revealSelection();

    bool hasSelectedKeys() const { return !selectedKeys.empty(); }
    bool hasSelectedClips() const { return !selectedClips.empty(); }
    std::vector<motion::Document::CopiedKey> copiedKeys() const;
    std::vector<motion::Document::CopiedClip> copiedClips() const;
    void deleteSelection();
    const std::set<motion::Id>& selectedClipIds() const { return selectedClips; }
    // Where a dropped file lands: the snapped time under the pointer and the
    // track there (0 for empty space); nothing outside the track area.
    std::optional<std::pair<double, motion::Id>> dropTarget(juce::Point<int> point) const;
    void selectClips(const std::vector<motion::Id>& ids);
    // Select all keys in visible lanes when a key is selected, else all clips.
    void selectAll();
    void toggleLanesForSelection();
    void zoomBy(double factor);
    // Zooms and scrolls so the whole composition fits.
    void fitProject();

    void revealTime(double seconds);

    void refreshTracks();
    void resized() override;
    // The lifted block floats over every row and header: an opaque card with
    // a shadow all round, its own rows, and its header drawn in place.
    void paintOverChildren(juce::Graphics& g) override;
    void repaintPlayhead() { playheadStrip.moveTo(*this, playheadX()); }
    bool isInterestedInDragSource(const SourceDetails& details) override;

    void itemDragEnter(const SourceDetails& details) override { itemDragMove(details); }
    void itemDragMove(const SourceDetails& details) override;
    void itemDragExit(const SourceDetails&) override;
    // An effect dragged over a clip, track or group is heard and seen before
    // it is dropped.
    std::function<void(const motion::Project*)> onPreview;
    void setEffectDragActive(bool active);

    void itemDropped(const SourceDetails& details) override;

    void insertAsset(motion::Id assetId, int x, int y);

    void paint(juce::Graphics& g) override;

    void mouseDown(const juce::MouseEvent& event) override;

    void mouseMove(const juce::MouseEvent& event) override;

    void mouseExit(const juce::MouseEvent&) override;
    // Project times of every clip start and end (edit points), sorted.
    std::vector<double> editPoints() const;
    void mouseDrag(const juce::MouseEvent& event) override;

    void mouseUp(const juce::MouseEvent&) override;

    void mouseDoubleClick(const juce::MouseEvent& event) override;
    std::function<void(motion::Id)> onOpenSource;

    // One convention across the timeline and graph: the wheel and
    // trackpad pan (Shift makes the wheel horizontal), Cmd/Ctrl+wheel or a
    // pinch zooms time around the pointer, Alt+wheel changes track height.
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;
    void mouseMagnify(const juce::MouseEvent& event, float scale) override;
    void setDefaultTrackHeight(int height);

    // Page-follows the playhead during playback, like Premiere and Ableton. A
    // manual scroll while playing pauses following until the playhead is back
    // in view (or playback restarts).
    void followPlayhead(double time, bool playing);

    bool keyPressed(const juce::KeyPress& key) override;
    // The layout the editor keeps between sessions.
    struct Layout {
        int namesWidth = 220, trackHeight = 32;
        bool follow = true; // Page-follow the playhead during playback.
    };
    Layout layout() const { return {namesWidth, defaultTrackHeight, followEnabled}; }
    void setLayout(const Layout& next);
    // Back to the composition's start and the top row.
    void scrollToStart() {
        scrollTime = 0;
        scrollY = 0;
    }
    bool easeSelectedKeys(bool in, bool out);

private:
    double pixelsPerSecond = 70;
    double scrollTime = 0;
    // Rows without their own height use this; Alt+wheel scales it.
    int defaultTrackHeight = 32;
    // Track names column; drag its edge to resize (140-420 px).
    int namesWidth = 220;
    bool resizingNames = false;
    // The resize handle is the strip just inside the names column, so clicks
    // on keys and clips at the start of the timeline are never taken.
    bool onNamesEdge(const juce::MouseEvent& event) const { return event.y >= rulerHeight && event.x >= namesWidth - 5 && event.x < namesWidth; }
    // The track whose bottom edge (in the names column) is under the pointer.
    int trackEdgeAt(juce::Point<int> point) const;
    struct HeightDrag { motion::Id track; int startHeight, downY, original; };
    std::optional<HeightDrag> heightDrag;
    static constexpr int laneHeight = 22;
    // The bottom strip of each track row resizes it.
    static constexpr int resizeStrip = 4;
    // Keyboard zoom and Fit glide to their view (about 140 ms, eased) rather
    // than jumping; wheel and pinch zoom stay immediate.
    void animateView(double zoom, double scroll);
    juce::TimedCallback viewAnimation {[this] {
        const auto t = std::clamp((juce::Time::getMillisecondCounterHiRes() - animationStart) / 140.0, 0.0, 1.0);
        const auto eased = 1 - std::pow(1 - t, 3);
        // Zoom moves in log space so it feels even at any scale.
        pixelsPerSecond = std::exp(std::lerp(std::log(animationFrom.first), std::log(animationTo.first), eased));
        scrollTime = std::lerp(animationFrom.second, animationTo.second, eased);
        if (t >= 1) { viewAnimation.stopTimer(); }
        resized();
        repaint();
    }};
    std::pair<double, double> animationFrom, animationTo;
    double animationStart = 0;
    // What a dragged source or effect would do where it is.
    void paintDropPreview(juce::Graphics& g);
    std::optional<int> playheadX() const;
    void paintPlayhead(juce::Graphics& g);
    void previewEffect(motion::Id owner);
    motion::Id previewedOwner = 0;
    bool effectDragActive = false;
    std::uint64_t previewedRevision = 0;
    // Overlay scroll bars: vertical for rows, horizontal for time (Premiere
    // style: drag the thumb to scroll, its ends to zoom).
    static constexpr int barSize = 8, scrollStrip = barSize + 4;
    double timelineSpan() const;
    juce::Rectangle<int> horizontalBar() const { return {namesWidth + 2, getHeight() - barSize - 2, std::max(0, getWidth() - namesWidth - 4), barSize}; }
    juce::Rectangle<int> horizontalThumb() const;
    bool horizontalBarShown() const { return std::max(1, getWidth() - namesWidth) / pixelsPerSecond < timelineSpan() * 0.999 || scrollTime > 0; }
    juce::Rectangle<int> verticalBar() const { return {getWidth() - barSize - 2, rulerHeight + 2, barSize, std::max(0, viewHeight() - 4)}; }
    juce::Rectangle<int> verticalThumb() const;
    bool verticalBarShown() const { return maximumScrollY() > 0; }
    void paintScrollBars(juce::Graphics& g) const;
    struct BarDrag { enum class Mode { scrollTime, zoomLeft, zoomRight, scrollRows } mode; int down; double scrollTime, pixelsPerSecond; int scrollY; };
    std::optional<BarDrag> barDrag;
    int hoveredBar = 0;
    int barAt(juce::Point<int> point) const;
    bool barDown(const juce::MouseEvent& event);
    void barDragged(const juce::MouseEvent& event);

    // The loop brace: a bar along the bottom of the time ruler.
    static constexpr int loopTop = 20, loopHeight = 6;
    juce::Rectangle<int> loopBounds() const;
    void paintLoop(juce::Graphics& g) const;
    struct LoopDrag { enum class Mode { move, left, right } mode; double start, end; int downX; motion::Project before; std::uint64_t revision; bool changed; };
    std::optional<LoopDrag> loopDrag;
    bool loopDown(const juce::MouseEvent& event);
    void loopDragged(const juce::MouseEvent& event);
    std::optional<juce::Rectangle<int>> blocked;
    // Keeps the time under `x` fixed while zooming.
    void zoomAround(int x, double factor);
    void userScrolled();
    // Vertical zoom: every row without its own height, keeping the row under
    // the pointer in place.
    void scaleTrackHeights(int y, double factor);
    bool followEnabled = true;

    static std::optional<juce::Colour> labelColour(const motion::Track& track);
    juce::Colour clipColour(const motion::Clip& clip, const motion::Track& track) const;
    // Project times of every key on a clip, restricted to its visible interval.
    std::vector<double> clipKeyTimes(const motion::Clip& clip, const std::string& property = {}) const;
    void paintClip(juce::Graphics& g, const motion::Clip& clip, const motion::Track& track, int index, float opacity) const;
    void paintLane(juce::Graphics& g, const Row& row, int y, int height) const;
    static bool sameTime(double a, double b) { return std::abs(a - b) < 1.0e-6; }
    bool isKeySelected(motion::Id clip, const std::string& property, double time) const {
        return std::any_of(selectedKeys.begin(), selectedKeys.end(), [&](const auto& key) { return key.clip == clip && key.property == property && sameTime(key.time, time); });
    }
    // Keys whose diamond lies under a point in a lane row.
    std::optional<KeyRef> keyAt(juce::Point<int> point) const;
    std::vector<KeyRef> keysInside(juce::Rectangle<int> area) const;
    // Moves keys by a project-time delta on a copy of the gesture's project.
    // Fails when a moved key would land on a key that is not moving.
    static bool moveKeys(motion::Project& project, const std::vector<KeyRef>& keys, double delta);
    void beginKeyDrag(const KeyRef& grabbed, int x) {
        keyDrag = KeyDrag{processor.document.project(), selectedKeys, grabbed, x, processor.document.revision(), false};
    }
    void dragKeys(int x, juce::ModifierKeys modifiers);
    void endKeyDrag();
    void cancelKeyDrag();
    void deleteSelectedKeys();
    void addKeyAt(const Row& lane, int x, juce::ModifierKeys modifiers);
    void showKeyMenu();
    void deleteSelectedClips(bool ripple);
    void duplicateClip(motion::Id id);
    // The camera track: a band under the ruler (and markers) whose clips are
    // the camera cuts. Between cuts the first camera (or the default view)
    // shows.
    static constexpr int cameraBandHeight = 22;
    // The marker band also carries tempo changes.
    bool showsMarkerBand() const { return !processor.document.project().markers.empty() || processor.document.project().tempoChanges != nullptr; }
    // Bands and rows are cards with rounded left ends, inset from the panel
    // edge and separated by one small gap.
    static constexpr int toolsHeight = 26, bandGap = 1, markerBandHeight = 22, cardInset = 4;
    static constexpr float cardRadius = 3.0f;
    int markerBandTop() const { return toolsHeight + bandGap; }
    int cameraBandTop() const { return (showsMarkerBand() ? markerBandTop() + markerBandHeight : toolsHeight) + bandGap; }
    // One row: a track's or group's card and clips, or a keyframe lane.
    void paintRow(juce::Graphics& g, int visible);
    void fillCard(juce::Graphics& g, int y, int height, juce::Colour header, std::optional<juce::Colour> lane) const;
    bool showsCameraBand() const { return true; }
    // Matches the add-track button above it.
    juce::Rectangle<int> addCameraBounds() const { return {namesWidth - 26, cameraBandTop(), 22, cameraBandHeight}; }
    bool inCameraBand(int y) const { return showsCameraBand() && y >= cameraBandTop() && y < cameraBandTop() + cameraBandHeight; }
    juce::Rectangle<int> cutBounds(const motion::CameraCut& cut) const;
    static juce::Colour cameraColour(const motion::Project& project, motion::Id camera);
    juce::String cameraName(motion::Id camera) const;
    void paintCameraBand(juce::Graphics& g) const;
    bool addHover = false;
    const motion::CameraCut* findCut(motion::Id id) const;
    const motion::CameraCut* cutAt(juce::Point<int> point) const;
    void cameraBandDown(const juce::MouseEvent& event);
    void cameraBandDrag(const juce::MouseEvent& event);
    void showCutMenu(motion::Id cut, double time);
    void report(const juce::Result& result) {
        if (result.failed() && onError) { onError(result.getErrorMessage()); }
    }
    struct CutDrag {
        enum class Mode { move, left, right };
        motion::Id cut = 0;
        double start = 0, end = 0;
        int downX = 0;
        Mode mode = Mode::move;
    };
    std::optional<CutDrag> cutDrag;
    // A camera key in the Cameras band stands for every property's key at
    // that time; dragging moves them together as one undo step.
    struct CameraKeyDrag {
        motion::Id camera = 0;
        double from = 0, to = 0;
        int downX = 0;
        motion::Project before;
        std::uint64_t revision = 0;
        bool moved = false;
    };
    std::optional<CameraKeyDrag> cameraKeyDrag;
    std::optional<std::pair<motion::Id, double>> selectedCameraKey;
    const motion::Camera* selectedCamera() const;
    // The selected camera's key nearest x, within 5 px.
    std::optional<double> cameraKeyAt(int x) const;
    static void moveCameraKeys(motion::Camera& camera, double from, double to);
    void dragCameraKey(const juce::MouseEvent& event);
    void deleteCameraKey(motion::Id id, double time);
    motion::Id selectedCut = 0;

    juce::Rectangle<int> markerBounds(const motion::Marker& marker) const;
    const motion::Marker* markerAt(juce::Point<int> point) const;
    void selectMarker(motion::Id id);
    void jumpMarker(bool forward);
    void showMarkerMenu(motion::Id id, double time);
    // The tempo change whose flag is within a few pixels of `time`.
    const motion::TempoChange* tempoChangeNear(double time) const;
    // Adds (or edits) a tempo change at the nearest quarter beat.
    void editTempoChange(double time);

    void showClipMenu(motion::Id id);
    void createGroup(motion::Id trackId, motion::Id parent);
    void showGroupMenu(motion::Id id);
    void showTrackMenu(motion::Id id);
    void toggleLock(motion::Id id);
    void toggleTrack(motion::Id id, bool solo);
    void placeTrack(motion::Id id, int boundary, motion::Id group);
    // Dragging a track by its grip: the track lifts and follows the pointer,
    // the rows around it ease aside to open the place it will land, and on
    // release it settles there.
    struct TrackDrag {
        motion::Id id;
        int first, last, height, grab, pointer;
        std::uint64_t revision;
        int boundary;
        motion::Id group = 0;
    };
    std::optional<TrackDrag> trackDrag;
    osci::PlayheadStrip playheadStrip;
    std::vector<float> rowShift;
    juce::TimedCallback rowAnimation {[this] { stepRows(); }};
    // A drag ends where it began if the document changes under it (an undo,
    // a load) or its track goes away.
    void validateTrackDrag();
    int liftedTop() const {
        return std::clamp(trackDrag->pointer - trackDrag->grab, rulerHeight - trackDrag->height / 2, rulerHeight + viewHeight() - trackDrag->height / 2);
    }
    int shiftOf(int row) const {
        return rowShift.size() == rows.size() && row >= 0 && row < static_cast<int>(rows.size()) ? juce::roundToInt(rowShift[static_cast<std::size_t>(row)]) : 0;
    }
    void dragTrack(motion::Id id, int y, bool finished);
    // Where the dragged block would land, as a row boundary in the current
    // layout: the block's centre is compared with the midpoints of the other
    // rows as they sit with it taken out, so the gap follows the block. Over
    // the middle of a group's row it joins the group, at its end.
    int dropBoundary(motion::Id& group) const;
    // The track array position and parent group that a row boundary means:
    // before the track at the boundary, inside a group it was dropped on, or
    // ahead of a group's first track (among that group's siblings).
    std::pair<int, motion::Id> dropTarget(const TrackDrag& drag) const;
    float targetShift(int row) const;
    void stepRows();
    // What an effect dropped here applies to: the clip under the pointer, a
    // track by its header, or a group; 0 for nothing.
    motion::Id effectOwnerAt(juce::Point<int> position) const;
    void insertEffect(const std::string& type, juce::Point<int> position);
    enum class Tool { move, slip, stretch, ripple };
    enum class Mode { move, left, right, slip, stretch, rippleLeft, rippleRight };

    // Right-clicking empty track space offers what people look for first.
    void showSpaceMenu();


    bool gestureIsCurrent();

    void cancelGesture();

    bool isClip(motion::Id id) const { return motion::findClip(processor.document.project(), id) != nullptr; }
    bool notifyingSelection = false;
    void notifySelection(motion::Id id);
    void selectClip(motion::Id id);

    const motion::Clip* clipAt(juce::Point<int> position, int& row) const;
    static int boundedPixel(double value);
    // ChangeBroadcaster delivery is deferred. Undo/delete may replace the
    // document before the next paint or pointer event, so cached indices must
    // be rebuilt synchronously before any row lookup. Keep header creation and
    // destruction in refreshTracks(), outside paint and hit testing.
    void ensureTrackRows() const;
    // Lanes list every animated property on a track's clips, in schema order.
    static std::vector<std::string> animatedProperties(const motion::Track& track);
    void rebuildRows() const;
    // Row heights: lanes are fixed; tracks use their own height or the
    // default; groups use the default.
    int heightOf(std::size_t row) const;
    int heightAt(int row) const { return row >= 0 && row < static_cast<int>(rows.size()) ? heightOf(static_cast<std::size_t>(row)) : defaultTrackHeight; }
    int trackIndex(motion::Id id) const;
    int trackHeight(int track) const;
    void layoutRows() const;
    // Rows end above the horizontal scroll strip.
    int viewHeight() const { return std::max(0, getHeight() - rulerHeight - scrollStrip); }
    // Room below the last track for the Add track button and for dropping
    // new tracks, without pushing whole rows off the last page.
    static constexpr int addTrackGap = 4, addTrackHeight = 24, bottomRoom = addTrackGap + addTrackHeight + 12;
    int maximumScrollY() const { return std::max(0, contentHeight + bottomRoom - viewHeight()); }
    int firstVisibleRow() const { return std::max(0, visualRowAt(rulerHeight)); }
    int visualRowAt(int y) const;
    int trackAtY(int y) const;
    motion::Id groupAtY(int y) const;
    const Row* laneAtY(int y) const;
    int trackY(int track) const;
    int timeX(double time) const { return namesWidth + boundedPixel((time - scrollTime) * pixelsPerSecond); }
    int rowY(int row) const;
    juce::Rectangle<int> clipBounds(const motion::Clip& clip, int row) const;
    double snapTime(double time, juce::ModifierKeys modifiers) const {
        return modifiers.isAltDown() ? time : processor.document.project().timeGrid().snap(time);
    }
    // Magnetic targets within 8 px: the playhead, markers, other clips' edges
    // and keys shown in lanes. Keys being dragged and excluded clips are skipped.
    std::optional<double> magnet(double time, const motion::Project& project, const std::set<motion::Id>& excludedClips, const std::vector<KeyRef>* movingKeys = nullptr, bool includePlayhead = true, motion::Id excludedMarker = 0) const;
    // Snaps a moving edge: magnets first, then the grid; Alt bypasses both.
    double snapEdge(double time, juce::ModifierKeys modifiers, const motion::Project& project, const std::set<motion::Id>& excludedClips, const std::vector<KeyRef>* movingKeys = nullptr, motion::Id excludedMarker = 0);
    std::optional<double> snapGuide;
    motion::ui::PlayheadFollow follow;
    void seek(int x, juce::ModifierKeys modifiers);
    motion::icons::LabelButton addTrack {"Add track", motion::icons::Icon::add};
    motion::icons::Button snapButton {"Snapping", motion::icons::Icon::magnet};
    motion::icons::Button selectTool {"Select tool", motion::icons::Icon::select}, slipTool {"Slip tool", motion::icons::Icon::slip};
    motion::icons::Button stretchTool {"Stretch tool", motion::icons::Icon::stretch}, rippleTool {"Ripple trim tool", motion::icons::Icon::ripple};
    std::vector<std::unique_ptr<MotionTrackHeader>> headers;
    juce::Component headerArea;
    // View state kept in step with the document. Undo or a load can replace
    // the document before its change message arrives, so every row lookup,
    // const ones included, first calls ensureTrackRows(): it rebuilds the rows
    // and drops selections of things that no longer exist.
    mutable motion::Id selected = 0;
    mutable std::set<motion::Id> selectedClips;
    mutable motion::Id selectedMarker = 0;
    // Vertical scroll in pixels of row content below the ruler.
    mutable int scrollY = 0;
    mutable int rulerHeight = 26;
    mutable std::vector<Row> rows;
    mutable std::vector<int> rowTops;
    mutable int contentHeight = 0;
    mutable std::set<motion::Id> expandedTracks;
    mutable std::set<motion::Id> collapsedGroups;
    mutable std::uint64_t layoutGeneration = 0;
    mutable std::optional<std::uint64_t> layoutRevision;
    MotionProcessor& processor;
    std::optional<motion::Project> before;
    motion::Clip original;
    int originalRow = 0;
    int downX = 0;
    Tool tool = Tool::move;
    void setTool(Tool next);
    void updateToolButtons();
    Mode mode = Mode::move;
    std::uint64_t expectedRevision = 0;
    motion::Id markerDragging = 0;
    double markerOriginalTime = 0;
    bool scrubbing = false;
    bool changed = false;
    std::optional<juce::Point<int>> dropPosition;
    motion::Id dropAssetId = 0;
    std::string dropEffect;
    std::vector<KeyRef> selectedKeys;
    struct KeyDrag {
        motion::Project before;
        std::vector<KeyRef> keys;
        KeyRef grabbed;
        int downX = 0;
        std::uint64_t revision = 0;
        bool changed = false;
    };
    std::optional<KeyDrag> keyDrag;
    std::optional<juce::Rectangle<int>> marquee, clipMarquee;
    juce::Point<int> clipMarqueeStart;
    std::set<motion::Id> clipMarqueeBase;
    motion::Id hoveredClip = 0;
    int hoveredEdge = -1;
    juce::Point<int> marqueeStart;
    std::vector<KeyRef> marqueeBase;
};

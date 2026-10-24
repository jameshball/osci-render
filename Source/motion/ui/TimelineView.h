#pragma once

#include "../MotionProcessor.h"
#include "../model/LightPresets.h"
#include "ViewNavigation.h"
#include "TrackHeader.h"
#include "TrackLayout.h"
#include "PlayheadStrip.h"
#include "PreviewGesture.h"
#include <optional>
#include <variant>
#include <limits>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include "../model/CompositionGraph.h"
#include "../model/PropertySchema.h"
#include "../model/KeyEasing.h"
#include "BeamShares.h"
#include "MotionIcons.h"
#include "DocumentMenu.h"

class MotionTimelineView : public juce::Component, public juce::DragAndDropTarget, public juce::SettableTooltipClient {
    // The composition's own row, above the tracks while it has effects: they
    // act on everything, as an adjustment layer spanning it would.
    static constexpr int compositionTrack = -2;
    static constexpr motion::Id compositionRow = std::numeric_limits<motion::Id>::max();
    struct Row : motion::TrackRow {
        // A keyframe lane under its track (or the composition's row): the keys
        // of `property` on each of `owners` (clips, their effects, or the
        // composition's effects), told apart from other lanes by `lane`.
        std::string lane, property;
        std::vector<motion::Id> owners;
        juce::String label;
        juce::Colour colour;
        bool isLane() const { return !lane.empty(); }
        bool group() const { return track == -1 && lane.empty(); }
        bool composition() const { return track == compositionTrack && lane.empty(); }
    };
    struct KeyRef {
        motion::Id owner = 0; // Whose key: a clip, an effect, or another owner of keys.
        std::string property;
        double time = 0; // Content-local key time.
    };
    // Keys looked up by owner and property, then by time, so checking a
    // selection of thousands costs a search rather than a scan.
    class KeySet {
    public:
        KeySet() = default;
        explicit KeySet(const std::vector<KeyRef>& keys) {
            for (const auto& key : keys) { add(key); }
        }
        void add(const KeyRef& key) { times[{key.owner, key.property}].insert(key.time); }
        bool contains(motion::Id owner, const std::string& property, double time) const {
            const auto found = times.find({owner, property});
            if (found == times.end()) { return false; }
            const auto next = found->second.lower_bound(time);
            if (next != found->second.end() && motion::sameTime(*next, time)) { return true; }
            return next != found->second.begin() && motion::sameTime(*std::prev(next), time);
        }
    private:
        std::map<std::pair<motion::Id, std::string>, std::set<double>> times;
    };
public:
    explicit MotionTimelineView(MotionProcessor& ownerProcessor);
    // Runs one of the editor's commands by name (Cut, Copy, Paste...).
    std::function<void(const juce::String&)> onCommand;
    // Whether there is anything to paste, for the menus' Paste.
    std::function<bool()> canPaste;
    // Add a camera that takes over the output at the playhead.
    std::function<void(double)> onAddCamera;
    // Double-clicking a camera's shot, or its menu: look through it.
    std::function<void(motion::Id)> onLookThrough;
    std::function<void(motion::Id)> onSelection, onTimingRequested, onMakeUnique, onEnterComposition, onRevealSource;
    std::function<void()> onLoopSelection;
    // The composition's effects row was clicked: show its effects.
    std::function<void()> onShowCompositionEffects;
    std::function<void(const juce::String&)> onError;
    // The source Sources has chosen (0: none), and a hint said meanwhile:
    // double-clicking empty track space adds that source there.
    std::function<motion::Id()> chosenSource;
    std::function<void(const juce::String&)> onHint;
    std::function<void(motion::Id, double)> onEditMarker;
    // (beat, current bpm, beat of the change being edited, if any)
    std::function<void(double, double, std::optional<double>)> onEditTempo;
    // The tempo change at (or nearest a beat to) `seconds`, as the ruler's menu edits it.
    void editTempoAt(double seconds) { editTempoChange(seconds); }
    // The editing tools (V, S, R, B) and the Add track button, as search uses them.
    enum class Tool { move, slip, stretch, ripple };
    void setTool(Tool next);
    Tool currentTool() const { return tool; }
    void addNewTrack() { addTrack.triggerClick(); }
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
    // Deletes the selected clips and closes the gaps they leave on their tracks.
    void rippleDeleteSelection() { deleteSelectedClips(true); }
    // Tracks or groups picked by their headers (or the one selected).
    bool hasPickedRows() const { return !pickedTracks().empty(); }
    // Whether Group tracks has tracks to group (see tracksToGroup).
    bool canGroupTracks() const { return !tracksToGroup().empty(); }
    // The picked or selected groups, which Ungroup dissolves.
    std::vector<motion::Id> groupsToUngroup() const;
    bool canUngroup() const { return !groupsToUngroup().empty(); }
    void ungroupSelected();
    // Something said about an edit that went through (what Ungroup lost).
    std::function<void(const juce::String&)> onNotice;
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
    // While auto-key is on, a red edge says so, as a recording would.
    void setAutoKey(bool on) {
        autoKey = on;
        repaint();
    }

    void refreshTracks();
    void resized() override;
    // The lifted block floats over every row and header: an opaque card with
    // a shadow all round, its own rows, and its header drawn in place.
    void paintOverChildren(juce::Graphics& g) override;
    // Shows the playhead at `time` (the editor moves it every display frame).
    void setPlayhead(double time) { playheadStrip.moveTo(*this, time, [this](double at) { return playheadX(at); }); }
    // The editor's timer: the bake strip repaints while the bake changes.
    void updateBake();
    bool isInterestedInDragSource(const SourceDetails& details) override;

    void itemDragEnter(const SourceDetails& details) override { itemDragMove(details); }
    void itemDragMove(const SourceDetails& details) override;
    void itemDragExit(const SourceDetails&) override;
    // An effect dragged over a clip, track or group is heard and seen before
    // it is dropped.
    std::function<void(const std::string&, std::optional<motion::Id>)> onEffectPreview;
    std::function<void(motion::Id, std::optional<motion::Id>)> onEffectMovePreview;
    // An effect card dropped on a clip or track moves its effect (Alt copies it).
    std::function<void(motion::Id, motion::Id, bool)> onEffectMoved;
    void setEffectDragActive(bool active);

    void itemDropped(const SourceDetails& details) override;

    // False when it doesn't fit there (a clip in the way, a locked track or
    // one of the other kind).
    bool insertAsset(motion::Id assetId, int x, int y);

    // The beam's time: every visual clip shows the share it gets as a fill
    // rising from its foot, and dragging that edge sets its Beam.
    std::function<const motion::PreparedComposition*()> beamComposition;
    void beamChanged() { beamCache.reset(); repaint(); }

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
    void followPlayhead(double time, bool playing, bool backwards = false);

    bool keyPressed(const juce::KeyPress& key) override;
    // The layout the editor keeps between sessions.
    struct Layout {
        int namesWidth = 184, trackHeight = 32;
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
    // The selected keys' interpolation (those on locked tracks keep theirs).
    bool setSelectedInterpolation(motion::Interpolation shape);

private:
    double pixelsPerSecond = 70;
    std::uint64_t viewGeneration = 0;
    double scrollTime = 0;
    bool autoKey = false;
    // Rows without their own height use this; Alt+wheel scales it.
    int defaultTrackHeight = 32;
    // Track names column; drag its edge to resize, within these (and half the
    // timeline's width).
    static constexpr int narrowestNames = 150, widestNames = 420;
    int namesWidth = 184;
    // The resize handle is the strip just inside the names column, so clicks
    // on keys and clips at the start of the timeline are never taken.
    bool onNamesEdge(const juce::MouseEvent& event) const { return event.y >= rulerHeight && event.x >= namesWidth - 5 && event.x < namesWidth; }
    // The row (track or group) whose bottom edge, in the names column, is
    // under the point, to resize it.
    int rowEdgeAt(juce::Point<int> point) const;
    int storedHeight(motion::Id id) const;
    struct HeightDrag { motion::Id track; int startHeight, downY, original; };
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
    // A drag held near the left or right edge of the tracks scrolls them,
    // faster the nearer (or further past) the edge, and carries on while
    // the pointer stays there.
    std::optional<juce::MouseEvent> edgeEvent;
    juce::TimedCallback edgeScroll {[this] { scrollAtEdge(); }};
    void followEdge(const juce::MouseEvent& event);
    void scrollAtEdge();
    // Pixels a tick the timeline scrolls at x: negative to the left.
    double edgeSpeed(int x) const;
    // Where the drag under way began, moved with the tracks as they scroll.
    int* dragOrigin();
    bool scrollsAtEdge() { return dragOrigin() != nullptr || active<Scrub>() != nullptr; }
    // What a dragged source or effect would do where it is.
    void paintDropPreview(juce::Graphics& g);
    // Where the playhead at `time` is drawn, between pixels as it moves.
    std::optional<float> playheadX(double time) const;
    void paintPlayhead(juce::Graphics& g);
    void previewEffect(motion::Id owner);
    motion::Id previewedOwner = 0;
    bool effectDragActive = false;
    std::uint64_t previewedRevision = 0;
    // Overlay scroll bars: vertical for rows, horizontal for time (Premiere
    // style: drag the thumb to scroll, its ends to zoom). They take the
    // pointer across barSize but are drawn restingBar thick until hovered.
    static constexpr int barSize = 8, restingBar = 4, scrollStrip = barSize + 4;
    double timelineSpan() const;
    // Where the composition or its last clip ends. Kept per document state
    // (scope and revision): scroll limits ask for it on every row lookup.
    double contentEnd() const;
    mutable double contentEndCache = 0;
    mutable std::optional<std::pair<std::uint64_t, std::uint64_t>> contentEndKey;
    juce::Rectangle<int> horizontalBar() const { return {namesWidth + 2, getHeight() - barSize - 2, std::max(0, getWidth() - namesWidth - 4), barSize}; }
    juce::Rectangle<int> horizontalThumb() const;
    bool horizontalBarShown() const { return visibleSpan() < timelineSpan() * 0.999 || scrollTime > 0; }
    juce::Rectangle<int> verticalBar() const { return {getWidth() - barSize - 2, rulerHeight + 2, barSize, std::max(0, viewHeight() - 4)}; }
    juce::Rectangle<int> verticalThumb() const;
    bool verticalBarShown() const { return maximumScrollY() > 0; }
    void paintScrollBars(juce::Graphics& g) const;
    struct BarDrag { enum class Mode { scrollTime, zoomLeft, zoomRight, scrollRows } mode; int down; double scrollTime, pixelsPerSecond; int scrollY; };
    int hoveredBar = 0;
    int barAt(juce::Point<int> point) const;
    bool barDown(const juce::MouseEvent& event);
    void barDragged(const juce::MouseEvent& event);

    // The loop brace: a bar along the bottom of the time ruler.
    static constexpr int loopTop = 20, loopHeight = 6;
    juce::Rectangle<int> loopBounds() const;
    void paintLoop(juce::Graphics& g) const;
    struct LoopDrag { enum class Mode { move, left, right } mode; double start, end; int downX; };
    bool loopDown(const juce::MouseEvent& event);
    // Cmd-click (Ctrl on Windows and Linux) on the ruler moves the nearer loop
    // point there, as modifier-clicks on Cubase's ruler set its locators;
    // without a loop it makes one from there to the playhead.
    bool moveNearestLoopEdge(const juce::MouseEvent& event);
    void loopDragged(const juce::MouseEvent& event);
    std::optional<juce::Rectangle<int>> blocked;
    // Keeps the time under `x` fixed while zooming.
    void zoomAround(int x, double factor);
    // Zoomed out no further than the whole timeline twice over; in, to 500 px a second.
    double clampZoom(double value) const {
        // A short composition on a wide timeline fits twice over beyond 500.
        const auto lowest = std::min(500.0, std::max(1, getWidth() - namesWidth) / (2 * std::max(1.0, contentEnd())));
        return std::clamp(value, lowest, 500.0);
    }
    void userScrolled();
    // Vertical zoom: every row without its own height, keeping the row under
    // the pointer in place.
    void scaleTrackHeights(int y, double factor);
    bool followEnabled = true;

    static std::optional<juce::Colour> labelColour(const motion::Track& track);
    juce::Colour clipColour(const motion::Clip& clip, const motion::Track& track) const;
    // Project times of every key on a clip, restricted to its visible interval.
    std::vector<double> clipKeyTimes(const motion::Clip& clip) const;
    // Key summary ticks along a foot: a diamond at each time between left and
    // right, none under 3 px from the last, kept within `inset` when given.
    template <typename Times>
    juce::Path keyTicks(const Times& times, float footY, float left, float right, std::optional<juce::Range<float>> inset = {}) const {
        juce::Path ticks;
        double lastX = -10;
        for (const auto time : times) {
            const auto x = static_cast<float>(timeX(time));
            if (x - lastX < 3 || x < left || x > right) { continue; }
            motion::style::addDiamond(ticks, {inset.has_value() ? std::clamp(x, inset->getStart(), inset->getEnd()) : x, footY}, 3.5f);
            lastX = x;
        }
        return ticks;
    }
    void paintClip(juce::Graphics& g, const motion::Clip& clip, const motion::Track& track, int index, float opacity) const;
    // The beam's shares across the visible time, one value per column.
    static constexpr int beamStep = 4;
    // A share between two sampled columns.
    static float interpolatedShare(const std::vector<float>& values, float offset);
    struct BeamCache { double start = 0, pixels = 0; int left = 0, width = 0; std::map<motion::Id, std::vector<float>> shares; };
    mutable std::optional<BeamCache> beamCache;
    mutable MotionBeamShares beamShares;
    const BeamCache* ensureBeamShares() const;
    std::optional<float> beamShareAt(motion::Id id, float x) const;
    // Where a share is drawn: inside a clip's body, or along a group's row.
    juce::Rectangle<float> beamArea(const motion::Clip& clip, int row) const;
    std::optional<juce::Rectangle<float>> groupBeamArea(motion::Id group) const;
    // Shares rise on a square-root scale, so one of many still shows and can
    // be grabbed: a quarter of the beam fills half the height.
    static float beamY(juce::Rectangle<float> area, float share) { return area.getBottom() - std::sqrt(std::clamp(share, 0.0f, 1.0f)) * area.getHeight(); }
    // True when it drew the share (only for the hovered, lit or chosen).
    bool paintBeamShare(juce::Graphics& g, motion::Id id, juce::Rectangle<float> area, juce::Rectangle<float> body, float opacity) const;
    void paintBeamLabel(juce::Graphics& g) const;
    // The clip or group whose share edge is under the point, if any.
    struct BeamTarget { motion::Id id; juce::Rectangle<float> area; };
    std::optional<BeamTarget> beamEdgeAt(juce::Point<int> point) const;
    struct BeamDrag { motion::Id id; juce::Rectangle<float> area; double time, fraction, beam; bool keyed; int downY; };
    // The hovered or dragged share's readout, drawn over every row.
    mutable std::optional<std::pair<juce::Rectangle<float>, juce::String>> beamLabel;
    bool beamDown(const juce::MouseEvent& event);
    void beamDragged(const juce::MouseEvent& event);
    motion::Id beamHover = 0;
    int beamPointer = 0;
    // Sets the hovered clip (or group) and beam edge, repainting only what
    // they light: the old and new ones and their readouts.
    void setHover(motion::Id clip, motion::Id beam, int pointer);
    // A clip's or group row's bounds, with room for the beam readout above
    // or below it; empty when it is gone.
    juce::Rectangle<int> hoverBounds(motion::Id id) const;
    void paintLane(juce::Graphics& g, const Row& row, int y, int height) const;
    // Each lane owner's value range over the visible time, kept while the
    // document and view stay as they are, so a repainted strip (the
    // playhead's) samples only its own columns.
    using LaneRangeView = std::tuple<std::uint64_t, std::uint64_t, std::uint64_t, double, double, int, int>;
    mutable std::optional<LaneRangeView> laneRangeView;
    mutable std::map<std::pair<std::string, motion::Id>, std::pair<double, double>> laneRanges;
    // The clip a lane's owner is, or is an effect of; none for the composition's effects.
    static const motion::Clip* laneClip(const motion::Project& project, motion::Id owner);
    bool isKeySelected(motion::Id owner, const std::string& property, double time) const {
        return std::any_of(selectedKeys.begin(), selectedKeys.end(), [&](const auto& key) { return key.owner == owner && key.property == property && motion::sameTime(key.time, time); });
    }
    // Moves the clips a ClipDrag holds, as the pointer goes.
    void dragClips(const juce::MouseEvent& event);
    void dragMarker(const juce::MouseEvent& event);
    // Keys whose diamond lies under a point in a lane row.
    std::optional<KeyRef> keyAt(juce::Point<int> point) const;
    std::vector<KeyRef> keysInside(juce::Rectangle<int> area) const;
    std::vector<KeyRef> allLaneKeys() const;
    bool dragging() const;
    // Moves keys by a project-time delta on a copy of the gesture's project.
    // Fails when a moved key would land on a key that is not moving.
    static bool moveKeys(motion::Project& project, const std::vector<KeyRef>& keys, double delta);
    void beginKeyDrag(const KeyRef& grabbed, int x) {
        edit.begin();
        gesture = KeyDrag {selectedKeys, grabbed, x};
    }
    void dragKeys(int x, juce::ModifierKeys modifiers);
    void deleteSelectedKeys();
    // Moves the selected keys `frames` frames along, as Alt+Left and Right
    // do (Shift: ten); presses in a row are one undo step.
    bool nudgeSelectedKeys(int frames);
    // The selected clips likewise, when no keys are selected.
    bool nudgeSelectedClips(int frames);
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
    // Where a top-level track's name starts; the bands' captions and the key
    // lanes' labels start in line with it.
    static constexpr int nameX = cardInset + MotionTrackHeader::nameInset;
    int markerBandTop() const { return toolsHeight + bandGap; }
    int cameraBandTop() const { return (showsMarkerBand() ? markerBandTop() + markerBandHeight : toolsHeight) + bandGap; }
    // One row: a track's or group's card and clips, or a keyframe lane.
    void paintRow(juce::Graphics& g, int visible);
    void paintCompositionRow(juce::Graphics& g, int y, int height) const;
    void fillCard(juce::Graphics& g, int y, int height, juce::Colour header, std::optional<juce::Colour> lane) const;
    // Matches the add-track button above it.
    juce::Rectangle<int> addCameraBounds() const { return {namesWidth - 26, cameraBandTop(), 22, cameraBandHeight}; }
    bool inCameraBand(int y) const { return y >= cameraBandTop() && y < cameraBandTop() + cameraBandHeight; }
    juce::Rectangle<int> cutBounds(const motion::CameraCut& cut) const;
    // Where a camera key draws: a key at the very start stays clear of the
    // name column, as a lane's does.
    int cameraKeyX(double time) const { return std::max(timeX(time), namesWidth + 6); }
    static juce::Colour cameraColour(const motion::Project& project, motion::Id camera);
    juce::String cameraName(motion::Id camera) const;
    void paintCameraBand(juce::Graphics& g) const;
    bool addHover = false;
    const motion::CameraCut* findCut(motion::Id id) const;
    const motion::CameraCut* cutAt(juce::Point<int> point) const;
    motion::Id cameraAt(motion::Id cut) const;
    // Delete acts on the camera's key or cut only while one is what was picked last.
    void clearCameraSelection();
    void cameraBandDown(const juce::MouseEvent& event);
    void cameraBandDrag(const juce::MouseEvent& event);
    void showCutMenu(motion::Id cut, double time);
    // Says why it failed, if it did; true when it didn't.
    bool report(const juce::Result& result) {
        if (result.failed() && onError) { onError(result.getErrorMessage()); }
        return result.wasOk();
    }
    struct CutDrag {
        enum class Mode { move, left, right };
        motion::Id cut = 0;
        double start = 0, end = 0;
        int downX = 0;
        Mode mode = Mode::move;
    };
    // A camera key in the Cameras band stands for every property's key at
    // that time; dragging moves them together as one undo step.
    struct CameraKeyDrag {
        motion::Id camera = 0;
        double from = 0, to = 0;
        int downX = 0;
    };
    mutable std::optional<std::pair<motion::Id, double>> selectedCameraKey;
    const motion::Camera* selectedCamera() const;
    // The selected camera's key nearest x, within 5 px.
    std::optional<double> cameraKeyAt(int x) const;
    static void moveCameraKeys(motion::Camera& camera, double from, double to);
    void dragCameraKey(const juce::MouseEvent& event);
    void deleteCameraKey(motion::Id id, double time);
    mutable motion::Id selectedCut = 0;

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
    // Puts a preset on a light clip, or takes its preset off.
    void applyLightPreset(motion::Id id, std::optional<motion::LightPreset> preset);
public:
    // Tracks and groups picked together by their headers (Cmd or Shift),
    // for grouping them in one go (Cmd+G).
    void groupSelectedTracks();
    // Selects the composition, so Properties shows its effects.
    void showCompositionEffects();
    // Every clip on the tracks from `from`'s to `to`'s within both their times;
    // `adding` keeps what was selected. False if either is gone.
    bool selectRange(motion::Id from, motion::Id to, bool adding);
    // The selected clips into a new composition, used where they were
    // (Cmd+Shift+C, as After Effects' Pre-compose; Alt+G too, as Final Cut).
    void createCompositionFromSelection();
    // Puts these tracks (or groups) in a new group, in one undo step.
    void groupTracks(const std::vector<motion::Id>& ids);
private:
    void pickTrack(motion::Id id, juce::ModifierKeys mods);
    // Shows or hides a track's (or the composition's) key lanes.
    void toggleLanes(motion::Id id) {
        if (expandedTracks.contains(id)) { expandedTracks.erase(id); } else { expandedTracks.insert(id); }
    }
    std::vector<motion::Id> tracksToGroup() const;
    // The track headers picked: several, or the one selected.
    std::vector<motion::Id> pickedTracks() const;
    void deleteTracks(const std::vector<motion::Id>& ids);
    mutable std::set<motion::Id> selectedTracks;
    mutable motion::Id trackAnchor = 0;
    void showGroupMenu(motion::Id id);
    void showTrackMenu(motion::Id id);
    void toggleLock(motion::Id id);
    void toggleTrack(motion::Id id, bool solo);
    void placeTrack(motion::Id id, int boundary, motion::Id group);
    void placeGroup(motion::Id id, int boundary, motion::Id parent);
    // Dragging a track by its header: the track lifts and follows the pointer,
    // the rows around it ease aside to open the place it will land, and on
    // release it settles there.
    struct TrackDrag {
        motion::Id id;
        int first, last, height, grab, pointer;
        std::uint64_t revision;
        int boundary;
        motion::Id group = 0;
    };
    motion::ui::PlayheadStrip playheadStrip;
    std::vector<float> rowShift;
    juce::TimedCallback rowAnimation {[this] { stepRows(); }};
    // A drag ends where it began if the document changes under it (an undo,
    // a load) or its track goes away.
    void validateTrackDrag();
    int liftedTop() const {
        const auto& drag = std::get<TrackDrag>(gesture);
        return std::clamp(drag.pointer - drag.grab, rulerHeight - drag.height / 2, rulerHeight + viewHeight() - drag.height / 2);
    }
    int shiftOf(int row) const {
        return rowShift.size() == rows.size() && row >= 0 && row < static_cast<int>(rows.size()) ? juce::roundToInt(rowShift[static_cast<std::size_t>(row)]) : 0;
    }
    void dragTrack(motion::Id id, int y, bool finished);
    bool inLiftedBlock(int row) const;
    // Where the dragged block would land, as a row boundary in the current
    // layout: the block's centre is compared with the midpoints of the other
    // rows as they sit with it taken out, so the gap follows the block. Over
    // the middle of a group's row it joins the group, at its end.
    int dropBoundary(motion::Id& group) const;
    // The track array position and parent group that a row boundary means:
    // before the track at the boundary, inside a group it was dropped on, or
    // ahead of a group's first track (among that group's siblings).
    std::pair<int, motion::Id> trackDropPlace(const TrackDrag& drag) const;
    float targetShift(int row) const;
    void stepRows();
    // What an effect dropped here applies to: the clip under the pointer, a
    // track by its header, or a group; 0 for nothing.
    motion::Id effectOwnerAt(juce::Point<int> position) const;
    void insertEffect(const std::string& type, juce::Point<int> position);
    enum class Mode { move, left, right, slip, stretch, rippleLeft, rippleRight };

    // Right-clicking empty track space offers what people look for first.
    void showSpaceMenu();

    // Ends a scrub, or a clip or marker drag and puts back what it changed;
    // the other drags carry on (see endDocumentDrag).
    void cancelClipGesture();
    // The undo step the document drag under way records, named for what it
    // did; empty when no document drag is under way.
    juce::String dragLabel() const;
    // Ends the document drag under way (a clip, marker, key, beam, loop, cut
    // or camera key): letting go records it as one undo step, Escape puts
    // back what it changed. False when none is under way.
    bool endDocumentDrag(bool commit);

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
    // A track's keyframe lanes: its clips' animated properties, then their
    // effects' animated parameters, each effect's under its name.
    static std::vector<Row> trackLanes(const motion::Project& project, const motion::Track& track);
    // A lane for each effect name and parameter keyed by any of `effects`.
    static void addEffectLanes(std::vector<Row>& lanes, const std::vector<const motion::EffectInstance*>& effects);
    // The composition's effects' animated parameters.
    static std::vector<Row> compositionLanes(const motion::Project& project);
    // Whether a track has any key lanes to show (trackLanes' rows, unbuilt).
    static bool hasLanes(const motion::Track& track);
    // The properties a track's lanes can show, in schema order: every kind's
    // object properties, or an audio track's.
    static const std::vector<motion::PropertySpec>& laneSpecs(motion::TrackKind kind);
    void rebuildRows() const;
    // Row heights: lanes are fixed; tracks use their own height or the
    // default; groups use the default.
    int heightOf(std::size_t row) const;
    int heightAt(int row) const { return row >= 0 && row < static_cast<int>(rows.size()) ? heightOf(static_cast<std::size_t>(row)) : defaultTrackHeight; }
    int trackHeight(int track) const;
    // The row of a track's or group's header (not a lane), or -1.
    int headerRow(motion::Id id) const {
        const auto found = headerRows.find(id);
        return found == headerRows.end() ? -1 : found->second;
    }
    void layoutRows() const;
    // Rows end above the horizontal scroll bar's strip, while it is shown.
    int viewHeight() const { return std::max(0, getHeight() - rulerHeight - (horizontalBarShown() ? scrollStrip : 0)); }
    // The horizontal bar comes and goes as the view's span changes: the rows
    // give up its strip or take it back, keeping the scroll in range.
    void fitRowsToBar() {
        if (headerArea.getHeight() != viewHeight()) { resized(); }
    }
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
    // The time at x, timeX's inverse.
    double timeAt(int x) const { return scrollTime + (x - namesWidth) / pixelsPerSecond; }
    // Seconds across the tracks' width.
    double visibleSpan() const { return std::max(1, getWidth() - namesWidth) / pixelsPerSecond; }
    // Where something dropped at x lands: over the names column, the playhead.
    double dropTime(int x) const { return x < namesWidth ? processor.position.load() : std::max(0.0, timeAt(x)); }
    // Where source `id` dropped at `x` starts, either edge of its clip caught
    // as a moving clip's is (shown by the snap guide); Alt places it freely.
    double dropStart(int x, motion::Id id, juce::ModifierKeys modifiers);
    double dropSnapped = 0;
    // Along the ruler's foot, the main composition's hidden-line bake: frames
    // baked, being baked and still to bake (each drawn whole until it is),
    // fading once every frame is.
    void paintBake(juce::Graphics& g) const;
    juce::Rectangle<int> bakeStrip() const { return {namesWidth, toolsHeight - bakeStripHeight - 1, std::max(0, getWidth() - namesWidth), bakeStripHeight}; }
    static constexpr int bakeStripHeight = 2;
    static constexpr double bakeFadeMs = 900;
    // The bake last seen, when it was first seen, whether the strip shows it
    // (only once it has baked a moment, so an edit that changes little
    // doesn't flash it), and when it was seen finished (0 while baking).
    const void* bakeSeen = nullptr;
    double bakeSeenAt = 0, bakeFinishedAt = 0;
    bool bakeShown = false;
    static constexpr double bakeShowAfterMs = 300;
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
    // Rebuilt with the rows: each header's row by id, and each track's by
    // its index in the project (-1 while hidden in a folded group).
    mutable std::unordered_map<motion::Id, int> headerRows;
    mutable std::vector<int> trackRows;
    mutable std::vector<int> rowTops;
    mutable int contentHeight = 0;
    mutable std::set<motion::Id> expandedTracks;
    // The clip clicked last: where a Shift-click's run starts.
    motion::Id clipAnchor = 0;
    mutable std::set<motion::Id> collapsedGroups;
    mutable std::uint64_t layoutGeneration = 0;
    mutable std::optional<std::uint64_t> layoutRevision;
    MotionProcessor& processor;
    // The document drag under way (a clip, marker, key, beam, loop, cut or
    // camera key): it previews its edit on the project it began from, Escape
    // puts that back, and letting go records one undo step.
    motion::ui::PreviewGesture edit {processor.document};
    Tool tool = Tool::move;
    void updateToolButtons();
    struct ClipDrag {
        motion::Clip original;
        int originalRow = 0;
        Mode mode = Mode::move;
        int downX = 0;
    };
    struct MarkerDrag {
        motion::Id marker = 0;
        double originalTime = 0;
        int downX = 0;
    };
    std::optional<juce::Point<int>> dropPosition;
    motion::Id dropAssetId = 0;
    std::string dropEffect;
    motion::Id dropInstance = 0; // an effect card being moved
    mutable std::vector<KeyRef> selectedKeys;
    struct KeyDrag {
        std::vector<KeyRef> keys;
        KeyRef grabbed;
        int downX = 0;
    };
    // A box dragged across key lanes, or across empty track space, picks the
    // keys or clips inside it, and keeps `base` picked when it adds to them.
    struct KeyMarquee {
        juce::Rectangle<int> area;
        juce::Point<int> start;
        std::vector<KeyRef> base;
        // The keys selected before it began, put back by Escape.
        std::vector<KeyRef> before;
    };
    struct ClipMarquee {
        juce::Rectangle<int> area;
        juce::Point<int> start;
        std::set<motion::Id> base;
    };
    // Dragging along the ruler moves the playhead.
    struct Scrub {};
    // Dragging the names column's edge resizes it.
    struct NamesResize {};
    // Middle-dragging grabs the view: time follows sideways, tracks up and down.
    struct Pan { juce::Point<int> down; double scrollTime; int scrollY; };
    // The one gesture under way, if any; starting another replaces it.
    std::variant<std::monostate, ClipDrag, KeyDrag, BeamDrag, LoopDrag, BarDrag, HeightDrag, CutDrag, CameraKeyDrag, TrackDrag, MarkerDrag, Scrub, NamesResize, Pan, KeyMarquee, ClipMarquee> gesture;
    template <typename T> T* active() { return std::get_if<T>(&gesture); }
    template <typename T> const T* active() const { return std::get_if<T>(&gesture); }
    // A clip or marker drag, or a scrub: what cancelClipGesture() ends.
    bool clipGesture() const { return active<ClipDrag>() != nullptr || active<MarkerDrag>() != nullptr || active<Scrub>() != nullptr; }
    // The clip, or group row, under the pointer: its beam share shows.
    motion::Id hoveredClip = 0;
    int hoveredEdge = -1;
};

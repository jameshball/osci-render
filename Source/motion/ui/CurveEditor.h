#pragma once

#include "../MotionProcessor.h"
#include "ViewNavigation.h"
#include "DocumentMenu.h"
#include "../model/KeyEdit.h"
#include "../model/KeyEasing.h"
#include "../render/PreparedDrivers.h"
#include "../model/PropertyTarget.h"
#include "../model/PropertySchema.h"
#include "MotionStyle.h"
#include "Chip.h"
#include <array>
#include <optional>
#include <set>
#include <limits>

// Keys retain content-local times; the ruler and snapping use project time.
// Drags preview locally and commit through Document once on release, so an
// unrelated document edit cannot be overwritten by a stale project snapshot.
// Every axis of the edited property's group (Position X/Y/Z) is editable at
// once: the selection spans those curves, and the primary curve is the one
// whose property the owner shows and whose selected key has tangent handles.
class MotionCurveEditor : public juce::Component {
public:
    explicit MotionCurveEditor(MotionProcessor& processor);
    // Sibling axes the channel list hid: not drawn and not selectable.
    void setHiddenCurves(std::set<std::string> curves);
    // Curves shown faintly behind the edited group (from the channel list).
    void setContextCurves(std::map<std::string, juce::Colour> curves);
    void repaintPlayhead() { playheadStrip.moveTo(*this, playheadX()); }
    // Page-follows the playhead during playback unless the view was just moved.
    void followPlayhead(double time, bool playing);

    struct ViewState {
        motion::Id target = 0;
        std::string property;
        std::optional<double> selected;
        double start = 0, end = 1, low = -1, high = 1;
        bool user = false;
    };
    ViewState viewState() const { return {targetId, propertyName, selectedTime, viewStart, viewEnd, low, high, userView}; }
    void restoreView(const ViewState& state);

    // Called synchronously with the previewed curves by property name; the map
    // is only valid during this call. nullptr ends the preview.
    std::function<void(const motion::PropertyMap*)> onPreview;
    // Another axis became primary (clicked near its curve, or one of its keys
    // was picked): the owner switches the edited property.
    std::function<void(const std::string&)> onPropertyChosen;

    void setSelection(motion::Id id, std::string property);

    void refresh();

    void paint(juce::Graphics& g) override;
    void resized() override;

    void mouseMove(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent& event) override;
    void mouseDown(const juce::MouseEvent& event) override;

    void mouseDoubleClick(const juce::MouseEvent& event) override;

    void scrubTo(float x, juce::ModifierKeys modifiers);
    void mouseDrag(const juce::MouseEvent& event) override;

    void mouseUp(const juce::MouseEvent&) override;

    // F frames every curve of the group (the clip plus all key times and values);
    // Shift+F frames only the primary curve. Escape or Cmd+Z during a drag
    // restores the keys and selection, and otherwise Escape deselects; Delete
    // removes the selection on every curve. Alt+Left/Right nudges the selected
    // keys a frame (Shift: ten), Alt+Up/Down a tenth of a value tick (Shift: a tick).
    bool keyPressed(const juce::KeyPress& key) override;

    // The same wheel convention as the timeline: wheel and trackpad pan
    // (Shift makes the wheel horizontal), Cmd/Ctrl+wheel or a pinch zooms
    // time around the pointer, Alt+wheel zooms values.
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;
    void mouseMagnify(const juce::MouseEvent& event, float scale) override;
    void zoomTime(float x, double factor);

private:
    using KeyRef = motion::CurveKey;
    bool userView = false;
    enum class DragMode { key, incoming, outgoing, scaleLeft, scaleRight };

    struct Drag {
        motion::PropertyMap originals; // Every curve of the group when the drag began.
        motion::PropertyMap previews;
        motion::Keyframe original;     // The grabbed key or tangent owner on the primary curve.
        juce::Point<float> down;
        double start = 0, duration = 0, offset = 0, rate = 1;
        DragMode mode = DragMode::key;
        juce::Point<float> handle;
        std::optional<double> primary; // The selection when the drag began, restored by Escape.
        std::vector<KeyRef> companions;
        double pivot = 0, edge = 0;    // Scaling: content times of the fixed and dragged edges.
    };

    void beginDrag(const motion::PropertyTarget& clip, DragMode mode, juce::Point<float> down);

    void cancelDrag();

    // The selection when the drag began, primary last.
    std::vector<KeyRef> dragSelection() const;
    void restoreDragSelection();
    void adoptSelection(std::vector<KeyRef> keys, bool hasPrimary);
    std::optional<KeyRef> primaryKey() const {
        return selectedTime.has_value() ? std::optional<KeyRef>(KeyRef { propertyName, *selectedTime }) : std::nullopt;
    }
    std::vector<KeyRef> selection() const;
    bool isSelected(const std::string& property, double time) const;
    // Makes a key primary; a key on another axis switches the edited property
    // (the owner's setSelection keeps this selection, as the property already matches).
    void makePrimary(const KeyRef& key);
    void toggleKey(const KeyRef& key);

    // Other axes of the same property group (Position Y/Z beside X), editable
    // alongside the primary curve and drawn in their axis colour.
    std::vector<std::pair<std::string, juce::Colour>> siblings(const motion::PropertyTarget& target) const;
    // The edited curve's colour: its axis colour (as in the channel list),
    // or the key green for single-value properties.
    juce::Colour primaryColour(const motion::PropertyTarget& target) const;
    // The edited property's routed modulators and link, prepared once per
    // document revision (null when nothing drives it).
    std::shared_ptr<const motion::CurveDrivers> resultDrivers(const motion::Curve& curve);
    std::tuple<std::uint64_t, std::uint64_t, motion::Id, std::string> driversKey;
    std::shared_ptr<const motion::CurveDrivers> cachedDrivers;
    // Round value ticks: steps of 1, 2 or 5 times a power of ten.
    static double valueStep(double span, int wanted);
    bool isSibling(const motion::PropertyTarget& target, const std::string& property) const;
    // The primary curve first, then its siblings.
    std::vector<std::string> groupNames(const motion::PropertyTarget& target) const;
    motion::PropertyMap groupCurves(const motion::PropertyTarget& target) const;
    // The drag preview of a group curve while dragging, otherwise the stored curve.
    const motion::Curve* displayed(const motion::PropertyTarget& target, const std::string& property) const;
    // The name the inspector and channel list use, never the internal id.
    static const motion::Curve* findCurve(const std::optional<motion::PropertyTarget>& target, const std::string& property) {
        return target.has_value() ? target->curve(property) : nullptr;
    }

    bool targetLocked() const;
    static motion::Curve* mutableCurve(motion::Project& project, motion::Id id, const std::string& property) {
        return motion::findPropertyCurve(project, id, property);
    }


    static bool sameCurve(const motion::Curve& a, const motion::Curve& b) { return a.sameAuthoring(b); }

    bool dragMatches(const motion::PropertyTarget& clip) const;


    // The time span of a selection of 2+ keys at different times, with the
    // content times of its earliest and latest keys.
    struct SelectionBox {
        juce::Rectangle<float> area;
        double first = 0, last = 0;
    };
    std::optional<SelectionBox> selectionBox(const motion::PropertyTarget& clip) const;
    static juce::Rectangle<float> scaleHandle(const SelectionBox& box, bool right);
    // A box too narrow for grips is still moved by its keys.
    static bool hasGrips(const SelectionBox& box) { return box.area.getWidth() >= 40.0f; }

    // The ruler band on top (as in the Timeline), value labels in the gutter
    // to the left of the plot.
    static constexpr int bandHeight = 26, gutter = 56;
    juce::Rectangle<float> plot() const {
        return { static_cast<float>(gutter), bandHeight + 6.0f, std::max(1.0f, getWidth() - gutter - 10.0f), std::max(1.0f, getHeight() - bandHeight - 14.0f) };
    }
    void paintRuler(juce::Graphics& g, double step, double minorStep);
    void paintGrid(juce::Graphics& g, double step, double minorStep);
    void paintReadout(juce::Graphics& g, juce::Point<float> anchor, const juce::String& text);
    // The curve between project times `from` and `to`, sampled `perPixel` times
    // a pixel and exactly at each of `keys` (and just before), so holds jump
    // and corners turn on their keys.
    template <typename Evaluate>
    juce::Path curvePath(double from, double to, float perPixel, const std::vector<double>& keys, Evaluate&& evaluate) const;
    // What the pointer is over, for hover feedback and the cursor.
    struct Hover {
        std::optional<KeyRef> key;
        std::optional<DragMode> handle;
        std::optional<bool> grip; // The selection box's right (true) or left grip.
        bool curve = false;
        bool operator==(const Hover&) const = default;
    };
    Hover hoverAt(juce::Point<float> point) const;
    juce::Point<float> pointer; // Where the ghost key on a hovered curve sits.
    bool nudgeSelected(int frames, double values);
    void setHover(Hover next);
    void selectionChanged();
    // Reserve enough headroom for subtracting both viewport endpoints. This
    // bounds only the displayed window, never the authored project or keys.
    static constexpr double viewLimit = std::numeric_limits<double>::max() / 2;
    void setView(double start, double end);
    void normalizeValueRange();
    std::optional<int> playheadX() const;
    float timeX(double time) const;
    float valueY(double value) const;
    double projectTime(float x) const;
    double valueAt(float y) const { return low + (plot().getBottom() - y) / plot().getHeight() * (high - low); }
    juce::Point<float> keyPoint(const motion::PropertyTarget& clip, const motion::Keyframe& key) const {
        return { timeX(clip.projectTime(key.time)), valueY(key.value) };
    }
    // Signed content-time length of the segment a handle belongs to, or 0.
    static double segmentSpan(const motion::Curve& curve, const motion::Keyframe& key, DragMode mode);
    // Handles sit on the true Bezier control points of cubic segments.
    std::optional<juce::Point<float>> tangentPoint(const motion::PropertyTarget& clip, const motion::Curve& curve, const motion::Keyframe& key, DragMode mode) const;
    double constrainedValue(const motion::PropertyTarget& target, double value, const std::string& property) const;

    double snappedTime(const motion::PropertyTarget& clip, double time, juce::ModifierKeys modifiers) const;
    // Magnetic targets within 8 px: the playhead, markers and the unselected
    // keys of the group's curves. Keys on ownCurve are skipped: landing there
    // would collide. Free mode (grid snapping off) turns magnets off too.
    std::optional<double> magnet(const motion::PropertyTarget& clip, double time, const std::string& ownCurve) const;
    // Snaps a dragged key time: magnets first (showing a guide), then the grid; Alt bypasses both.
    double snapKeyTime(const motion::PropertyTarget& clip, double time, juce::ModifierKeys modifiers, const std::string& ownCurve);
    // The nearest key on any curve of the group; the primary curve wins ties.
    std::optional<KeyRef> hitKey(const motion::PropertyTarget& clip, juce::Point<float> point) const;

    // Frames the clip, every key time and the values of the group's curves, or
    // of the primary curve alone.
    void fit(bool primaryOnly = false);

public:
    // Every key on the group's curves; the primary curve's first key leads.
    void selectAllKeys();
    bool hasSelectedKeys() const { return selectedTime.has_value() || !companions.empty(); }
    // Easy Ease (F9; Shift+F9 in, Cmd+Shift+F9 out) on every selected key, in
    // one undo step.
    bool easeSelected(bool in, bool out);

    // The selected keys for the Key panel: how many, which interpolations
    // they use, and whether they can be edited.
    struct KeySelection {
        std::size_t count = 0;
        std::array<bool, 4> present {};
        bool editable = false;
    };
    KeySelection keySelection() const;
    void setSelectedInterpolation(motion::Interpolation interpolation);
    // The one selected key, for typing its time and value: its property,
    // project time and value.
    struct ActiveKey {
        std::string property;
        double time = 0, value = 0;
        bool editable = false;
    };
    std::optional<ActiveKey> activeKey() const;
    // Moves the active key or sets its value; a field's edits within a
    // second merge into one undo step. A move onto another key is refused.
    void setActiveKeyTime(double projectTime);
    void setActiveKeyValue(double value);
    // The selection changed (or the keys it holds were edited).
    std::function<void()> onSelectionChanged;

private:
    bool deleteSelected();

    void showKeyMenu();

    MotionProcessor& processor;
    motion::ui::Chip frameButton {"Frame curves", motion::icons::Icon::frame};
    Hover hover;
    motion::Id targetId = 0;
    std::string propertyName;
    std::optional<double> selectedTime;  // The primary key, on propertyName (content time).
    std::vector<KeyRef> companions;      // Additional selected keys on any curve of the group.
    std::optional<juce::Rectangle<float>> marquee;
    juce::Point<float> marqueeStart;
    std::optional<Drag> drag;
    std::optional<double> snapGuide;     // Project time of the magnet a drag is snapped to.
    osci::PlayheadStrip playheadStrip;
    std::map<std::string, juce::Colour> contextCurves;
    std::set<std::string> hiddenCurves;
    motion::ui::PlayheadFollow follow;
    bool scrubbing = false;
    double viewStart = 0.0, viewEnd = 1.0;
    double low = -1.0, high = 1.0;
};

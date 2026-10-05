#pragma once

#include "MotionIcons.h"
#include "Chip.h"

#include "../MotionProcessor.h"
#include "ScrubField.h"
#include "ColourPicker.h"
#include "../model/PropertySchema.h"
#include "../model/SpatialMotion.h"
#include "../model/Drawing.h"

// Scrolling transform/appearance inspector for one property target (object,
// group, audio clip or camera). Rows group related axes; each row keys all of
// its axes at once and navigates between its keys. Values edit at the key
// under the playhead (frame-aligned) or, for animated curves, create one.
class MotionPropertyInspector final : public juce::Component, public juce::DragAndDropTarget {
public:
    explicit MotionPropertyInspector(MotionProcessor& owner);

    std::function<void(motion::Id, const std::string&)> onPropertySelected;
    // Content time of a key selected elsewhere (e.g. a motion-path key), if any.
    std::function<std::optional<double>(motion::Id)> selectedKeyTime;
    std::function<void()> onKeyTimeEdited;
    // The row's modulation chip: open the graph (oscillator, routes, link) for this property.
    std::function<void(motion::Id, const std::string&)> onModulate;
    // Opens a popover pointing at `anchor` (the colour picker).
    std::function<void(std::unique_ptr<juce::Component>, juce::Component& anchor)> onShowPopover;
    // Double-clicking a camera's name renames it.
    std::function<void(motion::Id, const juce::String&)> onRename;

    // Embedded inspectors (e.g. in the camera panel) supply their own title.
    // Distinguishes controls of several inspectors for automation and access.
    void setNamePrefix(juce::String prefix) { namePrefix = std::move(prefix); layoutSignature = "none"; refresh(); }
    // A section above the property rows (the clip's timing), sized by
    // `height`; 0 hides it.
    void setLead(juce::Component* component, std::function<int()> height);
    // A section below the property rows (the owner's effects).
    void setTrail(juce::Component* component, std::function<int()> height);
    // What the header says when the target has no properties of its own (a
    // track or the composition, shown for their effects).
    void setHeading(std::optional<std::pair<juce::String, juce::String>> value);
    void relayout() { layoutContent(); }
    // Scrolls so `component` (inside the inspector) is fully in view.
    void reveal(juce::Component& component);
    void setShowsHeader(bool shows) { showsHeader = shows; title.setVisible(shows); kind.setVisible(shows); resized(); }
    void setTarget(motion::Id id);
    motion::Id getTarget() const { return target; }
    void setSelectionCount(std::size_t count);

    // Rebuilds rows only when the target's property set changes, updates the
    // header and modes, then the values.
    void refresh();
    // Only the values and key states at the playhead, for playback ticks.
    void refreshValues();

    // Modulators dragged from the library route to the field or row dropped on.
    std::function<void(motion::Id modulator, motion::Id target, std::vector<std::string> properties)> onRouteModulator;
    void setModulatorDrag(bool active);
    bool isInterestedInDragSource(const SourceDetails& details) override { return details.description.toString().startsWith("motion-modulator:"); }
    void itemDragMove(const SourceDetails& details) override;
    void itemDragExit(const SourceDetails&) override;
    void itemDropped(const SourceDetails& details) override;
    void paintOverChildren(juce::Graphics& g) override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    struct Field {
        explicit Field(const motion::PropertySpec& value) : spec(value) {}
        motion::PropertySpec spec;
        MotionScrubField editor;
    };
    // The Colour row's swatch: the colour at the playhead; a click opens the picker.
    struct Swatch : juce::Button {
        Swatch();
        void paintButton(juce::Graphics& g, bool highlighted, bool) override;
        void setColour(juce::Colour value);
        juce::Colour colour = juce::Colours::white;
    };
    struct Row : juce::Component {
        juce::String group;
        std::vector<std::unique_ptr<Field>> fields;
        std::unique_ptr<Swatch> swatch;
        osci::KeyframeButton key;
        motion::ui::Chip modulate {"Modulate", motion::icons::Icon::wave};
        motion::style::ChevronButton previous {"Previous key", false}, next {"Next key", true};
        // Position: one spatial path; Rotation: quaternion orientation.
        std::unique_ptr<motion::ui::Chip> mode;
        bool misaligned = false; // mode on, but axes no longer share key times
        // A single value sits on one line: its name, then the value in the
        // grid's last column, beside its keys.
        bool compact() const { return fields.size() == 1 && mode == nullptr && swatch == nullptr; }
        int preferredHeight() const { return compact() ? motion::style::controlHeight : 17 + motion::style::controlHeight; }
        void paint(juce::Graphics& g) override;
        void resized() override;
        int captionRight = 0;
    };
    struct Gesture {
        motion::Project before;
        std::uint64_t revision;
        motion::Id target;
    };

    // Driven by a link or a shared modulator route.
    bool isModulated(const std::string& property) const;
    void build(std::span<const motion::PropertySpec> specs);
    void layoutContent();

    // Keys land on frames so they align with the timeline grid and exports.
    double keyTime(const motion::PropertyTarget& found) const;
    static bool hasKey(const motion::Curve& curve, double time);
    void apply(motion::Project& project, const std::string& property, double value, double time) const;
    void beginGesture(const std::string& property);
    void previewValue(const std::string& property, double value);
    void endGesture(const juce::String& name = "Change property");
    void cancelGesture();
    // The row's red, green and blue as shown (0..1).
    static MotionColourPicker::Rgb colourOf(const Row& row);
    // The picker edits all three channels as one gesture and one undo step.
    void openColourPicker(Row& row);
    void commitValue(const std::string& property, double value);
public:
    // Alt+Shift+P/R/S/T, as in After Effects: key a group at the playhead.
    bool toggleGroupKeys(const juce::String& group);
private:
    void toggleKeys(Row& row);
    void jumpToKey(Row& row, bool forward);

    // What a clip shows, named by its source rather than "Object".
    juce::String sourceKind(motion::Id clip) const;
    // Sliders a Lua clip's script reads (slider_a ...), plus any it already
    // animates. Their curves are created on first edit.
    std::vector<motion::PropertySpec> luaSliders() const;
    motion::Curve* createSlider(motion::Project& project, const std::string& property) const;
    // (spatial path, quaternion rotation) of the target clip or group.
    std::optional<std::pair<bool, bool>> currentModes() const;
    // Turning a mode on keys every axis wherever any axis has a key, so the
    // three curves share key times and the path or orientation applies.
    void setMotionMode(bool path, bool enabled);

    MotionProcessor& processor;
    juce::Viewport viewport;
    juce::Component content;
    juce::Label title, kind;
    std::vector<std::unique_ptr<Row>> rows;
    // The innermost field or row under `point` that a modulator can drive.
    juce::Component* routeTargetAt(juce::Point<int> point);
    bool modulatorDrag = false;
    juce::Component::SafePointer<juce::Component> dropTarget;
    juce::Component* lead = nullptr;
    juce::Component* trail = nullptr;
    std::function<int()> trailHeight;
    std::optional<std::pair<juce::String, juce::String>> heading;
    std::size_t selectionCount = 1;
    std::function<int()> leadHeight;
    std::vector<motion::PropertySpec> specList;
    motion::Id target = 0;
    juce::String layoutSignature = "none", namePrefix;
    std::optional<Gesture> gesture;
    double gestureTime = 0;
    bool changed = false, empty = true, showsHeader = true, motionModes = false;
};

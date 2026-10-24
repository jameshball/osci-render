#pragma once

#include <set>

#include "MotionIcons.h"
#include "PreviewGesture.h"
#include "Chip.h"
#include "KeyframeButton.h"
#include "TileGrid.h"
#include "PropertyRow.h"

#include "../MotionProcessor.h"
#include "ScrubField.h"
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

    // A section above the property rows (the clip's timing), sized by
    // `height`; 0 hides it.
    void setLead(juce::Component* component, std::function<int()> height);
    // A section below the property rows (the owner's effects).
    // `title` heads it as a section of its own (Effects).
    // A full-width trail draws its own sections, edge to edge like these.
    void setTrail(juce::Component* component, std::function<int()> height, juce::String title = {}, bool fullWidth = false);
    // What the header says when the target has no properties of its own (a
    // track or the composition, shown for their effects).
    void setHeading(std::optional<std::pair<juce::String, juce::String>> value);
    void relayout() { layoutContent(); }
    // Scrolls so `component` (inside the inspector) is fully in view.
    // Scrolled to and marked, as osci::reveal does, once laid out.
    void reveal(juce::Component& component, osci::RevealOptions options);
    void setTarget(motion::Id id);
    void setSelectionCount(std::size_t count);

    // Rebuilds rows only when the target's property set changes, updates the
    // header and modes, then the values.
    void refresh();
    // Only the values and key states at the playhead, for playback ticks.
    void refreshValues();

    // Modulators dragged from the library route to the field or row dropped on.
    std::function<void(motion::Id modulator, motion::Id target, std::vector<std::string> properties)> onRouteModulator;
    // While a modulator hovers a field or row, the route it would make (none
    // when it leaves), so its effect shows before the drop.
    struct RouteRequest { motion::Id modulator = 0, target = 0; std::vector<std::string> properties; };
    std::function<void(std::optional<RouteRequest>)> onRoutePreview;
    // The Anchor row's centre button.
    std::function<void(motion::Id)> onCentreAnchor;
    // The share of the beam's time an object gets at the playhead, beside
    // its Beam; none when it isn't drawn there.
    std::function<std::optional<double>(motion::Id)> beamShare;
    void setModulatorDrag(bool active);
    bool isInterestedInDragSource(const SourceDetails& details) override { return details.description.toString().startsWith("motion-modulator:"); }
    void itemDragMove(const SourceDetails& details) override;
    std::optional<RouteRequest> routeRequest(const SourceDetails& details, juce::Component* found) const;
    bool previewingRoute = false;
    void itemDragExit(const SourceDetails&) override;
    void itemDropped(const SourceDetails& details) override;
    void paintOverChildren(juce::Graphics& g) override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    struct Field {
        explicit Field(const motion::PropertySpec& value) : spec(value) {}
        motion::PropertySpec spec;
        motion::ui::ScrubField editor;
    };
    // The Colour row's swatch: the colour at the playhead; a click opens the picker.
    struct Swatch : juce::Button {
        Swatch();
        void paintButton(juce::Graphics& g, bool highlighted, bool) override;
        void setColour(juce::Colour value);
        juce::Colour colour = juce::Colours::white;
    };
    // A group of a property's axes (Position: X, Y, Z), on the shared row's
    // gutter: its name above them, or beside a single value.
    struct Row : motion::ui::PropertyRow {
        juce::String group, section;
        // What makes it (a generator's numbers, a fractal's depth, a
        // script's sliders, a light's colour), so it comes first, above the
        // clip's timing.
        bool first = false;
        std::vector<std::unique_ptr<Field>> fields;
        std::unique_ptr<Swatch> swatch;
        // Position: one spatial path; Rotation: quaternion orientation.
        std::unique_ptr<motion::ui::Chip> mode;
        bool misaligned = false; // mode on, but axes no longer share key times
        juce::String note; // beside a single value: what it amounts to (Beam's share)
        // A single value sits on one line: its name, then the value in the
        // grid's last column, beside its keys.
        bool compact() const { return fields.size() == 1 && mode == nullptr && swatch == nullptr; }
        int preferredHeight() const { return compact() ? motion::style::controlHeight : 17 + motion::style::controlHeight; }
        void paint(juce::Graphics& g) override;
        void resized() override;
    };
    // The scrolled page: rows, and between groups of them a hairline and the
    // group's heading (Transform, Appearance, ...), which may carry a muted
    // note at its right (Effects, when there are none: where they come from).
    struct Content : juce::Component {
        struct Section {
            int y = 0;
            juce::String title, note;
        };
        std::vector<Section> sections;
        void paint(juce::Graphics& g) override;
    };
    static constexpr int sectionGap = 10, sectionHeading = 26;
    // How a surface draws itself, as tiles: its edges, outline and patterns,
    // each on or off at a click, its rows below only for those on.
    std::unique_ptr<motion::ui::TileGrid> surfaceTiles;
    // Wireframe or solid: whether its faces hide what is behind them.
    std::unique_ptr<motion::ui::TileGrid> solidTiles;
    // A light's presets, as tiles: steady, or moving on its own.
    std::unique_ptr<motion::ui::TileGrid> presetTiles;
    // How a camera shades: by brightness, or by hatching.
    std::unique_ptr<motion::ui::TileGrid> shadingTiles;
    // Fills `slot` with two chips for a property that is off (0) or on (100),
    // as Shading's and Solidity's are; picking one records undoLabels' entry.
    void addChoiceTiles(std::unique_ptr<motion::ui::TileGrid>& slot, const juce::String& title, const char* property, std::vector<motion::ui::TileGrid::Choice> choices, std::array<juce::String, 2> undoLabels);
    // Under Surface's or Shading's tiles, the numbers behind them, folded
    // away until asked for; shown only when opening it would show some.
    motion::style::Disclosure advanced {"Advanced settings", "Advanced", "Advanced"};
    bool advancedAvailable = false;
    // Advanced rows not in play now, and what would bring them in.
    std::map<std::string, juce::String> outOfPlay;
    // A row of those numbers (Mirror is no style, and always shows).
    static bool isAdvanced(const Row& row);
    // Puts the tiles in step with the target at `time`, and shows the
    // Surface rows that are on; true when which show changed.
    bool refreshTiles(const motion::PropertyTarget& found, double time);
    // Which heading a property group sits under.
    juce::String sectionOf(const motion::PropertySpec& spec) const;
    // A generator's own number.
    bool isGeneratorSpec(const motion::PropertySpec& spec) const;
    // What makes it, which opens the page with no heading of its own: a
    // generator's numbers (and a light's colour), a fractal's depth, a
    // script's sliders.
    bool isMakingSpec(const motion::PropertySpec& spec) const;
    // Whether the project has a light, so that being lit by lights means
    // something.
    bool projectHasLight() const;

    // Driven by a link or a shared modulator route.
    bool isModulated(const std::string& property) const;
    // The properties the target's modulator routes drive, gathered once for
    // a refresh, and whether one is driven (by them or a link).
    std::vector<std::string> routedProperties() const;
    static bool isDriven(const motion::PropertyTarget& found, const std::vector<std::string>& routed, const std::string& property);
    void build(std::span<const motion::PropertySpec> specs);
    void layoutContent();

    // Keys land on frames so they align with the timeline grid and exports.
    double keyTime(const motion::PropertyTarget& found) const;
    // A property's value at `time` before its drivers: its curve's, or its default.
    double baseValue(const motion::PropertyTarget& found, std::string_view property, double time) const;
    void apply(motion::Project& project, const std::string& property, double value, double time) const;
    // A field's edit: with Scale linked, the other axes keep their proportions
    // to the edited one (all take its value when it was zero).
    void applyEdit(motion::Project& project, const std::string& property, double value, double time) const;
    bool scaleLinked() const;
    void beginGesture(const std::string& property);
    void previewValue(const std::string& property, double value);
    void endGesture(const juce::String& name = "Change property");
    void cancelGesture();
    // The row's red, green and blue as shown (0..1).
    static osci::ColourPicker::Rgb colourOf(const Row& row);
    // The picker edits all three channels as one gesture and one undo step.
    void openColourPicker(Row& row);
    void commitValue(const std::string& property, double value, const juce::String& label);
public:
    // Alt+Shift+P/R/S/T, as in After Effects: key a group at the playhead.
    bool toggleGroupKeys(const juce::String& group);
    // A row Properties shows for its target, as search finds it: its group
    // (Position), its fields' names (Position X...), the section it sits in,
    // and whether it is folded under Advanced.
    struct SearchableRow {
        std::string property;
        juce::String group, section;
        juce::StringArray labels;
        bool advanced = false;
        // Why it isn't shown now (Noise phase while there is no noise).
        juce::String unavailable;
    };
    std::vector<SearchableRow> searchableRows() const;
    // Scrolls to the row holding `property` and marks it, opening Advanced
    // first when it is folded there, with its first field taking the keys;
    // false when no row shows it.
    bool revealProperty(const std::string& property);
    // Whether what it shows has `group` (Position, Colour...) to key.
    bool showsGroup(const juce::String& group) const;
private:
    void toggleKeys(Row& row);
    // Right-clicked, the diamond offers what toggling it doesn't: all its keys gone.
    void showKeyMenu(Row& row);
    void jumpToKey(Row& row, bool forward);

    // What a clip shows, named by its source rather than "Object"; a
    // generator's by the generator (Sphere, Point light).
    juce::String sourceKind(motion::Id clip) const;
    // Sliders a Lua clip's script reads (slider_a ...), plus any it already
    // animates. Their curves are created on first edit.
    std::vector<motion::PropertySpec> luaSliders() const;
    // An L-system clip's depth, at the depth its source was imported at.
    std::optional<motion::PropertySpec> fractalDepthSpec() const;
    // What generates the target clip, whose parameters open its page.
    const motion::GeneratorDefinition* targetGenerator() const;
    // A Lua slider's or generator parameter's curve, made on first edit.
    motion::Curve* createSlider(motion::Project& project, const std::string& property) const;
    // (spatial path, quaternion rotation) of the target clip or group.
    std::optional<std::pair<bool, bool>> currentModes() const;
    // Turning a mode on keys every axis wherever any axis has a key, so the
    // three curves share key times and the path or orientation applies.
    void setMotionMode(bool path, bool enabled);

    MotionProcessor& processor;
    osci::ScrollFadeViewport viewport;
    // Scrolled, the rows slide under the heading: a rule marks where.
    Content content;
    // The panel's header: what is edited (renamed by double-click for a
    // camera), and its kind at the right; "Properties" with nothing to edit.
    juce::Label title, kind;
    static constexpr int headerHeight = osci::PanelHeader::height;
    void layoutHeader();
    std::vector<std::unique_ptr<Row>> rows;
    // The innermost field or row under `point` that a modulator can drive.
    juce::Component* routeTargetAt(juce::Point<int> point);
    bool modulatorDrag = false;
    juce::Component::SafePointer<juce::Component> dropTarget;
    juce::Component* lead = nullptr;
    juce::Component* trail = nullptr;
    std::function<int()> trailHeight;
    juce::String trailTitle;
    bool trailFullWidth = false;
    std::optional<std::pair<juce::String, juce::String>> heading;
    std::size_t selectionCount = 1;
    std::function<int()> leadHeight;
    std::vector<motion::PropertySpec> specList;
    motion::Id target = 0;
    // Rows shown only while in use that have been shown for `keptFor`.
    std::set<std::string> keptRows;
    motion::Id keptFor = 0;
    juce::String layoutSignature = "none";
    motion::ui::PreviewGesture gesture {processor.document};
    motion::Id gestureTarget = 0;
    double gestureTime = 0;
    bool empty = true, motionModes = false;
};

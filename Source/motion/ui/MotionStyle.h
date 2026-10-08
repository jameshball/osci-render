#pragma once

#include <JuceHeader.h>
#include "../../LookAndFeel.h"
#include "../model/KeyEasing.h"
#include <array>

// One place for osci-motion's visual language: a restrained dark palette built
// on the shared osci theme, four type styles and a 4 px spacing grid.
namespace motion::style {
inline constexpr int gap = 4;
inline constexpr int padding = 8;
inline constexpr int controlHeight = 24;
inline constexpr float radius = 3.0f;
inline constexpr float panelRadius = 5.0f;
// Every button, whatever surface it sits on.
inline constexpr float buttonRadius = 4.0f;

// Green means one thing: what is selected or focused, and the one action a
// panel exists for. A switch, tool or toggle that is on is neutral.
inline juce::Colour accent() { return osci::Colours::accentColor(); }
// The wash behind a selected row, region or choice.
inline juce::Colour accentFill() { return accent().withAlpha(.12f); }
// The primary action's fill.
inline juce::Colour accentStrong() { return accent().withAlpha(.5f); }
// A toggle, tool or switch that is on.
inline juce::Colour onFill() { return juce::Colours::white.withAlpha(.16f); }

// The only type styles in osci-motion. All UI text uses one of these;
// MotionTypographyTest fails if code anywhere else asks for a font.
// title: panel and tab names, the inspector heading, group names.
// body: values, names, menus, buttons, pickers and editors.
// caption: field labels, hints, secondary details and the ruler.
// heading: section headings in Properties, popovers and forms.
// mono: the position readout and code.
// The separator between short facts, as in the status bar.
inline juce::String dot() { return juce::String::fromUTF8(" \xc2\xb7 "); }
inline juce::Font title() { return juce::Font(juce::FontOptions(13.0f, juce::Font::bold)); }
inline juce::Font body() { return juce::Font(juce::FontOptions(13.0f)); }
inline juce::Font caption() { return juce::Font(juce::FontOptions(11.0f)); }
inline juce::Font heading() { return juce::Font(juce::FontOptions(11.0f, juce::Font::bold)); }
inline juce::Font mono() { return juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain)); }
// A shortcut written "Cmd+Shift+F9" as the platform shows it: ⇧⌘F9 on macOS,
// Ctrl+Shift+F9 elsewhere. Mouse gestures ("Alt+wheel") keep their words.
inline juce::String shortcutText(const juce::String& raw) {
   #if JUCE_MAC
    if (raw.containsIgnoreCase("wheel") || raw.containsIgnoreCase("drag") || raw.containsIgnoreCase("click") || raw.contains(" ")) { return raw.replace("Cmd", juce::String::fromUTF8("\u2318")); }
    auto parts = juce::StringArray::fromTokens(raw, "+", "");
    if (raw.endsWith("+")) { parts.removeEmptyStrings(); parts.add("+"); }
    if (parts.size() < 2) { return raw; }
    const auto key = parts[parts.size() - 1];
    parts.remove(parts.size() - 1);
    juce::String result;
    for (const auto& [name, glyph] : std::initializer_list<std::pair<const char*, const char*>> {{"Ctrl", "\u2303"}, {"Alt", "\u2325"}, {"Shift", "\u21e7"}, {"Cmd", "\u2318"}}) {
        if (parts.contains(name)) { result << juce::String::fromUTF8(glyph); }
    }
    return result + key;
   #else
    return raw.replace("Cmd", "Ctrl");
   #endif
}
// A menu item showing its shortcut on the right.
inline juce::PopupMenu::Item menuItem(const juce::String& text, int id, const juce::String& shortcut) {
    juce::PopupMenu::Item item(text);
    item.itemID = id;
    item.shortcutKeyDescription = shortcutText(shortcut);
    return item;
}


// The beam's phosphor green: traces, previews and the playhead. Keys are
// neutral (or their axis' colour) so they never read as selected.
inline juce::Colour key() { return juce::Colour(0xff72de98); }
inline juce::Colour playhead() { return key(); }
inline juce::Colour keyTick() { return juce::Colours::white.withAlpha(.7f); }
// Orange, apart from solo's yellow and the tangent handles' gold.
inline juce::Colour marker() { return juce::Colour(0xffeb9a5c); }
inline juce::Colour tempo() { return juce::Colour(0xff8fb6e8); }
// The selected stroke or point, brighter than the key green.
inline juce::Colour selection() { return juce::Colour(0xff9affb3); }
// A modulated property's value after its drivers, beside its keys: a hue
// no axis uses, so a channel's keys and its result never look alike.
inline juce::Colour result() { return juce::Colour(0xffbfa6ff); }
inline juce::Colour tangent() { return juce::Colour(0xffe7bc6c); }
inline juce::Colour motionPath() { return juce::Colour(0xffb8d4ea); }
inline juce::Colour waveform() { return juce::Colour(0xff97c7df); }
inline juce::Colour record() { return juce::Colour(0xffe5484d); }
// Softer than osci::Colours::danger() for text and refusals on dark panels.
inline juce::Colour error() { return juce::Colour(0xffe98080); }
inline juce::Colour axisX() { return juce::Colour(0xffe07a7a); }
inline juce::Colour axisY() { return juce::Colour(0xff7ad69a); }
inline juce::Colour axisZ() { return juce::Colour(0xff7aa6e0); }
// A spec's axis (X/Y/Z, or R/G/B for colour) as its colour; anything else
// takes the fallback.
inline juce::Colour axisColour(std::string_view axis, juce::Colour fallback = key()) {
    const auto letter = axis.empty() ? ' ' : axis[0];
    if (letter == 'X' || letter == 'R') { return axisX(); }
    if (letter == 'Y' || letter == 'G') { return axisY(); }
    if (letter == 'Z' || letter == 'B') { return axisZ(); }
    return fallback;
}

// Clip families are distinguished by hue at equal, low saturation.
inline juce::Colour visualClip() { return juce::Colour(0xff2e5a63); }
inline juce::Colour audioClip() { return juce::Colour(0xff2f4657); }
// A neutral slate: lilac means modulation.
inline juce::Colour compositionClip() { return juce::Colour(0xff464b58); }
// A track's mute, solo and lock switches when on.
inline juce::Colour trackMute() { return juce::Colour(0xff8b6434); }
// Solo is yellow and mute amber, as in Ableton and Logic: green stays for
// selection.
inline juce::Colour trackSolo() { return juce::Colour(0xff8a7a26); }
inline juce::Colour trackLock() { return juce::Colour(0xff5c5f6b); }
// Track label colours (After Effects / Premiere style), muted for clip fills.
// Index 0 means "automatic" (by clip kind).
struct TrackLabel { const char* name; std::uint32_t argb; };
inline const std::array<TrackLabel, 9>& trackLabels() {
    static const std::array<TrackLabel, 9> labels {{
        {"Automatic", 0}, {"Red", 0xff6e3a3a}, {"Orange", 0xff735033}, {"Yellow", 0xff6b6331}, {"Green", 0xff34524a},
        {"Aqua", 0xff2f5a5e}, {"Blue", 0xff2f4657}, {"Purple", 0xff4a4260}, {"Pink", 0xff66405a}}};
    return labels;
}

// A short eased fade for hover feedback. It runs only while changing.
class Fade final {
public:
    explicit Fade(juce::Component& owner) : animation(&owner) {
        animation.setValueChangedCallback([&owner](float) { owner.repaint(); });
    }
    void setTarget(bool on) {
        if (on != animation.getTargetState()) { animation.animateTo(on, 120, juce::Easings::createEaseOut()); }
    }
    // Buttons call this from buttonStateChanged(): lit while hovered and enabled.
    void follow(const juce::Button& button) { setTarget(button.isOver() && button.isEnabled()); }
    float value() const { return animation.getProgress(); }
private:
    osci::ToggleAnimationController animation;
};

// A borderless chevron for compact previous/next navigation.
class ChevronButton final : public juce::Button {
public:
    ChevronButton(const juce::String& name, bool pointsRight) : juce::Button(name), right(pointsRight) { setWantsKeyboardFocus(false); }
    void paintButton(juce::Graphics& g, bool highlighted, bool down) override {
        const auto centre = getLocalBounds().toFloat().getCentre();
        const auto size = 3.5f;
        juce::Path chevron;
        const auto direction = right ? 1.0f : -1.0f;
        chevron.startNewSubPath(centre.x - size * .5f * direction, centre.y - size);
        chevron.lineTo(centre.x + size * .5f * direction, centre.y);
        chevron.lineTo(centre.x - size * .5f * direction, centre.y + size);
        const auto alpha = !isEnabled() ? .25f : (down ? 1.0f : highlighted ? .9f : .6f);
        g.setColour(osci::Colours::text().withAlpha(alpha));
        g.strokePath(chevron, juce::PathStrokeType(1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
private:
    bool right;
};

// "More" / "Less": a caption with a chevron that folds a section open.
class Disclosure final : public juce::Button {
public:
    explicit Disclosure(const juce::String& name) : juce::Button(name) {
        setClickingTogglesState(true);
        setWantsKeyboardFocus(false);
    }
    void paintButton(juce::Graphics& g, bool highlighted, bool) override {
        auto area = getLocalBounds().toFloat();
        const auto open = getToggleState();
        const auto colour = highlighted ? osci::Colours::text() : osci::Colours::textMuted();
        const auto arrow = area.removeFromLeft(12).withSizeKeepingCentre(8, 8);
        juce::Path chevron;
        if (open) {
            chevron.startNewSubPath(arrow.getX(), arrow.getY() + 2);
            chevron.lineTo(arrow.getCentreX(), arrow.getBottom() - 2);
            chevron.lineTo(arrow.getRight(), arrow.getY() + 2);
        } else {
            chevron.startNewSubPath(arrow.getX() + 2, arrow.getY());
            chevron.lineTo(arrow.getRight() - 2, arrow.getCentreY());
            chevron.lineTo(arrow.getX() + 2, arrow.getBottom());
        }
        g.setColour(colour);
        g.strokePath(chevron, juce::PathStrokeType(1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setFont(caption());
        g.drawText(open ? "Less" : "More", area.withTrimmedLeft(2), juce::Justification::centredLeft, false);
    }
};

// Properties' rows share one grid: three value columns, then a gutter
// with the modulation glyph and the previous, key and next buttons, so
// "is this animated or driven?" reads down one column. One-value rows put
// the value in the last column.
struct PropertyGrid {
    static constexpr int keyButtons = 12 + 18 + 12, modulateWidth = 18;
    static constexpr int gutter = modulateWidth + gap + keyButtons;
    explicit PropertyGrid(int width) {
        const auto line = width - gutter - gap;
        column = std::min(110, (line - gap * 2) / 3);
        value = 2 * (column + gap);
        modulate = width - gutter;
        keys = width - keyButtons;
    }
    int column = 0, value = 0, modulate = 0, keys = 0;
};

inline void fillPanel(juce::Graphics& g, juce::Rectangle<int> bounds) {
    g.setColour(osci::Colours::surface());
    g.fillRoundedRectangle(bounds.toFloat(), panelRadius);
}
// A panel floating over a page, outlined like the popovers.
inline void fillFloatingPanel(juce::Graphics& g, juce::Rectangle<float> bounds, juce::Colour fill) {
    g.setColour(fill);
    g.fillRoundedRectangle(bounds, panelRadius + 1);
    g.setColour(juce::Colours::white.withAlpha(.08f));
    g.drawRoundedRectangle(bounds.reduced(.5f), panelRadius + 1, 1.0f);
}
// The dark well behind a thumbnail or preview.
inline void fillWell(juce::Graphics& g, juce::Rectangle<float> bounds) {
    g.setColour(juce::Colours::black.withAlpha(.35f));
    g.fillRoundedRectangle(bounds, radius);
}
// A library card's fill as the pointer fades over it.
// Popovers sit a step above the panel they point at.
inline juce::Colour popoverSurface() { return juce::Colour(0xff202126); }
inline juce::Colour cardFill(float hover) { return osci::Colours::veryDark().interpolatedWith(osci::Colours::surfaceRaised(), hover); }
// Many diamonds share one path, filled once.
inline void addDiamond(juce::Path& path, juce::Point<float> centre, float radius) {
    path.startNewSubPath(centre.x, centre.y - radius);
    path.lineTo(centre.x + radius, centre.y);
    path.lineTo(centre.x, centre.y + radius);
    path.lineTo(centre.x - radius, centre.y);
    path.closeSubPath();
}
// After Effects' key glyphs: square hold, diamond linear, hourglass eased,
// circle smooth or Bezier.
enum class KeyShape { hold, linear, eased, smooth };
inline juce::Path keyShapePath(juce::Point<float> centre, float radius, KeyShape shape) {
    juce::Path path;
    if (shape == KeyShape::hold) {
        path.addRoundedRectangle(juce::Rectangle<float>(radius * 1.6f, radius * 1.6f).withCentre(centre), radius * .2f);
    } else if (shape == KeyShape::smooth) {
        path.addEllipse(juce::Rectangle<float>(radius * 1.8f, radius * 1.8f).withCentre(centre));
    } else if (shape == KeyShape::eased) {
        path.addTriangle(centre.x - radius, centre.y - radius, centre.x + radius, centre.y - radius, centre.x, centre.y);
        path.addTriangle(centre.x - radius, centre.y + radius, centre.x + radius, centre.y + radius, centre.x, centre.y);
    } else {
        addDiamond(path, centre, radius);
    }
    return path;
}
inline void drawKeyShape(juce::Graphics& g, juce::Point<float> centre, float radius, KeyShape shape) { g.fillPath(keyShapePath(centre, radius, shape)); }
inline void strokeKeyShape(juce::Graphics& g, juce::Point<float> centre, float radius, KeyShape shape, float thickness) {
    g.strokePath(keyShapePath(centre, radius, shape), juce::PathStrokeType(thickness, juce::PathStrokeType::mitered));
}
// The shape names the key's outgoing interpolation.
inline KeyShape keyShapeOf(const motion::Keyframe& key) {
    if (key.interpolation == motion::Interpolation::hold) { return KeyShape::hold; }
    if (key.interpolation == motion::Interpolation::linear) { return KeyShape::linear; }
    return motion::isEased(key) ? KeyShape::eased : KeyShape::smooth;
}
// A key as the Graph and the Timeline draw it: cut out of what is behind it
// so it never merges with a line, grown a little on hover, and white with a
// ring in its colour when selected.
inline void drawKey(juce::Graphics& g, juce::Point<float> centre, float radius, const motion::Keyframe& key, juce::Colour colour, bool selected, bool hovered, juce::Colour behind) {
    const auto shape = keyShapeOf(key);
    const auto size = hovered ? radius + 1.0f : radius;
    g.setColour(behind);
    drawKeyShape(g, centre, size + 1.75f, shape);
    if (selected) {
        g.setColour(colour);
        strokeKeyShape(g, centre, size + 3.25f, shape, 1.5f);
    }
    g.setColour(selected ? juce::Colours::white : hovered ? colour.brighter(.3f) : colour);
    drawKeyShape(g, centre, size, shape);
}
inline void drawDiamond(juce::Graphics& g, juce::Point<float> centre, float radius, bool filled, float stroke = 1.2f) {
    juce::Path diamond;
    addDiamond(diamond, centre, radius);
    if (filled) { g.fillPath(diamond); } else { g.strokePath(diamond, juce::PathStrokeType(stroke)); }
}
// A dropdown: the field fill with a small stroked chevron, quiet until
// hovered, like the rest of the field-style controls.
inline void paintComboBox(juce::Graphics& g, int width, int height, juce::ComboBox& box) {
    const auto bounds = juce::Rectangle<float>(0, 0, static_cast<float>(width), static_cast<float>(height));
    g.setColour(box.findColour(juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle(bounds, radius);
    const auto hover = box.isEnabled() && box.isMouseOver(true);
    if (hover || box.hasKeyboardFocus(true)) {
        g.setColour(box.hasKeyboardFocus(true) ? osci::Colours::accentColor().withAlpha(.8f) : osci::Colours::outlineSubtle().withAlpha(.8f));
        g.drawRoundedRectangle(bounds.reduced(.5f), radius, 1.0f);
    }
    const auto centre = juce::Point<float>(static_cast<float>(width) - 12.0f, static_cast<float>(height) * .5f);
    juce::Path chevron;
    chevron.startNewSubPath(centre.x - 3.5f, centre.y - 1.75f);
    chevron.lineTo(centre.x, centre.y + 1.75f);
    chevron.lineTo(centre.x + 3.5f, centre.y - 1.75f);
    g.setColour(osci::Colours::text().withAlpha(!box.isEnabled() ? .25f : hover ? .85f : .55f));
    g.strokePath(chevron, juce::PathStrokeType(1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

// A button's fill: one radius everywhere, and a disabled one goes neutral
// so a dimmed primary never reads as available.
inline void paintButtonBackground(juce::Graphics& g, juce::Button& button, juce::Colour colour, bool highlighted, bool down) {
    const auto bounds = button.getLocalBounds().toFloat().reduced(.5f);
    auto fill = colour;
    if (!button.isEnabled()) {
        fill = juce::Colours::white.withAlpha(.06f);
    } else if (down) {
        fill = colour.brighter(.25f);
    } else if (highlighted) {
        fill = colour.brighter(.12f);
    }
    g.setColour(fill);
    g.fillRoundedRectangle(bounds, buttonRadius);
}

// Routes the editor's menus, buttons and pickers through the type styles.
class LookAndFeel final : public PluginLookAndFeel {
public:
    // One button fill everywhere unless a button says otherwise.
    LookAndFeel() {
        setColour(juce::TextButton::buttonColourId, osci::Colours::surfaceRaised());
        // Selected text: a muted accent behind white, not bright green.
        setColour(juce::TextEditor::highlightColourId, osci::Colours::accentColor().withAlpha(.35f));
        setColour(juce::TextEditor::highlightedTextColourId, juce::Colours::white);
    }
    juce::Font getMenuBarFont(juce::MenuBarComponent&, int, const juce::String&) override { return body(); }
    juce::Font getPopupMenuFont() override { return body(); }
    juce::Font getTextButtonFont(juce::TextButton&, int) override { return body(); }
    juce::Font getComboBoxFont(juce::ComboBox&) override { return body(); }
    void drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour& colour, bool highlighted, bool down) override { paintButtonBackground(g, button, colour, highlighted, down); }
    // Popovers float a step above the panels they point at, with a shadow.
    void drawCallOutBoxBackground(juce::CallOutBox& box, juce::Graphics& g, const juce::Path& path, juce::Image& cachedImage) override {
        if (cachedImage.isNull()) {
            cachedImage = juce::Image(juce::Image::ARGB, box.getWidth(), box.getHeight(), true);
            juce::Graphics shadow(cachedImage);
            juce::DropShadow(juce::Colours::black.withAlpha(.5f), 24, {0, 8}).drawForPath(shadow, path);
        }
        g.drawImageAt(cachedImage, 0, 0);
        g.setColour(popoverSurface());
        g.fillPath(path);
        g.setColour(juce::Colours::white.withAlpha(.08f));
        g.strokePath(path, juce::PathStrokeType(1.0f));
    }
    int getCallOutBoxBorderSize(const juce::CallOutBox&) override { return 16; }
    float getCallOutBoxCornerSize(const juce::CallOutBox&) override { return panelRadius + 1.0f; }
    void drawComboBox(juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box) override { paintComboBox(g, width, height, box); }
    // Combo text keeps the field inset and clears the arrow.
    void positionComboBoxText(juce::ComboBox& box, juce::Label& label) override {
        PluginLookAndFeel::positionComboBoxText(box, label);
        const auto left = 8 - getLabelBorderSize(label).getLeft();
        label.setBounds(left, 1, juce::jmax(1, box.getWidth() - 22 - left), box.getHeight() - 2);
    }
    // Tick boxes keep body text and one tick size at any height.
    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& button, bool highlighted, bool down) override {
        constexpr float tick = 14.0f;
        drawTickBox(g, button, 4.0f, (static_cast<float>(button.getHeight()) - tick) * 0.5f, tick, tick, button.getToggleState(), button.isEnabled(), highlighted, down);
        g.setColour(button.findColour(juce::ToggleButton::textColourId).withMultipliedAlpha(button.isEnabled() ? 1.0f : 0.5f));
        g.setFont(body());
        g.drawFittedText(button.getButtonText(), button.getLocalBounds().withTrimmedLeft(4 + static_cast<int>(tick) + 8).withTrimmedRight(2), juce::Justification::centredLeft, 2);
    }
    // Plain labels align with the controls around them; labels drawn as
    // fields keep their inner margin.
    juce::BorderSize<int> getLabelBorderSize(juce::Label& label) override {
        const auto border = label.getBorderSize();
        if (label.findColour(juce::Label::backgroundColourId).isTransparent() && !label.isBeingEdited()) { return {border.getTop(), 0, border.getBottom(), 0}; }
        return border;
    }
};
}

namespace motion::style {
// Dialogs keep the overlay's look but use the editor's type sizes.
class DialogLookAndFeel final : public osci::OverlayLookAndFeel {
public:
    DialogLookAndFeel() {
        setColour(juce::TextEditor::highlightColourId, osci::Colours::accentColor().withAlpha(.35f));
        setColour(juce::TextEditor::highlightedTextColourId, juce::Colours::white);
    }
    juce::Font getPopupMenuFont() override { return body(); }
    juce::Font getTextButtonFont(juce::TextButton&, int) override { return body(); }
    juce::Font getComboBoxFont(juce::ComboBox&) override { return body(); }
    void drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour& colour, bool highlighted, bool down) override { paintButtonBackground(g, button, colour, highlighted, down); }
    void drawComboBox(juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box) override { paintComboBox(g, width, height, box); }
    // Combo text starts where a dialog field's text does.
    void positionComboBoxText(juce::ComboBox& box, juce::Label& label) override {
        osci::OverlayLookAndFeel::positionComboBoxText(box, label);
        label.setBounds(fieldIndent - label.getBorderSize().getLeft(), 1, juce::jmax(1, box.getWidth() - 24 - fieldIndent + label.getBorderSize().getLeft()), box.getHeight() - 2);
    }
    static constexpr int fieldIndent = 8;
};

// The one action a panel exists for (Save, Add, Bake) stands out in the accent.
inline void makePrimary(juce::Button& button) { button.setColour(juce::TextButton::buttonColourId, accentStrong()); }

// The Scene while a source is written in it (Lua, text, drawing) is a
// mode: a raised header names it ("Editing Orbit.lua", "New Lua source")
// and holds Cancel and the one action (Add for a new source, Apply
// otherwise), over a dark page. A problem turns the header's dot red.
namespace sceneEditor {
inline constexpr int headerHeight = 30, buttonWidth = 72, inset = 12;
// Places the header's buttons and returns the room left for the name.
inline juce::Rectangle<int> layoutHeader(juce::Rectangle<int> bounds, juce::Component& primary, juce::Component& cancel) {
    auto header = bounds.removeFromTop(headerHeight).reduced(inset, 4);
    primary.setBounds(header.removeFromRight(buttonWidth));
    header.removeFromRight(gap + 2);
    cancel.setBounds(header.removeFromRight(buttonWidth));
    header.removeFromRight(padding);
    return header;
}
inline juce::Rectangle<int> page(juce::Rectangle<int> bounds) { return bounds.withTrimmedTop(headerHeight + 1); }
// Where the source's name goes, after the mode.
inline int modeWidth(const juce::String& mode = "Editing") { return juce::roundToInt(juce::TextLayout::getStringWidth(body(), mode)) + 6; }
inline juce::Rectangle<int> nameArea(juce::Rectangle<int> titleArea, const juce::String& mode = "Editing") { return titleArea.withTrimmedLeft(modeWidth(mode)); }
// `titleArea` is what layoutHeader returned; `name` is drawn after the
// mode unless the editor shows its own (an editable name).
inline void paint(juce::Graphics& g, juce::Rectangle<int> bounds, juce::Rectangle<int> titleArea, const juce::String& name = {}, const juce::String& mode = "Editing", bool problem = false) {
    auto header = bounds.removeFromTop(headerHeight);
    g.setColour(osci::Colours::surfaceRaised());
    g.fillRect(header);
    g.setColour(juce::Colours::white.withAlpha(.08f));
    g.fillRect(bounds.removeFromTop(1));
    g.setColour(osci::Colours::veryDark());
    g.fillRect(bounds);
    auto line = titleArea.withY(header.getY()).withHeight(header.getHeight());
    if (problem) {
        g.setColour(error());
        g.fillEllipse(juce::Rectangle<float>(6, 6).withCentre({static_cast<float>(line.getX()) + 3.0f, static_cast<float>(header.getCentreY())}));
        line.removeFromLeft(12);
    }
    g.setFont(body());
    g.setColour(osci::Colours::textMuted());
    g.drawText(mode, line.removeFromLeft(modeWidth(mode)), juce::Justification::centredLeft, false);
    if (name.isEmpty()) { return; }
    g.setFont(title());
    g.setColour(osci::Colours::text());
    g.drawText(name, line, juce::Justification::centredLeft, true);
}
}

// The sections under an object's properties (timing, composition, text
// animation): a muted heading, then four captioned fields two to a row.
namespace inspector {
inline constexpr int headingHeight = 16;
inline constexpr int row = 28;
inline void styleCaption(juce::Label& label, const juce::String& text) {
    label.setText(text, juce::dontSendNotification);
    label.setFont(caption());
    label.setColour(juce::Label::textColourId, osci::Colours::textMuted());
}
// Section headings (Timing, Characters) read like Properties' own
// (Transform, Effects): bold caption, brighter than a field's label.
inline void styleHeading(juce::Label& label, const juce::String& text) {
    label.setText(text, juce::dontSendNotification);
    label.setFont(heading());
    label.setColour(juce::Label::textColourId, osci::Colours::text());
    label.setBorderSize({});
}
// Two fields to a row, each with its caption inside on the left as X, Y
// and Z carry their axis, and the value on the right. They share the left
// and right edges of Properties' value columns, clear of the keys' gutter.
inline void layoutFields(juce::Rectangle<int>& area, std::array<juce::Label, 4>& captions, const std::array<juce::Component*, 4>& fields) {
    const PropertyGrid grid(area.getWidth());
    const auto right = grid.value + grid.column;
    const auto half = (right - gap) / 2;
    for (std::size_t line = 0; line < 2; ++line) {
        auto bounds = area.removeFromTop(row).withWidth(right);
        for (std::size_t column = 0; column < 2; ++column) {
            const auto index = line * 2 + column;
            const auto cell = (column == 0 ? bounds.withWidth(half) : bounds.withLeft(bounds.getRight() - half)).reduced(0, 3);
            auto* combo = dynamic_cast<juce::ComboBox*>(fields[index]);
            if (combo != nullptr) { combo->setJustificationType(juce::Justification::centredRight); }
            fields[index]->setBounds(cell);
            auto& caption = captions[index];
            caption.setInterceptsMouseClicks(false, false);
            caption.setBounds(cell.withTrimmedLeft(6).withWidth(juce::roundToInt(juce::TextLayout::getStringWidth(caption.getFont(), caption.getText())) + 2));
            caption.toFront(false);
        }
    }
}
}

// One form layout for every dialog: captions beside or above fields, and
// the action buttons right-aligned on the last line.
namespace dialog {
inline constexpr int margin = 12;
inline constexpr int row = 28;
inline constexpr int rowGap = 10;
inline constexpr int buttonHeight = 30;
inline constexpr int buttonWidth = 120;
inline void caption(juce::Label& label) {
    label.setFont(body());
    label.setColour(juce::Label::textColourId, osci::Colours::textMuted());
    label.setBorderSize({});
    label.setJustificationType(juce::Justification::centredLeft);
}
// Lays the buttons out right to left and returns the rest of the line.
inline juce::Rectangle<int> footer(juce::Rectangle<int>& area, std::initializer_list<juce::Component*> buttons, int width = buttonWidth) {
    auto line = area.removeFromBottom(buttonHeight);
    for (auto* button : buttons) {
        button->setBounds(line.removeFromRight(width));
        line.removeFromRight(8);
    }
    area.removeFromBottom(rowGap);
    return line;
}
// A caption beside its field.
inline void formRow(juce::Rectangle<int>& area, juce::Label& label, juce::Component& field, int captionWidth) {
    auto bounds = area.removeFromTop(row);
    label.setBounds(bounds.removeFromLeft(captionWidth));
    field.setBounds(bounds);
    area.removeFromTop(rowGap);
}
}

// Fields recess into the panel they sit on. Sheets (marked with this
// property) sit on a darker panel than the editor's, so theirs go darker.
inline constexpr const char* sheetProperty = "motionSheet";
inline juce::Colour fieldFill(const juce::Component& component) {
    for (auto* parent = &component; parent != nullptr; parent = parent->getParentComponent()) {
        if (parent->getProperties().contains(sheetProperty)) { return osci::Colours::surfaceSunken(); }
    }
    return osci::Colours::veryDark();
}
// One field look for dialogs and popovers: dark, borderless, text 8 px in.
inline void styleField(juce::Component& component) {
    auto* editor = dynamic_cast<juce::TextEditor*>(&component);
    auto* combo = dynamic_cast<juce::ComboBox*>(&component);
    if (editor != nullptr) {
        // A single line sits on the same baseline as the caption beside it.
        if (!editor->isMultiLine()) { editor->setJustification(editor->getJustificationType().getOnlyHorizontalFlags() | juce::Justification::verticallyCentred); }
        // A centred field centres its text below the top indent, so the
        // indent would push single lines low.
        const auto centred = (editor->getJustificationType().getFlags() & juce::Justification::verticallyCentred) != 0;
        editor->setIndents(DialogLookAndFeel::fieldIndent - editor->getBorder().getLeft(), centred ? 0 : editor->getTopIndent());
        editor->setColour(juce::TextEditor::backgroundColourId, fieldFill(component));
        editor->setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    }
    if (combo != nullptr) {
        combo->setColour(juce::ComboBox::backgroundColourId, fieldFill(component));
        combo->setColour(juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
    }
}
inline void styleFields(juce::Component& root) {
    styleField(root);
    for (auto* child : root.getChildren()) { styleFields(*child); }
}
// Gives a dialog's controls the editor's type sizes and field look. The
// overlay hands its controls its own look whenever it lays out, so the
// editor calls this after every overlay layout.
inline void restyleDialog(juce::Component& component, DialogLookAndFeel& look) {
    auto* current = &component.getLookAndFeel();
    if (current != &look && dynamic_cast<osci::OverlayLookAndFeel*>(current) != nullptr) {
        styleField(component);
        component.setLookAndFeel(&look);
    }
    for (auto* child : component.getChildren()) { restyleDialog(*child, look); }
}
// Releases the look before it is destroyed.
inline void unstyleDialogs(juce::Component& component, DialogLookAndFeel& look) {
    if (&component.getLookAndFeel() == &look) { component.setLookAndFeel(nullptr); }
    for (auto* child : component.getChildren()) { unstyleDialogs(*child, look); }
}
}


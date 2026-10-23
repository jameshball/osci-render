#pragma once

#include <type_traits>

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
// The three corner radii: small for swatches, key glyphs and the segments
// inside a control; control for buttons, fields, combos, chips, menu items
// and library rows; panel for panels, popovers and floating strips.
inline constexpr float radiusSmall = 2.0f, radiusControl = 4.0f, radiusPanel = 6.0f;
// The three icon sizes: toolbars and row actions, labelled buttons, and
// chips. A badge or a lock in a narrow column may be smaller, and an empty
// state's illustration larger.
inline constexpr float iconLarge = 18.0f, iconMedium = 16.0f, iconSmall = 14.0f;

// osci-motion's own green, as in its icon and logo (osci-render's and
// sosci's too).
inline juce::Colour brand() { return juce::Colour(0xff56d836); }
// Green means one thing: what is selected or focused, and the one action a
// panel exists for. A switch, tool or toggle that is on is neutral. It is
// the brand's green, so there is one green in the editing UI.
inline juce::Colour accent() { return brand(); }
// The wash behind a selected row, region or choice.
inline juce::Colour accentFill() { return accent().withAlpha(.12f); }
// The primary action's fill.
inline juce::Colour accentStrong() { return accent().withAlpha(.5f); }
// A toggle, tool or switch that is on.
inline juce::Colour onFill() { return juce::Colours::white.withAlpha(.16f); }
// Neutral washes over the dark surfaces, faintest first: what the pointer is
// over; a control at rest, or a neutral selection; a hairline (a rule, a
// separator, a border); and a control pressed.
inline juce::Colour hoverFill() { return juce::Colours::white.withAlpha(.05f); }
inline juce::Colour restFill() { return juce::Colours::white.withAlpha(.07f); }
inline juce::Colour hairline() { return juce::Colours::white.withAlpha(.08f); }
inline juce::Colour pressFill() { return juce::Colours::white.withAlpha(.12f); }
// Every button's wash: the hover fill as the pointer fades in (`hover`
// from 0 to 1), the press fill while held.
inline juce::Colour wash(float hover, bool down) { return down ? pressFill() : hoverFill().withMultipliedAlpha(hover); }
// The time and value grids behind the timeline and the Graph: the snap
// grid's fine lines, the labelled ticks', and the Graph's zero.
inline juce::Colour gridMinor() { return juce::Colours::white.withAlpha(.03f); }
inline juce::Colour gridMajor() { return juce::Colours::white.withAlpha(.055f); }
inline juce::Colour gridZero() { return juce::Colours::white.withAlpha(.12f); }

// The only type styles in osci-motion. All UI text uses one of these;
// MotionTypographyTest fails if code anywhere else asks for a font.
// title: panel and tab names, the inspector heading, group names.
// body: values, names, menus, buttons, pickers and editors.
// caption: field labels, hints, secondary details and the ruler.
// heading: section headings in Properties, popovers and forms.
// mono: the position readout and code; monoCaption: the full-screen
// Scope's time, at caption size.
// display: the app's name on the start screen, and nothing else.
// The separator between short facts, as in the status bar.
inline juce::String dot() { return juce::String::fromUTF8(" \xc2\xb7 "); }
// A number without decimals when it is whole, else with this many.
inline juce::String trimmed(double value, int decimals) { return juce::String(value, value == std::round(value) ? 0 : decimals); }
inline juce::String hertz(double value) { return trimmed(value, 2) + " Hz"; }
// A sample rate: 48 kHz, 44.1 kHz.
inline juce::String kilohertz(double rate) { return juce::String(rate / 1000.0, rate == std::floor(rate / 1000.0) * 1000.0 ? 0 : 1) + " kHz"; }
inline juce::String bpm(double value) { return trimmed(value, 2) + " BPM"; }
inline juce::Font title() { return juce::Font(juce::FontOptions(13.0f, juce::Font::bold)); }
inline juce::Font body() { return juce::Font(juce::FontOptions(13.0f)); }
inline juce::Font caption() { return juce::Font(juce::FontOptions(11.0f)); }
// A text button set with this property true is lettered in caption() rather
// than body(): one sitting in a line of caption text.
inline const juce::Identifier captionButton {"motionCaptionButton"};
// How wide a caption laid over a field is, set on the field, so its value
// keeps clear of it.
inline const juce::Identifier captionWidth {"motionCaptionWidth"};
inline juce::Font buttonFont(const juce::Button& button) { return button.getProperties()[captionButton] ? caption() : body(); }
// A label's whole text as its tooltip, only while its bounds cut it short
// (roughly, for a label tall enough to wrap onto more lines).
inline void tooltipWhenCut(juce::Label& label) {
    auto& look = label.getLookAndFeel();
    const auto font = look.getLabelFont(label);
    const auto area = look.getLabelBorderSize(label).subtractedFrom(label.getLocalBounds());
    const auto lines = std::max(1, static_cast<int>(static_cast<float>(area.getHeight()) / font.getHeight()));
    const auto cut = juce::TextLayout::getStringWidth(font, label.getText()) > static_cast<float>(area.getWidth() * lines);
    label.setTooltip(cut ? label.getText() : juce::String());
}
inline juce::Font heading() { return juce::Font(juce::FontOptions(11.0f, juce::Font::bold)); }
inline juce::Font display() { return juce::Font(juce::FontOptions(19.0f, juce::Font::bold)); }
inline juce::Font mono() { return juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain)); }
inline juce::Font monoCaption() { return juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::plain)); }
// A shortcut written "Cmd+Shift+F9" as the platform shows it: ⇧⌘F9 on macOS,
// Ctrl+Shift+F9 elsewhere, with arrow keys as arrows (⇧←) and Delete as ⌫.
// Mouse gestures ("Alt+wheel") keep their words.
inline juce::String shortcutText(const juce::String& raw) {
   #if JUCE_MAC
    if (raw.containsIgnoreCase("wheel") || raw.containsIgnoreCase("drag") || raw.containsIgnoreCase("click") || raw.contains(" ")) { return raw.replace("Cmd", juce::String::fromUTF8("\u2318")); }
    const auto arrow = [](const juce::String& key) {
        for (const auto& [word, glyph] : std::initializer_list<std::pair<const char*, const char*>> {{"Left", "\u2190"}, {"Right", "\u2192"}, {"Up", "\u2191"}, {"Down", "\u2193"}, {"Delete", "\u232b"}}) {
            if (key == word) { return juce::String::fromUTF8(glyph); }
        }
        return key;
    };
    auto parts = juce::StringArray::fromTokens(raw, "+", "");
    if (raw.endsWith("+")) { parts.removeEmptyStrings(); parts.add("+"); }
    if (parts.size() < 2) { return arrow(raw); }
    const auto key = arrow(parts[parts.size() - 1]);
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

// "More" / "Less" (or its own words): a caption with a chevron that folds a
// section open.
class Disclosure final : public juce::Button {
public:
    explicit Disclosure(const juce::String& name, juce::String closedText = "More", juce::String openText = "Less") : juce::Button(name), closed(std::move(closedText)), opened(std::move(openText)) {
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
        g.drawText(open ? opened : closed, area.withTrimmedLeft(2), juce::Justification::centredLeft, false);
    }

private:
    juce::String closed, opened;
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
    g.fillRoundedRectangle(bounds.toFloat(), radiusPanel);
}
// A panel floating over a page, outlined like the popovers.
inline void fillFloatingPanel(juce::Graphics& g, juce::Rectangle<float> bounds, juce::Colour fill) {
    g.setColour(fill);
    g.fillRoundedRectangle(bounds, radiusPanel);
    g.setColour(hairline());
    g.drawRoundedRectangle(bounds.reduced(.5f), radiusPanel, 1.0f);
}
// The dark well behind a thumbnail or preview.
inline void fillWell(juce::Graphics& g, juce::Rectangle<float> bounds) {
    g.setColour(juce::Colours::black.withAlpha(.35f));
    g.fillRoundedRectangle(bounds, radiusControl);
}
// Every scrolling view: a thin quiet scrollbar, and its content fading out
// into the surface behind it at an edge with more beyond it.
inline void fadeScrolling(osci::ScrollFadeViewport& viewport, juce::Colour behind = osci::Colours::surface()) {
    viewport.setScrollBarsShown(true, false);
    viewport.setScrollBarThickness(6);
    viewport.setFadeHeight(32);
    viewport.setColour(osci::scrollFadeOverlayBackgroundColourId, behind);
}
// Popovers sit a step above the panel they point at.
inline juce::Colour popoverSurface() { return juce::Colour(0xff202126); }

// The libraries' rows (Sources, Effects, Modulators) look alike: a face 40 px
// tall every 44 px, a 36 px picture in a well 8 px in, then the name with
// any detail under it. Nothing is behind a row at rest; the pointer washes
// it, and a selected one lifts with an accent bar down its left edge.
namespace libraryRow {
inline constexpr int pitch = 44, face = pitch - gap, picture = 36, inset = 8;
inline juce::Rectangle<int> pictureArea(juce::Rectangle<int> faceBounds) { return faceBounds.withTrimmedLeft(inset).withWidth(picture).withSizeKeepingCentre(picture, picture); }
inline juce::Rectangle<int> textArea(juce::Rectangle<int> faceBounds) { return faceBounds.withTrimmedLeft(inset + picture + padding); }
// `hover` from 0 to 1.
inline void paintFace(juce::Graphics& g, juce::Rectangle<float> faceBounds, float hover, bool selected) {
    if (selected) {
        g.setColour(osci::Colours::surfaceRaised());
        g.fillRoundedRectangle(faceBounds, radiusControl);
        g.setColour(accent());
        g.fillRect(faceBounds.withWidth(2.0f).reduced(0.0f, 5.0f));
    } else if (hover > 0.0f) {
        g.setColour(hoverFill().withMultipliedAlpha(hover));
        g.fillRoundedRectangle(faceBounds, radiusControl);
    }
}
// The name alone sits centred; with a detail, the two lines share the face.
inline void paintText(juce::Graphics& g, juce::Rectangle<int> area, const juce::String& name, const juce::String& detail, juce::Colour detailColour) {
    g.setColour(osci::Colours::text());
    g.setFont(body());
    if (detail.isEmpty()) {
        g.drawText(name, area, juce::Justification::centredLeft, true);
        return;
    }
    g.drawText(name, area.removeFromTop(area.getHeight() / 2 + 2), juce::Justification::bottomLeft, true);
    g.setColour(detailColour);
    g.setFont(caption());
    g.drawText(detail, area, juce::Justification::topLeft, true);
}
}
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
    g.fillRoundedRectangle(bounds, radiusControl);
    const auto hover = box.isEnabled() && box.isMouseOver(true);
    if (hover || box.hasKeyboardFocus(true)) {
        g.setColour(box.hasKeyboardFocus(true) ? accent().withAlpha(.8f) : osci::Colours::outlineSubtle().withAlpha(.8f));
        g.drawRoundedRectangle(bounds.reduced(.5f), radiusControl, 1.0f);
    }
    const auto centre = juce::Point<float>(static_cast<float>(width) - 12.0f, static_cast<float>(height) * .5f);
    juce::Path chevron;
    chevron.startNewSubPath(centre.x - 3.5f, centre.y - 1.75f);
    chevron.lineTo(centre.x, centre.y + 1.75f);
    chevron.lineTo(centre.x + 3.5f, centre.y - 1.75f);
    g.setColour(osci::Colours::text().withAlpha(!box.isEnabled() ? .25f : hover ? .85f : .55f));
    g.strokePath(chevron, juce::PathStrokeType(1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

// A button's fill: one radius everywhere. A disabled button is only an
// outline, so it never reads as available nor as a twin of the buttons
// beside it; a quiet button (no fill) shows its shape under the pointer.
inline void paintButtonBackground(juce::Graphics& g, juce::Button& button, juce::Colour colour, bool highlighted, bool down) {
    // Full bounds, so a button is exactly as tall and wide as a field beside it.
    const auto bounds = button.getLocalBounds().toFloat();
    if (!button.isEnabled()) {
        if (colour.isTransparent()) { return; }
        g.setColour(hairline());
        g.drawRoundedRectangle(bounds.reduced(.5f), radiusControl, 1.0f);
        return;
    }
    if (colour.isTransparent()) {
        if (!down && !highlighted) { return; }
        g.setColour(wash(1.0f, down));
    } else {
        g.setColour(down ? colour.brighter(.25f) : highlighted ? colour.brighter(.12f) : colour);
    }
    g.fillRoundedRectangle(bounds, radiusControl);
}

// Routes the editor's menus, buttons and pickers through the type styles.
class LookAndFeel final : public PluginLookAndFeel {
public:
    // One button fill everywhere unless a button says otherwise.
    LookAndFeel() {
        setColour(juce::TextButton::buttonColourId, osci::Colours::surfaceRaised());
        // Selected text: a muted accent behind white, not bright green.
        setColour(juce::TextEditor::highlightColourId, accent().withAlpha(.35f));
        setColour(juce::TextEditor::highlightedTextColourId, juce::Colours::white);
        // Focus and progress in motion's green, not the shared accent.
        setColour(juce::TextEditor::focusedOutlineColourId, accent());
        setColour(juce::ProgressBar::foregroundColourId, accent());
        setColour(juce::PopupMenu::highlightedBackgroundColourId, accent());
        // Scroll thumbs are quiet grey everywhere, never as bright as text.
        setColour(juce::ScrollBar::thumbColourId, juce::Colours::white.withAlpha(.16f));
    }
    juce::Font getMenuBarFont(juce::MenuBarComponent&, int, const juce::String&) override { return body(); }
    // The menus sit on the window like the transport beside them, with no
    // bar of their own.
    void drawMenuBarBackground(juce::Graphics&, int, int, bool, juce::MenuBarComponent&) override {}
    static constexpr int menuPadding = 8;
    int getMenuBarItemWidth(juce::MenuBarComponent&, int, const juce::String& text) override { return juce::roundToInt(std::ceil(juce::TextLayout::getStringWidth(body(), text))) + 2 * menuPadding; }
    // Quiet like the transport's icons until hovered; an open menu lifts.
    void drawMenuBarItem(juce::Graphics& g, int width, int height, int, const juce::String& text, bool over, bool open, bool, juce::MenuBarComponent& bar) override {
        if (open || (over && bar.isEnabled())) {
            g.setColour(wash(1.0f, open));
            g.fillRoundedRectangle(juce::Rectangle<float>(static_cast<float>(width), static_cast<float>(height)).reduced(0.0f, 4.0f), radiusControl);
        }
        g.setColour(osci::Colours::text().withAlpha(bar.isEnabled() ? (open || over ? 1.0f : .72f) : .4f));
        g.setFont(body());
        g.drawFittedText(text, 0, 0, width, height, juce::Justification::centred, 1);
    }
    juce::Font getPopupMenuFont() override { return body(); }
    // Room for an icon beside any menu item's name (the width a menu measures
    // is its text's alone), so a name beside one never squeezes to fit.
    void getIdealPopupMenuItemSize(const juce::String& text, bool separator, int standardHeight, int& width, int& height) override {
        PluginLookAndFeel::getIdealPopupMenuItemSize(text, separator, standardHeight, width, height);
        if (!separator) { width += 22; }
    }
    juce::Font getTextButtonFont(juce::TextButton& button, int) override { return buttonFont(button); }
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
        g.setColour(hairline());
        g.strokePath(path, juce::PathStrokeType(1.0f));
    }
    int getCallOutBoxBorderSize(const juce::CallOutBox&) override { return 16; }
    float getCallOutBoxCornerSize(const juce::CallOutBox&) override { return radiusPanel; }
    void drawComboBox(juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box) override { paintComboBox(g, width, height, box); }
    // Combo text keeps the field inset, starts clear of a caption laid over
    // it (see captionWidth) and stops clear of the chevron, as a dialog's does.
    void positionComboBoxText(juce::ComboBox& box, juce::Label& label) override {
        PluginLookAndFeel::positionComboBoxText(box, label);
        const auto caption = static_cast<int>(box.getProperties()[motion::style::captionWidth]);
        const auto left = (caption > 0 ? 6 + caption : 8) - getLabelBorderSize(label).getLeft();
        label.setBounds(left, 1, juce::jmax(1, box.getWidth() - chevronClearance - left), box.getHeight() - 2);
    }
    // Where combo text ends, short of the box's right edge.
    static constexpr int chevronClearance = 24;
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
        setColour(juce::TextEditor::highlightColourId, accent().withAlpha(.35f));
        setColour(juce::TextEditor::focusedOutlineColourId, accent().withAlpha(.9f));
        setColour(juce::PopupMenu::highlightedBackgroundColourId, accent());
        setColour(juce::TextEditor::highlightedTextColourId, juce::Colours::white);
        setColour(juce::ScrollBar::thumbColourId, juce::Colours::white.withAlpha(.16f));
    }
    juce::Font getPopupMenuFont() override { return body(); }
    juce::Font getTextButtonFont(juce::TextButton& button, int) override { return buttonFont(button); }
    juce::Font getComboBoxFont(juce::ComboBox&) override { return body(); }
    void drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour& colour, bool highlighted, bool down) override { paintButtonBackground(g, button, colour, highlighted, down); }
    void drawComboBox(juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box) override { paintComboBox(g, width, height, box); }
    // Combo text starts where a dialog field's text does.
    void positionComboBoxText(juce::ComboBox& box, juce::Label& label) override {
        osci::OverlayLookAndFeel::positionComboBoxText(box, label);
        label.setBounds(fieldIndent - label.getBorderSize().getLeft(), 1, juce::jmax(1, box.getWidth() - style::LookAndFeel::chevronClearance - fieldIndent + label.getBorderSize().getLeft()), box.getHeight() - 2);
    }
    static constexpr int fieldIndent = 8;
};

// The one action a panel exists for (Save, Add, Bake) stands out in the accent.
inline void makePrimary(juce::Button& button) { button.setColour(juce::TextButton::buttonColourId, accentStrong()); }
// An action that loses something (Discard, Remove): red, not the accent.
inline void makeDestructive(juce::Button& button) { button.setColour(juce::TextButton::buttonColourId, osci::Colours::danger().darker(.35f)); }
// A minor action beside the main ones: text only until hovered, red when
// it throws something away.
inline void makeQuiet(juce::Button& button, bool destructive = false) {
    button.setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    button.setColour(juce::TextButton::textColourOffId, destructive ? error() : osci::Colours::textMuted());
}

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
// and right edges of Properties' value columns, clear of the keys' gutter;
// fields that can't be keyed (`keyless`) take that gutter too.
// Visible fields only, two to a line in order.
template <std::size_t count>
inline void layoutFields(juce::Rectangle<int>& area, std::array<juce::Label, count>& captions, const std::type_identity_t<std::array<juce::Component*, count>>& fields, bool keyless = false) {
    const PropertyGrid grid(area.getWidth());
    const auto right = keyless ? area.getWidth() : grid.value + grid.column;
    const auto half = (right - gap) / 2;
    std::vector<std::size_t> shown;
    for (std::size_t index = 0; index < fields.size(); ++index) {
        if (fields[index]->isVisible()) { shown.push_back(index); }
    }
    for (std::size_t line = 0; line * 2 < shown.size(); ++line) {
        auto bounds = area.removeFromTop(row).withWidth(right);
        for (std::size_t column = 0; column < 2 && line * 2 + column < shown.size(); ++column) {
            const auto index = shown[line * 2 + column];
            // A field alone on the last line takes the whole line.
            const auto alone = line * 2 + 1 >= shown.size();
            const auto cell = (alone ? bounds : column == 0 ? bounds.withWidth(half) : bounds.withLeft(bounds.getRight() - half)).reduced(0, 3);
            auto* combo = dynamic_cast<juce::ComboBox*>(fields[index]);
            if (combo != nullptr) { combo->setJustificationType(juce::Justification::centredRight); }
            auto& caption = captions[index];
            caption.setInterceptsMouseClicks(false, false);
            const auto width = juce::roundToInt(juce::TextLayout::getStringWidth(caption.getFont(), caption.getText())) + 2;
            // Known before the field is placed: a combo puts its text clear of it then.
            fields[index]->getProperties().set(captionWidth, width + 4);
            fields[index]->setBounds(cell);
            caption.setBounds(cell.withTrimmedLeft(6).withWidth(width));
            auto* label = dynamic_cast<juce::Label*>(fields[index]);
            if (label != nullptr) { label->setBorderSize({0, 6 + width + 4, 0, 6}); }
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
inline bool inSheet(const juce::Component& component) {
    for (auto* parent = &component; parent != nullptr; parent = parent->getParentComponent()) {
        if (parent->getProperties().contains(sheetProperty)) { return true; }
    }
    return false;
}
inline juce::Colour fieldFill(const juce::Component& component) { return inSheet(component) ? osci::Colours::surfaceSunken() : osci::Colours::veryDark(); }
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
// Gives a dialog's controls the editor's type sizes, including any that set
// the overlay's look themselves (the audio settings do). Sheets recess their
// fields; shared dialogs keep the overlay's, as in osci-render.
inline void restyleDialog(juce::Component& component, DialogLookAndFeel& look) {
    auto* current = &component.getLookAndFeel();
    if (current != &look && dynamic_cast<osci::OverlayLookAndFeel*>(current) != nullptr) { component.setLookAndFeel(&look); }
    if (&component.getLookAndFeel() == &look && inSheet(component)) { styleField(component); }
    for (auto* child : component.getChildren()) { restyleDialog(*child, look); }
}
// Releases the look before it is destroyed.
inline void unstyleDialogs(juce::Component& component, DialogLookAndFeel& look) {
    if (&component.getLookAndFeel() == &look) { component.setLookAndFeel(nullptr); }
    for (auto* child : component.getChildren()) { unstyleDialogs(*child, look); }
}
}


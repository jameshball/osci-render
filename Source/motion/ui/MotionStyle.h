#pragma once

#include <JuceHeader.h>
#include "../../LookAndFeel.h"

// One place for osci-motion's visual language: a restrained dark palette built
// on the shared osci theme, four type styles and a 4 px spacing grid.
namespace motion::style {
inline constexpr int gap = 4;
inline constexpr int padding = 8;
inline constexpr int controlHeight = 24;
inline constexpr int rowHeight = 26;
inline constexpr int headerHeight = 28;
inline constexpr int transportHeight = 36;
inline constexpr float radius = 3.0f;
inline constexpr float panelRadius = 5.0f;

// The only type styles in osci-motion. All UI text uses one of these;
// MotionTypographyTest fails if code anywhere else asks for a font.
// title: panel and tab names, the inspector heading, group names.
// body: values, names, menus, buttons, pickers and editors.
// caption: field labels, hints, secondary details and the ruler.
// mono: the position readout and code.
// The separator between short facts, as in the status bar.
inline juce::String dot() { return juce::String::fromUTF8(" \xc2\xb7 "); }
inline juce::Font title() { return juce::Font(juce::FontOptions(13.0f, juce::Font::bold)); }
inline juce::Font body() { return juce::Font(juce::FontOptions(13.0f)); }
inline juce::Font caption() { return juce::Font(juce::FontOptions(11.0f)); }
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


// Beam-adjacent accents: keys and the playhead share the phosphor green.
inline juce::Colour key() { return juce::Colour(0xff72de98); }
inline juce::Colour playhead() { return key(); }
inline juce::Colour marker() { return juce::Colour(0xffcfb779); }
inline juce::Colour tempo() { return juce::Colour(0xff8fb6e8); }
// The selected stroke or point, brighter than the key green.
inline juce::Colour selection() { return juce::Colour(0xff9affb3); }
// A modulated property's value after its drivers, beside its keys.
inline juce::Colour result() { return juce::Colour(0xff80baff); }
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
inline juce::Colour visualClip() { return juce::Colour(0xff34524a); }
inline juce::Colour audioClip() { return juce::Colour(0xff2f4657); }
inline juce::Colour compositionClip() { return juce::Colour(0xff4a4260); }
inline juce::Colour midiClip() { return juce::Colour(0xff55503a); }
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
class Fade final : private juce::Timer {
public:
    explicit Fade(juce::Component& owner) : component(owner) {}
    void setTarget(bool on) {
        const auto target = on ? 1.0f : 0.0f;
        if (target != goal) {
            goal = target;
            startTimerHz(60);
        }
    }
    float value() const { return current; }
private:
    void timerCallback() override {
        current += (goal - current) * .3f;
        if (std::abs(goal - current) < .02f) {
            current = goal;
            stopTimer();
        }
        component.repaint();
    }
    juce::Component& component;
    float current = 0.0f, goal = 0.0f;
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

// Properties' one-line rows: names on the left, the value in the last of
// three columns, then the previous, key and next buttons at the right edge.
struct PropertyGrid {
    static constexpr int keyButtons = 12 + 18 + 12;
    explicit PropertyGrid(int width) {
        const auto line = width - keyButtons - gap;
        column = std::min(110, (line - gap * 2) / 3);
        value = 2 * (column + gap);
        keys = line + gap;
    }
    int column = 0, value = 0, keys = 0;
};

inline void fillPanel(juce::Graphics& g, juce::Rectangle<int> bounds) {
    g.setColour(osci::Colours::surface());
    g.fillRoundedRectangle(bounds.toFloat(), panelRadius);
}
// After Effects' key glyphs: square hold, diamond linear, hourglass eased,
// circle smooth or Bezier.
enum class KeyShape { hold, linear, eased, smooth };
inline void drawKeyShape(juce::Graphics& g, juce::Point<float> centre, float radius, KeyShape shape) {
    if (shape == KeyShape::hold) {
        g.fillRect(juce::Rectangle<float>(radius * 1.6f, radius * 1.6f).withCentre(centre));
    } else if (shape == KeyShape::smooth) {
        g.fillEllipse(juce::Rectangle<float>(radius * 1.8f, radius * 1.8f).withCentre(centre));
    } else if (shape == KeyShape::eased) {
        juce::Path hourglass;
        hourglass.addTriangle(centre.x - radius, centre.y - radius, centre.x + radius, centre.y - radius, centre.x, centre.y);
        hourglass.addTriangle(centre.x - radius, centre.y + radius, centre.x + radius, centre.y + radius, centre.x, centre.y);
        g.fillPath(hourglass);
    } else {
        juce::Path diamond;
        diamond.startNewSubPath(centre.x, centre.y - radius);
        diamond.lineTo(centre.x + radius, centre.y);
        diamond.lineTo(centre.x, centre.y + radius);
        diamond.lineTo(centre.x - radius, centre.y);
        diamond.closeSubPath();
        g.fillPath(diamond);
    }
}
inline void drawDiamond(juce::Graphics& g, juce::Point<float> centre, float radius, bool filled, float stroke = 1.2f) {
    juce::Path diamond;
    diamond.startNewSubPath(centre.x, centre.y - radius);
    diamond.lineTo(centre.x + radius, centre.y);
    diamond.lineTo(centre.x, centre.y + radius);
    diamond.lineTo(centre.x - radius, centre.y);
    diamond.closeSubPath();
    if (filled) { g.fillPath(diamond); } else { g.strokePath(diamond, juce::PathStrokeType(stroke)); }
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
    // Popovers look like the panels they sit over, with a soft shadow.
    void drawCallOutBoxBackground(juce::CallOutBox& box, juce::Graphics& g, const juce::Path& path, juce::Image& cachedImage) override {
        if (cachedImage.isNull()) {
            cachedImage = juce::Image(juce::Image::ARGB, box.getWidth(), box.getHeight(), true);
            juce::Graphics shadow(cachedImage);
            juce::DropShadow(juce::Colours::black.withAlpha(.55f), 14, {0, 4}).drawForPath(shadow, path);
        }
        g.drawImageAt(cachedImage, 0, 0);
        g.setColour(osci::Colours::surface());
        g.fillPath(path);
        g.setColour(juce::Colours::white.withAlpha(.1f));
        g.strokePath(path, juce::PathStrokeType(1.0f));
    }
    int getCallOutBoxBorderSize(const juce::CallOutBox&) override { return 16; }
    float getCallOutBoxCornerSize(const juce::CallOutBox&) override { return panelRadius + 1.0f; }
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
    // Combo text starts where a dialog field's text does.
    void positionComboBoxText(juce::ComboBox& box, juce::Label& label) override {
        osci::OverlayLookAndFeel::positionComboBoxText(box, label);
        label.setBounds(fieldIndent - label.getBorderSize().getLeft(), 1, juce::jmax(1, box.getWidth() - 24 - fieldIndent + label.getBorderSize().getLeft()), box.getHeight() - 2);
    }
    static constexpr int fieldIndent = 8;
};

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

// One field look for dialogs and popovers: dark, borderless, text 8 px in.
inline void styleField(juce::Component& component) {
    auto* editor = dynamic_cast<juce::TextEditor*>(&component);
    auto* combo = dynamic_cast<juce::ComboBox*>(&component);
    if (editor != nullptr) {
        // A centred field centres its text below the top indent, so the
        // indent would push single lines low.
        const auto centred = (editor->getJustificationType().getFlags() & juce::Justification::verticallyCentred) != 0;
        editor->setIndents(DialogLookAndFeel::fieldIndent - editor->getBorder().getLeft(), centred ? 0 : editor->getTopIndent());
        editor->setColour(juce::TextEditor::backgroundColourId, osci::Colours::veryDark());
        editor->setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    }
    if (combo != nullptr) {
        combo->setColour(juce::ComboBox::backgroundColourId, osci::Colours::veryDark());
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

// A resize handle that shows its grip only on hover, focus or drag.
class MotionDivider final : public osci::PanelDivider {
public:
    using osci::PanelDivider::PanelDivider;
    void paint(juce::Graphics& g) override {
        if (isMouseOverOrDragging() || hasKeyboardFocus(false)) { osci::PanelDivider::paint(g); }
    }
};

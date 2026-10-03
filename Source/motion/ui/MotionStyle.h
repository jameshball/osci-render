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

inline juce::Colour background() { return osci::Colours::veryDark(); }
inline juce::Colour panel() { return osci::Colours::surface(); }
inline juce::Colour raised() { return osci::Colours::surfaceRaised(); }
inline juce::Colour sunken() { return osci::Colours::surfaceSunken(); }
inline juce::Colour field() { return osci::Colours::veryDark(); }
inline juce::Colour text() { return osci::Colours::text(); }
inline juce::Colour muted() { return osci::Colours::textMuted(); }
inline juce::Colour subtle() { return osci::Colours::textSubtle(); }
inline juce::Colour outline() { return osci::Colours::outlineSubtle(); }
inline juce::Colour accent() { return osci::Colours::accentColor(); }
inline juce::Colour danger() { return osci::Colours::danger(); }
inline juce::Colour warning() { return osci::Colours::warning(); }
inline juce::Colour gridMinor() { return osci::Colours::gridMinor(); }
inline juce::Colour gridMajor() { return osci::Colours::gridMajor(); }

// Beam-adjacent accents: keys and the playhead share the phosphor green.
inline juce::Colour key() { return juce::Colour(0xff72de98); }
inline juce::Colour playhead() { return juce::Colour(0xff72de98); }
inline juce::Colour marker() { return juce::Colour(0xffcfb779); }
inline juce::Colour axisX() { return juce::Colour(0xffe07a7a); }
inline juce::Colour axisY() { return juce::Colour(0xff7ad69a); }
inline juce::Colour axisZ() { return juce::Colour(0xff7aa6e0); }

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

class Chip final : public juce::Button {
public:
    explicit Chip(const juce::String& text) : juce::Button(text), label(text) { setClickingTogglesState(true); }
    void setOnColour(juce::Colour colour) { on = colour; repaint(); }
    void paintButton(juce::Graphics& g, bool highlighted, bool down) override {
        const auto bounds = getLocalBounds().toFloat().reduced(.5f);
        const auto active = getToggleState();
        g.setColour(active ? on : (highlighted || down ? osci::Colours::surfaceRaised().brighter(.15f) : osci::Colours::veryDark()));
        g.fillRoundedRectangle(bounds, 3.0f);
        if (!active) {
            // An outline marks it as a toggle, not a heading.
            g.setColour(juce::Colours::white.withAlpha(highlighted ? .28f : .16f));
            g.drawRoundedRectangle(bounds.reduced(.5f), 3.0f, 1.0f);
        }
        g.setColour(active ? juce::Colours::white : osci::Colours::textMuted().withAlpha(highlighted ? 1.0f : .8f));
        g.setFont(caption());
        g.drawText(label, getLocalBounds(), juce::Justification::centred, false);
    }
private:
    juce::String label;
    juce::Colour on = osci::Colours::accentColor();
};

inline void fillPanel(juce::Graphics& g, juce::Rectangle<int> bounds) {
    g.setColour(panel());
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
    juce::Font getMenuBarFont(juce::MenuBarComponent&, int, const juce::String&) override { return body(); }
    juce::Font getPopupMenuFont() override { return body(); }
    juce::Font getTextButtonFont(juce::TextButton&, int) override { return body(); }
    juce::Font getComboBoxFont(juce::ComboBox&) override { return body(); }
};
}

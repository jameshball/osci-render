#pragma once

#include <JuceHeader.h>

// One place for osci-motion's visual language: a restrained dark palette built
// on the shared osci theme, a three-step type scale and a 4 px spacing grid.
namespace motion::style {
inline constexpr int gap = 4;
inline constexpr int padding = 8;
inline constexpr int controlHeight = 24;
inline constexpr int rowHeight = 26;
inline constexpr int headerHeight = 28;
inline constexpr int transportHeight = 36;
inline constexpr float radius = 3.0f;
inline constexpr float panelRadius = 5.0f;

inline juce::Font small() { return juce::Font(juce::FontOptions(11.0f)); }
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
inline juce::Font body() { return juce::Font(juce::FontOptions(12.0f)); }
inline juce::Font strong() { return juce::Font(juce::FontOptions(12.0f, juce::Font::bold)); }
inline juce::Font title() { return juce::Font(juce::FontOptions(13.0f, juce::Font::bold)); }
inline juce::Font mono() { return juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::plain)); }

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

// Painted transport glyphs; no image assets, crisp at any scale.
class IconButton final : public juce::Button {
public:
    enum class Icon { play, pause, start, end, loop };
    IconButton(const juce::String& name, Icon glyph) : juce::Button(name), icon(glyph) {}
    void setIcon(Icon glyph) { if (icon != glyph) { icon = glyph; repaint(); } }
    void paintButton(juce::Graphics& g, bool highlighted, bool down) override {
        const auto bounds = getLocalBounds().toFloat();
        if (highlighted || down || getToggleState()) {
            g.setColour(osci::Colours::surfaceRaised().brighter(down ? .25f : .12f));
            g.fillRoundedRectangle(bounds.reduced(1), 3.0f);
        }
        const auto c = bounds.getCentre();
        const auto s = std::min(bounds.getWidth(), bounds.getHeight()) * .22f;
        g.setColour((getToggleState() ? osci::Colours::accentColor() : osci::Colours::text()).withAlpha(isEnabled() ? .92f : .3f));
        juce::Path path;
        switch (icon) {
            case Icon::play: path.addTriangle(c.x - s * .8f, c.y - s, c.x - s * .8f, c.y + s, c.x + s, c.y); break;
            case Icon::pause:
                path.addRoundedRectangle(c.x - s * .85f, c.y - s, s * .6f, s * 2, 1.0f);
                path.addRoundedRectangle(c.x + s * .25f, c.y - s, s * .6f, s * 2, 1.0f);
                break;
            case Icon::start:
                path.addRectangle(c.x - s, c.y - s, s * .35f, s * 2);
                path.addTriangle(c.x + s, c.y - s, c.x + s, c.y + s, c.x - s * .55f, c.y);
                break;
            case Icon::end:
                path.addRectangle(c.x + s * .65f, c.y - s, s * .35f, s * 2);
                path.addTriangle(c.x - s, c.y - s, c.x - s, c.y + s, c.x + s * .55f, c.y);
                break;
            case Icon::loop: {
                juce::Path arc;
                arc.addCentredArc(c.x, c.y, s, s * .8f, 0, .5f, juce::MathConstants<float>::twoPi - .3f, true);
                g.strokePath(arc, juce::PathStrokeType(1.5f));
                path.addTriangle(c.x + s * .15f, c.y - s * 1.25f, c.x + s * .15f, c.y - s * .35f, c.x + s * .8f, c.y - s * .8f);
                break;
            }
        }
        g.fillPath(path);
    }
private:
    Icon icon;
};

// A small toggle chip whose label never ellipsises (track M / S / L).
// A magnet: snapping on (lit) or off.
class MagnetButton final : public juce::Button {
public:
    MagnetButton() : juce::Button("Snapping") { setWantsKeyboardFocus(false); }
    void paintButton(juce::Graphics& g, bool over, bool down) override {
        const auto bounds = getLocalBounds().toFloat().reduced(2);
        if (getToggleState()) {
            g.setColour(motion::style::accent().withAlpha(.3f));
            g.fillRoundedRectangle(bounds, 3);
        } else if (over || down) {
            g.setColour(juce::Colours::white.withAlpha(.08f));
            g.fillRoundedRectangle(bounds, 3);
        }
        const auto c = bounds.getCentre();
        juce::Path magnet;
        magnet.startNewSubPath(c.x - 5, c.y - 5);
        magnet.lineTo(c.x - 5, c.y + 1);
        magnet.addCentredArc(c.x, c.y + 1, 5, 5, 0, juce::MathConstants<float>::pi * 1.5f, juce::MathConstants<float>::halfPi);
        magnet.lineTo(c.x + 5, c.y - 5);
        g.setColour(getToggleState() ? juce::Colours::white : osci::Colours::text().withAlpha(.6f));
        g.strokePath(magnet, juce::PathStrokeType(2.0f));
    }
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
        g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
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
inline void drawDiamond(juce::Graphics& g, juce::Point<float> centre, float radius, bool filled, float stroke = 1.2f) {
    juce::Path diamond;
    diamond.startNewSubPath(centre.x, centre.y - radius);
    diamond.lineTo(centre.x + radius, centre.y);
    diamond.lineTo(centre.x, centre.y + radius);
    diamond.lineTo(centre.x - radius, centre.y);
    diamond.closeSubPath();
    if (filled) { g.fillPath(diamond); } else { g.strokePath(diamond, juce::PathStrokeType(stroke)); }
}
}

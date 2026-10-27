#pragma once

#include "MotionIcons.h"
#include "MotionStyle.h"
#include "Chip.h"
#include "KeyframeButton.h"

#include "../model/Id.h"

namespace motion::ui {
// One animatable value's row, drawn the same wherever it is (Properties, an
// effect's parameters): at its right the keys' gutter (previous, key, next)
// and, before it, the modulate glyph, on Properties' grid. A diamond with no
// keys recedes, and the modulate glyph shows only while the row is hovered,
// a modulator is dragged, or the row is driven, keeping its column so the
// values never shift: the rows that are keyed or driven stand out.
class PropertyRow : public juce::Component {
public:
    // The row's menu, from a right-click on its name or fields.
    std::function<void()> onContextMenu;
    PropertyRow() {
        key.restAlpha = .45f;
        // Keys are neutral: filled on a key, outlined off one.
        key.setColour(KeyframeButton::keyColourId, osci::Colours::text());
        // Like Motion's other icon buttons: no focus ring after a click.
        key.setWantsKeyboardFocus(false);
        modulate.showsOff = false;
        modulate.setClickingTogglesState(false);
        modulate.quiet = true;
        modulate.setOnColour(style::result().withAlpha(.16f));
        modulate.onContent = style::result();
        previous.setTooltip("Go to the previous key");
        next.setTooltip("Go to the next key");
        for (auto* child : std::initializer_list<juce::Component*> {&previous, &next, &modulate, &key}) { addAndMakeVisible(child); }
        addMouseListener(&hover, true);
    }
    ~PropertyRow() override { removeMouseListener(&hover); }

    KeyframeButton key;
    Chip modulate {"Modulate", icons::Icon::wave};
    style::ChevronButton previous {"Previous key", false}, next {"Next key", true};

    void setModulatorDrag(bool active) {
        modulatorDrag = active;
        showModulate();
    }
    // The key's state; a property's first key (on the same owner, not by
    // choosing one that already had keys) pulses once round it.
    void showState(KeyframeButton::State state, Id owner) {
        if (owner == shownTarget && shownState == KeyframeButton::State::unanimated && state != KeyframeButton::State::unanimated) {
            pulseStart = juce::Time::getMillisecondCounterHiRes();
            pulseFrames.startTimerHz(60);
        }
        if (state == shownState && owner == shownTarget) { return; }
        shownState = state;
        shownTarget = owner;
        key.setState(state);
        repaint();
    }

    // Hovered, a diamond with no keys yet opens into "Animate", saying what
    // it does; the first key's pulse.
    void paintOverChildren(juce::Graphics& g) override {
        const auto centre = key.getBounds().toFloat().getCentre();
        const auto since = juce::Time::getMillisecondCounterHiRes() - pulseStart;
        if (pulseFrames.isTimerRunning() && since < pulseLength) {
            const auto t = static_cast<float>(since / pulseLength);
            const auto eased = 1.0f - (1.0f - t) * (1.0f - t);
            const auto radius = 5.0f + 9.0f * eased;
            g.setColour(style::accent().withAlpha(.85f * (1.0f - t)));
            juce::Path ring;
            ring.addPolygon(centre, 4, radius * 1.15f, 0.0f);
            g.strokePath(ring, juce::PathStrokeType(1.5f));
        }
        if (!key.isEnabled() || !keyHovered || shownState != KeyframeButton::State::unanimated) { return; }
        const auto text = juce::String("Animate");
        const auto textWidth = std::ceil(juce::TextLayout::getStringWidth(style::caption(), text));
        const auto height = static_cast<float>(key.getHeight()) - 6.0f;
        // A label floating over the row's end, ending in the diamond it explains.
        const auto pill = juce::Rectangle<float>(centre.x + 10.0f - (textWidth + 34.0f), centre.y - height * .5f, textWidth + 34.0f, height);
        juce::Path shape;
        shape.addRoundedRectangle(pill, height * .5f);
        juce::DropShadow(juce::Colours::black.withAlpha(.45f), 8, {0, 1}).drawForPath(g, shape);
        g.setColour(osci::Colours::surfaceRaised().brighter(.12f));
        g.fillPath(shape);
        g.setColour(style::hairline().brighter(.2f));
        g.strokePath(shape, juce::PathStrokeType(1.0f));
        juce::Path diamond;
        diamond.addPolygon(centre, 4, 5.0f, 0.0f);
        g.setColour(osci::Colours::text());
        g.strokePath(diamond, juce::PathStrokeType(1.2f));
        g.setFont(style::caption());
        g.drawText(text, pill.withTrimmedLeft(10.0f).withWidth(textWidth + 2.0f), juce::Justification::centredLeft, false);
    }

protected:
    // Places the gutter on `line`, the row's value line: previous, key and
    // next at its right, the modulate glyph in the grid's column before them.
    void layoutGutter(juce::Rectangle<int> line, const style::PropertyGrid& grid) {
        next.setBounds(line.removeFromRight(12));
        key.setBounds(line.removeFromRight(18));
        previous.setBounds(line.removeFromRight(12));
        modulate.setBounds(juce::Rectangle<int>(grid.modulate, line.getY(), style::PropertyGrid::modulateWidth, line.getHeight()).reduced(0, 3));
    }

private:
    // The pointer over the row or anything in it. A listener of its own,
    // not the row, so wheel events still pass up once to the scroller.
    struct Hover final : juce::MouseListener {
        explicit Hover(PropertyRow& owner) : row(owner) {}
        // A right-click anywhere on the row (the key has a menu of its own).
        void mouseDown(const juce::MouseEvent& event) override {
            if (event.mods.isPopupMenu() && event.eventComponent != &row.key && row.onContextMenu) { row.onContextMenu(); }
        }
        void mouseEnter(const juce::MouseEvent& event) override {
            row.hoverKey(event, true);
            row.showModulate();
        }
        void mouseExit(const juce::MouseEvent& event) override {
            row.hoverKey(event, false);
            row.showModulate();
        }
        PropertyRow& row;
    };
    void showModulate() {
        const auto shown = modulatorDrag || isMouseOver(true);
        if (modulate.showsOff == shown) { return; }
        modulate.showsOff = shown;
        modulate.repaint();
    }
    void hoverKey(const juce::MouseEvent& event, bool over) {
        if (event.eventComponent != &key || keyHovered == over) { return; }
        keyHovered = over;
        repaint();
    }

    Hover hover {*this};
    bool modulatorDrag = false, keyHovered = false;
    KeyframeButton::State shownState = KeyframeButton::State::unanimated;
    Id shownTarget = 0;
    double pulseStart = 0;
    static constexpr double pulseLength = 650;
    juce::TimedCallback pulseFrames {[this] {
        if (juce::Time::getMillisecondCounterHiRes() - pulseStart > pulseLength) { pulseFrames.stopTimer(); }
        repaint();
    }};
};
}

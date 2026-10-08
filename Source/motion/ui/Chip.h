#pragma once

#include "MotionIcons.h"

namespace motion::ui {
// A small toggle showing a word or an icon.
class Chip final : public juce::Button {
public:
    explicit Chip(const juce::String& text) : juce::Button(text) { setUp(); }
    Chip(const juce::String& name, icons::Icon glyph) : juce::Button(name), icon(glyph) { setUp(); }

    void setOnColour(juce::Colour colour) {
        if (on != colour) {
            on = colour;
            repaint();
        }
    }

    void paintButton(juce::Graphics& g, bool highlighted, bool down) override {
        const auto bounds = getLocalBounds().toFloat().reduced(.5f);
        const auto active = getToggleState();
        if (quiet && !active) {
            if (highlighted || down) {
                g.setColour(juce::Colours::white.withAlpha(down ? .12f : .07f));
                g.fillRoundedRectangle(bounds, style::radius);
            }
            paintContent(g, bounds, osci::Colours::textMuted().withAlpha(highlighted ? 1.0f : .6f));
            return;
        }
        g.setColour(active ? on : (highlighted || down ? osci::Colours::surfaceRaised().brighter(.15f) : osci::Colours::veryDark()));
        g.fillRoundedRectangle(bounds, style::radius);
        if (!active) {
            // An outline marks it as a toggle, not a heading.
            g.setColour(juce::Colours::white.withAlpha(highlighted ? .28f : .16f));
            g.drawRoundedRectangle(bounds.reduced(.5f), style::radius, 1.0f);
        }
        paintContent(g, bounds, active ? onContent : osci::Colours::textMuted().withAlpha(highlighted ? 1.0f : .85f));
    }

    float iconSize = 13.0f;
    // The glyph or word when on: white on the accent; a modulation chip's
    // lilac on a faint lilac fill says what drives the value.
    juce::Colour onContent = juce::Colours::white;
    // Just the content until hovered or on: a column of these stays light.
    bool quiet = false;

private:
    void setUp() {
        setClickingTogglesState(true);
        setWantsKeyboardFocus(false);
    }

    void paintContent(juce::Graphics& g, juce::Rectangle<float> bounds, juce::Colour colour) {
        if (icon.has_value()) {
            icons::draw(g, *icon, bounds, colour, iconSize);
            return;
        }
        g.setColour(colour);
        g.setFont(style::caption());
        g.drawText(getButtonText(), bounds, juce::Justification::centred, false);
    }

    std::optional<icons::Icon> icon;
    // A toggle that is on is neutral, like the tool strips.
    juce::Colour on = style::onFill();
};
}

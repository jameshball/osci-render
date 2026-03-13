#pragma once

#include <JuceHeader.h>
#include "ValuePopupHelper.h"
#include "../LookAndFeel.h"

// Vital-inspired slim horizontal slider with an embedded knob handle.
// - Slim rounded track
// - Small knob that moves along the track
// - Double-click resets to default
// - Shows a tooltip popup while hovering or dragging
class PhaseSliderComponent : public juce::Component {
public:
    PhaseSliderComponent() {
        setRepaintsOnMouseActivity(true);
    }

    void paint(juce::Graphics& g) override {
        auto bounds = getLocalBounds().toFloat();
        float trackH = 4.0f;
        float trackY = (bounds.getHeight() - trackH) * 0.5f;
        auto trackBounds = juce::Rectangle<float>(bounds.getX(), trackY,
                                                   bounds.getWidth(), trackH);
        float r = trackH * 0.5f;

        // Track background
        g.setColour(Colours::veryDark);
        g.fillRoundedRectangle(trackBounds, r);

        // Filled portion
        float knobX = getKnobX();
        auto filledBounds = trackBounds.withRight(knobX);
        g.setColour(accentColour.withAlpha(0.35f));
        g.fillRoundedRectangle(filledBounds, r);

        // Track border
        bool hovering = isMouseOver(true) || isDragging;
        g.setColour(juce::Colours::white.withAlpha(hovering ? 0.15f : 0.08f));
        g.drawRoundedRectangle(trackBounds.reduced(0.5f), r, 1.0f);

        // Rounded rectangle knob (like ----|----)
        float knobW = 6.0f;
        float knobH = bounds.getHeight() - 2.0f;
        float knobY = (bounds.getHeight() - knobH) * 0.5f;
        float knobR = 2.0f;
        auto knobBounds = juce::Rectangle<float>(knobX - knobW * 0.5f, knobY,
                                                  knobW, knobH);
        g.setColour(hovering ? accentColour.brighter(0.15f) : accentColour);
        g.fillRoundedRectangle(knobBounds, knobR);

        // Knob highlight line
        g.setColour(juce::Colours::white.withAlpha(0.25f));
        auto highlightBounds = knobBounds.reduced(1.0f, knobH * 0.25f);
        g.fillRoundedRectangle(highlightBounds, 1.0f);
    }

    void mouseDown(const juce::MouseEvent& e) override {
        if (e.mods.isPopupMenu()) return;
        isDragging = true;
        setValueFromMouse(e);
        popup.show(*this, getPopupText());
    }

    void mouseDrag(const juce::MouseEvent& e) override {
        if (!isDragging) return;
        setValueFromMouse(e);
        popup.show(*this, getPopupText());
    }

    void mouseUp(const juce::MouseEvent&) override {
        isDragging = false;
        popup.hide();
        repaint();
    }

    void mouseEnter(const juce::MouseEvent&) override {
        popup.show(*this, getPopupText());
    }

    void mouseExit(const juce::MouseEvent&) override {
        if (!isDragging)
            popup.hide();
    }

    void mouseDoubleClick(const juce::MouseEvent&) override {
        setValue(defaultValue);
    }

    // Value access
    float getValue() const { return value; }

    void setValue(float newValue) {
        newValue = juce::jlimit(0.0f, 1.0f, newValue);
        if (std::abs(value - newValue) < 1e-6f) return;
        value = newValue;
        repaint();
        if (onValueChanged) onValueChanged(value);
    }

    void setDefaultValue(float v) { defaultValue = juce::jlimit(0.0f, 1.0f, v); }
    void setAccentColour(juce::Colour c) { accentColour = c; repaint(); }

    std::function<void(float)> onValueChanged;

private:
    float value = 0.0f;
    float defaultValue = 0.0f;
    bool isDragging = false;
    juce::Colour accentColour { 0xFF00E5FF };
    ValuePopupHelper popup;

    float getKnobX() const {
        float knobHalfW = 3.0f;
        float minX = knobHalfW;
        float maxX = (float)getWidth() - knobHalfW;
        return minX + value * (maxX - minX);
    }

    void setValueFromMouse(const juce::MouseEvent& e) {
        float knobHalfW = 3.0f;
        float minX = knobHalfW;
        float maxX = (float)getWidth() - knobHalfW;
        float newVal = (e.position.x - minX) / (maxX - minX);
        setValue(juce::jlimit(0.0f, 1.0f, newVal));
    }

    juce::String getPopupText() const {
        return "Phase: " + juce::String(value, 2);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PhaseSliderComponent)
};

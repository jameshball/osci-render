#pragma once

#include <JuceHeader.h>
#include "../LabelledBarComponent.h"

// Base class for Vital-inspired dark-bar modulation controls.
// Inherits from LabelledBarComponent (generic dark bar + label strip)
// and adds modulation-specific features: source index, display text,
// optional icon area, and processor sync.
class ModulationControlComponent : public LabelledBarComponent {
public:
    ModulationControlComponent() = default;
    ~ModulationControlComponent() override = default;

    // --- Virtual interface for subclasses ---

    virtual juce::String getDisplayText() const = 0;
    virtual bool hasIconArea() const { return false; }
    virtual void drawIcon(juce::Graphics& g, juce::Rectangle<float> area) const { juce::ignoreUnused(g, area); }
    virtual void syncFromProcessor() = 0;

    // --- Source index ---

    void setSourceIndex(int index) {
        sourceIndex = juce::jlimit(0, maxIndex - 1, index);
        repaint();
    }
    int getSourceIndex() const { return sourceIndex; }

    // --- Content painting (delegates to LabelledBarComponent) ---

    void paintContent(juce::Graphics& g, juce::Rectangle<int> area) override {
        // Value text
        g.setColour(juce::Colours::white.withAlpha(0.9f));
        g.setFont(juce::Font(juce::FontOptions(14.0f, juce::Font::bold)));
        g.drawText(getDisplayText(), valueArea.toFloat(), juce::Justification::centred);

        // Optional icon area with separator
        if (hasIconArea() && !iconArea.isEmpty()) {
            auto iconBounds = iconArea.toFloat();
            g.setColour(juce::Colours::white.withAlpha(0.15f));
            g.drawVerticalLine((int)iconBounds.getX(),
                               iconBounds.getY() + 3.0f,
                               iconBounds.getBottom() - 3.0f);

            bool iconHover = iconHovered;
            g.setColour(iconHover ? juce::Colours::white.withAlpha(0.7f) : osci::Colours::grey());
            drawIcon(g, iconBounds.reduced(2.0f, 2.0f));
        }
    }

    // --- Content layout ---

    void resizedContent(juce::Rectangle<int> area) override {
        if (hasIconArea()) {
            iconArea = area.removeFromRight(kIconWidth);
        } else {
            iconArea = {};
        }
        valueArea = area;
    }

    // --- Mouse tracking for icon hover ---

    void mouseMove(const juce::MouseEvent&) override {
        if (!hasIconArea() || iconArea.isEmpty()) return;
        bool hover = iconArea.contains(getMouseXYRelative());
        if (hover != iconHovered) {
            iconHovered = hover;
            repaint();
        }
    }

    void mouseExit(const juce::MouseEvent&) override {
        if (iconHovered) {
            iconHovered = false;
            repaint();
        }
    }

protected:
    int sourceIndex = 0;
    int maxIndex = 1;
    bool iconHovered = false;

    juce::Rectangle<int> valueArea;
    juce::Rectangle<int> iconArea;

    static constexpr int kIconWidth = 26;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ModulationControlComponent)
};

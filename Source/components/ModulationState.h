#pragma once

#include <JuceHeader.h>
#include <atomic>

// Global modulation UI state shared across all modulation-aware components
// (EffectComponent, KnobContainerComponent, etc.). Previously these lived
// as static members of EffectComponent, creating an architectural coupling.
struct ModulationState {
    // When true, all modulation-aware components draw a highlight border
    // to indicate they are valid modulation drop targets.
    static std::atomic<bool> anyDragActive;

    // When non-empty, the component with this paramId draws a hover highlight.
    static juce::String highlightedParamId;

    // Modulation range highlight: when hovering a depth indicator,
    // show the modulated range on the slider.
    static juce::String rangeParamId;
    static std::atomic<float> rangeDepth;
    static std::atomic<bool> rangeBipolar;

    // Paint modulation highlight overlays. Call from paintOverChildren().
    // Returns true if any overlay was drawn.
    static bool paintModHighlight(juce::Graphics& g, juce::Component& comp,
                                  bool modDropHighlight, const juce::String& paramId) {
        auto modGreen = juce::Colour(0xFF00FF00);

        if (modDropHighlight) {
            auto b = comp.getLocalBounds().toFloat().reduced(1.0f);
            g.setColour(modGreen.withAlpha(0.18f));
            g.fillRoundedRectangle(b, 4.0f);
            g.setColour(modGreen.withAlpha(0.8f));
            g.drawRoundedRectangle(b, 4.0f, 2.0f);
            return true;
        }

        if (anyDragActive.load(std::memory_order_relaxed)) {
            auto b = comp.getLocalBounds().toFloat().reduced(1.0f);
            g.setColour(modGreen.withAlpha(0.06f));
            g.fillRoundedRectangle(b, 4.0f);
            g.setColour(modGreen.withAlpha(0.25f));
            g.drawRoundedRectangle(b, 4.0f, 1.0f);
            return true;
        }

        if (highlightedParamId.isNotEmpty() && paramId == highlightedParamId) {
            auto b = comp.getLocalBounds().toFloat().reduced(1.0f);
            g.setColour(modGreen.withAlpha(0.18f));
            g.fillRoundedRectangle(b, 4.0f);
            g.setColour(modGreen.withAlpha(0.8f));
            g.drawRoundedRectangle(b, 4.0f, 2.0f);
            return true;
        }

        return false;
    }

    // Check if repaint is needed due to modulation highlight state.
    static bool needsHighlightRepaint(bool modDropHighlight, const juce::String& paramId) {
        return modDropHighlight
            || anyDragActive.load(std::memory_order_relaxed)
            || (highlightedParamId.isNotEmpty() && paramId == highlightedParamId);
    }
};

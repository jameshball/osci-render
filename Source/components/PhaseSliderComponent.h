#pragma once

#include <JuceHeader.h>
#include "../LookAndFeel.h"

// Slim horizontal slider for LFO phase offset, extending juce::Slider.
// Custom rendering (thin track + handle) is self-contained via a nested LookAndFeel.
class PhaseSliderComponent : public juce::Slider {
public:
    PhaseSliderComponent() {
        setSliderStyle(juce::Slider::LinearHorizontal);
        setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
        setRange(0.0, 1.0);
        setDoubleClickReturnValue(true, 0.0);
        setPopupDisplayEnabled(true, false, nullptr);
        setColour(juce::Slider::trackColourId, Colours::accentColor());
        setLookAndFeel(&lnf);
    }

    ~PhaseSliderComponent() override { setLookAndFeel(nullptr); }

    void setAccentColour(juce::Colour c) {
        setColour(juce::Slider::trackColourId, c);
    }

private:
    struct LookAndFeel : public OscirenderLookAndFeel {
        void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                              float sliderPos, float /*minSliderPos*/, float /*maxSliderPos*/,
                              const juce::Slider::SliderStyle, juce::Slider& slider) override {
            auto bounds = juce::Rectangle<float>((float)x, (float)y, (float)width, (float)height);
            float trackH = bounds.getHeight();
            float r = trackH * 0.5f;
            auto accentColour = slider.findColour(juce::Slider::trackColourId);
            bool hovering = slider.isMouseOverOrDragging();

            // Track background
            g.setColour(Colours::veryDark());
            g.fillRoundedRectangle(bounds, r);

            // Clip to track shape so fill and handle don't extend past rounded ends
            juce::Path trackClip;
            trackClip.addRoundedRectangle(bounds, r);
            g.saveState();
            g.reduceClipRegion(trackClip);

            // Filled portion
            auto filledBounds = bounds.withRight(sliderPos);
            g.setColour(accentColour.withAlpha(0.35f));
            g.fillRect(filledBounds);

            // Handle: wider rounded rect, same height as track
            float knobW = 10.0f;
            float knobR = 2.5f;
            auto knobBounds = juce::Rectangle<float>(sliderPos - knobW * 0.5f, (float)y,
                                                      knobW, trackH);
            g.setColour(hovering ? accentColour.brighter(0.15f) : accentColour);
            g.fillRoundedRectangle(knobBounds, knobR);

            // Handle highlight line
            g.setColour(juce::Colours::white.withAlpha(0.25f));
            auto highlightBounds = knobBounds.reduced(1.5f, trackH * 0.25f);
            g.fillRoundedRectangle(highlightBounds, 1.0f);

            g.restoreState();

            // Track border (outside clip so it's not clipped)
            g.setColour(juce::Colours::white.withAlpha(hovering ? 0.15f : 0.08f));
            g.drawRoundedRectangle(bounds.reduced(0.5f), r, 1.0f);
        }

        int getSliderThumbRadius(juce::Slider&) override { return 0; }
    };

    LookAndFeel lnf;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PhaseSliderComponent)
};

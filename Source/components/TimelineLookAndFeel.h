#pragma once

#include <JuceHeader.h>
#include "../LookAndFeel.h"

class TimelineLookAndFeel : public OscirenderLookAndFeel {
public:
    TimelineLookAndFeel() {}

    void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height, 
                         float sliderPos, float minSliderPos, float maxSliderPos, 
                         const juce::Slider::SliderStyle style, juce::Slider& slider) override 
    {
        auto trackWidth = juce::jmin(4.0f, slider.isHorizontal() ? (float) height * 0.25f : (float) width * 0.25f);

        juce::Point<float> startPoint(slider.isHorizontal() ? (float) x : (float) x + (float) width * 0.5f,
                                    slider.isHorizontal() ? (float) y + (float) height * 0.5f : (float) (height + y));

        juce::Point<float> endPoint(slider.isHorizontal() ? (float) (width + x) : startPoint.x,
                                  slider.isHorizontal() ? startPoint.y : (float) y);

        // Draw background track with rounded corners
        juce::Path backgroundTrack;
        backgroundTrack.startNewSubPath(startPoint);
        backgroundTrack.lineTo(endPoint);
        g.setColour(slider.findColour(juce::Slider::backgroundColourId));
        g.strokePath(backgroundTrack, { trackWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });

        // Draw value track with gradient
        juce::Path valueTrack;
        juce::Point<float> minPoint, maxPoint;

        auto kx = slider.isHorizontal() ? sliderPos : ((float) x + (float) width * 0.5f);
        auto ky = slider.isHorizontal() ? ((float) y + (float) height * 0.5f) : sliderPos;

        minPoint = startPoint;
        maxPoint = { kx, ky };

        valueTrack.startNewSubPath(minPoint);
        valueTrack.lineTo(maxPoint);

        juce::ColourGradient gradient(
            slider.findColour(juce::Slider::trackColourId),
            minPoint,
            slider.findColour(juce::Slider::trackColourId).withAlpha(0.8f),
            maxPoint,
            false
        );
        g.setGradientFill(gradient);
        g.strokePath(valueTrack, { trackWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });

        // Draw thumb
        auto thumbWidth = 10;
        g.setColour(slider.findColour(juce::Slider::thumbColourId));
        
        // Draw a more modern rectangular thumb with rounded corners
        auto thumbHeight = 1.5f * thumbWidth;
        auto thumbRect = juce::Rectangle<float>(thumbWidth / 2, thumbHeight).withCentre(maxPoint);
        g.fillRoundedRectangle(thumbRect, 2.0f);

        // Add a subtle outline to the thumb
        g.setColour(slider.findColour(sliderThumbOutlineColourId).withAlpha(0.5f));
        g.drawRoundedRectangle(thumbRect, 2.0f, 1.0f);

        // Add a subtle highlight at the top of the thumb
        g.setColour(juce::Colours::white.withAlpha(0.3f));
        g.drawHorizontalLine(static_cast<int>(thumbRect.getY() + 1), 
                           thumbRect.getX(), 
                           thumbRect.getRight());
    }
};

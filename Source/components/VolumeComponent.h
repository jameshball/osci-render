#pragma once

#include <JuceHeader.h>
#include "../concurrency/BufferConsumer.h"
#include "../PluginProcessor.h"
#include "../LookAndFeel.h"

class ThumbRadiusLookAndFeel : public OscirenderLookAndFeel {
public:
    ThumbRadiusLookAndFeel(int thumbRadius) : thumbRadius(thumbRadius) {}

    int getSliderThumbRadius(juce::Slider& slider) override {
        return juce::jmin(thumbRadius, slider.isHorizontal() ? slider.getHeight() : slider.getWidth());
    }

private:
    int thumbRadius = 12;
};

// this is for a vertical slider
class ThresholdLookAndFeel : public ThumbRadiusLookAndFeel {
public:
    ThresholdLookAndFeel(int thumbRadius) : ThumbRadiusLookAndFeel(thumbRadius) {}

    void drawLinearSliderThumb(juce::Graphics& g, int x, int y, int width, int height, float sliderPos, float minSliderPos, float maxSliderPos, const juce::Slider::SliderStyle style, juce::Slider& slider) override {
        float kx = (float) x + (float) width * 0.5f;
        float ky = sliderPos;

        auto outlineThickness = slider.isEnabled() ? 0.8f : 0.3f;
        auto sliderRadius = (float) getSliderThumbRadius(slider);
        auto diameter = sliderRadius * 2.0f;
        auto halfThickness = outlineThickness * 0.5f;

        auto isDownOrDragging = slider.isEnabled() && (slider.isMouseOverOrDragging() || slider.isMouseButtonDown());

        auto knobColour = slider.findColour(juce::Slider::thumbColourId)
            .withMultipliedSaturation((slider.hasKeyboardFocus (false) || isDownOrDragging) ? 1.3f : 0.9f)
            .withMultipliedAlpha(slider.isEnabled() ? 1.0f : 0.7f);

        y = (int) (ky - sliderRadius);

        // draw triangle that points left
        juce::Path p;
        p.addTriangle(
            x + diameter, y,
            x + diameter, y + diameter,
            x, ky
        );

        g.setColour(knobColour);
        g.fillPath(p);

        g.setColour(slider.findColour(sliderThumbOutlineColourId));
        g.strokePath(p, juce::PathStrokeType(outlineThickness));
    }

    void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height, float sliderPos, float minSliderPos, float maxSliderPos, const juce::Slider::SliderStyle style, juce::Slider& slider) override {
        drawLinearSliderThumb(g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
    }
};

class VolumeComponent : public juce::Component, public juce::Timer, public juce::Thread {
public:
	VolumeComponent(OscirenderAudioProcessor& p);
    ~VolumeComponent() override;

    void paint(juce::Graphics&) override;
	void timerCallback() override;
	void run() override;
	void resized() override;

private:
	OscirenderAudioProcessor& audioProcessor;
	std::shared_ptr<BufferConsumer> consumer = std::make_shared<BufferConsumer>(1 << 11);

	std::atomic<float> leftVolume = 0;
	std::atomic<float> rightVolume = 0;
	std::atomic<float> avgLeftVolume = 0;
	std::atomic<float> avgRightVolume = 0;
	
    ThumbRadiusLookAndFeel thumbRadiusLookAndFeel{20};
    juce::Slider volumeSlider;
    ThresholdLookAndFeel thresholdLookAndFeel{7};
	juce::Slider thresholdSlider;

    std::unique_ptr<juce::Drawable> volumeIcon;
    std::unique_ptr<juce::Drawable> thresholdIcon;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VolumeComponent)
};
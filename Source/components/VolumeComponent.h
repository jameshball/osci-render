#pragma once

#include <JuceHeader.h>
#include "../CommonPluginProcessor.h"
#include "../LookAndFeel.h"
#include "SvgButton.h"

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

        auto sliderRadius = (float) getSliderThumbRadius(slider);
        auto diameter = sliderRadius * 2.0f;

        auto isDownOrDragging = slider.isEnabled() && (slider.isMouseOverOrDragging() || slider.isMouseButtonDown());

        y = (int) (ky - sliderRadius);

        // Create a simple arrow path inspired by the SVG
        juce::Path p;
        float arrowWidth = diameter;
        float arrowHeight = diameter;
        
        // Start at the point of the arrow
        p.startNewSubPath(x, y + arrowHeight * 0.5f);
        // Draw line to top-right corner
        p.lineTo(x + arrowWidth, y);
        // Draw line to bottom-right corner
        p.lineTo(x + arrowWidth, y + arrowHeight);
        // Close the path back to the point
        p.closeSubPath();

        g.setColour(juce::Colours::white.withAlpha(isDownOrDragging ? 1.0f : 0.9f));
        g.fillPath(p);
    }

    void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height, float sliderPos, float minSliderPos, float maxSliderPos, const juce::Slider::SliderStyle style, juce::Slider& slider) override {
        drawLinearSliderThumb(g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
    }
};

class VolumeComponent : public juce::Component, public juce::AsyncUpdater, public osci::AudioBackgroundThread {
public:
	VolumeComponent(CommonAudioProcessor& p);

    void paint(juce::Graphics&) override;
    void handleAsyncUpdate() override;
    int prepareTask(double sampleRate, int bufferSize) override;
    void runTask(const std::vector<osci::Point>& points) override;
    void stopTask() override;
	void resized() override;

private:
    CommonAudioProcessor& audioProcessor;
    
    const int DEFAULT_SAMPLE_RATE = 192000;
    const double BUFFER_DURATION_SECS = 1.0/60.0;
    
    int sampleRate = DEFAULT_SAMPLE_RATE;

	std::atomic<float> leftVolume = 0;
	std::atomic<float> rightVolume = 0;
	
    ThumbRadiusLookAndFeel thumbRadiusLookAndFeel{12};
    juce::Slider volumeSlider;
    ThresholdLookAndFeel thresholdLookAndFeel{7};
	juce::Slider thresholdSlider;

    SvgButton volumeButton = SvgButton("VolumeButton", BinaryData::volume_svg, juce::Colours::white, juce::Colours::red, audioProcessor.muteParameter, BinaryData::mute_svg);

    // Animation smoothing
    juce::SmoothedValue<float> leftVolumeSmoothed;
    juce::SmoothedValue<float> rightVolumeSmoothed;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VolumeComponent)
};

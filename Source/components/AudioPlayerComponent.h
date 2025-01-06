#pragma once

#include <JuceHeader.h>
#include "../CommonPluginProcessor.h"
#include "../LookAndFeel.h"
#include "../wav/WavParser.h"

class TimelineLookAndFeel : public OscirenderLookAndFeel {
public:
    TimelineLookAndFeel() {}

    void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height, float sliderPos, float minSliderPos, float maxSliderPos, const juce::Slider::SliderStyle style, juce::Slider& slider) override {
        auto trackWidth = juce::jmin (6.0f, slider.isHorizontal() ? (float) height * 0.25f : (float) width * 0.25f);

        juce::Point<float> startPoint (slider.isHorizontal() ? (float) x : (float) x + (float) width * 0.5f,
            slider.isHorizontal() ? (float) y + (float) height * 0.5f : (float) (height + y));

        juce::Point<float> endPoint (slider.isHorizontal() ? (float) (width + x) : startPoint.x,
            slider.isHorizontal() ? startPoint.y : (float) y);

        juce::Path backgroundTrack;
        backgroundTrack.startNewSubPath (startPoint);
        backgroundTrack.lineTo (endPoint);
        g.setColour (slider.findColour (juce::Slider::backgroundColourId));
        g.strokePath (backgroundTrack, { trackWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });

        juce::Path valueTrack;
        juce::Point<float> minPoint, maxPoint, thumbPoint;

        auto kx = slider.isHorizontal() ? sliderPos : ((float) x + (float) width * 0.5f);
        auto ky = slider.isHorizontal() ? ((float) y + (float) height * 0.5f) : sliderPos;

        minPoint = startPoint;
        maxPoint = { kx, ky };

        auto thumbWidth = getSliderThumbRadius (slider);

        valueTrack.startNewSubPath (minPoint);
        valueTrack.lineTo (maxPoint);
        g.setColour (slider.findColour (juce::Slider::trackColourId));
        g.strokePath (valueTrack, { trackWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });

        g.setColour (slider.findColour (juce::Slider::thumbColourId));
        g.fillRect (juce::Rectangle<float> (thumbWidth / 2, 1.5 * thumbWidth).withCentre (maxPoint));

        g.setColour(slider.findColour(sliderThumbOutlineColourId).withAlpha(slider.isEnabled() ? 1.0f : 0.5f));
        g.drawRect(juce::Rectangle<float>(thumbWidth / 2, 1.5 * thumbWidth).withCentre(maxPoint));
    }
};

class AudioPlayerListener {
public:
    virtual void parserChanged(std::shared_ptr<WavParser> parser) = 0;
};

class CommonAudioProcessor;
class AudioPlayerComponent : public juce::Component, public AudioPlayerListener {
public:
    AudioPlayerComponent(CommonAudioProcessor& processor);
    ~AudioPlayerComponent() override;

	void resized() override;
    void setup();
    void parserChanged(std::shared_ptr<WavParser> parser) override;
    void setPaused(bool paused);

private:
    CommonAudioProcessor& audioProcessor;

    bool parserWasNull = true;
    std::shared_ptr<WavParser> parser;
    
    TimelineLookAndFeel timelineLookAndFeel;

    SvgButton playButton{ "Play", BinaryData::play_svg, juce::Colours::white, juce::Colours::white};
    SvgButton pauseButton{ "Pause", BinaryData::pause_svg, juce::Colours::white, juce::Colours::white };
    SvgButton repeatButton{ "Repeat", BinaryData::repeat_svg, juce::Colours::white, Colours::accentColor };
    SvgButton stopButton{ "Stop", BinaryData::stop_svg, juce::Colours::white, juce::Colours::white };
	juce::Slider slider;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioPlayerComponent)
    JUCE_DECLARE_WEAK_REFERENCEABLE(AudioPlayerComponent)
};

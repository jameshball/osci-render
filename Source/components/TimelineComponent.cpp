#include "TimelineComponent.h"

TimelineComponent::TimelineComponent()
{
    addAndMakeVisible(slider);
    slider.setSliderStyle(juce::Slider::SliderStyle::LinearHorizontal);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    slider.setOpaque(false);
    slider.setRange(0, 1, 0.001);
    slider.setLookAndFeel(&timelineLookAndFeel);
    slider.setColour(juce::Slider::ColourIds::trackColourId, juce::Colours::white.withAlpha(0.8f));
    slider.setColour(juce::Slider::ColourIds::backgroundColourId, juce::Colours::white.withAlpha(0.2f));
    slider.setColour(juce::Slider::ColourIds::thumbColourId, juce::Colours::white);
    
    addChildComponent(playButton);
    addChildComponent(pauseButton);
    addAndMakeVisible(stopButton);
    addAndMakeVisible(repeatButton);

    // Configure button tooltips and colors
    playButton.setTooltip("Play");
    pauseButton.setTooltip("Pause");
    stopButton.setTooltip("Stop");
    repeatButton.setTooltip("Loop");

    slider.setMouseDragSensitivity(150); // Make slider more responsive
    
    playButton.onClick = [this]() {
        setPlaying(true);
        if (onPlay) onPlay();
    };

    pauseButton.onClick = [this]() {
        setPlaying(false);
        if (onPause) onPause();
    };
    
    repeatButton.onClick = [this]() {
        if (onRepeatChanged)
            onRepeatChanged(repeatButton.getToggleState());
    };
    
    stopButton.onClick = [this]() {
        setPlaying(false);
        slider.setValue(0, juce::sendNotification);
        if (onStop) onStop();
    };

    slider.onValueChange = [this]() {
        if (onValueChange)
            onValueChange(slider.getValue());
    };

    startTimer(20);
}

TimelineComponent::~TimelineComponent()
{
    stopTimer();
    slider.setLookAndFeel(nullptr);
}

void TimelineComponent::setValue(double value, juce::NotificationType notification)
{
    slider.setValue(value, notification);
}

double TimelineComponent::getValue() const
{
    return slider.getValue();
}

void TimelineComponent::setPlaying(bool shouldBePlaying)
{
    playButton.setVisible(!shouldBePlaying);
    pauseButton.setVisible(shouldBePlaying);
}

bool TimelineComponent::isPlaying() const
{
    return pauseButton.isVisible();
}

void TimelineComponent::setRepeat(bool shouldRepeat)
{
    repeatButton.setToggleState(shouldRepeat, juce::dontSendNotification);
}

bool TimelineComponent::getRepeat() const
{
    return repeatButton.getToggleState();
}

void TimelineComponent::setRange(double start, double end)
{
    slider.setRange(start, end);
}

void TimelineComponent::resized()
{
    auto r = getLocalBounds();

    auto playPauseBounds = r.removeFromLeft(25);
    playButton.setBounds(playPauseBounds);
    pauseButton.setBounds(playPauseBounds);
    stopButton.setBounds(r.removeFromLeft(25));

    repeatButton.setBounds(r.removeFromRight(25));

    slider.setBounds(r);
}

void TimelineComponent::timerCallback()
{
    // Base implementation does nothing - derived classes will override if needed
}

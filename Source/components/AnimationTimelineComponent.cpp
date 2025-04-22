#include "AnimationTimelineComponent.h"
#include "../PluginProcessor.h"

AnimationTimelineComponent::AnimationTimelineComponent(OscirenderAudioProcessor& processor)
    : audioProcessor(processor)
{
    setOpaque(false);

    addAndMakeVisible(slider);
    slider.setSliderStyle(juce::Slider::SliderStyle::LinearHorizontal);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    slider.setOpaque(false);
    slider.setRange(0, 1, 0.001);
    slider.setColour(juce::Slider::ColourIds::thumbColourId, juce::Colours::black);

    slider.onValueChange = [this]() {
        juce::SpinLock::ScopedLockType sl(audioProcessor.parsersLock);
        int currentFileIndex = audioProcessor.getCurrentFileIndex();
        if (currentFileIndex < 0) return;
        auto parser = audioProcessor.parsers[currentFileIndex];
        if (parser != nullptr) {
            audioProcessor.animationFrame = slider.getValue() * (parser->getNumFrames() - 1);
            parser->setFrame((int) audioProcessor.animationFrame);
        }
    };

    addChildComponent(playButton);
    addChildComponent(pauseButton);
    addAndMakeVisible(stopButton);
    addAndMakeVisible(repeatButton);

    // Set up button behavior
    playButton.onClick = [this]() {
        audioProcessor.animateFrames->setValueNotifyingHost(true);
        playButton.setVisible(false);
        pauseButton.setVisible(true);
    };

    pauseButton.onClick = [this]() {
        audioProcessor.animateFrames->setValueNotifyingHost(false);
        playButton.setVisible(true);
        pauseButton.setVisible(false);
    };
    
    repeatButton.onClick = [this]() {
        audioProcessor.loopAnimation->setValueNotifyingHost(repeatButton.getToggleState());
    };
    
    stopButton.onClick = [this]() {
        audioProcessor.animateFrames->setValueNotifyingHost(false);
        playButton.setVisible(true);
        pauseButton.setVisible(false);
        slider.setValue(0, juce::sendNotification);
    };

    setup();
    startTimer(20);
}

AnimationTimelineComponent::~AnimationTimelineComponent()
{
    stopTimer();
}

void AnimationTimelineComponent::timerCallback()
{ 
     juce::SpinLock::ScopedLockType sl(audioProcessor.parsersLock);
     int currentFileIndex = audioProcessor.getCurrentFileIndex();
     if (currentFileIndex < 0) return;
     auto parser = audioProcessor.parsers[currentFileIndex];
     if (parser == nullptr) return;
     int totalFrames = parser->getNumFrames();
     double frame = std::fmod(audioProcessor.animationFrame, totalFrames);
     slider.setValue(frame / (totalFrames - 1), juce::dontSendNotification);
}

void AnimationTimelineComponent::setup()
{
    // Get the current file parser
    juce::SpinLock::ScopedLockType sl(audioProcessor.parsersLock);
    int currentFileIndex = audioProcessor.getCurrentFileIndex();
    
    bool hasAnimatableContent = false;
    
    if (currentFileIndex >= 0) {
        auto parser = audioProcessor.parsers[currentFileIndex];
        
        if (parser->isAnimatable) {
            hasAnimatableContent = true;
            int totalFrames = parser->getNumFrames();
            int currentFrame = parser->getCurrentFrame();
            
            // Update the slider position without triggering the callback
            if (totalFrames > 1) {
                slider.setValue(static_cast<double>(currentFrame) / (totalFrames - 1), juce::dontSendNotification);
            } else {
                slider.setValue(0, juce::dontSendNotification);
            }
        }
    }
    
    // Update visibility of components
    slider.setVisible(hasAnimatableContent);
    repeatButton.setVisible(hasAnimatableContent);
    stopButton.setVisible(hasAnimatableContent);
    
    if (hasAnimatableContent) {
        playButton.setVisible(!audioProcessor.animateFrames->getBoolValue());
        pauseButton.setVisible(audioProcessor.animateFrames->getBoolValue());
    } else {
        playButton.setVisible(false);
        pauseButton.setVisible(false);
    }
}

void AnimationTimelineComponent::update()
{
    setup();
}

void AnimationTimelineComponent::resized()
{
    auto r = getLocalBounds();

    auto playPauseBounds = r.removeFromLeft(25);
    playButton.setBounds(playPauseBounds);
    pauseButton.setBounds(playPauseBounds);
    stopButton.setBounds(r.removeFromLeft(25));

    repeatButton.setBounds(r.removeFromRight(25));

    slider.setBounds(r);
}

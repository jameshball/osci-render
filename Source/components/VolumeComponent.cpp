#include "VolumeComponent.h"

VolumeComponent::VolumeComponent(CommonAudioProcessor& p) 
    : osci::AudioBackgroundThread("VolumeComponent", p.threadManager), 
      audioProcessor(p)
{
    setOpaque(false);
    setShouldBeRunning(true);

    leftVolumeSmoothed.reset(10);  // Reset with 5 steps for smooth animation
    rightVolumeSmoothed.reset(10);
    leftVolumeSmoothed.setCurrentAndTargetValue(0.0f);
    rightVolumeSmoothed.setCurrentAndTargetValue(0.0f);

    addAndMakeVisible(volumeSlider);
    volumeSlider.setSliderStyle(juce::Slider::SliderStyle::LinearVertical);
    volumeSlider.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    volumeSlider.setColour(juce::Slider::ColourIds::backgroundColourId, juce::Colours::transparentWhite);
    volumeSlider.setColour(juce::Slider::ColourIds::trackColourId, juce::Colours::transparentWhite);
    volumeSlider.setOpaque(false);
    auto volumeParam = audioProcessor.volumeEffect->parameters[0];
    volumeSlider.setRange(volumeParam->min, volumeParam->max, volumeParam->step);
    volumeSlider.setValue(volumeParam->getValueUnnormalised());
    volumeSlider.setDoubleClickReturnValue(true, 1.0);
    volumeSlider.setLookAndFeel(&thumbRadiusLookAndFeel);
    volumeSlider.setColour(juce::Slider::ColourIds::thumbColourId, juce::Colours::white);

    addAndMakeVisible(thresholdSlider);
    thresholdSlider.setSliderStyle(juce::Slider::SliderStyle::LinearVertical);
    thresholdSlider.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    thresholdSlider.setColour(juce::Slider::ColourIds::backgroundColourId, juce::Colours::transparentWhite);
    thresholdSlider.setColour(juce::Slider::ColourIds::trackColourId, juce::Colours::transparentWhite);
    thresholdSlider.setOpaque(false);
    auto& thresholdParam = audioProcessor.thresholdEffect->parameters[0];
    thresholdSlider.setRange(thresholdParam->min, thresholdParam->max, thresholdParam->step);
    thresholdSlider.setValue(thresholdParam->getValueUnnormalised());
    thresholdSlider.setDoubleClickReturnValue(true, 1.0);
    thresholdSlider.setLookAndFeel(&thresholdLookAndFeel);
    thresholdSlider.setColour(juce::Slider::ColourIds::thumbColourId, juce::Colours::black);

    volumeSlider.onValueChange = [this]() {
        audioProcessor.volumeEffect->setValue(volumeSlider.getValue());
    };

    thresholdSlider.onValueChange = [this]() {
        audioProcessor.thresholdEffect->setValue(thresholdSlider.getValue());
    };

    addAndMakeVisible(volumeButton);
    volumeButton.onClick = [this] {
        audioProcessor.muteParameter->setBoolValueNotifyingHost(!audioProcessor.muteParameter->getBoolValue());
    };
}

void VolumeComponent::paint(juce::Graphics& g) {
    g.setColour(juce::Colours::transparentBlack);
    g.fillAll();

    auto r = getLocalBounds().toFloat();
    r.removeFromTop(20);
    r.removeFromRight(r.getWidth() / 2);
    r.removeFromTop(volumeSlider.getLookAndFeel().getSliderThumbRadius(volumeSlider));
    r.removeFromBottom(volumeSlider.getLookAndFeel().getSliderThumbRadius(volumeSlider));

    auto channelHeight = r.getHeight();
    auto leftVolumeHeight = channelHeight * leftVolume;
    auto rightVolumeHeight = channelHeight * rightVolume;

    auto overallRect = r;

    auto leftRect = r.removeFromLeft(r.getWidth() / 2);
    auto rightRect = r;
    auto leftRegion = leftRect;
    auto rightRegion = rightRect;

    g.setColour(juce::Colours::white.withAlpha(0.1f));
    g.drawRoundedRectangle(overallRect, 4.0f, 1.0f);

    // Enable anti-aliasing for smoother rendering
    g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);

    auto drawVolumeMeter = [&](juce::Rectangle<float>& rect, float volume) {
        auto meterRect = rect.removeFromBottom(volume * channelHeight);
        auto gradient = juce::ColourGradient(
            juce::Colour(0xff00ff00),  // Green
            0, overallRect.getBottom(),
            juce::Colour(0xffff0000),  // Red
            0, overallRect.getY(),
            false);
        gradient.addColour(0.3, juce::Colour(0xffffe600));  // Yellow in the middle
        g.setGradientFill(gradient);
        // Draw rounded meter
        g.saveState();
        g.reduceClipRegion(meterRect.toNearestInt());
        g.fillRoundedRectangle(meterRect.reduced(1.0f), 2.0f);
        g.restoreState();
    };

    drawVolumeMeter(leftRect, leftVolume);
    drawVolumeMeter(rightRect, rightVolume);

    // Draw smooth volume indicators
    auto drawSmoothIndicator = [&](const juce::Rectangle<float>& region, float value) {
        if (value > 0.0f) {  // Only draw indicator if volume is greater than 0
            auto y = region.getBottom() - (value * channelHeight);
            auto indicatorHeight = 3.0f;
            auto indicatorRect = juce::Rectangle<float>(
                region.getX(), y - indicatorHeight / 2,
                region.getWidth(), indicatorHeight);
                
            g.setColour(juce::Colours::white.withAlpha(0.8f));
            g.fillRoundedRectangle(indicatorRect, 1.5f);
        }
    };

    drawSmoothIndicator(leftRegion, leftVolumeSmoothed.getNextValue());
    drawSmoothIndicator(rightRegion, rightVolumeSmoothed.getNextValue());

    // Draw threshold line with modern style
    auto thresholdY = rightRegion.getBottom() - (thresholdSlider.getValue() * channelHeight);
    auto thresholdRect = juce::Rectangle<float>(
        leftRegion.getX(),
        thresholdY - 1.5f,
        leftRegion.getWidth() + rightRegion.getWidth(),
        3.0f
    );
    
    g.setColour(juce::Colours::white.withAlpha(0.7f));
    g.fillRoundedRectangle(thresholdRect, 1.5f);
}

void VolumeComponent::handleAsyncUpdate() {
    repaint();
    volumeSlider.setValue(audioProcessor.volumeEffect->getValue(), juce::NotificationType::dontSendNotification);
    thresholdSlider.setValue(audioProcessor.thresholdEffect->getValue(), juce::NotificationType::dontSendNotification);
}

void VolumeComponent::runTask(const juce::AudioBuffer<float>& buffer) {
    float leftVolume = 0;
    float rightVolume = 0;

    for (int i = 0; i < buffer.getNumSamples(); i++) {
        leftVolume += buffer.getSample(0, i) * buffer.getSample(0, i);
        rightVolume += buffer.getSample(1, i) * buffer.getSample(1, i);
    }
    // RMS
    leftVolume = std::sqrt(leftVolume / buffer.getNumSamples());
    rightVolume = std::sqrt(rightVolume / buffer.getNumSamples());

    if (std::isnan(leftVolume) || std::isnan(rightVolume)) {
        leftVolume = 0;
        rightVolume = 0;
    }
    
    this->leftVolume = leftVolume;
    this->rightVolume = rightVolume;

    leftVolumeSmoothed.setTargetValue(leftVolume);
    rightVolumeSmoothed.setTargetValue(rightVolume);
    
    triggerAsyncUpdate();
}

int VolumeComponent::prepareTask(double sampleRate, int bufferSize) {
    return BUFFER_DURATION_SECS * sampleRate;
}

void VolumeComponent::stopTask() {}

void VolumeComponent::resized() {
    auto r = getLocalBounds();

    auto iconRow = r.removeFromTop(20);
    volumeButton.setBounds(iconRow);
    
    volumeSlider.setBounds(r.removeFromLeft(r.getWidth() / 2));
    thresholdSlider.setBounds(r.reduced(0, 5.0));
}

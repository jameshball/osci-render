#include "VolumeComponent.h"
#include "../audio/OutputClip.h"

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

VolumeComponent::~VolumeComponent() {
    // Stop the background thread before the derived vptr is destroyed.
    setShouldBeRunning(false);
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
    auto leftVolumeValue = juce::jlimit(0.0f, 1.0f, leftVolume.load());
    auto rightVolumeValue = juce::jlimit(0.0f, 1.0f, rightVolume.load());
    auto leftPeakValue = leftPeak.load();
    auto rightPeakValue = rightPeak.load();
    auto thresholdValue = static_cast<float>(thresholdSlider.getValue());
    auto clipActive = osci::isOutputClipActive(thresholdValue);

    auto overallRect = r;

    auto leftRect = r.removeFromLeft(r.getWidth() / 2);
    auto rightRect = r;
    auto leftRegion = leftRect;
    auto rightRegion = rightRect;

    g.setColour(juce::Colours::white.withAlpha(0.1f));
    g.drawRoundedRectangle(overallRect, 4.0f, 1.0f);

    // Enable anti-aliasing for smoother rendering
    g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);

    auto drawVolumeMeter = [&](juce::Rectangle<float> rect, float volume) {
        auto displayVolume = juce::jlimit(0.0f, 1.0f, volume);
        auto meterRect = rect.removeFromBottom(displayVolume * channelHeight);
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

    drawVolumeMeter(leftRect, leftVolumeValue);
    drawVolumeMeter(rightRect, rightVolumeValue);

    // Draw smooth volume indicators
    auto drawSmoothIndicator = [&](const juce::Rectangle<float>& region, float value) {
        auto displayValue = juce::jlimit(0.0f, 1.0f, value);

        if (displayValue > 0.0f) {  // Only draw indicator if volume is greater than 0
            auto y = region.getBottom() - (displayValue * channelHeight);
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

    // Draw the clipping threshold, or a dim unity reference when clipping is bypassed.
    auto thresholdReference = clipActive ? juce::jlimit(0.0f, 1.0f, thresholdValue) : 1.0f;
    auto thresholdLineHeight = clipActive ? 3.0f : 2.0f;
    auto thresholdY = rightRegion.getBottom() - (thresholdReference * channelHeight);
    thresholdY = juce::jlimit(rightRegion.getY() + thresholdLineHeight * 0.5f, rightRegion.getBottom() - thresholdLineHeight * 0.5f, thresholdY);
    auto thresholdRect = juce::Rectangle<float>(
        leftRegion.getX(),
        thresholdY - thresholdLineHeight * 0.5f,
        leftRegion.getWidth() + rightRegion.getWidth(),
        thresholdLineHeight
    );

    g.setColour(juce::Colours::white.withAlpha(clipActive ? 0.7f : 0.22f));
    g.fillRoundedRectangle(thresholdRect, thresholdLineHeight * 0.5f);

    auto drawPeakCap = [&](const juce::Rectangle<float>& region, float peak) {
        auto crossedClipThreshold = clipActive && peak > 0.0f && peak >= thresholdValue - osci::kOutputClipPeakEpsilon;
        auto crossedUnity = !clipActive && peak > 1.0f + osci::kOutputClipPeakEpsilon;

        if (!crossedClipThreshold && !crossedUnity) {
            return;
        }

        auto capRect = juce::Rectangle<float>(
            region.getX() + 1.0f,
            region.getY() + 1.0f,
            region.getWidth() - 2.0f,
            3.0f);

        g.setColour(crossedClipThreshold ? juce::Colours::red.withAlpha(0.78f) : juce::Colours::orange.withAlpha(0.65f));
        g.fillRoundedRectangle(capRect, 1.5f);
    };

    drawPeakCap(leftRegion, leftPeakValue);
    drawPeakCap(rightRegion, rightPeakValue);
}

void VolumeComponent::handleAsyncUpdate() {
    // Update SmoothedValue targets on the message thread (they are not thread-safe).
    leftVolumeSmoothed.setTargetValue(leftVolume.load());
    rightVolumeSmoothed.setTargetValue(rightVolume.load());
    repaint();
    volumeSlider.setValue(audioProcessor.volumeEffect->getValue(), juce::NotificationType::dontSendNotification);
    thresholdSlider.setValue(audioProcessor.thresholdEffect->getValue(), juce::NotificationType::dontSendNotification);
}

void VolumeComponent::runTask(const juce::AudioBuffer<float>& buffer) {
    const int numSamples = buffer.getNumSamples();

    if (numSamples <= 0 || buffer.getNumChannels() < 2) {
        leftVolume = 0;
        rightVolume = 0;
        leftPeak = 0;
        rightPeak = 0;
        triggerAsyncUpdate();
        return;
    }

    float leftRms = 0;
    float rightRms = 0;
    float leftPeakValue = 0;
    float rightPeakValue = 0;
    const float* leftChannel = buffer.getReadPointer(0);
    const float* rightChannel = buffer.getReadPointer(1);

    for (int i = 0; i < numSamples; i++) {
        auto leftSample = leftChannel[i];
        auto rightSample = rightChannel[i];
        leftRms += leftSample * leftSample;
        rightRms += rightSample * rightSample;
        leftPeakValue = juce::jmax(leftPeakValue, std::abs(leftSample));
        rightPeakValue = juce::jmax(rightPeakValue, std::abs(rightSample));
    }
    // RMS
    leftRms = std::sqrt(leftRms / numSamples);
    rightRms = std::sqrt(rightRms / numSamples);

    if (std::isnan(leftRms) || std::isnan(rightRms)) {
        leftRms = 0;
        rightRms = 0;
    }

    this->leftVolume = leftRms;
    this->rightVolume = rightRms;
    this->leftPeak = leftPeakValue;
    this->rightPeak = rightPeakValue;

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

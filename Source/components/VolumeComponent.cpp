#include "VolumeComponent.h"

VolumeComponent::VolumeComponent(OscirenderAudioProcessor& p) : AudioBackgroundThread("VolumeComponent", p.threadManager), audioProcessor(p) {
    setOpaque(false);
    setShouldBeRunning(true);

    addAndMakeVisible(volumeSlider);
    volumeSlider.setSliderStyle(juce::Slider::SliderStyle::LinearVertical);
    volumeSlider.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    volumeSlider.setColour(juce::Slider::ColourIds::backgroundColourId, juce::Colours::transparentWhite);
    volumeSlider.setColour(juce::Slider::ColourIds::trackColourId, juce::Colours::transparentWhite);
    volumeSlider.setOpaque(false);
    auto volumeParam = audioProcessor.volumeEffect->parameters[0];
    volumeSlider.setRange(volumeParam->min, volumeParam->max, volumeParam->step);
    volumeSlider.setValue(volumeParam->getValueUnnormalised());
    volumeSlider.setLookAndFeel(&thumbRadiusLookAndFeel);
    volumeSlider.setColour(juce::Slider::ColourIds::thumbColourId, juce::Colours::black);

    addAndMakeVisible(thresholdSlider);
    thresholdSlider.setSliderStyle(juce::Slider::SliderStyle::LinearVertical);
    thresholdSlider.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    thresholdSlider.setColour(juce::Slider::ColourIds::backgroundColourId, juce::Colours::transparentWhite);
    thresholdSlider.setColour(juce::Slider::ColourIds::trackColourId, juce::Colours::transparentWhite);
    thresholdSlider.setOpaque(false);
    auto& thresholdParam = audioProcessor.thresholdEffect->parameters[0];
    thresholdSlider.setRange(thresholdParam->min, thresholdParam->max, thresholdParam->step);
    thresholdSlider.setValue(thresholdParam->getValueUnnormalised());
    thresholdSlider.setLookAndFeel(&thresholdLookAndFeel);
    thresholdSlider.setColour(juce::Slider::ColourIds::thumbColourId, juce::Colours::black);

    volumeSlider.onValueChange = [this]() {
        audioProcessor.volumeEffect->setValue(volumeSlider.getValue());
    };

    thresholdSlider.onValueChange = [this]() {
        audioProcessor.thresholdEffect->setValue(thresholdSlider.getValue());
    };

    auto doc = juce::XmlDocument::parse(BinaryData::volume_svg);
    volumeIcon = juce::Drawable::createFromSVG(*doc);
    doc = juce::XmlDocument::parse(BinaryData::threshold_svg);
    thresholdIcon = juce::Drawable::createFromSVG(*doc);

    addAndMakeVisible(*volumeIcon);
    addAndMakeVisible(*thresholdIcon);
}

void VolumeComponent::paint(juce::Graphics& g) {
    auto r = getLocalBounds().toFloat();
    r.removeFromTop(20);
    r.removeFromRight(r.getWidth() / 2);
    r.removeFromTop(volumeSlider.getLookAndFeel().getSliderThumbRadius(volumeSlider));
    r.removeFromBottom(volumeSlider.getLookAndFeel().getSliderThumbRadius(volumeSlider));

    g.setColour(juce::Colours::white);
    g.fillRect(r);

    auto channelHeight = r.getHeight();
    auto leftVolumeHeight = channelHeight * leftVolume;
    auto rightVolumeHeight = channelHeight * rightVolume;

    auto leftRect = r.removeFromLeft(r.getWidth() / 2);
    auto rightRect = r;
    auto leftRegion = leftRect;
    auto rightRegion = rightRect;

    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff00ff00), 0, leftRect.getBottom(), juce::Colours::red, 0, leftRect.getY(), false));
    g.fillRect(leftRect.removeFromBottom(leftVolumeHeight));

    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff00ff00), 0, rightRect.getBottom(), juce::Colours::red, 0, rightRect.getY(), false));
    g.fillRect(rightRect.removeFromBottom(rightVolumeHeight));

    auto barWidth = 5.0f;

    g.setColour(juce::Colours::black);
    g.fillRect(leftRegion.getX(), leftRegion.getBottom() - (avgLeftVolume * channelHeight) - barWidth / 2, leftRegion.getWidth(), barWidth);
    g.fillRect(rightRegion.getX(), rightRegion.getBottom() - (avgRightVolume * channelHeight) - barWidth / 2, rightRegion.getWidth(), barWidth);

    g.fillRect(leftRegion.getX(), rightRegion.getBottom() - (thresholdSlider.getValue() * channelHeight) - barWidth / 2, leftRegion.getWidth() + rightRegion.getWidth(), barWidth);
}

void VolumeComponent::handleAsyncUpdate() {
    repaint();
    volumeSlider.setValue(audioProcessor.volumeEffect->getValue(), juce::NotificationType::dontSendNotification);
    thresholdSlider.setValue(audioProcessor.thresholdEffect->getValue(), juce::NotificationType::dontSendNotification);
}

void VolumeComponent::runTask(const std::vector<OsciPoint>& buffer) {
    float leftVolume = 0;
    float rightVolume = 0;

    for (int i = 0; i < buffer.size(); i++) {
        leftVolume += buffer[i].x * buffer[i].x;
        rightVolume += buffer[i].y * buffer[i].y;
    }
    // RMS
    leftVolume = std::sqrt(leftVolume / buffer.size());
    rightVolume = std::sqrt(rightVolume / buffer.size());

    if (std::isnan(leftVolume) || std::isnan(rightVolume)) {
        leftVolume = 0;
        rightVolume = 0;
    }

    this->leftVolume = leftVolume;
    this->rightVolume = rightVolume;

    avgLeftVolume = (avgLeftVolume * 0.95) + (leftVolume * 0.05);
    avgRightVolume = (avgRightVolume * 0.95) + (rightVolume * 0.05);
    
    triggerAsyncUpdate();
}

int VolumeComponent::prepareTask(double sampleRate, int bufferSize) {
    return BUFFER_DURATION_SECS * sampleRate;
}

void VolumeComponent::stopTask() {}

void VolumeComponent::resized() {
    auto r = getLocalBounds();

    auto iconRow = r.removeFromTop(20).toFloat();
    volumeIcon->setTransformToFit(iconRow.removeFromLeft(iconRow.getWidth() / 2).reduced(1), juce::RectanglePlacement::centred);
    thresholdIcon->setTransformToFit(iconRow.reduced(2), juce::RectanglePlacement::centred);
    volumeSlider.setBounds(r.removeFromLeft(r.getWidth() / 2));
    auto radius = volumeSlider.getLookAndFeel().getSliderThumbRadius(volumeSlider);
    thresholdSlider.setBounds(r.reduced(0, radius / 2));
}

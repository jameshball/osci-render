#include "VolumeComponent.h"

VolumeComponent::VolumeComponent(OscirenderAudioProcessor& p) : audioProcessor(p), juce::Thread("VolumeComponent") {
    setOpaque(false);
    startTimerHz(60);
    startThread();

    addAndMakeVisible(volumeSlider);
    volumeSlider.setSliderStyle(juce::Slider::SliderStyle::LinearVertical);
    volumeSlider.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    volumeSlider.setColour(juce::Slider::ColourIds::backgroundColourId, juce::Colours::transparentWhite);
    volumeSlider.setColour(juce::Slider::ColourIds::trackColourId, juce::Colours::transparentWhite);
    volumeSlider.setOpaque(false);
    volumeSlider.setRange(0, 2, 0.001);
    volumeSlider.setValue(1);
    volumeSlider.setLookAndFeel(&thumbRadiusLookAndFeel);
    volumeSlider.setColour(juce::Slider::ColourIds::thumbColourId, juce::Colours::black);

    addAndMakeVisible(thresholdSlider);
    thresholdSlider.setSliderStyle(juce::Slider::SliderStyle::LinearVertical);
    thresholdSlider.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    thresholdSlider.setColour(juce::Slider::ColourIds::backgroundColourId, juce::Colours::transparentWhite);
    thresholdSlider.setColour(juce::Slider::ColourIds::trackColourId, juce::Colours::transparentWhite);
    thresholdSlider.setOpaque(false);
    thresholdSlider.setRange(0, 1, 0.001);
    thresholdSlider.setValue(1);
    thresholdSlider.setLookAndFeel(&thresholdLookAndFeel);
    thresholdSlider.setColour(juce::Slider::ColourIds::thumbColourId, juce::Colours::black);

    volumeSlider.onValueChange = [this]() {
        audioProcessor.volume = volumeSlider.getValue();
    };

    thresholdSlider.onValueChange = [this]() {
        audioProcessor.threshold = thresholdSlider.getValue();
    };

    auto doc = juce::XmlDocument::parse(BinaryData::volume_svg);
    volumeIcon = juce::Drawable::createFromSVG(*doc);
    doc = juce::XmlDocument::parse(BinaryData::threshold_svg);
    thresholdIcon = juce::Drawable::createFromSVG(*doc);

    addAndMakeVisible(*volumeIcon);
    addAndMakeVisible(*thresholdIcon);
}

VolumeComponent::~VolumeComponent() {
    audioProcessor.audioProducer.unregisterConsumer(consumer);
    stopThread(1000);
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

void VolumeComponent::timerCallback() {
    repaint();
}

void VolumeComponent::run() {
    audioProcessor.audioProducer.registerConsumer(consumer);

    while (!threadShouldExit()) {
        auto buffer = consumer->startProcessing();

        float leftVolume = 0;
        float rightVolume = 0;

        for (int i = 0; i < buffer->size(); i += 2) {
            leftVolume += buffer->at(i) * buffer->at(i);
            rightVolume += buffer->at(i + 1) * buffer->at(i + 1);
        }
        // RMS
        leftVolume = std::sqrt(leftVolume / (buffer->size() / 2));
        rightVolume = std::sqrt(rightVolume / (buffer->size() / 2));

        this->leftVolume = leftVolume;
        this->rightVolume = rightVolume;

        avgLeftVolume = (avgLeftVolume * 0.95) + (leftVolume * 0.05);
        avgRightVolume = (avgRightVolume * 0.95) + (rightVolume * 0.05);

        consumer->finishedProcessing();
    }
}

void VolumeComponent::resized() {
    auto r = getLocalBounds();

    auto iconRow = r.removeFromTop(20).toFloat();
    volumeIcon->setTransformToFit(iconRow.removeFromLeft(iconRow.getWidth() / 2).reduced(1), juce::RectanglePlacement::centred);
    thresholdIcon->setTransformToFit(iconRow.reduced(2), juce::RectanglePlacement::centred);
    volumeSlider.setBounds(r.removeFromLeft(r.getWidth() / 2));
    thresholdSlider.setBounds(r);
}

#include "FrameSettingsComponent.h"
#include "PluginEditor.h"

FrameSettingsComponent::FrameSettingsComponent(OscirenderAudioProcessor& p, OscirenderAudioProcessorEditor& editor) : audioProcessor(p), pluginEditor(editor) {
	setText("Frame Settings");

	addAndMakeVisible(animate);
	addAndMakeVisible(sync);
	addAndMakeVisible(rateLabel);
	addAndMakeVisible(rateBox);
	addAndMakeVisible(offsetLabel);
	addAndMakeVisible(offsetBox);
	addAndMakeVisible(invertImage);
	addAndMakeVisible(threshold);
	addAndMakeVisible(stride);

	animate.setTooltip("Enables animation for files that have multiple frames, such as GIFs or Line Art.");
	sync.setTooltip("Synchronises the animation's framerate with the BPM of your DAW.");
	offsetLabel.setTooltip("Offsets the animation's start point by a specified number of frames.");

	rateLabel.setText("Frames per Second", juce::dontSendNotification);
	rateBox.setJustification(juce::Justification::left);

	offsetLabel.setText("Start Frame", juce::dontSendNotification);
	offsetBox.setJustification(juce::Justification::left);
	
	update();

	auto updateAnimation = [this]() {
		audioProcessor.animateFrames->setValueNotifyingHost(animate.getToggleState());
		audioProcessor.animationSyncBPM->setValueNotifyingHost(sync.getToggleState());
		audioProcessor.animationRate->setUnnormalisedValueNotifyingHost(rateBox.getValue());
		audioProcessor.animationOffset->setUnnormalisedValueNotifyingHost(offsetBox.getValue());
		audioProcessor.invertImage->setValueNotifyingHost(invertImage.getToggleState());
	};

	animate.onClick = updateAnimation;
	sync.onClick = updateAnimation;
	rateBox.onFocusLost = updateAnimation;
	offsetBox.onFocusLost = updateAnimation;

	invertImage.onClick = [this]() {
        audioProcessor.invertImage->setValue(invertImage.getToggleState());
    };

	threshold.slider.onValueChange = [this]() {
        audioProcessor.imageThreshold->setValue(threshold.slider.getValue());
    };

	stride.slider.onValueChange = [this]() {
        audioProcessor.imageStride->setValue(stride.slider.getValue());
    };

	audioProcessor.animateFrames->addListener(this);
	audioProcessor.animationSyncBPM->addListener(this);
	audioProcessor.animationRate->addListener(this);
	audioProcessor.animationOffset->addListener(this);
	audioProcessor.invertImage->addListener(this);
}

FrameSettingsComponent::~FrameSettingsComponent() {
	audioProcessor.invertImage->removeListener(this);
	audioProcessor.animationOffset->removeListener(this);
	audioProcessor.animationRate->removeListener(this);
	audioProcessor.animationSyncBPM->removeListener(this);
	audioProcessor.animateFrames->removeListener(this);
}

void FrameSettingsComponent::resized() {
	auto area = getLocalBounds().withTrimmedTop(20).reduced(20);
	auto firstColumn = area.removeFromLeft(220);
	double rowHeight = 20;
	double rowSpace = 10;
	auto animateBounds = firstColumn.removeFromTop(rowHeight);
	animate.setBounds(animateBounds.removeFromLeft(100));
	sync.setBounds(animateBounds.removeFromLeft(100));
	firstColumn.removeFromTop(rowSpace);

	animateBounds = firstColumn.removeFromTop(rowHeight);
	rateLabel.setBounds(animateBounds.removeFromLeft(140));
	rateBox.setBounds(animateBounds.removeFromLeft(60));
	firstColumn.removeFromTop(rowSpace);

	animateBounds = firstColumn.removeFromTop(rowHeight);
	offsetLabel.setBounds(animateBounds.removeFromLeft(140));
	offsetBox.setBounds(animateBounds.removeFromLeft(60));

	auto secondColumn = area;
	auto invertBounds = secondColumn.removeFromTop(rowHeight);
	invertImage.setBounds(invertBounds.removeFromLeft(100));
	secondColumn.removeFromTop(rowSpace);

	threshold.setBounds(secondColumn.removeFromTop(rowHeight));
	stride.setBounds(secondColumn.removeFromTop(rowHeight));
}

void FrameSettingsComponent::update() {
	rateBox.setValue(audioProcessor.animationRate->getValueUnnormalised(), false, 2);
	offsetBox.setValue(audioProcessor.animationOffset->getValueUnnormalised(), false, 2);
	animate.setToggleState(audioProcessor.animateFrames->getValue(), false);
	sync.setToggleState(audioProcessor.animationSyncBPM->getValue(), false);
	invertImage.setToggleState(audioProcessor.invertImage->getValue(), false);
	if (sync.getToggleState()) {
		rateLabel.setText("Frames per Beat", juce::dontSendNotification);
		rateLabel.setTooltip("Set the animation's speed in frames per beat.");
	} else {
		rateLabel.setText("Frames per Second", juce::dontSendNotification);
		rateLabel.setTooltip("Set the animation's speed in frames per second.");
	}
}

void FrameSettingsComponent::parameterValueChanged(int parameterIndex, float newValue) {
	triggerAsyncUpdate();
}

void FrameSettingsComponent::parameterGestureChanged(int parameterIndex, bool gestureIsStarting) {}

void FrameSettingsComponent::handleAsyncUpdate() {
	update();
}

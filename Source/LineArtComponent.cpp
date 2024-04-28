#include "LineArtComponent.h"
#include "PluginEditor.h"

LineArtComponent::LineArtComponent(OscirenderAudioProcessor& p, OscirenderAudioProcessorEditor& editor) : audioProcessor(p), pluginEditor(editor) {
	setText("Line Art Settings");

	addAndMakeVisible(animate);
	addAndMakeVisible(sync);
	addAndMakeVisible(rateLabel);
	addAndMakeVisible(rateBox);
	addAndMakeVisible(offsetLabel);
	addAndMakeVisible(offsetBox);

	animate.setTooltip("Enables animation for line art files.");
	sync.setTooltip("Synchronises the animation's framerate with the BPM of your DAW.");
	offsetLabel.setTooltip("Offsets the animation's start point by a specified number of frames.");

	rateLabel.setText("Frames per Second", juce::dontSendNotification);
	rateBox.setJustification(juce::Justification::left);

	offsetLabel.setText("Start Frame", juce::dontSendNotification);
	offsetBox.setJustification(juce::Justification::left);
	
	update();

	auto updateAnimation = [this]() {
		audioProcessor.animateLineArt->setValueNotifyingHost(animate.getToggleState());
		audioProcessor.animationSyncBPM->setValueNotifyingHost(sync.getToggleState());
		audioProcessor.animationRate->setUnnormalisedValueNotifyingHost(rateBox.getValue());
		audioProcessor.animationOffset->setUnnormalisedValueNotifyingHost(offsetBox.getValue());
	};

	animate.onClick = updateAnimation;
	sync.onClick = updateAnimation;
	rateBox.onFocusLost = updateAnimation;
	offsetBox.onFocusLost = updateAnimation;

	audioProcessor.animateLineArt->addListener(this);
	audioProcessor.animationSyncBPM->addListener(this);
	audioProcessor.animationRate->addListener(this);
	audioProcessor.animationOffset->addListener(this);
}

LineArtComponent::~LineArtComponent() {
	audioProcessor.animationOffset->removeListener(this);
	audioProcessor.animationRate->removeListener(this);
	audioProcessor.animationSyncBPM->removeListener(this);
	audioProcessor.animateLineArt->removeListener(this);
}

void LineArtComponent::resized() {
	auto area = getLocalBounds().withTrimmedTop(20).reduced(20);
	double rowHeight = 20;
	double rowSpace = 10;
	auto animateBounds = area.removeFromTop(rowHeight);
	animate.setBounds(animateBounds.removeFromLeft(100));
	sync.setBounds(animateBounds.removeFromLeft(100));
	area.removeFromTop(rowSpace);

	animateBounds = area.removeFromTop(rowHeight);
	rateLabel.setBounds(animateBounds.removeFromLeft(140));
	rateBox.setBounds(animateBounds.removeFromLeft(60));
	area.removeFromTop(rowSpace);

	animateBounds = area.removeFromTop(rowHeight);
	offsetLabel.setBounds(animateBounds.removeFromLeft(140));
	offsetBox.setBounds(animateBounds.removeFromLeft(60));
}

void LineArtComponent::update() {
	rateBox.setValue(audioProcessor.animationRate->getValueUnnormalised(), false, 2);
	offsetBox.setValue(audioProcessor.animationOffset->getValueUnnormalised(), false, 2);
	animate.setToggleState(audioProcessor.animateLineArt->getValue(), false);
	sync.setToggleState(audioProcessor.animationSyncBPM->getValue(), false);
	if (sync.getToggleState()) {
		rateLabel.setText("Frames per Beat", juce::dontSendNotification);
		rateLabel.setTooltip("Set the animation's speed in frames per beat.");
	} else {
		rateLabel.setText("Frames per Second", juce::dontSendNotification);
		rateLabel.setTooltip("Set the animation's speed in frames per second.");
	}
}

void LineArtComponent::parameterValueChanged(int parameterIndex, float newValue) {
	triggerAsyncUpdate();
}

void LineArtComponent::parameterGestureChanged(int parameterIndex, bool gestureIsStarting) {}

void LineArtComponent::handleAsyncUpdate() {
	update();
}

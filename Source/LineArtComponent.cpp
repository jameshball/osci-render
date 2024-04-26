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

	rateLabel.setText("Framerate", juce::dontSendNotification);
	rateBox.setJustification(juce::Justification::left);

	offsetLabel.setText("Offset", juce::dontSendNotification);
	offsetBox.setJustification(juce::Justification::left);
	
	update();

	auto updateAnimation = [this]() {
		audioProcessor.animateLineArt->setValueNotifyingHost(animate.getToggleState());
		audioProcessor.syncMIDIAnimation->setValueNotifyingHost(sync.getToggleState());
		audioProcessor.animationRate->setUnnormalisedValueNotifyingHost(rateBox.getValue());
		audioProcessor.animationOffset->setUnnormalisedValueNotifyingHost(offsetBox.getValue());
	};

	animate.onClick = updateAnimation;
	sync.onClick = updateAnimation;
	rateBox.onFocusLost = updateAnimation;
	offsetBox.onFocusLost = updateAnimation;

	audioProcessor.animateLineArt->addListener(this);
	audioProcessor.syncMIDIAnimation->addListener(this);
	audioProcessor.animationRate->addListener(this);
	audioProcessor.animationOffset->addListener(this);
}

LineArtComponent::~LineArtComponent() {
	audioProcessor.animationOffset->removeListener(this);
	audioProcessor.animationRate->removeListener(this);
	audioProcessor.syncMIDIAnimation->removeListener(this);
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
	rateLabel.setBounds(animateBounds.removeFromLeft(80));
	rateBox.setBounds(animateBounds.removeFromLeft(60));
	area.removeFromTop(rowSpace);

	animateBounds = area.removeFromTop(rowHeight);
	offsetLabel.setBounds(animateBounds.removeFromLeft(80));
	offsetBox.setBounds(animateBounds.removeFromLeft(60));
}

void LineArtComponent::update() {
	rateBox.setValue(audioProcessor.animationRate->getValueUnnormalised(), false, 2);
	offsetBox.setValue(audioProcessor.animationOffset->getValueUnnormalised(), false, 2);
	animate.setToggleState(audioProcessor.animateLineArt->getValue(), false);
	sync.setToggleState(audioProcessor.syncMIDIAnimation->getValue(), false);
}

void LineArtComponent::parameterValueChanged(int parameterIndex, float newValue) {
	triggerAsyncUpdate();
}

void LineArtComponent::parameterGestureChanged(int parameterIndex, bool gestureIsStarting) {}

void LineArtComponent::handleAsyncUpdate() {
	update();
}

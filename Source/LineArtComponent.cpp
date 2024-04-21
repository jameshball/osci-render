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

	rateLabel.setText("Framerate: ", juce::dontSendNotification);
	rateBox.setValue(audioProcessor.animationRate->getValueUnnormalised(), false, 2);
	rateBox.setJustification(juce::Justification::left);

	offsetLabel.setText("   Offset: ", juce::dontSendNotification);
	offsetBox.setValue(audioProcessor.animationOffset->getValueUnnormalised(), false, 2);
	offsetBox.setJustification(juce::Justification::left);

	audioProcessor.openFile(audioProcessor.currentFile);
	update();

	auto updateAnimation = [this]() {
		audioProcessor.animateLineArt->setValue(animate.getToggleState());
		audioProcessor.syncMIDIAnimation->setValue(sync.getToggleState());
		audioProcessor.animationRate->setValueUnnormalised(rateBox.getValue());
		audioProcessor.animationOffset->setValueUnnormalised(offsetBox.getValue());
	};

	animate.onClick = updateAnimation;
	sync.onClick = updateAnimation;
	rateBox.onFocusLost = updateAnimation;
	offsetBox.onFocusLost = updateAnimation;
}

void LineArtComponent::resized() {
	auto area = getLocalBounds().withTrimmedTop(20).reduced(20);
	double rowHeight = 30;
	double rowSpace = 5;
	auto animateBounds = area.removeFromTop(rowHeight);
	animate.setBounds(animateBounds);
	area.removeFromTop(rowSpace);

	animateBounds = area.removeFromTop(rowHeight);
	sync.setBounds(animateBounds);
	area.removeFromTop(rowSpace);

	animateBounds = area.removeFromTop(rowHeight);
	rateLabel.setBounds(animateBounds.removeFromLeft(100));
	rateBox.setBounds(animateBounds);
	area.removeFromTop(rowSpace);

	animateBounds = area.removeFromTop(rowHeight);
	offsetLabel.setBounds(animateBounds.removeFromLeft(100));
	offsetBox.setBounds(animateBounds);
	area.removeFromTop(rowSpace);
}

void LineArtComponent::update() {
	rateBox.setValue(audioProcessor.animationRate->getValueUnnormalised(), true, 2);
	offsetBox.setValue(audioProcessor.animationOffset->getValueUnnormalised(), true, 2);
	animate.setToggleState(audioProcessor.animateLineArt->getValue(), true);
	sync.setToggleState(audioProcessor.syncMIDIAnimation->getValue(), true);
}

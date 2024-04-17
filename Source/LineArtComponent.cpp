#include "LineArtComponent.h"
#include "PluginEditor.h"

LineArtComponent::LineArtComponent(OscirenderAudioProcessor& p, OscirenderAudioProcessorEditor& editor) : audioProcessor(p), pluginEditor(editor) {
	setText("Line Art Settings");


	addAndMakeVisible(animate);
	addAndMakeVisible(sync);
	addAndMakeVisible(rateLabel);
	addAndMakeVisible(rate);
	addAndMakeVisible(offsetLabel);
	addAndMakeVisible(offset);

	rateLabel.setText("Framerate: ", juce::dontSendNotification);
	rate.setText("8", false);
	rate.setInputRestrictions(6, "0123456789.");

	offsetLabel.setText("   Offset: ", juce::dontSendNotification);
	offset.setText("0", false);
	offset.setInputRestrictions(6, "0123456789");

	audioProcessor.openFile(audioProcessor.currentFile);
	update();

	auto updateAnimation = [this]() {
		audioProcessor.animateLineArt = animate.getToggleState();
		audioProcessor.syncMIDIAnimation = sync.getToggleState();
		try {
			audioProcessor.animationRate = std::stof(rate.getText().toStdString());
		}
		catch (std::exception e) {
			audioProcessor.animationRate = 8.f;
		}
		try {
			audioProcessor.animationOffset = std::stoi(offset.getText().toStdString());
		}
		catch (std::exception e) {
			audioProcessor.animationOffset = 0;
		}
	};

	animate.onClick = updateAnimation;
	sync.onClick = updateAnimation;
	rate.onFocusLost = updateAnimation;
	offset.onFocusLost = updateAnimation;
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
	rate.setBounds(animateBounds);
	area.removeFromTop(rowSpace);

	animateBounds = area.removeFromTop(rowHeight);
	offsetLabel.setBounds(animateBounds.removeFromLeft(100));
	offset.setBounds(animateBounds);
	area.removeFromTop(rowSpace);
}

void LineArtComponent::update() {
	rate.setText(juce::String(audioProcessor.animationRate));
	offset.setText(juce::String(audioProcessor.animationOffset));
	animate.setToggleState(audioProcessor.animateLineArt, false);
	sync.setToggleState(audioProcessor.syncMIDIAnimation, false);
}

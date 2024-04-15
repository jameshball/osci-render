#include "LineArtComponent.h"
#include "PluginEditor.h"

LineArtComponent::LineArtComponent(OscirenderAudioProcessor& p, OscirenderAudioProcessorEditor& editor) : audioProcessor(p), pluginEditor(editor) {
	setText("Line Art Settings");


	addAndMakeVisible(animate);
	addAndMakeVisible(sync);
	addAndMakeVisible(rate);
	rate.setText("10", false);
	rate.setInputRestrictions(6, "0123456789.");

	update();

	auto updateAnimation = [this]() {
		audioProcessor.animateLineArt = animate.getToggleState();
		audioProcessor.syncMIDIAnimation = sync.getToggleState();
		try {
			audioProcessor.animationRate = std::stof(rate.getText().toStdString());
		}
		catch (std::exception e) {
			audioProcessor.animationRate = 10.f;
		}
		audioProcessor.openFile(audioProcessor.currentFile);
	};

	animate.onClick = updateAnimation;
	sync.onClick = updateAnimation;
	rate.onFocusLost = updateAnimation;
}

void LineArtComponent::resized() {
	auto area = getLocalBounds().withTrimmedTop(20).reduced(20);
	double rowHeight = 30;
	animate.setBounds(area.removeFromTop(rowHeight));
	sync.setBounds(area.removeFromTop(rowHeight));
	rate.setBounds(area.removeFromTop(rowHeight));
}

void LineArtComponent::update() {
	rate.setText(juce::String(audioProcessor.animationRate));
	animate.setToggleState(audioProcessor.animateLineArt, false);
	sync.setToggleState(audioProcessor.syncMIDIAnimation, false);
}

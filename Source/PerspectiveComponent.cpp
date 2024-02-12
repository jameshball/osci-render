#include "PerspectiveComponent.h"
#include "PluginEditor.h"
#include <numbers>

PerspectiveComponent::PerspectiveComponent(OscirenderAudioProcessor& p, OscirenderAudioProcessorEditor& editor) : audioProcessor(p), pluginEditor(editor) {
	setText("3D Settings");

	addAndMakeVisible(perspective);
	addAndMakeVisible(focalLength);
	addAndMakeVisible(distance);

	perspective.setSliderOnValueChange();
	focalLength.setSliderOnValueChange();
	distance.setSliderOnValueChange();
}

PerspectiveComponent::~PerspectiveComponent() {}

void PerspectiveComponent::resized() {
	auto area = getLocalBounds().withTrimmedTop(20).reduced(20);
	double rowHeight = 30;
	perspective.setBounds(area.removeFromTop(rowHeight));
	focalLength.setBounds(area.removeFromTop(rowHeight));
	distance.setBounds(area.removeFromTop(rowHeight));
}

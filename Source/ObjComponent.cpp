#include "ObjComponent.h"
#include "PluginEditor.h"

ObjComponent::ObjComponent(OscirenderAudioProcessor& p, OscirenderAudioProcessorEditor& editor) : audioProcessor(p), pluginEditor(editor) {
	setText("3D .obj File Settings");

	addAndMakeVisible(focalLength);
	addAndMakeVisible(rotateX);
	addAndMakeVisible(rotateY);
	addAndMakeVisible(rotateZ);
	addAndMakeVisible(rotateSpeed);
}

void ObjComponent::resized() {
	auto area = getLocalBounds().reduced(20);
	double sliderHeight = 30;
	focalLength.setBounds(area.removeFromTop(sliderHeight));
	rotateX.setBounds(area.removeFromTop(sliderHeight));
	rotateY.setBounds(area.removeFromTop(sliderHeight));
	rotateZ.setBounds(area.removeFromTop(sliderHeight));
	rotateSpeed.setBounds(area.removeFromTop(sliderHeight));
}

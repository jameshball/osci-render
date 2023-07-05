#include "ObjComponent.h"
#include "PluginEditor.h"
#include <numbers>

ObjComponent::ObjComponent(OscirenderAudioProcessor& p, OscirenderAudioProcessorEditor& editor) : audioProcessor(p), pluginEditor(editor) {
	setText("3D .obj File Settings");

	addAndMakeVisible(focalLength);
	addAndMakeVisible(rotateX);
	addAndMakeVisible(rotateY);
	addAndMakeVisible(rotateZ);
	addAndMakeVisible(rotateSpeed);

	focalLength.slider.onValueChange = [this] {
		juce::SpinLock::ScopedLockType lock(audioProcessor.parsersLock);
		audioProcessor.focalLength.setValue(focalLength.slider.getValue());
		audioProcessor.focalLength.apply();
	};

	auto onRotationChange = [this]() {
		juce::SpinLock::ScopedLockType lock(audioProcessor.parsersLock);
		audioProcessor.rotateX.setValue(rotateX.slider.getValue());
		audioProcessor.rotateY.setValue(rotateY.slider.getValue());
		audioProcessor.rotateZ.setValue(rotateZ.slider.getValue());
		// all the rotate apply functions are the same
		audioProcessor.rotateX.apply();
	};

	rotateX.slider.onValueChange = onRotationChange;
	rotateY.slider.onValueChange = onRotationChange;
	rotateZ.slider.onValueChange = onRotationChange;

	rotateSpeed.slider.onValueChange = [this] {
		juce::SpinLock::ScopedLockType lock(audioProcessor.parsersLock);
		audioProcessor.rotateSpeed.setValue(rotateSpeed.slider.getValue());
		audioProcessor.rotateSpeed.apply();
	};
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

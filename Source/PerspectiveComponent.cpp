#include "PerspectiveComponent.h"
#include "PluginEditor.h"
#include <numbers>

PerspectiveComponent::PerspectiveComponent(OscirenderAudioProcessor& p, OscirenderAudioProcessorEditor& editor) : audioProcessor(p), pluginEditor(editor) {
	setText("3D Settings");

	juce::Desktop::getInstance().addGlobalMouseListener(this);

	addAndMakeVisible(perspective);
	addAndMakeVisible(focalLength);
	addAndMakeVisible(distance);
	addAndMakeVisible(rotateSpeed);
	addAndMakeVisible(rotateX);
	addAndMakeVisible(rotateY);
	addAndMakeVisible(rotateZ);
	addAndMakeVisible(resetRotation);
	addAndMakeVisible(mouseRotate);

	perspective.setSliderOnValueChange();
	focalLength.setSliderOnValueChange();
	distance.setSliderOnValueChange();
	rotateSpeed.setSliderOnValueChange();
	rotateX.setSliderOnValueChange();
	rotateY.setSliderOnValueChange();
	rotateZ.setSliderOnValueChange();

	resetRotation.onClick = [this] {
		fixedRotateX->setToggleState(false, juce::NotificationType::dontSendNotification);
		fixedRotateY->setToggleState(false, juce::NotificationType::dontSendNotification);
		fixedRotateZ->setToggleState(false, juce::NotificationType::dontSendNotification);

		audioProcessor.perspective->getParameter("perspectiveRotateX")->setUnnormalisedValueNotifyingHost(0);
		audioProcessor.perspective->getParameter("perspectiveRotateY")->setUnnormalisedValueNotifyingHost(0);
		audioProcessor.perspective->getParameter("perspectiveRotateZ")->setUnnormalisedValueNotifyingHost(0);
		audioProcessor.perspective->getParameter("perspectiveRotateSpeed")->setUnnormalisedValueNotifyingHost(0);

		audioProcessor.perspectiveEffect->resetRotation();
		
		mouseRotate.setToggleState(false, juce::NotificationType::dontSendNotification);
	};

	rotateX.setComponent(fixedRotateX);
	rotateY.setComponent(fixedRotateY);
	rotateZ.setComponent(fixedRotateZ);

	juce::String tooltip = "Toggles whether the rotation around this axis is fixed, or changes according to the rotation speed.";
	fixedRotateX->setTooltip(tooltip);
	fixedRotateY->setTooltip(tooltip);
	fixedRotateZ->setTooltip(tooltip);
}

PerspectiveComponent::~PerspectiveComponent() {
	juce::Desktop::getInstance().removeGlobalMouseListener(this);
}

// listen for mouse movement and rotate the object if mouseRotate is enabled
void PerspectiveComponent::mouseMove(const juce::MouseEvent& e) {
	if (mouseRotate.getToggleState()) {
		auto globalEvent = e.getEventRelativeTo(&pluginEditor);
		auto width = pluginEditor.getWidth();
		auto height = pluginEditor.getHeight();
		auto x = globalEvent.position.getX();
		auto y = globalEvent.position.getY();

		audioProcessor.perspective->getParameter("perspectiveRotateX")->setUnnormalisedValueNotifyingHost(2 * x / width - 1);
		audioProcessor.perspective->getParameter("perspectiveRotateY")->setUnnormalisedValueNotifyingHost(1 - 2 * y / height);
	}
}

void PerspectiveComponent::disableMouseRotation() {
	mouseRotate.setToggleState(false, juce::NotificationType::dontSendNotification);
}

void PerspectiveComponent::resized() {
	auto area = getLocalBounds().withTrimmedTop(20).reduced(20);
	double rowHeight = 30;
	perspective.setBounds(area.removeFromTop(rowHeight));
	focalLength.setBounds(area.removeFromTop(rowHeight));
	distance.setBounds(area.removeFromTop(rowHeight));
	rotateX.setBounds(area.removeFromTop(rowHeight));
	rotateY.setBounds(area.removeFromTop(rowHeight));
	rotateZ.setBounds(area.removeFromTop(rowHeight));
	rotateSpeed.setBounds(area.removeFromTop(rowHeight));

	area.removeFromTop(10);
	auto row = area.removeFromTop(rowHeight);
	resetRotation.setBounds(row.removeFromLeft(120));
	row.removeFromLeft(20);
	mouseRotate.setBounds(row);
}

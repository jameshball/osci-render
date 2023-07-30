#include "ObjComponent.h"
#include "PluginEditor.h"
#include <numbers>

ObjComponent::ObjComponent(OscirenderAudioProcessor& p, OscirenderAudioProcessorEditor& editor) : audioProcessor(p), pluginEditor(editor) {
	setText("3D .obj File Settings");

	juce::Desktop::getInstance().addGlobalMouseListener(this);

	addAndMakeVisible(focalLength);
	addAndMakeVisible(rotateX);
	addAndMakeVisible(rotateY);
	addAndMakeVisible(rotateZ);
	addAndMakeVisible(rotateSpeed);

	focalLength.slider.onValueChange = [this] {
		juce::SpinLock::ScopedLockType lock(audioProcessor.parsersLock);
		audioProcessor.focalLength->setValue(focalLength.slider.getValue());
		audioProcessor.focalLength->apply();
	};

	auto onRotationChange = [this]() {
		juce::SpinLock::ScopedLockType lock(audioProcessor.parsersLock);
		audioProcessor.rotateX->setValue(rotateX.slider.getValue());
		audioProcessor.rotateY->setValue(rotateY.slider.getValue());
		audioProcessor.rotateZ->setValue(rotateZ.slider.getValue());

		audioProcessor.fixedRotateX->setBoolValueNotifyingHost(fixedRotateX->getToggleState());
		audioProcessor.fixedRotateY->setBoolValueNotifyingHost(fixedRotateY->getToggleState());
		audioProcessor.fixedRotateZ->setBoolValueNotifyingHost(fixedRotateZ->getToggleState());
	};

	rotateX.slider.onValueChange = onRotationChange;
	rotateY.slider.onValueChange = onRotationChange;
	rotateZ.slider.onValueChange = onRotationChange;

	rotateSpeed.slider.onValueChange = [this] {
		juce::SpinLock::ScopedLockType lock(audioProcessor.parsersLock);
		audioProcessor.rotateSpeed->setValue(rotateSpeed.slider.getValue());
		audioProcessor.rotateSpeed->apply();
	};

	addAndMakeVisible(resetRotation);
	addAndMakeVisible(mouseRotate);

	resetRotation.onClick = [this] {
		fixedRotateX->setToggleState(false, juce::NotificationType::dontSendNotification);
		fixedRotateY->setToggleState(false, juce::NotificationType::dontSendNotification);
		fixedRotateZ->setToggleState(false, juce::NotificationType::dontSendNotification);

		rotateX.slider.setValue(0);
		rotateY.slider.setValue(0);
		rotateZ.slider.setValue(0);
		rotateSpeed.slider.setValue(0);
		
		mouseRotate.setToggleState(false, juce::NotificationType::dontSendNotification);

		juce::SpinLock::ScopedLockType lock(audioProcessor.parsersLock);
		if (audioProcessor.getCurrentFileIndex() != -1) {
			auto obj = audioProcessor.getCurrentFileParser()->getObject();
			if (obj != nullptr) {
				obj->setCurrentRotationX(0);
				obj->setCurrentRotationY(0);
				obj->setCurrentRotationZ(0);
			}
		}
	};

	fixedRotateX->onClick = onRotationChange;
	fixedRotateY->onClick = onRotationChange;
	fixedRotateZ->onClick = onRotationChange;

	rotateX.setComponent(fixedRotateX);
	rotateY.setComponent(fixedRotateY);
	rotateZ.setComponent(fixedRotateZ);
}

ObjComponent::~ObjComponent() {
	juce::Desktop::getInstance().removeGlobalMouseListener(this);
}

// listen for mouse movement and rotate the object if mouseRotate is enabled
void ObjComponent::mouseMove(const juce::MouseEvent& e) {
	if (mouseRotate.getToggleState()) {
		auto globalEvent = e.getEventRelativeTo(&pluginEditor);
		auto width = pluginEditor.getWidth();
		auto height = pluginEditor.getHeight();
		auto x = globalEvent.position.getX();
		auto y = globalEvent.position.getY();

		rotateX.slider.setValue(2 * x / width - 1);
		rotateY.slider.setValue(1 - 2 * y / height);
	}
}

void ObjComponent::disableMouseRotation() {
	mouseRotate.setToggleState(false, juce::NotificationType::dontSendNotification);
}

void ObjComponent::resized() {
	auto area = getLocalBounds().withTrimmedTop(20).reduced(20);
	double rowHeight = 30;
	focalLength.setBounds(area.removeFromTop(rowHeight));
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

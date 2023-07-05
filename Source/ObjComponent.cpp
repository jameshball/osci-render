#include "ObjComponent.h"
#include "PluginEditor.h"
#include <numbers>
#include "Util.h"

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
		audioProcessor.focalLength.setValue(focalLength.slider.getValue());
		audioProcessor.focalLength.apply();
	};

	auto onRotationChange = [this]() {
		juce::SpinLock::ScopedLockType lock(audioProcessor.parsersLock);
		double x = fixedRotateX->getToggleState() ? 0 : rotateX.slider.getValue();
		double y = fixedRotateY->getToggleState() ? 0 : rotateY.slider.getValue();
		double z = fixedRotateZ->getToggleState() ? 0 : rotateZ.slider.getValue();
		audioProcessor.rotateX.setValue(x);
		audioProcessor.rotateY.setValue(y);
		audioProcessor.rotateZ.setValue(z);
		// all the rotate apply functions are the same
		audioProcessor.rotateX.apply();

		if (fixedRotateX->getToggleState()) {
			audioProcessor.currentRotateX.setValue(rotateX.slider.getValue());
			audioProcessor.currentRotateX.apply();
		}
		if (fixedRotateY->getToggleState()) {
			audioProcessor.currentRotateY.setValue(rotateY.slider.getValue());
			audioProcessor.currentRotateY.apply();
		}
		if (fixedRotateZ->getToggleState()) {
			audioProcessor.currentRotateZ.setValue(rotateZ.slider.getValue());
			audioProcessor.currentRotateZ.apply();
		}

		audioProcessor.fixedRotateX = fixedRotateX->getToggleState();
		audioProcessor.fixedRotateY = fixedRotateY->getToggleState();
		audioProcessor.fixedRotateZ = fixedRotateZ->getToggleState();
	};

	rotateX.slider.onValueChange = onRotationChange;
	rotateY.slider.onValueChange = onRotationChange;
	rotateZ.slider.onValueChange = onRotationChange;

	rotateSpeed.slider.onValueChange = [this] {
		juce::SpinLock::ScopedLockType lock(audioProcessor.parsersLock);
		audioProcessor.rotateSpeed.setValue(rotateSpeed.slider.getValue());
		audioProcessor.rotateSpeed.apply();
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
		audioProcessor.currentRotateX.setValue(0);
		audioProcessor.currentRotateY.setValue(0);
		audioProcessor.currentRotateZ.setValue(0);
		audioProcessor.currentRotateX.apply();
		audioProcessor.currentRotateY.apply();
		audioProcessor.currentRotateZ.apply();
	};

	auto doc = juce::XmlDocument::parse(BinaryData::fixed_rotate_svg);
	Util::changeSvgColour(doc.get(), "white");
	fixedRotateWhite = juce::Drawable::createFromSVG(*doc);
	Util::changeSvgColour(doc.get(), "red");
	DBG(doc->toString());
	fixedRotateRed = juce::Drawable::createFromSVG(*doc);

	// TODO: any way of removing this duplication?
	getLookAndFeel().setColour(juce::DrawableButton::backgroundOnColourId, juce::Colours::transparentWhite);
	fixedRotateX->setClickingTogglesState(true);
	fixedRotateY->setClickingTogglesState(true);
	fixedRotateZ->setClickingTogglesState(true);
	fixedRotateX->setImages(fixedRotateWhite.get(), nullptr, nullptr, nullptr, fixedRotateRed.get());
	fixedRotateY->setImages(fixedRotateWhite.get(), nullptr, nullptr, nullptr, fixedRotateRed.get());
	fixedRotateZ->setImages(fixedRotateWhite.get(), nullptr, nullptr, nullptr, fixedRotateRed.get());

	fixedRotateX->onClick = onRotationChange;
	fixedRotateY->onClick = onRotationChange;
	fixedRotateZ->onClick = onRotationChange;

	rotateX.addComponent(fixedRotateX);
	rotateY.addComponent(fixedRotateY);
	rotateZ.addComponent(fixedRotateZ);

	fixedRotateX->setToggleState(audioProcessor.fixedRotateX, juce::NotificationType::dontSendNotification);
	fixedRotateY->setToggleState(audioProcessor.fixedRotateY, juce::NotificationType::dontSendNotification);
	fixedRotateZ->setToggleState(audioProcessor.fixedRotateZ, juce::NotificationType::dontSendNotification);
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
	auto area = getLocalBounds().reduced(20);
	double rowHeight = 30;
	focalLength.setBounds(area.removeFromTop(rowHeight));
	rotateX.setBounds(area.removeFromTop(rowHeight));
	rotateY.setBounds(area.removeFromTop(rowHeight));
	rotateZ.setBounds(area.removeFromTop(rowHeight));
	rotateSpeed.setBounds(area.removeFromTop(rowHeight));

	// TODO this is a bit of a hack
	focalLength.setRightPadding(25);
	rotateSpeed.setRightPadding(25);

	area.removeFromTop(10);
	auto row = area.removeFromTop(rowHeight);
	resetRotation.setBounds(row.removeFromLeft(120));
	row.removeFromLeft(20);
	mouseRotate.setBounds(row);
}

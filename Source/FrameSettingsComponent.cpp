#include "FrameSettingsComponent.h"
#include "PluginEditor.h"

FrameSettingsComponent::FrameSettingsComponent(OscirenderAudioProcessor& p, OscirenderAudioProcessorEditor& editor) : audioProcessor(p), pluginEditor(editor) {
	setText("Frame Settings");

    if (!juce::JUCEApplicationBase::isStandaloneApp()) {
        addAndMakeVisible(animate);
	    addAndMakeVisible(sync);
        addAndMakeVisible(offsetLabel);
	    addAndMakeVisible(offsetBox);

        offsetLabel.setText("Start Frame", juce::dontSendNotification);
	    offsetBox.setJustification(juce::Justification::left);

        offsetLabel.setTooltip("Offsets the animation's start point by a specified number of frames.");
    } else {
        audioProcessor.animationSyncBPM->setValueNotifyingHost(false);
#if OSCI_PREMIUM
        addAndMakeVisible(timeline);
#endif
    }
	addAndMakeVisible(rateLabel);
	addAndMakeVisible(rateBox);
	addAndMakeVisible(invertImage);
	addAndMakeVisible(threshold);
	addAndMakeVisible(stride);

	rateLabel.setText("Frames per Second", juce::dontSendNotification);
	rateBox.setJustification(juce::Justification::left);
	
	update();

	auto updateAnimation = [this]() {
		audioProcessor.animationRate->setUnnormalisedValueNotifyingHost(rateBox.getValue());
		audioProcessor.animationOffset->setUnnormalisedValueNotifyingHost(offsetBox.getValue());
	};

    rateBox.onReturnKey = updateAnimation;
	rateBox.onFocusLost = updateAnimation;
    
	offsetBox.onFocusLost = updateAnimation;
    offsetBox.onReturnKey = updateAnimation;

    animate.setClickingTogglesState(true);
    animate.onClick = [this]() {
        audioProcessor.animateFrames->setValue(animate.getToggleState());
    };

	threshold.slider.onValueChange = [this]() {
        audioProcessor.imageThreshold->setValue(threshold.slider.getValue());
    };

	stride.slider.onValueChange = [this]() {
        audioProcessor.imageStride->setValue(stride.slider.getValue());
    };
    
	audioProcessor.animationRate->addListener(this);
	audioProcessor.animationOffset->addListener(this);
    audioProcessor.animationSyncBPM->addListener(this);
}

FrameSettingsComponent::~FrameSettingsComponent() {
    audioProcessor.animationSyncBPM->removeListener(this);
	audioProcessor.animationOffset->removeListener(this);
	audioProcessor.animationRate->removeListener(this);
}

void FrameSettingsComponent::resized() {
	auto area = getLocalBounds().withTrimmedTop(20).reduced(20);
    double rowHeight = 20;

#if OSCI_PREMIUM
    auto timelineArea = juce::JUCEApplicationBase::isStandaloneApp() ? area.removeFromBottom(30) : juce::Rectangle<int>();
#endif
    
    auto toggleBounds = juce::JUCEApplicationBase::isStandaloneApp() ? juce::Rectangle<int>() : area.removeFromTop(rowHeight);
    auto toggleWidth = juce::jmin(area.getWidth() / 3, 150);

    auto firstColumn = area.removeFromLeft(220);
    
#if OSCI_PREMIUM
    if (juce::JUCEApplicationBase::isStandaloneApp()) {
        timeline.setVisible(animated);
    }
#endif
    
    if (animated) {
        if (juce::JUCEApplicationBase::isStandaloneApp()) {
#if OSCI_PREMIUM
            timeline.setBounds(timelineArea);
#endif
        } else {
            animate.setBounds(toggleBounds.removeFromLeft(toggleWidth));
            sync.setBounds(toggleBounds.removeFromLeft(toggleWidth));
        }
        
        double rowSpace = 10;
        
        firstColumn.removeFromTop(rowSpace);

        auto animateBounds = firstColumn.removeFromTop(rowHeight);
        rateLabel.setBounds(animateBounds.removeFromLeft(140));
        rateBox.setBounds(animateBounds.removeFromLeft(60));
        firstColumn.removeFromTop(rowSpace);

        if (!juce::JUCEApplicationBase::isStandaloneApp()) {
            animateBounds = firstColumn.removeFromTop(rowHeight);
            offsetLabel.setBounds(animateBounds.removeFromLeft(140));
            offsetBox.setBounds(animateBounds.removeFromLeft(60));
        }
    }

    if (image) {
        if (juce::JUCEApplicationBase::isStandaloneApp()) {
            invertImage.setBounds(firstColumn.removeFromTop(rowHeight));
        } else {
            invertImage.setBounds(toggleBounds.removeFromLeft(toggleWidth));
        }
        
        auto secondColumn = area;
        secondColumn.removeFromTop(5);
        
        rowHeight = 30;
        threshold.setBounds(secondColumn.removeFromTop(rowHeight));
        stride.setBounds(secondColumn.removeFromTop(rowHeight));
    }
}

void FrameSettingsComponent::update() {
	rateBox.setValue(audioProcessor.animationRate->getValueUnnormalised(), false, 2);
	offsetBox.setValue(audioProcessor.animationOffset->getValueUnnormalised(), false, 2);
	if (sync.getToggleState()) {
		rateLabel.setText("Frames per Beat", juce::dontSendNotification);
		rateLabel.setTooltip("Set the animation's speed in frames per beat.");
	} else {
		rateLabel.setText("Frames per Second", juce::dontSendNotification);
		rateLabel.setTooltip("Set the animation's speed in frames per second.");
	}
}

void FrameSettingsComponent::parameterValueChanged(int parameterIndex, float newValue) {
	triggerAsyncUpdate();
}

void FrameSettingsComponent::parameterGestureChanged(int parameterIndex, bool gestureIsStarting) {}

void FrameSettingsComponent::handleAsyncUpdate() {
	update();
}

void FrameSettingsComponent::setAnimated(bool animated) {
    this->animated = animated;
    animate.setVisible(animated);
    sync.setVisible(animated);
    rateBox.setVisible(animated);
    offsetBox.setVisible(animated);
    rateLabel.setVisible(animated);
    offsetLabel.setVisible(animated);
}

void FrameSettingsComponent::setImage(bool image) {
    this->image = image;
    invertImage.setVisible(image);
    threshold.setVisible(image);
    stride.setVisible(image);
}

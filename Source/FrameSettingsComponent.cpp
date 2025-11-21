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
    // Calculate preferred height while laying out components
    const int topMargin = 20;
    const int sideMargins = 40; // reduced(20) removes 20 from each side
    int heightUsed = topMargin + sideMargins;
    
	auto area = getLocalBounds().withTrimmedTop(topMargin).reduced(20);
    double rowHeight = 20;
    
    bool isPlugin = !juce::JUCEApplicationBase::isStandaloneApp();
    auto toggleBounds = isPlugin && animated ? area.removeFromTop(rowHeight) : juce::Rectangle<int>();
    auto toggleWidth = juce::jmin(area.getWidth() / 3, 150);
    
    // Track toggle row usage and heights for each column (they're side by side)
    bool toggleRowUsed = false;
    int firstColumnHeight = 0;
    int secondColumnHeight = 0;

    auto firstColumn = area.removeFromLeft(220);
    
    if (animated) {
        if (isPlugin) {
            animate.setBounds(toggleBounds.removeFromLeft(toggleWidth));
            sync.setBounds(toggleBounds.removeFromLeft(toggleWidth));
            if (!toggleRowUsed) {
                heightUsed += rowHeight; // Toggle row (only count once)
                toggleRowUsed = true;
            }
        }
        
        firstColumn.removeFromTop(5);
        firstColumnHeight += 5; // Spacing

        auto animateBounds = firstColumn.removeFromTop(rowHeight);
        rateLabel.setBounds(animateBounds.removeFromLeft(140));
        rateBox.setBounds(animateBounds.removeFromLeft(60));
        firstColumnHeight += rowHeight; // Rate row
        
        firstColumn.removeFromTop(10);
        firstColumnHeight += 10; // Spacing

        if (isPlugin) {
            animateBounds = firstColumn.removeFromTop(rowHeight);
            offsetLabel.setBounds(animateBounds.removeFromLeft(140));
            offsetBox.setBounds(animateBounds.removeFromLeft(60));
            firstColumnHeight += rowHeight; // Offset row
        }
    }

    if (image) {
        if (!animated) {
            firstColumn.removeFromTop(5);
            firstColumnHeight += 5; // Spacing
        }

        if (!isPlugin || !animated) {
            // Standalone: invertImage goes in first column
            invertImage.setBounds(firstColumn.removeFromTop(rowHeight));
            firstColumnHeight += rowHeight; // Invert toggle row
        } else {
            // Plugin: invertImage goes in toggle row
            invertImage.setBounds(toggleBounds.removeFromLeft(toggleWidth));
            if (!toggleRowUsed) {
                heightUsed += rowHeight; // Toggle row (only if not already counted)
                toggleRowUsed = true;
            }
        }
        
        // Second column for threshold and stride (parallel to first column)
        auto secondColumn = area;
        
        rowHeight = 30;
        threshold.setBounds(secondColumn.removeFromTop(rowHeight));
        secondColumnHeight += rowHeight; // Threshold row
        
        stride.setBounds(secondColumn.removeFromTop(rowHeight));
        secondColumnHeight += rowHeight; // Stride row
    }
    
    // Add the taller of the two columns (they're side by side)
    heightUsed += juce::jmax(firstColumnHeight, secondColumnHeight);
    
    // Cache the calculated height
    cachedPreferredHeight = heightUsed;
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

int FrameSettingsComponent::getPreferredHeight() const {
    // Return cached height calculated during resized()
    // If not yet calculated (cachedPreferredHeight == 0), return a reasonable default
    return cachedPreferredHeight > 0 ? cachedPreferredHeight : 60;
}

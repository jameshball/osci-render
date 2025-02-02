#include "VisualiserSettings.h"
#include "VisualiserComponent.h"
#include "../PluginEditor.h"


VisualiserSettings::VisualiserSettings(VisualiserParameters& p, int numChannels) : parameters(p), numChannels(numChannels) {
    addAndMakeVisible(lineColour);
    addAndMakeVisible(lightEffects);
    addAndMakeVisible(videoEffects);
    addAndMakeVisible(lineEffects);
    addAndMakeVisible(sweepMs);
    addAndMakeVisible(triggerValue);
    addAndMakeVisible(upsamplingToggle);
    addAndMakeVisible(sweepToggle);
    addAndMakeVisible(screenOverlayLabel);
    addAndMakeVisible(screenOverlay);
#if SOSCI_FEATURES
    addAndMakeVisible(positionSize);
    addAndMakeVisible(screenColour);
    addAndMakeVisible(flipVerticalToggle);
    addAndMakeVisible(flipHorizontalToggle);
#endif
    
    for (int i = 1; i <= parameters.screenOverlay->max; i++) {
        screenOverlay.addItem(parameters.screenOverlay->getText(parameters.screenOverlay->getNormalisedValue(i)), i);
    }
    screenOverlay.setSelectedId(parameters.screenOverlay->getValueUnnormalised());
    screenOverlay.onChange = [this] {
        parameters.screenOverlay->setUnnormalisedValueNotifyingHost(screenOverlay.getSelectedId());
    };
    
    sweepMs.setSliderOnValueChange();
    triggerValue.setSliderOnValueChange();
    
    sweepMs.setEnabled(sweepToggle.getToggleState());
    triggerValue.setEnabled(sweepToggle.getToggleState());
    
    sweepMs.slider.setSkewFactorFromMidPoint(100);
    
    sweepToggle.onClick = [this] {
        sweepMs.setEnabled(sweepToggle.getToggleState());
        triggerValue.setEnabled(sweepToggle.getToggleState());
        resized();
    };

    parameters.screenOverlay->addListener(this);
}

VisualiserSettings::~VisualiserSettings() {
    parameters.screenOverlay->removeListener(this);
}

void VisualiserSettings::paint(juce::Graphics& g) {
    g.fillAll(Colours::darker);
}

void VisualiserSettings::resized() {
	auto area = getLocalBounds().reduced(20, 0).withTrimmedBottom(20);
	double rowHeight = 30;
    
    auto screenOverlayArea = area.removeFromTop(2 * rowHeight);
    screenOverlayArea = screenOverlayArea.withSizeKeepingCentre(300, rowHeight);
    screenOverlayLabel.setBounds(screenOverlayArea.removeFromLeft(120));
    screenOverlay.setBounds(screenOverlayArea.removeFromRight(180));
    
    lineColour.setBounds(area.removeFromTop(lineColour.getHeight()));
#if SOSCI_FEATURES
    area.removeFromTop(10);
    screenColour.setBounds(area.removeFromTop(screenColour.getHeight()));
#endif
    area.removeFromTop(10);
    lightEffects.setBounds(area.removeFromTop(lightEffects.getHeight()));
    area.removeFromTop(10);
    videoEffects.setBounds(area.removeFromTop(videoEffects.getHeight()));
    area.removeFromTop(10);
    lineEffects.setBounds(area.removeFromTop(lineEffects.getHeight()));
    
#if SOSCI_FEATURES
    area.removeFromTop(10);
    positionSize.setBounds(area.removeFromTop(positionSize.getHeight()));
    area.removeFromTop(10);
    flipVerticalToggle.setBounds(area.removeFromTop(rowHeight));
    flipHorizontalToggle.setBounds(area.removeFromTop(rowHeight));
#endif

#if !SOSCI_FEATURES
    area.removeFromTop(10);
#endif
    upsamplingToggle.setBounds(area.removeFromTop(rowHeight));
    sweepToggle.setBounds(area.removeFromTop(rowHeight));
    sweepMs.setBounds(area.removeFromTop(rowHeight));
    triggerValue.setBounds(area.removeFromTop(rowHeight));
}

void VisualiserSettings::parameterValueChanged(int parameterIndex, float newValue) {
    if (parameterIndex == parameters.screenOverlay->getParameterIndex()) {
        screenOverlay.setSelectedId(parameters.screenOverlay->getValueUnnormalised());
    }
}

void VisualiserSettings::parameterGestureChanged(int parameterIndex, bool gestureIsStarting) {}

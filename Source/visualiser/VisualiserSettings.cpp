#include "VisualiserSettings.h"
#include "VisualiserComponent.h"

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
#if OSCI_PREMIUM
    addAndMakeVisible(scale);
    addAndMakeVisible(position);
    addAndMakeVisible(screenColour);
    addAndMakeVisible(flipVerticalToggle);
    addAndMakeVisible(flipHorizontalToggle);
    addAndMakeVisible(goniometerToggle);
    addAndMakeVisible(shutterSyncToggle);
#endif
#if !OSCI_PREMIUM
    addAndMakeVisible(upgradeButton);
    upgradeButton.setColour(juce::TextButton::buttonColourId, Colours::accentColor);
    upgradeButton.setColour(juce::TextButton::textColourOffId, Colours::veryDark);
    upgradeButton.onClick = [this] {
        if (onUpgradeRequested) {
            onUpgradeRequested();
        }
    };
#endif
    
    for (int i = 1; i <= parameters.screenOverlay->max; i++) {
        screenOverlay.addItem(parameters.screenOverlay->getText(parameters.screenOverlay->getNormalisedValue(i)), i);
    }
    screenOverlay.setSelectedId(parameters.screenOverlay->getValueUnnormalised());
    screenOverlay.onChange = [this] {
        parameters.screenOverlay->setUnnormalisedValueNotifyingHost(screenOverlay.getSelectedId());
    };
    
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
#if OSCI_PREMIUM
    area.removeFromTop(10);
    screenColour.setBounds(area.removeFromTop(screenColour.getHeight()));
#endif
    area.removeFromTop(10);
    lightEffects.setBounds(area.removeFromTop(lightEffects.getHeight()));
    area.removeFromTop(10);
    videoEffects.setBounds(area.removeFromTop(videoEffects.getHeight()));
    area.removeFromTop(10);
    lineEffects.setBounds(area.removeFromTop(lineEffects.getHeight()));
    
#if OSCI_PREMIUM
    area.removeFromTop(10);
    scale.setBounds(area.removeFromTop(scale.getHeight()));
    area.removeFromTop(10);
    position.setBounds(area.removeFromTop(position.getHeight()));
    area.removeFromTop(10);
    flipVerticalToggle.setBounds(area.removeFromTop(rowHeight));
    flipHorizontalToggle.setBounds(area.removeFromTop(rowHeight));
    goniometerToggle.setBounds(area.removeFromTop(rowHeight));
    shutterSyncToggle.setBounds(area.removeFromTop(rowHeight));
#endif

#if !OSCI_PREMIUM
#endif
    upsamplingToggle.setBounds(area.removeFromTop(rowHeight));
    sweepToggle.setBounds(area.removeFromTop(rowHeight));
    sweepMs.setBounds(area.removeFromTop(rowHeight));
    triggerValue.setBounds(area.removeFromTop(rowHeight));
#if !OSCI_PREMIUM
    area.removeFromTop(10);
    const int buttonHeight = 36;
    auto buttonArea = area.removeFromTop(buttonHeight);
    auto buttonWidth = juce::jlimit(180, 320, buttonArea.getWidth());
    upgradeButton.setBounds(buttonArea.withSizeKeepingCentre(buttonWidth, buttonHeight));
#endif
}

void VisualiserSettings::parameterValueChanged(int parameterIndex, float newValue) {
    if (parameterIndex == parameters.screenOverlay->getParameterIndex()) {
        screenOverlay.setSelectedId(parameters.screenOverlay->getValueUnnormalised());
    }
}

void VisualiserSettings::parameterGestureChanged(int parameterIndex, bool gestureIsStarting) {}

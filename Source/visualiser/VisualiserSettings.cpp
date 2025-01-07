#include "VisualiserSettings.h"
#include "VisualiserComponent.h"
#include "../PluginEditor.h"


VisualiserSettings::VisualiserSettings(VisualiserParameters& p, int numChannels) : parameters(p), numChannels(numChannels) {
    addAndMakeVisible(intensity);
	addAndMakeVisible(persistence);
    addAndMakeVisible(hue);
    addAndMakeVisible(lineSaturation);
    addAndMakeVisible(screenSaturation);
    addAndMakeVisible(focus);
    addAndMakeVisible(noise);
    addAndMakeVisible(glow);
    addAndMakeVisible(ambient);
    addAndMakeVisible(smooth);
    addAndMakeVisible(sweepMs);
    addAndMakeVisible(triggerValue);
    addAndMakeVisible(upsamplingToggle);
    addAndMakeVisible(sweepToggle);
    addAndMakeVisible(screenOverlayLabel);
    addAndMakeVisible(screenOverlay);
    
    for (int i = 1; i <= parameters.screenOverlay->max; i++) {
        screenOverlay.addItem(parameters.screenOverlay->getText(parameters.screenOverlay->getNormalisedValue(i)), i);
    }
    screenOverlay.setSelectedId(parameters.screenOverlay->getValueUnnormalised());
    screenOverlay.onChange = [this] {
        parameters.screenOverlay->setUnnormalisedValueNotifyingHost(screenOverlay.getSelectedId());
    };
    
    intensity.setSliderOnValueChange();
    persistence.setSliderOnValueChange();
    hue.setSliderOnValueChange();
    lineSaturation.setSliderOnValueChange();
    screenSaturation.setSliderOnValueChange();
    focus.setSliderOnValueChange();
    noise.setSliderOnValueChange();
    glow.setSliderOnValueChange();
    ambient.setSliderOnValueChange();
    smooth.setSliderOnValueChange();
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
}

VisualiserSettings::~VisualiserSettings() {}

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
    
    intensity.setBounds(area.removeFromTop(rowHeight));
    persistence.setBounds(area.removeFromTop(rowHeight));
    hue.setBounds(area.removeFromTop(rowHeight));
    lineSaturation.setBounds(area.removeFromTop(rowHeight));
    screenSaturation.setBounds(area.removeFromTop(rowHeight));
    focus.setBounds(area.removeFromTop(rowHeight));
    noise.setBounds(area.removeFromTop(rowHeight));
    glow.setBounds(area.removeFromTop(rowHeight));
    ambient.setBounds(area.removeFromTop(rowHeight));
    smooth.setBounds(area.removeFromTop(rowHeight));
    
    upsamplingToggle.setBounds(area.removeFromTop(rowHeight));
    
    sweepToggle.setBounds(area.removeFromTop(rowHeight));
    sweepMs.setBounds(area.removeFromTop(rowHeight));
    triggerValue.setBounds(area.removeFromTop(rowHeight));
}

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
    addAndMakeVisible(screenTypeLabel);
    addAndMakeVisible(screenType);
    
    for (int i = 1; i <= parameters.screenType->max; i++) {
        screenType.addItem(parameters.screenType->getText(parameters.screenType->getNormalisedValue(i)), i);
    }
    screenType.setSelectedId(parameters.screenType->getValueUnnormalised());
    screenType.onChange = [this] {
        parameters.screenType->setUnnormalisedValueNotifyingHost(screenType.getSelectedId());
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
    
    auto screenTypeArea = area.removeFromTop(2 * rowHeight);
    screenTypeArea = screenTypeArea.withSizeKeepingCentre(300, rowHeight);
    screenTypeLabel.setBounds(screenTypeArea.removeFromLeft(120));
    screenType.setBounds(screenTypeArea.removeFromRight(180));
    
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
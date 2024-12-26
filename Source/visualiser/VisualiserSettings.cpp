#include "VisualiserSettings.h"
#include "VisualiserComponent.h"
#include "../PluginEditor.h"


VisualiserSettings::VisualiserSettings(VisualiserParameters& p, int numChannels) : parameters(p), numChannels(numChannels) {
    addAndMakeVisible(intensity);
	addAndMakeVisible(persistence);
    addAndMakeVisible(hue);
    addAndMakeVisible(saturation);
    addAndMakeVisible(focus);
    addAndMakeVisible(noise);
    addAndMakeVisible(glow);
    addAndMakeVisible(ambient);
    addAndMakeVisible(smooth);
    addChildComponent(sweepMs);
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
    saturation.setSliderOnValueChange();
    focus.setSliderOnValueChange();
    noise.setSliderOnValueChange();
    glow.setSliderOnValueChange();
    ambient.setSliderOnValueChange();
    smooth.setSliderOnValueChange();
    sweepMs.setSliderOnValueChange();
    
    sweepToggle.onClick = [this] {
        sweepMs.setVisible(sweepToggle.getToggleState());
        resized();
    };
}

VisualiserSettings::~VisualiserSettings() {}

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
    saturation.setBounds(area.removeFromTop(rowHeight));
    focus.setBounds(area.removeFromTop(rowHeight));
    noise.setBounds(area.removeFromTop(rowHeight));
    glow.setBounds(area.removeFromTop(rowHeight));
    ambient.setBounds(area.removeFromTop(rowHeight));
    smooth.setBounds(area.removeFromTop(rowHeight));
    
    upsamplingToggle.setBounds(area.removeFromTop(rowHeight));
    
    sweepToggle.setBounds(area.removeFromTop(rowHeight));
    if (sweepToggle.getToggleState()) {
        sweepMs.setBounds(area.removeFromTop(rowHeight));
    }
}

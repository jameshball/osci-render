#include "VisualiserSettings.h"
#include "VisualiserComponent.h"
#include "../PluginEditor.h"


VisualiserSettings::VisualiserSettings(VisualiserParameters& parameters, int numChannels) : parameters(parameters), numChannels(numChannels) {
    addAndMakeVisible(brightness);
    addAndMakeVisible(intensity);
	addAndMakeVisible(persistence);
    addAndMakeVisible(hue);
    addAndMakeVisible(saturation);
    addAndMakeVisible(focus);
    addAndMakeVisible(noise);
    addAndMakeVisible(glow);
    addAndMakeVisible(smooth);
    addChildComponent(sweepMs);
    addAndMakeVisible(graticuleToggle);
    addAndMakeVisible(smudgeToggle);
    addAndMakeVisible(upsamplingToggle);
    addAndMakeVisible(sweepToggle);
    
    brightness.setSliderOnValueChange();
    intensity.setSliderOnValueChange();
    persistence.setSliderOnValueChange();
    hue.setSliderOnValueChange();
    saturation.setSliderOnValueChange();
    focus.setSliderOnValueChange();
    noise.setSliderOnValueChange();
    glow.setSliderOnValueChange();
    smooth.setSliderOnValueChange();
    sweepMs.setSliderOnValueChange();
    
    sweepToggle.onClick = [this] {
        sweepMs.setVisible(sweepToggle.getToggleState());
        resized();
    };
}

VisualiserSettings::~VisualiserSettings() {}

void VisualiserSettings::resized() {
	auto area = getLocalBounds().reduced(20);
	double rowHeight = 30;
    brightness.setBounds(area.removeFromTop(rowHeight));
    intensity.setBounds(area.removeFromTop(rowHeight));
    persistence.setBounds(area.removeFromTop(rowHeight));
    hue.setBounds(area.removeFromTop(rowHeight));
    saturation.setBounds(area.removeFromTop(rowHeight));
    focus.setBounds(area.removeFromTop(rowHeight));
    noise.setBounds(area.removeFromTop(rowHeight));
    glow.setBounds(area.removeFromTop(rowHeight));
    smooth.setBounds(area.removeFromTop(rowHeight));
    graticuleToggle.setBounds(area.removeFromTop(rowHeight));
    smudgeToggle.setBounds(area.removeFromTop(rowHeight));
    upsamplingToggle.setBounds(area.removeFromTop(rowHeight));
    
    sweepToggle.setBounds(area.removeFromTop(rowHeight));
    if (sweepToggle.getToggleState()) {
        sweepMs.setBounds(area.removeFromTop(rowHeight));
    }
}

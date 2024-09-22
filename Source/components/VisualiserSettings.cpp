#include "VisualiserSettings.h"
#include "VisualiserComponent.h"
#include "../PluginEditor.h"


VisualiserSettings::VisualiserSettings(VisualiserParameters& parameters, int numChannels) : parameters(parameters), numChannels(numChannels) {
    addAndMakeVisible(brightness);
    addAndMakeVisible(intensity);
	addAndMakeVisible(persistence);
    addAndMakeVisible(hue);
    addAndMakeVisible(graticuleToggle);
    addAndMakeVisible(smudgeToggle);
    addAndMakeVisible(upsamplingToggle);
    
    brightness.setSliderOnValueChange();
    intensity.setSliderOnValueChange();
    persistence.setSliderOnValueChange();
    hue.setSliderOnValueChange();
}

VisualiserSettings::~VisualiserSettings() {}

void VisualiserSettings::resized() {
	auto area = getLocalBounds().reduced(20);
	double rowHeight = 30;
    brightness.setBounds(area.removeFromTop(rowHeight));
    intensity.setBounds(area.removeFromTop(rowHeight));
    persistence.setBounds(area.removeFromTop(rowHeight));
    hue.setBounds(area.removeFromTop(rowHeight));
    graticuleToggle.setBounds(area.removeFromTop(rowHeight));
    smudgeToggle.setBounds(area.removeFromTop(rowHeight));
    upsamplingToggle.setBounds(area.removeFromTop(rowHeight));
}

juce::var VisualiserSettings::getSettings() {
    auto settings = new juce::DynamicObject();
    settings->setProperty("brightness", parameters.brightnessEffect->getActualValue() - 2);
    settings->setProperty("intensity", parameters.intensityEffect->getActualValue() / 100);
    settings->setProperty("persistence", parameters.persistenceEffect->getActualValue() - 1.33);
    settings->setProperty("hue", parameters.hueEffect->getActualValue());
    settings->setProperty("graticule", parameters.graticuleEnabled->getBoolValue());
    settings->setProperty("smudges", parameters.smudgesEnabled->getBoolValue());
    settings->setProperty("upsampling", parameters.upsamplingEnabled->getBoolValue());
    settings->setProperty("numChannels", numChannels);
    return juce::var(settings);
}

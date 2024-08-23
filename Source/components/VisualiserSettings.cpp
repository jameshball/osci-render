#include "VisualiserSettings.h"
#include "VisualiserComponent.h"
#include "../PluginEditor.h"


VisualiserSettings::VisualiserSettings(OscirenderAudioProcessor& p) : audioProcessor(p) {
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
    settings->setProperty("brightness", audioProcessor.brightnessEffect->getActualValue() - 2);
    settings->setProperty("intensity", audioProcessor.intensityEffect->getActualValue() / 100);
    settings->setProperty("persistence", audioProcessor.persistenceEffect->getActualValue() - 1.33);
    settings->setProperty("hue", audioProcessor.hueEffect->getActualValue());
    settings->setProperty("graticule", audioProcessor.graticuleEnabled->getBoolValue());
    settings->setProperty("smudges", audioProcessor.smudgesEnabled->getBoolValue());
    settings->setProperty("upsampling", audioProcessor.upsamplingEnabled->getBoolValue());
    return juce::var(settings);
}

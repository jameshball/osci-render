#include "VisualiserSettings.h"
#include "../PluginEditor.h"

VisualiserSettings::VisualiserSettings(OscirenderAudioProcessor& p, VisualiserComponent& visualiser) : audioProcessor(p), visualiser(visualiser) {
	addAndMakeVisible(intensity);
	addAndMakeVisible(persistence);
    addAndMakeVisible(hue);

    intensity.slider.onValueChange = [this] {
        double value = intensity.slider.getValue();
        intensity.effect.setValue(value);
        this->visualiser.setIntensity(value);
    };
    persistence.slider.onValueChange = [this] {
        double value = persistence.slider.getValue();
        persistence.effect.setValue(value);
        this->visualiser.setPersistence(value);
    };
    hue.slider.onValueChange = [this] {
        double value = hue.slider.getValue();
        hue.effect.setValue(value);
        this->visualiser.setHue(value);
    };
}

VisualiserSettings::~VisualiserSettings() {}

void VisualiserSettings::resized() {
	auto area = getLocalBounds().withTrimmedTop(20).reduced(20);
	double rowHeight = 30;
    intensity.setBounds(area.removeFromTop(rowHeight));
    persistence.setBounds(area.removeFromTop(rowHeight));
    hue.setBounds(area.removeFromTop(rowHeight));
}

#include "EffectsComponent.h"

EffectsComponent::EffectsComponent(OscirenderAudioProcessor& p) : audioProcessor(p) {
	setText("Audio Effects");

    addAndMakeVisible(frequency);

    frequency.slider.setRange(0.0, 12000.0);
    frequency.slider.setSkewFactorFromMidPoint(500.0);
    frequency.slider.setTextValueSuffix("Hz");
    frequency.slider.setValue(440.0);

    frequency.slider.onValueChange = [this] {
        audioProcessor.frequency = frequency.slider.getValue();
        if (audioProcessor.currentSampleRate > 0.0) {
            audioProcessor.updateAngleDelta();
        }
    };
}

EffectsComponent::~EffectsComponent() {
}

void EffectsComponent::resized() {
    auto xPadding = 10;
    auto yPadding = 20;
    frequency.setBounds(xPadding, yPadding, getWidth() - xPadding, 40);
}

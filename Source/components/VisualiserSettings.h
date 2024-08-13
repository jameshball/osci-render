#pragma once

#include <JuceHeader.h>
#include "EffectComponent.h"
#include "SvgButton.h"
#include "../LookAndFeel.h"
#include "SwitchButton.h"

class VisualiserComponent;
class VisualiserSettings : public juce::Component {
public:
    VisualiserSettings(OscirenderAudioProcessor&, VisualiserComponent&);
    ~VisualiserSettings();

    void resized() override;
    juce::var getSettings();
private:
    OscirenderAudioProcessor& audioProcessor;
    VisualiserComponent& visualiser;

    EffectComponent intensity{audioProcessor, *audioProcessor.intensityEffect};
    EffectComponent persistence{audioProcessor, *audioProcessor.persistenceEffect};
    EffectComponent hue{audioProcessor, *audioProcessor.hueEffect};
    
    jux::SwitchButton gridToggle = { "gridToggle", false };
    jux::SwitchButton noiseToggle = { "noiseToggle", false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VisualiserSettings)
};

class SettingsWindow : public juce::DocumentWindow {
public:
    SettingsWindow(juce::String name) : juce::DocumentWindow(name, Colours::darker, juce::DocumentWindow::TitleBarButtons::closeButton) {}
    
    void closeButtonPressed() override {
        setVisible(false);
    }
};

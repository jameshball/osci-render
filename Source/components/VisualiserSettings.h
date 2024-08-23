#pragma once

#include <JuceHeader.h>
#include "EffectComponent.h"
#include "SvgButton.h"
#include "../LookAndFeel.h"
#include "SwitchButton.h"

class VisualiserSettings : public juce::Component {
public:
    VisualiserSettings(OscirenderAudioProcessor&);
    ~VisualiserSettings();

    void resized() override;
    juce::var getSettings();
private:
    OscirenderAudioProcessor& audioProcessor;

    EffectComponent brightness{audioProcessor, *audioProcessor.brightnessEffect};
    EffectComponent intensity{audioProcessor, *audioProcessor.intensityEffect};
    EffectComponent persistence{audioProcessor, *audioProcessor.persistenceEffect};
    EffectComponent hue{audioProcessor, *audioProcessor.hueEffect};
    
    jux::SwitchButton graticuleToggle{audioProcessor.graticuleEnabled};
    jux::SwitchButton smudgeToggle{audioProcessor.smudgesEnabled};
    jux::SwitchButton upsamplingToggle{audioProcessor.upsamplingEnabled};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VisualiserSettings)
};

class SettingsWindow : public juce::DocumentWindow {
public:
    SettingsWindow(juce::String name) : juce::DocumentWindow(name, Colours::darker, juce::DocumentWindow::TitleBarButtons::closeButton) {}
    
    void closeButtonPressed() override {
        setVisible(false);
    }
};

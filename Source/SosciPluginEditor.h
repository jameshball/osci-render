#pragma once

#include <JuceHeader.h>
#include "SosciPluginProcessor.h"
#include "CommonPluginEditor.h"
#include "visualiser/VisualiserComponent.h"
#include "LookAndFeel.h"
#include "visualiser/VisualiserSettings.h"
#include "components/SosciMainMenuBarModel.h"
#include "components/SvgButton.h"

class SosciPluginEditor : public CommonPluginEditor {
public:
    SosciPluginEditor(SosciAudioProcessor&);
    ~SosciPluginEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    SosciAudioProcessor& audioProcessor;

    ScrollableComponent visualiserSettingsWrapper = ScrollableComponent(visualiserSettings);
    
    SosciMainMenuBarModel model{*this, audioProcessor};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SosciPluginEditor)
};

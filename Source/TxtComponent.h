#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class OscirenderAudioProcessorEditor;
class TxtComponent : public juce::GroupComponent, public juce::MouseListener {
public:
    TxtComponent(OscirenderAudioProcessor&, OscirenderAudioProcessorEditor&);

    void resized() override;
private:
    OscirenderAudioProcessor& audioProcessor;
    OscirenderAudioProcessorEditor& pluginEditor;

    juce::StringArray installedFonts = juce::Font::findAllTypefaceNames();

    juce::ComboBox font;
    juce::ToggleButton bold{"Bold"};
    juce::ToggleButton italic{"Italic"};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TxtComponent)
};
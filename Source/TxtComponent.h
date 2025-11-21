#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class OscirenderAudioProcessorEditor;
class TxtComponent : public juce::ComboBox {
public:
    TxtComponent(OscirenderAudioProcessor&, OscirenderAudioProcessorEditor&);

    void update();
    
private:
    OscirenderAudioProcessor& audioProcessor;
    OscirenderAudioProcessorEditor& pluginEditor;

    juce::StringArray installedFonts = juce::Font::findAllTypefaceNames();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TxtComponent)
};
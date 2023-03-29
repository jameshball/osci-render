/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "EffectComponentGroup.h"
#include "EffectsComponent.h"
#include "MainComponent.h"

//==============================================================================
/**
*/
class OscirenderAudioProcessorEditor  : public juce::AudioProcessorEditor
{
public:
    OscirenderAudioProcessorEditor (OscirenderAudioProcessor&);
    ~OscirenderAudioProcessorEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    OscirenderAudioProcessor& audioProcessor;
    
    MainComponent main;
    EffectsComponent effects;
    juce::CodeDocument codeDocument;
    juce::LuaTokeniser luaTokeniser;
	std::unique_ptr<juce::CodeEditorComponent> codeEditor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OscirenderAudioProcessorEditor)
};

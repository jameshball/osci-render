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
class OscirenderAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::CodeDocument::Listener
{
public:
    OscirenderAudioProcessorEditor (OscirenderAudioProcessor&);
    ~OscirenderAudioProcessorEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;

    void updateCodeEditor();
private:
    OscirenderAudioProcessor& audioProcessor;
    
    MainComponent main;
    EffectsComponent effects;
    juce::CodeDocument codeDocument;
    juce::LuaTokeniser luaTokeniser;
	std::unique_ptr<juce::CodeEditorComponent> codeEditor;
	juce::ShapeButton collapseButton;

	void codeDocumentTextInserted(const juce::String& newText, int insertIndex) override;
	void codeDocumentTextDeleted(int startIndex, int endIndex) override;

    bool keyPressed(const juce::KeyPress& key) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OscirenderAudioProcessorEditor)
};

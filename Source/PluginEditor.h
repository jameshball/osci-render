/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "EffectsComponent.h"
#include "MainComponent.h"
#include "LuaComponent.h"
#include "ObjComponent.h"

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
    
    void addCodeEditor(int index);
    void removeCodeEditor(int index);
    void updateCodeEditor();
    void fileUpdated(juce::File file);
private:
    OscirenderAudioProcessor& audioProcessor;
    
    MainComponent main;
    LuaComponent lua;
    ObjComponent obj;
    EffectsComponent effects;
    std::vector<std::shared_ptr<juce::CodeDocument>> codeDocuments;
    std::vector<std::shared_ptr<juce::CodeEditorComponent>> codeEditors;
    juce::LuaTokeniser luaTokeniser;
    juce::XmlTokeniser xmlTokeniser;
	juce::ShapeButton collapseButton;

	void codeDocumentTextInserted(const juce::String& newText, int insertIndex) override;
	void codeDocumentTextDeleted(int startIndex, int endIndex) override;
    void updateCodeDocument();

    bool keyPressed(const juce::KeyPress& key) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OscirenderAudioProcessorEditor)
};

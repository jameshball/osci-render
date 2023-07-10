#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "EffectsComponent.h"
#include "MainComponent.h"
#include "LuaComponent.h"
#include "ObjComponent.h"
#include "components/VolumeComponent.h"


class OscirenderAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::CodeDocument::Listener {
public:
    OscirenderAudioProcessorEditor(OscirenderAudioProcessor&);
    ~OscirenderAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    
    void addCodeEditor(int index);
    void removeCodeEditor(int index);
    void fileUpdated(juce::String fileName);
private:
    OscirenderAudioProcessor& audioProcessor;
    
    MainComponent main{audioProcessor, *this};
    LuaComponent lua{audioProcessor, *this};
    ObjComponent obj{audioProcessor, *this};
    EffectsComponent effects{audioProcessor};
    VolumeComponent volume{audioProcessor};
    std::vector<std::shared_ptr<juce::CodeDocument>> codeDocuments;
    std::vector<std::shared_ptr<juce::CodeEditorComponent>> codeEditors;
    juce::LuaTokeniser luaTokeniser;
    juce::XmlTokeniser xmlTokeniser;
	juce::ShapeButton collapseButton;

	void codeDocumentTextInserted(const juce::String& newText, int insertIndex) override;
	void codeDocumentTextDeleted(int startIndex, int endIndex) override;
    void updateCodeDocument();
    void updateCodeEditor();

    bool keyPressed(const juce::KeyPress& key) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OscirenderAudioProcessorEditor)
};

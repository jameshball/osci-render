/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
OscirenderAudioProcessorEditor::OscirenderAudioProcessorEditor (OscirenderAudioProcessor& p)
	: AudioProcessorEditor(&p), audioProcessor(p), effects(p), main(p)
{
    // Make sure that before the constructor has finished, you've set the
    // editor's size to whatever you need it to be.
    setSize(1100, 750);
	setResizable(true, true);

    addAndMakeVisible(effects);
    addAndMakeVisible(main);

	codeEditor = std::make_unique<juce::CodeEditorComponent>(codeDocument, &luaTokeniser);
	addAndMakeVisible(*codeEditor);
    
    codeEditor->loadContent (R"LUA(
    -- defines a factorial function
    function fact (n)
      if n == 0 then
        return 1
      else
        return n * fact(n-1)
      end
    end
    
    print("enter a number:")
    a = io.read("*number")        -- read a number
    print(fact(a))
)LUA");
}

OscirenderAudioProcessorEditor::~OscirenderAudioProcessorEditor() {}

//==============================================================================
void OscirenderAudioProcessorEditor::paint (juce::Graphics& g)
{
    // (Our component is opaque, so we must completely fill the background with a solid colour)
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));

    g.setColour (juce::Colours::white);
    g.setFont (15.0f);
}

void OscirenderAudioProcessorEditor::resized() {
    effects.setBounds(getWidth() / 2, 0, getWidth() / 2, getHeight());
	main.setBounds(0, 0, getWidth() / 2, getHeight() / 2);
    if (codeEditor != nullptr) {
        codeEditor->setBounds(0, getHeight() / 2, getWidth() / 2, getHeight() / 2);
    }
}

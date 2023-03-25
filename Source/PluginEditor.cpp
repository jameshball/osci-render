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
}

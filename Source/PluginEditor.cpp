/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
OscirenderAudioProcessorEditor::OscirenderAudioProcessorEditor(OscirenderAudioProcessor& p)
	: AudioProcessorEditor(&p), audioProcessor(p), effects(p), main(p, *this), collapseButton("Collapse", juce::Colours::white, juce::Colours::white, juce::Colours::white)
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

    addAndMakeVisible(collapseButton);
	collapseButton.onClick = [this] {
		if (codeEditor->isVisible()) {
			codeEditor->setVisible(false);
            juce::Path path;
            path.addTriangle(0.0f, 0.5f, 1.0f, 1.0f, 1.0f, 0.0f);
            collapseButton.setShape(path, false, true, true);
		} else {
			codeEditor->setVisible(true);
            juce::Path path;
            path.addTriangle(0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.5f);
            collapseButton.setShape(path, false, true, true);
		}
        resized();
	};
	juce::Path path;
	path.addTriangle(0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.5f);
	collapseButton.setShape(path, false, true, true);
}

OscirenderAudioProcessorEditor::~OscirenderAudioProcessorEditor() {}

//==============================================================================
void OscirenderAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));

    g.setColour(juce::Colours::white);
    g.setFont(15.0f);
}

void OscirenderAudioProcessorEditor::resized() {
    auto area = getLocalBounds();
    auto sections = 2;
    if (codeEditor != nullptr) {
		if (codeEditor->isVisible()) {
            sections++;
            codeEditor->setBounds(area.removeFromRight(getWidth() / sections));
		} else {
			codeEditor->setBounds(0, 0, 0, 0);
		}
		collapseButton.setBounds(area.removeFromRight(20));
    }
	effects.setBounds(area.removeFromRight(getWidth() / sections));
	main.setBounds(area.removeFromTop(getHeight() / 2));
}

std::shared_ptr<juce::MemoryBlock> OscirenderAudioProcessorEditor::addFile(juce::File file) {
	fileBlocks.push_back(std::make_shared<juce::MemoryBlock>());
	files.push_back(file);
	file.createInputStream()->readIntoMemoryBlock(*fileBlocks.back());

	openFile(fileBlocks.size() - 1);
	
	return fileBlocks.back();
}

void OscirenderAudioProcessorEditor::removeFile(int index) {
    openFile(index - 1);
    fileBlocks.erase(fileBlocks.begin() + index);
    files.erase(files.begin() + index);
}

int OscirenderAudioProcessorEditor::numFiles() {
    return fileBlocks.size();
}

void OscirenderAudioProcessorEditor::openFile(int index) {
    audioProcessor.parser.parse(files[index].getFileExtension(), std::make_unique<juce::MemoryInputStream>(*fileBlocks[index], false));
    codeEditor->loadContent(juce::MemoryInputStream(*fileBlocks[index], false).readEntireStreamAsString());
}

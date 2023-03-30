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

    // I need to disable accessibility otherwise it doesn't work! Appears to be a JUCE issue, very annoying!
    codeEditor->setAccessible(false);

	// listen for changes to the code editor
	codeDocument.addListener(this);

    addAndMakeVisible(collapseButton);
	collapseButton.onClick = [this] {
		if (codeEditor->isVisible()) {
			codeEditor->setVisible(false);
            juce::Path path;
            path.addTriangle(0.0f, 0.5f, 1.0f, 1.0f, 1.0f, 0.0f);
            collapseButton.setShape(path, false, true, true);
		} else {
			codeEditor->setVisible(true);
            updateCodeEditor();
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

void OscirenderAudioProcessorEditor::updateCodeEditor() {
    if (codeEditor->isVisible() && audioProcessor.getCurrentFileIndex() != -1) {
        codeEditor->loadContent(juce::MemoryInputStream(*audioProcessor.getFileBlock(audioProcessor.getCurrentFileIndex()), false).readEntireStreamAsString());
    }
}
    
void OscirenderAudioProcessorEditor::codeDocumentTextInserted(const juce::String& newText, int insertIndex) {
    juce::String file = codeDocument.getAllContent();
    audioProcessor.updateFileBlock(audioProcessor.getCurrentFileIndex(), std::make_shared<juce::MemoryBlock>(file.toRawUTF8(), file.getNumBytesAsUTF8() + 1));
}

void OscirenderAudioProcessorEditor::codeDocumentTextDeleted(int startIndex, int endIndex) {
    juce::String file = codeDocument.getAllContent();
    audioProcessor.updateFileBlock(audioProcessor.getCurrentFileIndex(), std::make_shared<juce::MemoryBlock>(file.toRawUTF8(), file.getNumBytesAsUTF8() + 1));
}

bool OscirenderAudioProcessorEditor::keyPressed(const juce::KeyPress& key) {
    int numFiles = audioProcessor.numFiles();
    int currentFile = audioProcessor.getCurrentFileIndex();
    if (key.getTextCharacter() == 'j') {
        currentFile++;
        if (currentFile == numFiles) {
            currentFile = 0;
        }
        audioProcessor.changeCurrentFile(currentFile);
        updateCodeEditor();
        return true;
    } else if (key.getTextCharacter() == 'k') {
        currentFile--;
        if (currentFile < 0) {
            currentFile = numFiles - 1;
        }
        audioProcessor.changeCurrentFile(currentFile);
        updateCodeEditor();
        return true;
    }
    return false;
}

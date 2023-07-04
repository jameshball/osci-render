/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
OscirenderAudioProcessorEditor::OscirenderAudioProcessorEditor(OscirenderAudioProcessor& p)
	: AudioProcessorEditor(&p), audioProcessor(p), effects(p), main(p, *this), collapseButton("Collapse", juce::Colours::white, juce::Colours::white, juce::Colours::white), lua(p, *this), obj(p, *this)
{
    // Make sure that before the constructor has finished, you've set the
    // editor's size to whatever you need it to be.
    setSize(1100, 750);
	setResizable(true, true);

    addAndMakeVisible(effects);
    addAndMakeVisible(main);
    addChildComponent(lua);
    addChildComponent(obj);

    addAndMakeVisible(collapseButton);
	collapseButton.onClick = [this] {
        juce::SpinLock::ScopedLockType lock(audioProcessor.parsersLock);
        int index = audioProcessor.getCurrentFileIndex();
        if (index != -1) {
            if (codeEditors[index]->isVisible()) {
                codeEditors[index]->setVisible(false);
                juce::Path path;
                path.addTriangle(0.0f, 0.5f, 1.0f, 1.0f, 1.0f, 0.0f);
                collapseButton.setShape(path, false, true, true);
            } else {
                codeEditors[index]->setVisible(true);
                updateCodeEditor();
                juce::Path path;
                path.addTriangle(0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.5f);
                collapseButton.setShape(path, false, true, true);
            }
            resized();
        }
	};
	juce::Path path;
    path.addTriangle(0.0f, 0.5f, 1.0f, 1.0f, 1.0f, 0.0f);
	collapseButton.setShape(path, false, true, true);
    resized();
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
    int index = audioProcessor.getCurrentFileIndex();
    if (index != -1) {
		if (codeEditors[index]->isVisible()) {
            sections++;
            codeEditors[index]->setBounds(area.removeFromRight(getWidth() / sections));
		} else {
            codeEditors[index]->setBounds(0, 0, 0, 0);
		}
		collapseButton.setBounds(area.removeFromRight(20));
    } else {
		collapseButton.setBounds(0, 0, 0, 0);
	}
	effects.setBounds(area.removeFromRight(getWidth() / sections));
	main.setBounds(area.removeFromTop(getHeight() / 2));
    lua.setBounds(area);
    obj.setBounds(area);
}

void OscirenderAudioProcessorEditor::addCodeEditor(int index) {
    std::shared_ptr<juce::CodeDocument> codeDocument = std::make_shared<juce::CodeDocument>();
    codeDocuments.insert(codeDocuments.begin() + index, codeDocument);
    juce::String extension = audioProcessor.getFile(index).getFileExtension();
    juce::CodeTokeniser* tokeniser = nullptr;
    if (extension == ".lua") {
        tokeniser = &luaTokeniser;
    } else if (extension == ".svg") {
        tokeniser = &xmlTokeniser;
	}
    std::shared_ptr<juce::CodeEditorComponent> editor = std::make_shared<juce::CodeEditorComponent>(*codeDocument, tokeniser);
    // I need to disable accessibility otherwise it doesn't work! Appears to be a JUCE issue, very annoying!
    editor->setAccessible(false);
    // listen for changes to the code editor
    codeDocument->addListener(this);
    codeEditors.insert(codeEditors.begin() + index, editor);
    addChildComponent(*editor);
}

void OscirenderAudioProcessorEditor::removeCodeEditor(int index) {
    codeEditors.erase(codeEditors.begin() + index);
    codeDocuments.erase(codeDocuments.begin() + index);
}


// parsersLock must be locked before calling this function
void OscirenderAudioProcessorEditor::updateCodeEditor() {
    // check if any code editors are visible
    bool visible = false;
    for (int i = 0; i < codeEditors.size(); i++) {
        if (codeEditors[i]->isVisible()) {
			visible = true;
			break;
		}
	}
    int index = audioProcessor.getCurrentFileIndex();
    if (index != -1 && visible) {
        for (int i = 0; i < codeEditors.size(); i++) {
            codeEditors[i]->setVisible(false);
        }
        codeEditors[index]->setVisible(true);
        codeEditors[index]->loadContent(juce::MemoryInputStream(*audioProcessor.getFileBlock(index), false).readEntireStreamAsString());
    }
    resized();
}

void OscirenderAudioProcessorEditor::fileUpdated(juce::File file) {
    lua.setVisible(false);
    obj.setVisible(false);
    if (file.getFileExtension() == ".lua") {
        lua.setVisible(true);
    } else if (file.getFileExtension() == ".obj") {
		obj.setVisible(true);
	}
}
    
void OscirenderAudioProcessorEditor::codeDocumentTextInserted(const juce::String& newText, int insertIndex) {
    juce::SpinLock::ScopedLockType lock(audioProcessor.parsersLock);
    int index = audioProcessor.getCurrentFileIndex();
    juce::String file = codeDocuments[index]->getAllContent();
    audioProcessor.updateFileBlock(index, std::make_shared<juce::MemoryBlock>(file.toRawUTF8(), file.getNumBytesAsUTF8() + 1));
}

void OscirenderAudioProcessorEditor::codeDocumentTextDeleted(int startIndex, int endIndex) {
    juce::SpinLock::ScopedLockType lock(audioProcessor.parsersLock);
    int index = audioProcessor.getCurrentFileIndex();
    juce::String file = codeDocuments[index]->getAllContent();
    audioProcessor.updateFileBlock(index, std::make_shared<juce::MemoryBlock>(file.toRawUTF8(), file.getNumBytesAsUTF8() + 1));
}

bool OscirenderAudioProcessorEditor::keyPressed(const juce::KeyPress& key) {
    juce::SpinLock::ScopedLockType lock(audioProcessor.parsersLock);
    int numFiles = audioProcessor.numFiles();
    int currentFile = audioProcessor.getCurrentFileIndex();
    bool updated = false;
    if (key.getTextCharacter() == 'j') {
        currentFile++;
        if (currentFile == numFiles) {
            currentFile = 0;
        }
        updated = true;
    } else if (key.getTextCharacter() == 'k') {
        currentFile--;
        if (currentFile < 0) {
            currentFile = numFiles - 1;
        }
        updated = true;
    }

    if (updated) {
        audioProcessor.changeCurrentFile(currentFile);
        fileUpdated(audioProcessor.getCurrentFile());
        updateCodeEditor();
        main.updateFileLabel();
    }

    return updated;
}

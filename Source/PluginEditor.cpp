#include "PluginProcessor.h"
#include "PluginEditor.h"

OscirenderAudioProcessorEditor::OscirenderAudioProcessorEditor(OscirenderAudioProcessor& p)
	: AudioProcessorEditor(&p), audioProcessor(p), collapseButton("Collapse", juce::Colours::white, juce::Colours::white, juce::Colours::white)
{
    addAndMakeVisible(effects);
    addAndMakeVisible(main);
    addChildComponent(lua);
    addChildComponent(obj);
    addAndMakeVisible(volume);

    addAndMakeVisible(collapseButton);
	collapseButton.onClick = [this] {
        juce::SpinLock::ScopedLockType lock(audioProcessor.parsersLock);
        int originalIndex = audioProcessor.getCurrentFileIndex();
        int index = editingPerspective ? 0 : audioProcessor.getCurrentFileIndex() + 1;
        if (originalIndex != -1 || editingPerspective) {
            if (codeEditors[index]->isVisible()) {
                codeEditors[index]->setVisible(false);
            } else {
                codeEditors[index]->setVisible(true);
                updateCodeEditor();
            }
            triggerAsyncUpdate();
        }
	};
	juce::Path path;
    path.addTriangle(0.0f, 0.5f, 1.0f, 1.0f, 1.0f, 0.0f);
	collapseButton.setShape(path, false, true, true);

    {
        juce::SpinLock::ScopedLockType lock(audioProcessor.parsersLock);
        addCodeEditor(-1);
        for (int i = 0; i < audioProcessor.numFiles(); i++) {
            addCodeEditor(i);
        }
        fileUpdated(audioProcessor.getCurrentFileName());
    }

    setSize(1100, 750);
    setResizable(true, true);
    setResizeLimits(500, 400, 999999, 999999);
}

OscirenderAudioProcessorEditor::~OscirenderAudioProcessorEditor() {}

void OscirenderAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));

    g.setColour(juce::Colours::white);
    g.setFont(15.0f);
}

void OscirenderAudioProcessorEditor::resized() {
    auto area = getLocalBounds();
    area.removeFromLeft(3);
    auto volumeArea = area.removeFromLeft(30);
    volume.setBounds(volumeArea.withSizeKeepingCentre(volumeArea.getWidth(), juce::jmin(volumeArea.getHeight(), 300)));
    area.removeFromLeft(3);
    auto sections = 2;
    bool editorVisible = false;
    {
        juce::SpinLock::ScopedLockType lock(audioProcessor.parsersLock);
        int originalIndex = audioProcessor.getCurrentFileIndex();
        int index = editingPerspective ? 0 : audioProcessor.getCurrentFileIndex() + 1;
        if (originalIndex != -1 || editingPerspective) {
            if (codeEditors[index]->isVisible()) {
                sections++;
                editorVisible = true;
                codeEditors[index]->setBounds(area.removeFromRight(getWidth() / sections));
            } else {
                codeEditors[index]->setBounds(0, 0, 0, 0);
            }
            collapseButton.setBounds(area.removeFromRight(20));
        } else {
            collapseButton.setBounds(0, 0, 0, 0);
        }
    }

    if (editorVisible) {
        juce::Path path;
        path.addTriangle(0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.5f);
        collapseButton.setShape(path, false, true, true);
    } else {
        juce::Path path;
        path.addTriangle(0.0f, 0.5f, 1.0f, 1.0f, 1.0f, 0.0f);
        collapseButton.setShape(path, false, true, true);
    }
    
    auto effectsSection = area.removeFromRight(1.2 * getWidth() / sections);
    main.setBounds(area);
    if (lua.isVisible() || obj.isVisible()) {
        auto altEffectsSection = effectsSection.removeFromBottom(juce::jmin(effectsSection.getHeight() / 2, 300));
        lua.setBounds(altEffectsSection);
        obj.setBounds(altEffectsSection);
    }
	effects.setBounds(effectsSection);
    
}

void OscirenderAudioProcessorEditor::addCodeEditor(int index) {
    int originalIndex = index;
    index++;
    std::shared_ptr<juce::CodeDocument> codeDocument;
    std::shared_ptr<juce::CodeEditorComponent> editor;

    if (index == 0) {
        codeDocument = perspectiveCodeDocument;
        editor = perspectiveCodeEditor;
    } else {
        codeDocument = std::make_shared<juce::CodeDocument>();
        juce::String extension = audioProcessor.getFileName(originalIndex).fromLastOccurrenceOf(".", true, false);
        juce::CodeTokeniser* tokeniser = nullptr;
        if (extension == ".lua") {
            tokeniser = &luaTokeniser;
        } else if (extension == ".svg") {
            tokeniser = &xmlTokeniser;
        }
        editor = std::make_shared<juce::CodeEditorComponent>(*codeDocument, tokeniser);
    }
    
    codeDocuments.insert(codeDocuments.begin() + index, codeDocument);
    codeEditors.insert(codeEditors.begin() + index, editor);
    addChildComponent(*editor);
    // I need to disable accessibility otherwise it doesn't work! Appears to be a JUCE issue, very annoying!
    editor->setAccessible(false);
    // listen for changes to the code editor
    codeDocument->addListener(this);
}

void OscirenderAudioProcessorEditor::removeCodeEditor(int index) {
    index++;
    codeEditors.erase(codeEditors.begin() + index);
    codeDocuments.erase(codeDocuments.begin() + index);
}


// parsersLock AND effectsLock must be locked before calling this function
void OscirenderAudioProcessorEditor::updateCodeEditor() {
    // check if any code editors are visible
    bool visible = false;
    for (int i = 0; i < codeEditors.size(); i++) {
        if (codeEditors[i]->isVisible()) {
            visible = true;
            break;
        }
    }
    int originalIndex = audioProcessor.getCurrentFileIndex();
    int index = editingPerspective ? 0 : audioProcessor.getCurrentFileIndex() + 1;
    if ((originalIndex != -1 || editingPerspective) && visible) {
        for (int i = 0; i < codeEditors.size(); i++) {
            codeEditors[i]->setVisible(false);
        }
        codeEditors[index]->setVisible(true);
        if (index == 0) {
            codeEditors[index]->loadContent(audioProcessor.perspectiveEffect->getCode());
        } else {
            codeEditors[index]->loadContent(juce::MemoryInputStream(*audioProcessor.getFileBlock(originalIndex), false).readEntireStreamAsString());
        }
    }
    triggerAsyncUpdate();
}

// parsersLock MUST be locked before calling this function
void OscirenderAudioProcessorEditor::fileUpdated(juce::String fileName) {
    juce::String extension = fileName.fromLastOccurrenceOf(".", true, false);
    lua.setVisible(false);
    obj.setVisible(false);
    if (fileName.isEmpty()) {
        // do nothing
    } else if (extension == ".lua") {
        lua.setVisible(true);
    } else if (extension == ".obj") {
		obj.setVisible(true);
	}
    main.updateFileLabel();
    updateCodeEditor();
}

void OscirenderAudioProcessorEditor::handleAsyncUpdate() {
    resized();
}

void OscirenderAudioProcessorEditor::editPerspectiveFunction(bool enable) {
    editingPerspective = enable;
    juce::SpinLock::ScopedLockType lock1(audioProcessor.parsersLock);
    juce::SpinLock::ScopedLockType lock2(audioProcessor.effectsLock);
    codeEditors[0]->setVisible(enable);
    updateCodeEditor();
}

// parsersLock AND effectsLock must be locked before calling this function
void OscirenderAudioProcessorEditor::codeDocumentTextInserted(const juce::String& newText, int insertIndex) {
    updateCodeDocument();
}

// parsersLock AND effectsLock must be locked before calling this function
void OscirenderAudioProcessorEditor::codeDocumentTextDeleted(int startIndex, int endIndex) {
    updateCodeDocument();
}

// parsersLock AND effectsLock must be locked before calling this function
void OscirenderAudioProcessorEditor::updateCodeDocument() {
    if (editingPerspective) {
        juce::String file = codeDocuments[0]->getAllContent();
        audioProcessor.perspectiveEffect->updateCode(file);
    } else {
        int originalIndex = audioProcessor.getCurrentFileIndex();
        int index = audioProcessor.getCurrentFileIndex();
        index++;
        juce::String file = codeDocuments[index]->getAllContent();
        audioProcessor.updateFileBlock(originalIndex, std::make_shared<juce::MemoryBlock>(file.toRawUTF8(), file.getNumBytesAsUTF8() + 1));
    }
}

bool OscirenderAudioProcessorEditor::keyPressed(const juce::KeyPress& key) {
    juce::SpinLock::ScopedLockType parserLock(audioProcessor.parsersLock);
    juce::SpinLock::ScopedLockType effectsLock(audioProcessor.effectsLock);

    int numFiles = audioProcessor.numFiles();
    int currentFile = audioProcessor.getCurrentFileIndex();
    bool changedFile = false;
    bool consumeKey = true;
    if (key.getTextCharacter() == 'j') {
        if (numFiles > 1) {
            currentFile++;
            if (currentFile == numFiles) {
                currentFile = 0;
            }
            changedFile = true;
        }
    } else if (key.getTextCharacter() == 'k') {
        if (numFiles > 1) {
			currentFile--;
            if (currentFile < 0) {
				currentFile = numFiles - 1;
			}
			changedFile = true;
		}
    } else if (key.isKeyCode(juce::KeyPress::escapeKey)) {
        obj.disableMouseRotation();
    } else {
		consumeKey = false;
	}

    if (changedFile) {
        audioProcessor.changeCurrentFile(currentFile);
        fileUpdated(audioProcessor.getCurrentFileName());
    }

    return consumeKey;
}

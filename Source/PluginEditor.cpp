#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

OscirenderAudioProcessorEditor::OscirenderAudioProcessorEditor(OscirenderAudioProcessor& p)
	: CommonPluginEditor(p, "osci-render", "osci"), audioProcessor(p), collapseButton("Collapse", juce::Colours::white, juce::Colours::white, juce::Colours::white) {
    addAndMakeVisible(volume);
    
    addAndMakeVisible(console);
    console.setConsoleOpen(false);
    
	LuaParser::onPrint = [this](const std::string& message) {
		console.print(message);
	};

    LuaParser::onClear = [this]() {
        console.clear();
    };

    addAndMakeVisible(collapseButton);
	collapseButton.onClick = [this] {
        {
            juce::SpinLock::ScopedLockType lock(audioProcessor.parsersLock);
            int originalIndex = audioProcessor.getCurrentFileIndex();
            int index = editingCustomFunction ? 0 : audioProcessor.getCurrentFileIndex() + 1;
            if (originalIndex != -1 || editingCustomFunction) {
                codeEditors[index]->setVisible(!codeEditors[index]->isVisible());
                updateCodeEditor(!editingCustomFunction && isBinaryFile(audioProcessor.getCurrentFileName()));
            }
        }
	};
	juce::Path path;
    path.addTriangle(0.0f, 0.5f, 1.0f, 1.0f, 1.0f, 0.0f);
	collapseButton.setShape(path, false, true, true);
    collapseButton.setMouseCursor(juce::MouseCursor::PointingHandCursor);

    colourScheme = lookAndFeel.getDefaultColourScheme();

    {
        juce::SpinLock::ScopedLockType lock(audioProcessor.parsersLock);
        initialiseCodeEditors();
    }

    {   
        juce::MessageManagerLock lock;
        audioProcessor.fileChangeBroadcaster.addChangeListener(this);
        audioProcessor.broadcaster.addChangeListener(this);
    }

    layout.setItemLayout(0, -0.3, -1.0, -0.7);
    layout.setItemLayout(1, RESIZER_BAR_SIZE, RESIZER_BAR_SIZE, RESIZER_BAR_SIZE);
    layout.setItemLayout(2, -0.0, -1.0, -0.3);

    addAndMakeVisible(settings);
    addAndMakeVisible(resizerBar);

    luaLayout.setItemLayout(0, -0.3, -1.0, -0.7);
    luaLayout.setItemLayout(1, RESIZER_BAR_SIZE, RESIZER_BAR_SIZE, RESIZER_BAR_SIZE);
    luaLayout.setItemLayout(2, -0.1, -1.0, -0.3);

    addAndMakeVisible(lua);
    addAndMakeVisible(luaResizerBar);
    addAndMakeVisible(visualiser);

    initialiseMenuBar(model);
}

OscirenderAudioProcessorEditor::~OscirenderAudioProcessorEditor() {
    juce::MessageManagerLock lock;
    audioProcessor.broadcaster.removeChangeListener(this);
    audioProcessor.fileChangeBroadcaster.removeChangeListener(this);
}

bool OscirenderAudioProcessorEditor::isBinaryFile(juce::String name) {
    return name.endsWith(".gpla") || name.endsWith(".gif") || name.endsWith(".png") || name.endsWith(".jpg") || name.endsWith(".jpeg") || name.endsWith(".wav") || name.endsWith(".aiff");
}

// parsersLock must be held
void OscirenderAudioProcessorEditor::initialiseCodeEditors() {
    codeEditors.clear();
    codeDocuments.clear();
    // -1 is the perspective function
    addCodeEditor(-1);
    for (int i = 0; i < audioProcessor.numFiles(); i++) {
        addCodeEditor(i);
    }
    fileUpdated(audioProcessor.getCurrentFileName());
}

void OscirenderAudioProcessorEditor::paint(juce::Graphics& g) {
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

void OscirenderAudioProcessorEditor::resized() {
    auto area = getLocalBounds();

    if (audioProcessor.visualiserParameters.visualiserFullScreen->getBoolValue()) {
        visualiser.setBounds(area);
        return;
    }

    if (!usingNativeMenuBar) {
        menuBar.setBounds(area.removeFromTop(25));
    }
    
    area.removeFromTop(2);
    area.removeFromLeft(3);
    auto volumeArea = area.removeFromLeft(30);
    volume.setBounds(volumeArea.withSizeKeepingCentre(volumeArea.getWidth(), juce::jmin(volumeArea.getHeight(), 300)));
    area.removeFromLeft(3);
    bool editorVisible = false;

    {
        juce::SpinLock::ScopedLockType lock(audioProcessor.parsersLock);

        int originalIndex = audioProcessor.getCurrentFileIndex();
        int index = editingCustomFunction ? 0 : audioProcessor.getCurrentFileIndex() + 1;

        bool ableToEditFile = (originalIndex != -1 && !isBinaryFile(audioProcessor.getCurrentFileName())) || editingCustomFunction;
        bool fileOpen = false;
        bool luaFileOpen = false;
        
        if (ableToEditFile) {
            if (codeEditors[index]->isVisible()) {
                editorVisible = true;

                juce::Component dummy;
                juce::Component dummy2;
                juce::Component dummy3;

                juce::Component* columns[] = { &dummy, &resizerBar, &dummy2 };
                 
                // offsetting the y position by -1 and the height by +1 is a hack to fix a bug where the code editor
                // doesn't draw up to the edges of the menu bar above.
                layout.layOutComponents(columns, 3, area.getX(), area.getY() - 1, area.getWidth(), area.getHeight() + 1, false, true);
                auto dummyBounds = dummy.getBounds();
                collapseButton.setBounds(dummyBounds.removeFromRight(20));
                area = dummyBounds;

                auto dummy2Bounds = dummy2.getBounds();
                dummy2Bounds.removeFromBottom(5);
                dummy2Bounds.removeFromTop(5);
                dummy2Bounds.removeFromRight(5);

                juce::String extension;
                if (originalIndex >= 0) {
                    extension = audioProcessor.getFileName(originalIndex).fromLastOccurrenceOf(".", true, false);
                }

                if (editingCustomFunction || extension == ".lua") {
                    juce::Component* rows[] = { &dummy3, &luaResizerBar, &lua };
                    luaLayout.layOutComponents(rows, 3, dummy2Bounds.getX(), dummy2Bounds.getY(), dummy2Bounds.getWidth(), dummy2Bounds.getHeight(), true, true);
                    auto dummy3Bounds = dummy3.getBounds();
					console.setBounds(dummy3Bounds.removeFromBottom(console.getConsoleOpen() ? 200 : 30));
                    dummy3Bounds.removeFromBottom(RESIZER_BAR_SIZE);
                    codeEditors[index]->setBounds(dummy3Bounds);
                    luaFileOpen = true;
                } else {
                    codeEditors[index]->setBounds(dummy2Bounds);
                }

                fileOpen = true;
            } else {
                collapseButton.setBounds(area.removeFromRight(20));
            }
        }

        collapseButton.setVisible(ableToEditFile);

        codeEditors[index]->setVisible(fileOpen);
        resizerBar.setVisible(fileOpen);

        console.setVisible(luaFileOpen);
        luaResizerBar.setVisible(luaFileOpen);
        lua.setVisible(luaFileOpen);
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

    settings.setBounds(area);
    repaint();
}

void OscirenderAudioProcessorEditor::addCodeEditor(int index) {
    int originalIndex = index;
    index++;
    std::shared_ptr<juce::CodeDocument> codeDocument;
    std::shared_ptr<OscirenderCodeEditorComponent> editor;

    if (index == 0) {
        codeDocument = customFunctionCodeDocument;
        editor = customFunctionCodeEditor;
    } else {
        codeDocument = std::make_shared<juce::CodeDocument>();
        juce::String extension = audioProcessor.getFileName(originalIndex).fromLastOccurrenceOf(".", true, false);
        juce::CodeTokeniser* tokeniser = nullptr;
        if (extension == ".lua") {
            tokeniser = &luaTokeniser;
        } else if (extension == ".svg") {
            tokeniser = &xmlTokeniser;
        }
        editor = std::make_shared<OscirenderCodeEditorComponent>(*codeDocument, tokeniser, audioProcessor, audioProcessor.getFileId(originalIndex), audioProcessor.getFileName(originalIndex));
    }
    
    codeDocuments.insert(codeDocuments.begin() + index, codeDocument);
    codeEditors.insert(codeEditors.begin() + index, editor);
    addChildComponent(*editor);
    // I need to disable accessibility otherwise it doesn't work! Appears to be a JUCE issue, very annoying!
    editor->setAccessible(false);
    // listen for changes to the code editor
    codeDocument->addListener(this);
    editor->getEditor().setColourScheme(colourScheme);
}

void OscirenderAudioProcessorEditor::removeCodeEditor(int index) {
    index++;
    codeEditors.erase(codeEditors.begin() + index);
    codeDocuments.erase(codeDocuments.begin() + index);
}


// parsersLock AND effectsLock must be locked before calling this function
void OscirenderAudioProcessorEditor::updateCodeEditor(bool binaryFile, bool shouldOpenEditor) {
    // check if any code editors are visible
    bool visible = shouldOpenEditor;
    if (!visible) {
        for (int i = 0; i < codeEditors.size(); i++) {
            if (codeEditors[i]->isVisible()) {
                if (binaryFile) {
                    codeEditors[i]->setVisible(false);
                } else {
                    visible = true;
                }
                break;
            }
        }
    }
    
    collapseButton.setVisible(!binaryFile);
    
    if (!binaryFile) {
        int originalIndex = audioProcessor.getCurrentFileIndex();
        int index = editingCustomFunction ? 0 : audioProcessor.getCurrentFileIndex() + 1;
        if ((originalIndex != -1 || editingCustomFunction) && visible) {
            for (int i = 0; i < codeEditors.size(); i++) {
                codeEditors[i]->setVisible(false);
            }
            codeEditors[index]->setVisible(true);
            // used so that codeDocumentTextInserted and codeDocumentTextDeleted know whether the parserLock
            // is held by the message thread or not. We hold the lock in this function, but not when the
            // code document is updated by the user editing text. Since both functions are called by the
            // message thread, this is safe.
            updatingDocumentsWithParserLock = true;
            if (index == 0) {
                codeEditors[index]->getEditor().loadContent(audioProcessor.customEffect->getCode());
            } else {
                codeEditors[index]->getEditor().loadContent(juce::MemoryInputStream(*audioProcessor.getFileBlock(originalIndex), false).readEntireStreamAsString());
            }
            updatingDocumentsWithParserLock = false;
        }
    }
    triggerAsyncUpdate();
}

// parsersLock MUST be locked before calling this function
void OscirenderAudioProcessorEditor::fileUpdated(juce::String fileName, bool shouldOpenEditor) {
    settings.fileUpdated(fileName);
    updateCodeEditor(isBinaryFile(fileName), shouldOpenEditor);
}

void OscirenderAudioProcessorEditor::handleAsyncUpdate() {
    resized();
}

void OscirenderAudioProcessorEditor::changeListenerCallback(juce::ChangeBroadcaster* source) {
    
    if (source == &audioProcessor.broadcaster) {
        {
            juce::SpinLock::ScopedLockType lock(audioProcessor.parsersLock);
            initialiseCodeEditors();
            settings.update();
        }
        resized();
        repaint();
    } else if (source == &audioProcessor.fileChangeBroadcaster) {
        juce::SpinLock::ScopedLockType lock(audioProcessor.parsersLock);
        // triggered when the audioProcessor changes the current file (e.g. to Blender)
        settings.fileUpdated(audioProcessor.getCurrentFileName());
    }
}

void OscirenderAudioProcessorEditor::toggleLayout(juce::StretchableLayoutManager& layout, double prefSize) {
    double minSize, maxSize, preferredSize;
    double otherMinSize, otherMaxSize, otherPreferredSize;
    layout.getItemLayout(2, minSize, maxSize, preferredSize);
    layout.getItemLayout(0, otherMinSize, otherMaxSize, otherPreferredSize);

    if (preferredSize == CLOSED_PREF_SIZE) {
        double otherPrefSize = -(1 + prefSize);
        if (prefSize > 0) {
            otherPrefSize = -1.0;
        }
        layout.setItemLayout(2, CLOSED_PREF_SIZE, maxSize, prefSize);
        layout.setItemLayout(0, CLOSED_PREF_SIZE, otherMaxSize, otherPrefSize);
    } else {
        layout.setItemLayout(2, CLOSED_PREF_SIZE, maxSize, CLOSED_PREF_SIZE);
        layout.setItemLayout(0, CLOSED_PREF_SIZE, otherMaxSize, -1.0);
    }
}

void OscirenderAudioProcessorEditor::editCustomFunction(bool enable) {
    editingCustomFunction = enable;
    juce::SpinLock::ScopedLockType lock1(audioProcessor.parsersLock);
    juce::SpinLock::ScopedLockType lock2(audioProcessor.effectsLock);
    codeEditors[0]->setVisible(enable);
    updateCodeEditor(!editingCustomFunction && isBinaryFile(audioProcessor.getCurrentFileName()));
}

// parsersLock AND effectsLock must be locked before calling this function
void OscirenderAudioProcessorEditor::codeDocumentTextInserted(const juce::String& newText, int insertIndex) {
    if (updatingDocumentsWithParserLock) {
        updateCodeDocument();
    } else {
        juce::SpinLock::ScopedLockType parserLock(audioProcessor.parsersLock);
        updateCodeDocument();
    }
}

// parsersLock AND effectsLock must be locked before calling this function
void OscirenderAudioProcessorEditor::codeDocumentTextDeleted(int startIndex, int endIndex) {
    if (updatingDocumentsWithParserLock) {
        updateCodeDocument();
    } else {
        juce::SpinLock::ScopedLockType parserLock(audioProcessor.parsersLock);
        updateCodeDocument();
    }
}

// parsersLock AND effectsLock must be locked before calling this function
void OscirenderAudioProcessorEditor::updateCodeDocument() {
    if (editingCustomFunction) {
        juce::String file = codeDocuments[0]->getAllContent();
        audioProcessor.customEffect->updateCode(file);
    } else {
        int originalIndex = audioProcessor.getCurrentFileIndex();
        int index = audioProcessor.getCurrentFileIndex();
        index++;
        juce::String file = codeDocuments[index]->getAllContent();
        audioProcessor.updateFileBlock(originalIndex, std::make_shared<juce::MemoryBlock>(file.toRawUTF8(), file.getNumBytesAsUTF8() + 1));
    }
}

bool OscirenderAudioProcessorEditor::keyPressed(const juce::KeyPress& key) {
    bool consumeKey = false;
    {
        juce::SpinLock::ScopedLockType parserLock(audioProcessor.parsersLock);
        juce::SpinLock::ScopedLockType effectsLock(audioProcessor.effectsLock);

        int numFiles = audioProcessor.numFiles();
        int currentFile = audioProcessor.getCurrentFileIndex();
        bool changedFile = false;

        if (key.getTextCharacter() == 'j') {
            if (numFiles > 1) {
                currentFile++;
                if (currentFile == numFiles) {
                    currentFile = 0;
                }
                changedFile = true;
            }
            consumeKey = true;
        } else if (key.getTextCharacter() == 'k') {
            if (numFiles > 1) {
                currentFile--;
                if (currentFile < 0) {
                    currentFile = numFiles - 1;
                }
                changedFile = true;
            }
            consumeKey = true;
        }

        if (changedFile) {
            audioProcessor.changeCurrentFile(currentFile);
            fileUpdated(audioProcessor.getCurrentFileName());
        }
    }
    
    CommonPluginEditor::keyPressed(key);

    return consumeKey;
}

void OscirenderAudioProcessorEditor::mouseDown(const juce::MouseEvent& e) {
    if (console.getBoundsInParent().removeFromTop(30).contains(e.getPosition())) {
        console.setConsoleOpen(!console.getConsoleOpen());
        resized();
    }
}

void OscirenderAudioProcessorEditor::mouseMove(const juce::MouseEvent& event) {
    if (console.getBoundsInParent().removeFromTop(30).contains(event.getPosition())) {
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    } else {
        setMouseCursor(juce::MouseCursor::NormalCursor);
    }
}

void OscirenderAudioProcessorEditor::openAudioSettings() {
    juce::StandalonePluginHolder* standalone = juce::StandalonePluginHolder::getInstance();
    standalone->showAudioSettingsDialog();
}

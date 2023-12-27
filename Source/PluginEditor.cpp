#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

OscirenderAudioProcessorEditor::OscirenderAudioProcessorEditor(OscirenderAudioProcessor& p)
	: AudioProcessorEditor(&p), audioProcessor(p), collapseButton("Collapse", juce::Colours::white, juce::Colours::white, juce::Colours::white)
{
#if !JUCE_MAC
    // use OpenGL on Windows and Linux for much better performance. The default on Mac is CoreGraphics which is much faster.
    openGlContext.attachTo(*getTopLevelComponent());
#endif

    setLookAndFeel(&lookAndFeel);
    addAndMakeVisible(volume);

#if JUCE_MAC
    if (audioProcessor.wrapperType == juce::AudioProcessor::WrapperType::wrapperType_Standalone) {
        usingNativeMenuBar = true;
        menuBarModel.setMacMainMenu(&menuBarModel);
    }
#endif

    if (!usingNativeMenuBar) {
        menuBar.setModel(&menuBarModel);
        addAndMakeVisible(menuBar);
    }

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

    if (audioProcessor.wrapperType == juce::AudioProcessor::WrapperType::wrapperType_Standalone) {
        if (juce::TopLevelWindow::getNumTopLevelWindows() == 1) {
            juce::TopLevelWindow* w = juce::TopLevelWindow::getTopLevelWindow(0);
            juce::DocumentWindow* dw = dynamic_cast<juce::DocumentWindow*>(w);
            if (dw != nullptr) {
                dw->setColour(juce::ResizableWindow::backgroundColourId, Colours::veryDark);
                dw->setTitleBarButtonsRequired(juce::DocumentWindow::allButtons, false);
                dw->setUsingNativeTitleBar(true);
            }
        }
    }

    setSize(1100, 750);
    setResizable(true, true);
    setResizeLimits(500, 400, 999999, 999999);

    layout.setItemLayout(0, -0.3, -1.0, -0.7);
    layout.setItemLayout(1, 7, 7, 7);
    layout.setItemLayout(2, -0.1, -1.0, -0.3);

    addAndMakeVisible(settings);
    addAndMakeVisible(resizerBar);
}

OscirenderAudioProcessorEditor::~OscirenderAudioProcessorEditor() {
    setLookAndFeel(nullptr);
    juce::Desktop::getInstance().setDefaultLookAndFeel(nullptr);
    juce::MessageManagerLock lock;
    audioProcessor.broadcaster.removeChangeListener(this);
    audioProcessor.fileChangeBroadcaster.removeChangeListener(this);
    
#if JUCE_MAC
    if (usingNativeMenuBar) {
        menuBarModel.setMacMainMenu(nullptr);
    }
#endif
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

    if (!usingNativeMenuBar) {
        // add drop shadow to the menu bar
        auto ds = juce::DropShadow(juce::Colours::black, 5, juce::Point<int>(0, 0));
        ds.drawForRectangle(g, menuBar.getBounds());
    }

    // draw drop shadow around code editor if visible
    juce::SpinLock::ScopedLockType lock(audioProcessor.parsersLock);

    int originalIndex = audioProcessor.getCurrentFileIndex();
    int index = editingPerspective ? 0 : audioProcessor.getCurrentFileIndex() + 1;
    if ((originalIndex != -1 || editingPerspective) && index < codeEditors.size() && codeEditors[index]->isVisible()) {
        auto ds = juce::DropShadow(juce::Colours::black, 5, juce::Point<int>(0, 0));
        ds.drawForRectangle(g, codeEditors[index]->getBounds());
    }
}

void OscirenderAudioProcessorEditor::resized() {
    auto area = getLocalBounds();
    if (!usingNativeMenuBar) {
        menuBar.setBounds(area.removeFromTop(25));
    }
    
    area.removeFromTop(2);
    area.removeFromLeft(3);
    auto volumeArea = area.removeFromLeft(30);
    volume.setBounds(volumeArea.withSizeKeepingCentre(volumeArea.getWidth(), juce::jmin(volumeArea.getHeight(), 300)));
    area.removeFromLeft(3);
    bool editorVisible = false;

    juce::Component dummy;

    {
        juce::SpinLock::ScopedLockType lock(audioProcessor.parsersLock);

        int originalIndex = audioProcessor.getCurrentFileIndex();
        int index = editingPerspective ? 0 : audioProcessor.getCurrentFileIndex() + 1;
        if (originalIndex != -1 || editingPerspective) {
            if (codeEditors[index]->isVisible()) {
                editorVisible = true;

                juce::Component* columns[] = { &dummy, &resizerBar, codeEditors[index].get() };
                 
                // offsetting the y position by -1 and the height by +1 is a hack to fix a bug where the code editor
                // doesn't draw up to the edges of the menu bar above.
                layout.layOutComponents(columns, 3, area.getX(), area.getY() - 1, area.getWidth(), area.getHeight() + 1, false, true);
                auto dummyBounds = dummy.getBounds();
                collapseButton.setBounds(dummyBounds.removeFromRight(20));

                area = dummyBounds;
                
            } else {
                codeEditors[index]->setBounds(0, 0, 0, 0);
                resizerBar.setBounds(0, 0, 0, 0);
                collapseButton.setBounds(area.removeFromRight(20));
            }
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

    settings.setBounds(area);
    repaint();
}

void OscirenderAudioProcessorEditor::addCodeEditor(int index) {
    int originalIndex = index;
    index++;
    std::shared_ptr<juce::CodeDocument> codeDocument;
    std::shared_ptr<ErrorCodeEditorComponent> editor;

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
        editor = std::make_shared<ErrorCodeEditorComponent>(*codeDocument, tokeniser, audioProcessor, audioProcessor.getFileId(originalIndex));
    }
    
    codeDocuments.insert(codeDocuments.begin() + index, codeDocument);
    codeEditors.insert(codeEditors.begin() + index, editor);
    addChildComponent(*editor);
    // I need to disable accessibility otherwise it doesn't work! Appears to be a JUCE issue, very annoying!
    editor->setAccessible(false);
    // listen for changes to the code editor
    codeDocument->addListener(this);
    editor->setColourScheme(colourScheme);
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
        // used so that codeDocumentTextInserted and codeDocumentTextDeleted know whether the parserLock
        // is held by the message thread or not. We hold the lock in this function, but not when the
        // code document is updated by the user editing text. Since both functions are called by the
        // message thread, this is safe.
        updatingDocumentsWithParserLock = true;
        if (index == 0) {
            codeEditors[index]->loadContent(audioProcessor.perspectiveEffect->getCode());
        } else {
            codeEditors[index]->loadContent(juce::MemoryInputStream(*audioProcessor.getFileBlock(originalIndex), false).readEntireStreamAsString());
        }
        updatingDocumentsWithParserLock = false;
    }
    triggerAsyncUpdate();
}

// parsersLock MUST be locked before calling this function
void OscirenderAudioProcessorEditor::fileUpdated(juce::String fileName) {
    settings.fileUpdated(fileName);
    updateCodeEditor();
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

void OscirenderAudioProcessorEditor::editPerspectiveFunction(bool enable) {
    editingPerspective = enable;
    juce::SpinLock::ScopedLockType lock1(audioProcessor.parsersLock);
    juce::SpinLock::ScopedLockType lock2(audioProcessor.effectsLock);
    codeEditors[0]->setVisible(enable);
    updateCodeEditor();
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
    if (editingPerspective) {
        juce::String file = codeDocuments[0]->getAllContent();
        audioProcessor.perspectiveEffect->updateCode(file);
        audioProcessor.updateLuaValues();
    } else {
        int originalIndex = audioProcessor.getCurrentFileIndex();
        int index = audioProcessor.getCurrentFileIndex();
        index++;
        juce::String file = codeDocuments[index]->getAllContent();
        audioProcessor.updateFileBlock(originalIndex, std::make_shared<juce::MemoryBlock>(file.toRawUTF8(), file.getNumBytesAsUTF8() + 1));
    }
}

bool OscirenderAudioProcessorEditor::keyPressed(const juce::KeyPress& key) {
    bool consumeKey1 = true;
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
        } else if (key.getTextCharacter() == 'k') {
            if (numFiles > 1) {
                currentFile--;
                if (currentFile < 0) {
                    currentFile = numFiles - 1;
                }
                changedFile = true;
            }
        } else {
            consumeKey1 = false;
        }

        if (changedFile) {
            audioProcessor.changeCurrentFile(currentFile);
            fileUpdated(audioProcessor.getCurrentFileName());
        }
    }
    
    bool consumeKey2 = true;
    if (key.isKeyCode(juce::KeyPress::escapeKey)) {
        settings.disableMouseRotation();
    } else if (key.getModifiers().isCommandDown() && key.getModifiers().isShiftDown() && key.getKeyCode() == 'S') {
        saveProjectAs();
    } else if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'S') {
        saveProject();
    } else if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'O') {
        openProject();
    } else {
        consumeKey2 = false;
	}

    return consumeKey1 || consumeKey2;
}

void OscirenderAudioProcessorEditor::newProject() {
    // TODO: open a default project
}

void OscirenderAudioProcessorEditor::openProject() {
    chooser = std::make_unique<juce::FileChooser>("Load osci-render Project", juce::File::getSpecialLocation(juce::File::userHomeDirectory), "*.osci");
    auto flags = juce::FileBrowserComponent::openMode |
        juce::FileBrowserComponent::canSelectFiles;

    chooser->launchAsync(flags, [this](const juce::FileChooser& chooser) {
        auto file = chooser.getResult();
        if (file != juce::File()) {
            auto data = juce::MemoryBlock();
            if (file.loadFileAsData(data)) {
                audioProcessor.setStateInformation(data.getData(), data.getSize());
            }
            audioProcessor.currentProjectFile = file.getFullPathName();
            updateTitle();
        }
    });
}

void OscirenderAudioProcessorEditor::saveProject() {
    if (audioProcessor.currentProjectFile.isEmpty()) {
        saveProjectAs();
    } else {
        auto data = juce::MemoryBlock();
        audioProcessor.getStateInformation(data);
        auto file = juce::File(audioProcessor.currentProjectFile);
        file.create();
        file.replaceWithData(data.getData(), data.getSize());
        updateTitle();
    }
}

void OscirenderAudioProcessorEditor::saveProjectAs() {
    chooser = std::make_unique<juce::FileChooser>("Save osci-render Project", juce::File::getSpecialLocation(juce::File::userHomeDirectory), "*.osci");
    auto flags = juce::FileBrowserComponent::saveMode;

    chooser->launchAsync(flags, [this](const juce::FileChooser& chooser) {
        auto file = chooser.getResult();
        if (file != juce::File()) {
            audioProcessor.currentProjectFile = file.getFullPathName();
            saveProject();
        }
    });
}

void OscirenderAudioProcessorEditor::updateTitle() {
    juce::String title = "osci-render";
    if (!audioProcessor.currentProjectFile.isEmpty()) {
        title += " - " + audioProcessor.currentProjectFile;
    }
    getTopLevelComponent()->setName(title);
}

void OscirenderAudioProcessorEditor::openAudioSettings() {
    juce::StandalonePluginHolder* standalone = juce::StandalonePluginHolder::getInstance();
    standalone->showAudioSettingsDialog();
}

void OscirenderAudioProcessorEditor::resetToDefault() {
    juce::StandaloneFilterWindow* window = findParentComponentOfClass<juce::StandaloneFilterWindow>();
    if (window != nullptr) {
        window->resetToDefaultState();
    }
}

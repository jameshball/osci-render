#include "PluginProcessor.h"
#include "PluginEditor.h"

OscirenderAudioProcessorEditor::OscirenderAudioProcessorEditor(OscirenderAudioProcessor& p)
	: AudioProcessorEditor(&p), audioProcessor(p), collapseButton("Collapse", juce::Colours::white, juce::Colours::white, juce::Colours::white)
{
    addAndMakeVisible(effects);
    addAndMakeVisible(main);
    addChildComponent(lua);
    addChildComponent(obj);
    addChildComponent(txt);
    addAndMakeVisible(volume);

    menuBar.setModel(&menuBarModel);
    addAndMakeVisible(menuBar);

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

    colourScheme = lookAndFeel.getDefaultColourScheme();

    {
        juce::SpinLock::ScopedLockType lock(audioProcessor.parsersLock);
        initialiseCodeEditors();
    }

    {
        juce::MessageManagerLock lock;
        audioProcessor.broadcaster.addChangeListener(this);
    }

    if (audioProcessor.wrapperType == juce::AudioProcessor::WrapperType::wrapperType_Standalone) {
        if (juce::TopLevelWindow::getNumTopLevelWindows() == 1) {
            juce::TopLevelWindow* w = juce::TopLevelWindow::getTopLevelWindow(0);
            w->setColour(juce::ResizableWindow::backgroundColourId, Colours::veryDark);
        }
    }

    juce::Desktop::getInstance().setDefaultLookAndFeel(&lookAndFeel);
    setLookAndFeel(&lookAndFeel);

    setSize(1100, 750);
    setResizable(true, true);
    setResizeLimits(500, 400, 999999, 999999);
}

OscirenderAudioProcessorEditor::~OscirenderAudioProcessorEditor() {
    setLookAndFeel(nullptr);
    juce::Desktop::getInstance().setDefaultLookAndFeel(nullptr);
    juce::MessageManagerLock lock;
    audioProcessor.broadcaster.removeChangeListener(this);
}

// parsersLock must be held
void OscirenderAudioProcessorEditor::initialiseCodeEditors() {
    codeDocuments.clear();
    codeEditors.clear();
    // -1 is the perspective function
    addCodeEditor(-1);
    for (int i = 0; i < audioProcessor.numFiles(); i++) {
        addCodeEditor(i);
    }
    fileUpdated(audioProcessor.getCurrentFileName());
}

void OscirenderAudioProcessorEditor::paint(juce::Graphics& g) {
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));

    juce::DropShadow ds(juce::Colours::black, 10, juce::Point<int>(0, 0));
    ds.drawForRectangle(g, main.getBounds());
    ds.drawForRectangle(g, effects.getBounds());
    if (lua.isVisible()) {
        ds.drawForRectangle(g, lua.getBounds());
    }
    if (obj.isVisible()) {
        ds.drawForRectangle(g, obj.getBounds());
    }

    g.setColour(juce::Colours::white);
    g.setFont(15.0f);
}

void OscirenderAudioProcessorEditor::resized() {
    auto area = getLocalBounds();
    menuBar.setBounds(area.removeFromTop(25));
    area.removeFromTop(2);
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
    main.setBounds(area.reduced(5));
    if (lua.isVisible() || obj.isVisible() || txt.isVisible()) {
        auto altEffectsSection = effectsSection.removeFromBottom(juce::jmin(effectsSection.getHeight() / 2, txt.isVisible() ? 150 : 300));
        lua.setBounds(altEffectsSection.reduced(5));
        obj.setBounds(altEffectsSection.reduced(5));
        txt.setBounds(altEffectsSection.reduced(5));
    }
	effects.setBounds(effectsSection.reduced(5));

    repaint();
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
    txt.setVisible(false);
    if (fileName.isEmpty()) {
        // do nothing
    } else if (extension == ".lua") {
        lua.setVisible(true);
    } else if (extension == ".obj") {
		obj.setVisible(true);
    } else if (extension == ".txt") {
        txt.setVisible(true);
    }
    main.updateFileLabel();
    updateCodeEditor();
}

void OscirenderAudioProcessorEditor::handleAsyncUpdate() {
    resized();
}

void OscirenderAudioProcessorEditor::changeListenerCallback(juce::ChangeBroadcaster* source) {
    juce::SpinLock::ScopedLockType lock(audioProcessor.parsersLock);
    initialiseCodeEditors();
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
        obj.disableMouseRotation();
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
    auto flags = juce::FileBrowserComponent::openMode;

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

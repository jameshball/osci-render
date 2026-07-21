#include "PluginEditor.h"
#include "parser/FileFormatRegistry.h"

#include <cmath>
#include <memory>

#include <osci_standalone/osci_standalone.h>
#include "PluginProcessor.h"
#include "parser/FileParser.h"
#include "components/effects/EffectComponent.h"
#include "components/OverlayDialogHelpers.h"

namespace {
    constexpr double kDefaultCodeEditorMainPanelSize = -0.7;
    constexpr double kMinimumCodeEditorMainPanelFraction = 0.3;

    double sanitiseCodeEditorMainPanelSize(double preferredSize) {
        if (!std::isfinite(preferredSize) || preferredSize > -kMinimumCodeEditorMainPanelFraction) {
            return kDefaultCodeEditorMainPanelSize;
        }

        return preferredSize;
    }
}

void OscirenderAudioProcessorEditor::registerFileRemovedCallback() {
    juce::Component::SafePointer<OscirenderAudioProcessorEditor> safeThis(this);
    audioProcessor.setFileRemovedCallback([safeThis](int index) {
        if (safeThis == nullptr)
            return;

        safeThis->removeCodeEditor(index);
        safeThis->fileUpdated(safeThis->audioProcessor.getCurrentFileName());
        juce::MessageManager::callAsync([safeThis] {
            if (safeThis != nullptr)
                safeThis->resized();
        });
    });
}

OscirenderAudioProcessorEditor::OscirenderAudioProcessorEditor(OscirenderAudioProcessor& p) : CommonPluginEditor(p, "osci-render", "osci", 1100, 770), audioProcessor(p), collapseButton("Collapse", juce::Colours::white, juce::Colours::white, juce::Colours::white) {
    // Create timeline controllers for osci-render
    animationTimelineController = std::make_shared<AnimationTimelineController>(audioProcessor);
    audioTimelineController = std::make_shared<OscirenderAudioTimelineController>(audioProcessor);

    // Register the file removal callback
    registerFileRemovedCallback();

#if !OSCI_PREMIUM
    addAndMakeVisible(upgradeButton);
    upgradeButton.onClick = [this] {
        showPremiumSplashScreen();
    };
    upgradeButton.setColour(juce::TextButton::buttonColourId, osci::Colours::accentColor());
    upgradeButton.setColour(juce::TextButton::textColourOffId, osci::Colours::veryDark());
#else
    addChildComponent(mtsEspLabel);
    mtsEspLabel.setFont(juce::Font(11.0f));
    mtsEspLabel.setColour(juce::Label::textColourId, juce::Colours::limegreen);
    mtsEspLabel.setJustificationType(juce::Justification::centredRight);
#endif

    console.setButtonSvgSources(juce::String(BinaryData::delete_svg), juce::String(BinaryData::pause_svg));
    console.setButtonColours(juce::Colours::red, juce::Colours::white, osci::Colours::accentColor());
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
        setCodeEditorVisible(std::nullopt);
    };

    juce::Path path;
    path.addTriangle(0.0f, 0.5f, 1.0f, 1.0f, 1.0f, 0.0f);
    collapseButton.setShape(path, false, true, true);
    collapseButton.setMouseCursor(juce::MouseCursor::PointingHandCursor);

    {
        juce::SpinLock::ScopedLockType lock(audioProcessor.parsersLock);
        initialiseCodeEditors();
    }

    {
        juce::MessageManagerLock lock;
        audioProcessor.fileChangeBroadcaster.addChangeListener(this);
        audioProcessor.broadcaster.addChangeListener(this);
    }

    double codeEditorLayoutPreferredSize = sanitiseCodeEditorMainPanelSize(std::any_cast<double>(audioProcessor.getProperty("codeEditorLayoutPreferredSize", kDefaultCodeEditorMainPanelSize)));
    double luaLayoutPreferredSize = std::any_cast<double>(audioProcessor.getProperty("luaLayoutPreferredSize", -0.7));

    layout.setItemLayout(0, -0.3, -1.0, codeEditorLayoutPreferredSize);
    layout.setItemLayout(1, RESIZER_BAR_SIZE, RESIZER_BAR_SIZE, RESIZER_BAR_SIZE);
    layout.setItemLayout(2, -0.0, -1.0, -(1.0 + codeEditorLayoutPreferredSize));

    addAndMakeVisible(settings);
    addAndMakeVisible(resizerBar);

    luaLayout.setItemLayout(0, -0.3, -1.0, luaLayoutPreferredSize);
    luaLayout.setItemLayout(1, RESIZER_BAR_SIZE, RESIZER_BAR_SIZE, RESIZER_BAR_SIZE);
    luaLayout.setItemLayout(2, -0.1, -1.0, -(1.0 + luaLayoutPreferredSize));

    addAndMakeVisible(lua);
    addAndMakeVisible(luaResizerBar);
    addChildComponent(txtFont);
    addAndMakeVisible(visualiser);

    visualiser.openSettings = [this] {
        openVisualiserSettings();
    };

    visualiser.closeSettings = [this] {
        visualiserSettingsWindow.setVisible(false);
    };

#if !OSCI_PREMIUM
    visualiserSettings.onUpgradeRequested = [this] {
        showPremiumSplashScreen();
    };
#endif

    visualiserSettings.wireModulation(audioProcessor);

#if JUCE_WINDOWS
    // if not standalone, use native title bar for compatibility with DAWs
    visualiserSettingsWindow.setUsingNativeTitleBar(processor.wrapperType == juce::AudioProcessor::WrapperType::wrapperType_Standalone);
#elif JUCE_MAC
    visualiserSettingsWindow.setUsingNativeTitleBar(true);
#endif

    visualiserSettingsWindow.addKeyListener(this);

    initialiseMenuBar(model);
    startTimer(100);
}

OscirenderAudioProcessorEditor::~OscirenderAudioProcessorEditor() {
    stopTextureInput();
    stopTimer();
    visualiserSettingsWindow.removeKeyListener(this);

    for (auto& editorModel : codeModels) {
        if (editorModel != nullptr) {
            audioProcessor.removeErrorListener(editorModel.get());
        }
    }

    // Clear the file removal callback
    audioProcessor.setFileRemovedCallback(nullptr);

    menuBar.setModel(nullptr);
    juce::MessageManagerLock lock;
    audioProcessor.broadcaster.removeChangeListener(this);
    audioProcessor.fileChangeBroadcaster.removeChangeListener(this);
}

void OscirenderAudioProcessorEditor::setTextureInputSource(osci::texture::SourceInfo source) {
#if !OSCI_PREMIUM
    juce::ignoreUnused(source);
    showPremiumSplashScreen();
    return;
#endif

    const osci::texture::BackendStatus status = osci::texture::getOpenGLBackendStatus();
    if (!status.isAvailable()) {
        const juce::String message = status.message.isNotEmpty()
            ? status.message
            : "Texture input is not available in this build.";
        osci::showOverlayMessage(*this, "Texture Input", message, osci::ErrorOverlay::Icon::None);
        return;
    }

    if (source.displayName.trim().isEmpty() && source.opaqueId.trim().isEmpty()) {
        osci::showOverlayMessage(*this, "Texture Input", "The selected texture source is no longer available.");
        return;
    }

    stopTextureInput();

    juce::Component::SafePointer<OscirenderAudioProcessorEditor> safeThis(this);
    auto grabber = std::make_unique<osci::texture::OpenGLTextureFrameGrabber>(std::move(source));
    auto* grabberPtr = grabber.get();
    grabber->inputStarted = [safeThis](juce::String sourceName, int width, int height) {
        if (safeThis != nullptr) {
            safeThis->audioProcessor.startTextureInput(std::move(sourceName), width, height);
        }
    };
    grabber->inputStopped = [safeThis, grabberPtr] {
        if (safeThis != nullptr && safeThis->textureInputFrameGrabber.get() == grabberPtr) {
            safeThis->audioProcessor.stopTextureInput();
            safeThis->textureInputFrameGrabber = nullptr;
        }
    };
    grabber->inputFailed = [safeThis, grabberPtr](juce::String message) {
        if (safeThis == nullptr) {
            return;
        }

        if (safeThis->textureInputFrameGrabber.get() != grabberPtr) {
            return;
        }

        safeThis->audioProcessor.stopTextureInput();
        safeThis->textureInputFrameGrabber = nullptr;
        osci::showOverlayMessage(*safeThis.getComponent(), "Texture Input", message);
    };
    grabber->frameReady = [processor = &audioProcessor](const std::vector<std::uint8_t>& rgba, int width, int height, bool verticallyFlipped) {
        processor->updateTextureInputFrame(rgba, width, height, verticallyFlipped);
    };

    textureInputFrameGrabber = std::move(grabber);
}

void OscirenderAudioProcessorEditor::openProject(const juce::File& file) {
    const int programChangeChannel = audioProcessor.getProgramChangeChannel();
    if (file != juce::File()) {
        stopTextureInput();
    }

    CommonPluginEditor::openProject(file);
    audioProcessor.setProgramChangeChannel(programChangeChannel);
}

void OscirenderAudioProcessorEditor::openProject() {
    CommonPluginEditor::openProject();
}

void OscirenderAudioProcessorEditor::resetToDefault() {
    const int programChangeChannel = audioProcessor.getProgramChangeChannel();
    stopTextureInput();
    CommonPluginEditor::resetToDefault();
    audioProcessor.setProgramChangeChannel(programChangeChannel);
}

void OscirenderAudioProcessorEditor::stopTextureInput() {
    if (textureInputFrameGrabber != nullptr) {
        textureInputFrameGrabber->stop();
        textureInputFrameGrabber = nullptr;
    }

    audioProcessor.stopTextureInput();
}

bool OscirenderAudioProcessorEditor::isTextureInputActive() const {
    return textureInputFrameGrabber != nullptr && textureInputFrameGrabber->isActive();
}

juce::String OscirenderAudioProcessorEditor::getTextureInputName() const {
    if (textureInputFrameGrabber != nullptr && textureInputFrameGrabber->isActive()) {
        return textureInputFrameGrabber->getSourceName();
    }

    return audioProcessor.getTextureInputName();
}

void OscirenderAudioProcessorEditor::setCodeEditorVisible(std::optional<bool> visible) {
    juce::SpinLock::ScopedLockType lock(audioProcessor.parsersLock);
    const auto originalIndex = audioProcessor.getCurrentFileIndex();
    const int index = editingCustomFunction ? 0 : static_cast<int>(originalIndex.value_or(0)) + 1;
    if (originalIndex.has_value() || editingCustomFunction) {
        codeEditors[index]->setVisible(visible.has_value() ? visible.value() : !codeEditors[index]->isVisible());
        updateCodeEditor(!editingCustomFunction && isBinaryFile(audioProcessor.getCurrentFileName()));
    }
}

bool OscirenderAudioProcessorEditor::isInterestedInFileDrag(const juce::StringArray& files) {
    if (files.size() != 1) {
        return false;
    }
    juce::File file(files[0]);
    return osci::files::isSupportedSource(file) || osci::files::isOsciProject(file);
}

void OscirenderAudioProcessorEditor::filesDropped(const juce::StringArray& files, int x, int y) {
    if (files.size() != 1) {
        return;
    }
    juce::File file(files[0]);

    if (osci::files::isOsciProject(file)) {
        openProject(file);
    } else {
        stopTextureInput();

        juce::SpinLock::ScopedLockType parsersLock(audioProcessor.parsersLock);
        juce::SpinLock::ScopedLockType effectsLock(audioProcessor.effectsLock);

        audioProcessor.addFile(file);
        const auto currentFile = audioProcessor.getCurrentFileIndex();
        if (currentFile.has_value()) {
            addCodeEditor(static_cast<int>(*currentFile));
        }
        fileUpdated(audioProcessor.getCurrentFileName());
    }
}

void OscirenderAudioProcessorEditor::editFile(int index) {
    juce::String fileName;
    {
        juce::SpinLock::ScopedLockType lock(audioProcessor.parsersLock);
        if (index < 0 || index >= audioProcessor.numFiles()) {
            return;
        }
        fileName = audioProcessor.getFileName(index);
    }

    if (isBinaryFile(fileName)) {
        return;
    }

    if (editingCustomFunction) {
        editCustomFunction(false);
    }
    audioProcessor.selectFile(index);
    setCodeEditorVisible(true);
}

juce::String OscirenderAudioProcessorEditor::renameFile(int index, juce::String newName) {
    const juce::String renamedFile = audioProcessor.renameFile(index, std::move(newName));
    if (renamedFile.isEmpty()) {
        return {};
    }

    juce::SpinLock::ScopedLockType lock(audioProcessor.parsersLock);
    const int modelIndex = index + 1;
    if (modelIndex >= 0 && modelIndex < static_cast<int>(codeModels.size())) {
        codeModels[(size_t)modelIndex]->setDisplayName(renamedFile);
    }
    if (audioProcessor.getCurrentFileIndex() == static_cast<std::size_t>(index)) {
        fileUpdated(renamedFile);
    }
    return renamedFile;
}

void OscirenderAudioProcessorEditor::duplicateFile(int index) {
    const int modelIndex = index + 1;
    if (modelIndex >= 0 && modelIndex < static_cast<int>(codeModels.size())) {
        codeModels[(size_t)modelIndex]->flushPendingEdit();
    }

    const int duplicateIndex = audioProcessor.duplicateFile(index);
    if (duplicateIndex < 0) {
        return;
    }

    juce::String duplicateName;
    {
        juce::SpinLock::ScopedLockType lock(audioProcessor.parsersLock);
        addCodeEditor(duplicateIndex);
        duplicateName = audioProcessor.getFileName(duplicateIndex);
        fileUpdated(duplicateName);
    }
}

void OscirenderAudioProcessorEditor::exportFile(int index) {
    const int modelIndex = index + 1;
    if (modelIndex >= 0 && modelIndex < static_cast<int>(codeModels.size())) {
        codeModels[(size_t)modelIndex]->flushPendingEdit();
    }

    juce::String fileName;
    std::shared_ptr<juce::MemoryBlock> fileData;
    {
        juce::SpinLock::ScopedLockType lock(audioProcessor.parsersLock);
        if (index < 0 || index >= audioProcessor.numFiles()) {
            return;
        }
        fileName = audioProcessor.getFileName(index);
        fileData = std::make_shared<juce::MemoryBlock>(*audioProcessor.getFileBlock(index));
    }

    const juce::String extension = fileName.fromLastOccurrenceOf(".", true, false);
    const juce::String wildcard = extension.isNotEmpty() ? "*" + extension : "*";
    const juce::File suggestedFile = audioProcessor.getLastOpenedDirectory().getChildFile(fileName);
    chooser = std::make_unique<juce::FileChooser>("Export file", suggestedFile, wildcard);
    const int flags = juce::FileBrowserComponent::saveMode
        | juce::FileBrowserComponent::canSelectFiles
        | juce::FileBrowserComponent::warnAboutOverwriting;

    juce::Component::SafePointer<OscirenderAudioProcessorEditor> safeThis(this);
    chooser->launchAsync(flags, [safeThis, fileData, extension](const juce::FileChooser& fileChooser) {
        if (safeThis == nullptr) {
            return;
        }

        juce::File outputFile = fileChooser.getResult();
        if (outputFile == juce::File()) {
            return;
        }
        if (outputFile.getFileExtension().isEmpty() && extension.isNotEmpty()) {
            outputFile = outputFile.withFileExtension(extension);
        }

        safeThis->audioProcessor.setLastOpenedDirectory(outputFile.getParentDirectory());
        if (!outputFile.replaceWithData(fileData->getData(), fileData->getSize())) {
            osci::showOverlayMessage(*safeThis.getComponent(), "Export file", "The file could not be written to the selected location.");
        }
    });
}

void OscirenderAudioProcessorEditor::removeFile(int index) {
    juce::SpinLock::ScopedLockType parserLock(audioProcessor.parsersLock);
    juce::SpinLock::ScopedLockType effectsLock(audioProcessor.effectsLock);
    if (index >= 0 && index < audioProcessor.numFiles()) {
        audioProcessor.removeFile(index);
    }
}

// Anything with these extensions will not be opened in the code editor
bool OscirenderAudioProcessorEditor::isBinaryFile(juce::String name) {
    return !osci::files::isCodeEditable(name);
}

// parsersLock must be held
void OscirenderAudioProcessorEditor::initialiseCodeEditors() {
    for (auto& editorModel : codeModels) {
        if (editorModel != nullptr) {
            audioProcessor.removeErrorListener(editorModel.get());
        }
    }

    codeEditors.clear();
    codeModels.clear();
    // -1 is the perspective function
    addCodeEditor(-1);
    for (int i = 0; i < audioProcessor.numFiles(); i++) {
        addCodeEditor(i);
    }
    bool codeEditorVisible = std::any_cast<bool>(audioProcessor.getProperty("codeEditorVisible", false));
    fileUpdated(audioProcessor.getCurrentFileName(), codeEditorVisible);
}

void OscirenderAudioProcessorEditor::dragOperationEnded(const juce::DragAndDropTarget::SourceDetails&) {
    EffectComponent::modAnyDragActive.store(false, std::memory_order_relaxed);
    repaint();
}

void OscirenderAudioProcessorEditor::paint(juce::Graphics& g) {
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
    if (!usingNativeMenuBar) {
        g.setColour(getLookAndFeel().findColour(juce::TextButton::buttonColourId));
        g.fillRect(0, 0, getWidth(), kMenuBarHeight);
    }
}

void OscirenderAudioProcessorEditor::resized() {
    CommonPluginEditor::resized();

    auto area = getLocalBounds();

    if (audioProcessor.visualiserParameters.visualiserFullScreen->getBoolValue()) {
        visualiser.setBounds(area);
        undoRedoControls.setVisible(false);
        betaUpdatesButton.setVisible(false);
        return;
    }

    undoRedoControls.setVisible(true);

    if (!usingNativeMenuBar) {
        auto topBar = area.removeFromTop(kMenuBarHeight);
#if !OSCI_PREMIUM
        upgradeButton.setBounds(topBar.removeFromRight(juce::jmin(150, topBar.getWidth())).reduced(2, 2));
#endif
    layoutBetaUpdatesButton(topBar);
        // Menu bar gets priority — allocate from the left first
        menuBar.setBounds(topBar.removeFromLeft(juce::jmin(kMenuBarMaxWidth, topBar.getWidth())));
        // Right-side items share whatever remains, clamped to available width
        undoRedoControls.setBounds(topBar.removeFromRight(juce::jmin(undoRedoControls.getPreferredWidth(), topBar.getWidth())));
#if OSCI_PREMIUM
        if (mtsEspLabel.isVisible())
            mtsEspLabel.setBounds(topBar.removeFromRight(juce::jmin(150, topBar.getWidth())).reduced(2, 2));
#endif
    }

    bool editorVisible = false;

    {
        juce::SpinLock::ScopedLockType lock(audioProcessor.parsersLock);

        const auto originalIndex = audioProcessor.getCurrentFileIndex();
        const int index = editingCustomFunction ? 0 : static_cast<int>(originalIndex.value_or(0)) + 1;

        bool ableToEditFile = (originalIndex.has_value() && !isBinaryFile(audioProcessor.getCurrentFileName())) || editingCustomFunction;
        bool fileOpen = false;
        bool luaFileOpen = false;

        if (ableToEditFile) {
            if (index < codeEditors.size() && codeEditors[index]->isVisible()) {
                editorVisible = true;

                juce::Component dummy;
                juce::Component dummy2;
                juce::Component dummy3;

                juce::Component* columns[] = {&dummy, &resizerBar, &dummy2};

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
                if (originalIndex.has_value()) {
                    extension = audioProcessor.getFileName(static_cast<int>(*originalIndex)).fromLastOccurrenceOf(".", true, false);
                }

                bool isTxtFile = extension == ".txt";
                txtFont.setVisible(isTxtFile);

                if (editingCustomFunction || extension == ".lua") {
                    juce::Component* rows[] = {&dummy3, &luaResizerBar, &lua};
                    luaLayout.layOutComponents(rows, 3, dummy2Bounds.getX(), dummy2Bounds.getY(), dummy2Bounds.getWidth(), dummy2Bounds.getHeight(), true, true);
                    auto dummy3Bounds = dummy3.getBounds();
                    console.setBounds(dummy3Bounds.removeFromBottom(console.getConsoleOpen() ? 200 : 30));
                    dummy3Bounds.removeFromBottom(RESIZER_BAR_SIZE);
                    codeEditors[index]->setBounds(dummy3Bounds);
                    luaFileOpen = true;
                } else {
                    auto editorBounds = dummy2Bounds;
                    if (isTxtFile) {
                        txtFont.setBounds(editorBounds.removeFromTop(30));
                        editorBounds.removeFromTop(5); // Add small gap
                    }
                    codeEditors[index]->setBounds(editorBounds);
                }

                fileOpen = true;
            } else {
                collapseButton.setBounds(area.removeFromRight(20));
            }
        }

        collapseButton.setVisible(ableToEditFile);

        if (index < codeEditors.size()) {
            codeEditors[index]->setVisible(fileOpen);
        }
        resizerBar.setVisible(fileOpen);

        console.setVisible(luaFileOpen);
        luaResizerBar.setVisible(luaFileOpen);
        lua.setVisible(luaFileOpen);

        // Hide txtFont if code editor is not visible
        if (!fileOpen) {
            txtFont.setVisible(false);
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

    if (editorVisible) {
        audioProcessor.setProperty("codeEditorLayoutPreferredSize", sanitiseCodeEditorMainPanelSize(layout.getItemCurrentRelativeSize(0)));
    }
    audioProcessor.setProperty("luaLayoutPreferredSize", luaLayout.getItemCurrentRelativeSize(0));

    repaint();
}

void OscirenderAudioProcessorEditor::addCodeEditor(int index) {
    int originalIndex = index;
    index++;
    std::shared_ptr<osci::LuaScriptEditorModel> editorModel;
    osci::LuaScriptEditorComponent::Options options;
    options.showTitle = true;
    options.showConsole = false;
    options.legacyGroupChrome = true;
    options.helpButtonSvg = juce::String(BinaryData::help_svg);
    options.resetButtonSvg = juce::String(BinaryData::refresh_svg);
    options.buttonColour = juce::Colours::white;
    options.buttonOnColour = juce::Colours::white;

    if (index == 0) {
        editorModel = std::make_shared<osci::LuaScriptEditorModel>(LuaEffectState::UNIQUE_ID,
                                                                   LuaEffectState::FILE_NAME,
                                                                   audioProcessor.luaEffectState != nullptr ? audioProcessor.luaEffectState->getCode() : "return { x, y, z }",
                                                                   true,
                                                                   250);
        options.showHelpButton = true;
        options.showResetButton = true;
    } else {
        juce::String extension = audioProcessor.getFileName(originalIndex).fromLastOccurrenceOf(".", true, false);
        const bool luaFile = extension == ".lua";
        const bool txtFile = extension == ".txt";
        editorModel = std::make_shared<osci::LuaScriptEditorModel>(audioProcessor.getFileId(originalIndex),
                                                                   audioProcessor.getFileName(originalIndex),
                                                                   juce::MemoryInputStream(*audioProcessor.getFileBlock(originalIndex), false).readEntireStreamAsString(),
                                                                   luaFile,
                                                                   txtFile ? 0 : 250);
        if (extension == ".svg") {
            options.externalTokeniser = &xmlTokeniser;
        } else {
            options.useLuaTokeniser = luaFile;
        }
        options.showHelpButton = luaFile;
        options.showResetButton = luaFile;
    }

    editorModel->onCodeCommitted = [this, weakModel = std::weak_ptr<osci::LuaScriptEditorModel>(editorModel)](const juce::String&) {
        if (auto model = weakModel.lock()) {
            commitCodeModel(*model);
        }
    };
    editorModel->onHelpRequested = [this] {
        showLuaDocumentation();
    };
    editorModel->onResetRequested = [this, weakModel = std::weak_ptr<osci::LuaScriptEditorModel>(editorModel)] {
        auto model = weakModel.lock();
        if (model == nullptr) {
            return;
        }

        juce::SpinLock::ScopedLockType parserLock(audioProcessor.parsersLock);
        auto parser = audioProcessor.getCurrentFileParser();
        if (parser != nullptr) {
            auto luaParser = parser->getLua();
            if (luaParser != nullptr) {
                luaParser->forgetAllStates();
            }
        }
        if (audioProcessor.luaEffectState != nullptr && audioProcessor.luaEffectState->parser != nullptr) {
            audioProcessor.luaEffectState->parser->forgetAllStates();
        }
    };

    auto editor = std::make_shared<osci::LuaScriptEditorComponent>(*editorModel, options);
    audioProcessor.addErrorListener(editorModel.get());

    codeModels.insert(codeModels.begin() + index, editorModel);
    codeEditors.insert(codeEditors.begin() + index, editor);
    addChildComponent(*editor);
    editor->setAccessible(false);
}

void OscirenderAudioProcessorEditor::removeCodeEditor(int index) {
    index++;
    if (index >= 0 && index < codeModels.size()) {
        audioProcessor.removeErrorListener(codeModels[index].get());
    }
    codeEditors.erase(codeEditors.begin() + index);
    codeModels.erase(codeModels.begin() + index);
}

// parsersLock AND effectsLock must be locked before calling this function
void OscirenderAudioProcessorEditor::updateCodeEditor(bool binaryFile, bool shouldOpenEditor) {
    // While editing the custom Lua effect, we should not treat the currently-selected file's
    // extension as a reason to close the editor panel (the custom editor is always valid).
    binaryFile = binaryFile && !editingCustomFunction;

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
        const auto originalIndex = audioProcessor.getCurrentFileIndex();
        const int index = editingCustomFunction ? 0 : static_cast<int>(originalIndex.value_or(0)) + 1;
        if ((originalIndex.has_value() || editingCustomFunction) && visible) {
            for (int i = 0; i < codeEditors.size(); i++) {
                codeEditors[i]->setVisible(false);
            }
            codeEditors[index]->setVisible(true);
            if (index == 0) {
                codeModels[index]->replaceCodeFromHost(audioProcessor.luaEffectState->getCode());
            } else {
                codeModels[index]->replaceCodeFromHost(juce::MemoryInputStream(*audioProcessor.getFileBlock(static_cast<int>(*originalIndex)), false).readEntireStreamAsString());
            }
        }
    }

    audioProcessor.setProperty("codeEditorVisible", visible);

    triggerAsyncUpdate();
}

// parsersLock MUST be locked before calling this function
void OscirenderAudioProcessorEditor::fileUpdated(juce::String fileName, bool shouldOpenEditor) {
    CommonPluginEditor::fileUpdated(fileName);
    settings.fileUpdated(fileName);
    updateCodeEditor(isBinaryFile(fileName), shouldOpenEditor);
    updateTimelineController();
}

void OscirenderAudioProcessorEditor::handleAsyncUpdate() {
    resized();
}

void OscirenderAudioProcessorEditor::changeListenerCallback(juce::ChangeBroadcaster* source) {
    if (source == &audioProcessor.broadcaster) {
        {
            juce::SpinLock::ScopedLockType parsersLock(audioProcessor.parsersLock);
            initialiseCodeEditors();
            settings.update();
        }
        resized();
        repaint();
    } else if (source == &audioProcessor.fileChangeBroadcaster) {
        juce::SpinLock::ScopedLockType parsersLock(audioProcessor.parsersLock);
        // Triggered when the processor changes the current file (e.g. Blender or automation).
        fileUpdated(audioProcessor.getCurrentFileName());
    }
}

void OscirenderAudioProcessorEditor::toggleLayout(juce::StretchableLayoutManager& layout, double prefSize) {
    double minSize, maxSize, preferredSize;
    double otherMinSize, otherMaxSize, otherPreferredSize;
    layout.getItemLayout(2, minSize, maxSize, preferredSize);
    layout.getItemLayout(0, otherMinSize, otherMaxSize, otherPreferredSize);

    if (layout.getItemCurrentAbsoluteSize(2) <= CLOSED_PREF_SIZE) {
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
    if (enable) {
        // Record whether the code editor was open before entering custom-function edit mode.
        // We'll restore this state when exiting.
        codeEditorWasVisibleBeforeEditingCustomFunction = std::any_cast<bool>(audioProcessor.getProperty("codeEditorVisible", false));
    }

    editingCustomFunction = enable;
    juce::SpinLock::ScopedLockType lock1(audioProcessor.parsersLock);
    juce::SpinLock::ScopedLockType lock2(audioProcessor.effectsLock);

    const auto currentFileName = audioProcessor.getCurrentFileName();
    const bool binaryFile = !editingCustomFunction && isBinaryFile(currentFileName);

    // When disabling the pencil icon, don't close the editor if it was already open.
    // Instead, switch back to the currently-open file (unless it's a binary file).
    const bool shouldOpenEditor = enable || (codeEditorWasVisibleBeforeEditingCustomFunction && !binaryFile);

    codeEditors[0]->setVisible(enable);
    updateCodeEditor(binaryFile, shouldOpenEditor);
}

void OscirenderAudioProcessorEditor::commitCodeModel(osci::LuaScriptEditorModel& model) {
    juce::SpinLock::ScopedLockType parserLock(audioProcessor.parsersLock);
    juce::SpinLock::ScopedLockType effectsLock(audioProcessor.effectsLock);

    const auto code = model.getCode();
    if (model.getScriptId() == LuaEffectState::UNIQUE_ID) {
        if (audioProcessor.luaEffectState != nullptr) {
            audioProcessor.luaEffectState->updateCode(code);
        }
        return;
    }

    for (int i = 0; i < audioProcessor.numFiles(); i++) {
        if (audioProcessor.getFileId(i) == model.getScriptId()) {
            audioProcessor.updateFileBlock(i, std::make_shared<juce::MemoryBlock>(code.toRawUTF8(), code.getNumBytesAsUTF8() + 1));
            return;
        }
    }
}

std::shared_ptr<osci::LuaScriptEditorModel> OscirenderAudioProcessorEditor::getVisibleLuaEditorModel() const {
    for (int i = 0; i < codeEditors.size() && i < codeModels.size(); i++) {
        if (codeEditors[i]->isVisible()) {
            return codeModels[i];
        }
    }

    return nullptr;
}

bool OscirenderAudioProcessorEditor::keyPressed(const juce::KeyPress& key) {
    bool consumeKey = false;
    const juce::juce_wchar textCharacter = key.getTextCharacter();
    if ((textCharacter == 'j' || textCharacter == 'k') && (isTextureInputActive() || audioProcessor.isTextureInputActive())) {
        stopTextureInput();
    }

    int targetFile = -1;
    {
        juce::SpinLock::ScopedLockType parserLock(audioProcessor.parsersLock);
        int numFiles = audioProcessor.numFiles();
        const auto selectedFile = audioProcessor.getCurrentFileIndex();
        int currentFile = selectedFile.has_value() ? static_cast<int>(*selectedFile) : -1;

        if (textCharacter == 'j') {
            if (numFiles > 1) {
                currentFile++;
                if (currentFile == numFiles) {
                    currentFile = 0;
                }
                targetFile = currentFile;
            }
            consumeKey = true;
        } else if (textCharacter == 'k') {
            if (numFiles > 1) {
                currentFile--;
                if (currentFile < 0) {
                    currentFile = numFiles - 1;
                }
                targetFile = currentFile;
            }
            consumeKey = true;
        }
    }

    if (targetFile >= 0) {
        audioProcessor.selectFile(targetFile);
    }

    if (CommonPluginEditor::keyPressed(key))
        return true;

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

void OscirenderAudioProcessorEditor::openVisualiserSettings() {
    visualiserSettingsWindow.setVisible(true);
    visualiserSettingsWindow.toFront(true);
}

void OscirenderAudioProcessorEditor::openRecordingSettings() {
#if OSCI_PREMIUM
    CommonPluginEditor::openRecordingSettings();
#else
    showPremiumSplashScreen();
#endif
}

void OscirenderAudioProcessorEditor::showPremiumSplashScreen() {
#if !OSCI_PREMIUM
    if (findActiveOverlay<SplashScreenComponent>() != nullptr)
        return;

    auto splash = std::make_unique<SplashScreenComponent>();
    splash->onUpgradeClicked = [] {
        juce::URL("https://osci-render.com/#purchase").launchInDefaultBrowser();
    };
    showOverlay(std::move(splash));
#endif
}

void OscirenderAudioProcessorEditor::timerCallback() {
#if OSCI_PREMIUM
    auto mtsEspDisplayText = [this]() -> juce::String {
        auto scaleName = audioProcessor.getMtsEspScaleName();
        if (scaleName.isNotEmpty())
            return "MTS-ESP: " + scaleName;
        return "MTS-ESP Connected";
    };

    bool connected = audioProcessor.isMtsEspConnected();
    if (connected != mtsEspLabel.isVisible()) {
        mtsEspLabel.setVisible(connected);
        if (connected)
            mtsEspLabel.setText(mtsEspDisplayText(), juce::dontSendNotification);
        resized();
    } else if (connected) {
        juce::String newText = mtsEspDisplayText();
        if (mtsEspLabel.getText() != newText)
            mtsEspLabel.setText(newText, juce::dontSendNotification);
    }
#endif
}

void OscirenderAudioProcessorEditor::showLuaDocumentation() {
    if (findActiveOverlay<LuaDocumentationComponent>() != nullptr)
        return;

    showOverlay(std::make_unique<LuaDocumentationComponent>());
}

void OscirenderAudioProcessorEditor::updateTimelineController() {
    std::shared_ptr<TimelineController> controller = nullptr;

    const auto currentFileIndex = audioProcessor.getCurrentFileIndex();
    if (currentFileIndex.has_value() && audioProcessor.parsers[*currentFileIndex] != nullptr) {
        auto parser = audioProcessor.parsers[*currentFileIndex];

        // Check if it's an animatable file (gpla, gif, video)
        if (parser->isAnimatable) {
            controller = animationTimelineController;
        }
        // Check if it's an audio file (FileParser contains a WavParser)
        else if (parser->getWav() != nullptr) {
            controller = audioTimelineController;
        }
    }

    visualiser.setTimelineController(controller);
}

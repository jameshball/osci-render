#include "PluginEditor.h"
#include "parser/FileFormatRegistry.h"

#include <cmath>
#include <memory>

#include <osci_standalone/osci_standalone.h>
#include "PluginProcessor.h"
#include "parser/FileParser.h"
#include "components/effects/EffectComponent.h"
#include "components/OverlayDialogHelpers.h"
#if OSCI_PREMIUM
#include "components/ExternalLaserLinkComponent.h"
#endif

namespace {
    constexpr double kDefaultCodeEditorMainPanelSize = -0.7;
    constexpr double kMinimumCodeEditorMainPanelFraction = 0.3;

    double sanitiseCodeEditorMainPanelSize(double preferredSize) {
        if (!std::isfinite(preferredSize) || preferredSize > -kMinimumCodeEditorMainPanelFraction) {
            return kDefaultCodeEditorMainPanelSize;
        }

        return preferredSize;
    }

    bool isBinaryFile(const juce::String& name) {
        return !osci::files::isCodeEditable(name);
    }
}

void OscirenderAudioProcessorEditor::registerFileRemovedCallback() {
    juce::Component::SafePointer<OscirenderAudioProcessorEditor> safeThis(this);
    audioProcessor.getFileController().setFileRemovedCallback([safeThis](int index) {
        if (safeThis == nullptr)
            return;

        safeThis->removeCodeEditor(index);
        safeThis->refreshFileUiLocked(safeThis->audioProcessor.getFileController().getCurrentFileName(), false);
        juce::MessageManager::callAsync([safeThis] {
            if (safeThis != nullptr) {
                safeThis->updateTimelineController();
                safeThis->resized();
            }
        });
    });
}

OscirenderAudioProcessorEditor::OscirenderAudioProcessorEditor(OscirenderAudioProcessor& p) : OscilloscopePluginEditorBase(p, "osci-render", "osci", 1100, 770), audioProcessor(p), collapseButton("Collapse", juce::Colours::white, juce::Colours::white, juce::Colours::white) {
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
        juce::SpinLock::ScopedLockType lock(audioProcessor.getFileController().lock);
        initialiseCodeEditors();
    }
    updateTimelineController();

    {
        juce::MessageManagerLock lock;
        audioProcessor.getFileController().addChangeListener(this);
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

#if OSCI_PREMIUM
    visualiser.openLaserWorkspace = [this] {
        openLaserWorkspace();
    };
#endif

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
    audioProcessor.getFileController().setFileRemovedCallback(nullptr);

    menuBar.setModel(nullptr);
    juce::MessageManagerLock lock;
    audioProcessor.broadcaster.removeChangeListener(this);
    audioProcessor.getFileController().removeChangeListener(this);
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
            safeThis->audioProcessor.getFileController().startTextureInput(std::move(sourceName), width, height);
        }
    };
    grabber->inputStopped = [safeThis, grabberPtr] {
        if (safeThis != nullptr && safeThis->textureInputFrameGrabber.get() == grabberPtr) {
            safeThis->audioProcessor.getFileController().stopTextureInput();
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

        safeThis->audioProcessor.getFileController().stopTextureInput();
        safeThis->textureInputFrameGrabber = nullptr;
        osci::showOverlayMessage(*safeThis.getComponent(), "Texture Input", message);
    };
    grabber->frameReady = [processor = &audioProcessor](const std::vector<std::uint8_t>& rgba, int width, int height, bool verticallyFlipped) {
        processor->getFileController().updateTextureInputFrame(rgba, width, height, verticallyFlipped);
    };

    textureInputFrameGrabber = std::move(grabber);
}

void OscirenderAudioProcessorEditor::openProject(const juce::File& file) {
    if (file != juce::File()) {
        stopTextureInput();
    }

    OscilloscopePluginEditorBase::openProject(file);
}

void OscirenderAudioProcessorEditor::openProject() {
    OscilloscopePluginEditorBase::openProject();
}

void OscirenderAudioProcessorEditor::resetToDefault() {
    stopTextureInput();
    OscilloscopePluginEditorBase::resetToDefault();
}

void OscirenderAudioProcessorEditor::stopTextureInput() {
    if (textureInputFrameGrabber != nullptr) {
        textureInputFrameGrabber->stop();
        textureInputFrameGrabber = nullptr;
    }

    audioProcessor.getFileController().stopTextureInput();
}

bool OscirenderAudioProcessorEditor::isTextureInputActive() const {
    return textureInputFrameGrabber != nullptr && textureInputFrameGrabber->isActive();
}

juce::String OscirenderAudioProcessorEditor::getTextureInputName() const {
    if (textureInputFrameGrabber != nullptr && textureInputFrameGrabber->isActive()) {
        return textureInputFrameGrabber->getSourceName();
    }

    return audioProcessor.getFileController().getTextureInputName();
}

void OscirenderAudioProcessorEditor::setCodeEditorVisible(std::optional<bool> visible) {
    auto& files = audioProcessor.getFileController();
    juce::SpinLock::ScopedLockType lock(files.lock);
    const auto originalIndex = files.getCurrentFileIndex();
    const int index = editingCustomFunction ? 0 : static_cast<int>(originalIndex.value_or(0)) + 1;
    if (originalIndex.has_value() || editingCustomFunction) {
        codeEditors[index]->setVisible(visible.has_value() ? visible.value() : !codeEditors[index]->isVisible());
        updateCodeEditor(!editingCustomFunction && isBinaryFile(files.getCurrentFileName()));
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

        auto& fileController = audioProcessor.getFileController();
        const int fileIndex = fileController.addFile(file);
        addCodeEditor(fileIndex);
        refreshFileUi(fileController.getCurrentFileName());
    }
}

void OscirenderAudioProcessorEditor::editFile(int index) {
    juce::String fileName;
    {
        auto& files = audioProcessor.getFileController();
        juce::SpinLock::ScopedLockType lock(files.lock);
        if (!files.contains(index)) {
            return;
        }
        fileName = files.getFileName(index);
    }

    if (isBinaryFile(fileName)) {
        return;
    }

    if (editingCustomFunction) {
        editCustomFunction(false);
    }
    auto& files = audioProcessor.getFileController();
    files.clearPendingSelection();
    files.selectFile(index);
    setCodeEditorVisible(true);
}

juce::String OscirenderAudioProcessorEditor::renameFile(int index, juce::String newName) {
    auto& files = audioProcessor.getFileController();
    const juce::String renamedFile = files.renameFile(index, std::move(newName));
    if (renamedFile.isEmpty()) {
        return {};
    }

    const int modelIndex = index + 1;
    if (modelIndex >= 0 && modelIndex < static_cast<int>(codeModels.size())) {
        codeModels[(size_t)modelIndex]->setDisplayName(renamedFile);
    }
    if (files.getCurrentFileIndex() == index) {
        refreshFileUi(renamedFile);
    }
    return renamedFile;
}

void OscirenderAudioProcessorEditor::duplicateFile(int index) {
    const int modelIndex = index + 1;
    if (modelIndex >= 0 && modelIndex < static_cast<int>(codeModels.size())) {
        codeModels[(size_t)modelIndex]->flushPendingEdit();
    }

    auto& files = audioProcessor.getFileController();
    const int duplicateIndex = files.duplicateFile(index);
    if (duplicateIndex < 0) {
        return;
    }

    juce::String duplicateName;
    {
        addCodeEditor(duplicateIndex);
        duplicateName = files.getFileName(duplicateIndex);
        refreshFileUi(duplicateName);
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
        auto& files = audioProcessor.getFileController();
        juce::SpinLock::ScopedLockType lock(files.lock);
        if (!files.contains(index)) {
            return;
        }
        fileName = files.getFileName(index);
        fileData = std::make_shared<juce::MemoryBlock>(*files.getFileData(index));
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

// FileController::lock must be held.
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
    auto& files = audioProcessor.getFileController();
    for (int i = 0; i < files.size(); i++) {
        addCodeEditor(i);
    }
    bool codeEditorVisible = std::any_cast<bool>(audioProcessor.getProperty("codeEditorVisible", false));
    refreshFileUiLocked(files.getCurrentFileName(), codeEditorVisible);
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
    OscilloscopePluginEditorBase::resized();

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
        auto& files = audioProcessor.getFileController();
        juce::SpinLock::ScopedLockType lock(files.lock);

        const auto originalIndex = files.getCurrentFileIndex();
        const int index = editingCustomFunction ? 0 : static_cast<int>(originalIndex.value_or(0)) + 1;

        bool ableToEditFile = (originalIndex.has_value() && !isBinaryFile(files.getCurrentFileName())) || editingCustomFunction;
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
                    extension = files.getFileName(*originalIndex).fromLastOccurrenceOf(".", true, false);
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
        auto& files = audioProcessor.getFileController();
        juce::String extension = files.getFileName(originalIndex).fromLastOccurrenceOf(".", true, false);
        const bool luaFile = extension == ".lua";
        const bool txtFile = extension == ".txt";
        editorModel = std::make_shared<osci::LuaScriptEditorModel>(files.getFileId(originalIndex),
                                                                   files.getFileName(originalIndex),
                                                                   juce::MemoryInputStream(*files.getFileData(originalIndex), false).readEntireStreamAsString(),
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

        auto& files = audioProcessor.getFileController();
        juce::SpinLock::ScopedLockType parserLock(files.lock);
        auto parser = files.getCurrentParser();
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

// FileController::lock and effectsLock must be held.
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
        auto& files = audioProcessor.getFileController();
        const auto originalIndex = files.getCurrentFileIndex();
        const int index = editingCustomFunction ? 0 : originalIndex.value_or(0) + 1;
        if ((originalIndex.has_value() || editingCustomFunction) && visible) {
            for (int i = 0; i < codeEditors.size(); i++) {
                codeEditors[i]->setVisible(false);
            }
            codeEditors[index]->setVisible(true);
            if (index == 0) {
                codeModels[index]->replaceCodeFromHost(audioProcessor.luaEffectState->getCode());
            } else {
                codeModels[index]->replaceCodeFromHost(juce::MemoryInputStream(*files.getFileData(*originalIndex), false).readEntireStreamAsString());
            }
        }
    }

    audioProcessor.setProperty("codeEditorVisible", visible);

    triggerAsyncUpdate();
}

void OscirenderAudioProcessorEditor::refreshFileUi(juce::String fileName, bool shouldOpenEditor) {
    {
        juce::SpinLock::ScopedLockType fileLock(audioProcessor.getFileController().lock);
        refreshFileUiLocked(fileName, shouldOpenEditor);
    }
    updateTimelineController();
}

// FileController::lock must be held.
void OscirenderAudioProcessorEditor::refreshFileUiLocked(const juce::String& fileName, bool shouldOpenEditor) {
    OscilloscopePluginEditorBase::fileUpdated(fileName);
    settings.fileUpdated(fileName);
    updateCodeEditor(isBinaryFile(fileName), shouldOpenEditor);
}

void OscirenderAudioProcessorEditor::handleAsyncUpdate() {
    resized();
}

void OscirenderAudioProcessorEditor::changeListenerCallback(juce::ChangeBroadcaster* source) {
    if (source == &audioProcessor.broadcaster) {
        {
            juce::SpinLock::ScopedLockType fileLock(audioProcessor.getFileController().lock);
            initialiseCodeEditors();
            settings.update();
        }
        updateTimelineController();
        resized();
        repaint();
    } else if (source == &audioProcessor.getFileController()) {
        auto& files = audioProcessor.getFileController();
        {
            juce::SpinLock::ScopedLockType fileLock(files.lock);
            // Triggered when the processor changes the current file (e.g. Blender or automation).
            refreshFileUiLocked(files.getCurrentFileName(), false);
        }
        updateTimelineController();
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

void OscirenderAudioProcessorEditor::resetWindowSizeAndPosition() {
    OscilloscopePluginEditorBase::resetWindowSizeAndPosition();

    layout.setItemLayout(0, -0.3, -1.0, kDefaultCodeEditorMainPanelSize);
    layout.setItemLayout(1, RESIZER_BAR_SIZE, RESIZER_BAR_SIZE, RESIZER_BAR_SIZE);
    layout.setItemLayout(2, -0.0, -1.0, -(1.0 + kDefaultCodeEditorMainPanelSize));

    constexpr double defaultLuaLayoutSize = -0.7;
    luaLayout.setItemLayout(0, -0.3, -1.0, defaultLuaLayoutSize);
    luaLayout.setItemLayout(1, RESIZER_BAR_SIZE, RESIZER_BAR_SIZE, RESIZER_BAR_SIZE);
    luaLayout.setItemLayout(2, -0.1, -1.0, -(1.0 + defaultLuaLayoutSize));

    audioProcessor.setProperty("codeEditorLayoutPreferredSize", kDefaultCodeEditorMainPanelSize);
    audioProcessor.setProperty("luaLayoutPreferredSize", defaultLuaLayoutSize);
    settings.resetLayoutToDefault();
    resized();
}

void OscirenderAudioProcessorEditor::editCustomFunction(bool enable) {
    if (enable) {
        // Record whether the code editor was open before entering custom-function edit mode.
        // We'll restore this state when exiting.
        codeEditorWasVisibleBeforeEditingCustomFunction = std::any_cast<bool>(audioProcessor.getProperty("codeEditorVisible", false));
    }

    editingCustomFunction = enable;
    auto& files = audioProcessor.getFileController();
    juce::SpinLock::ScopedLockType lock1(files.lock);
    juce::SpinLock::ScopedLockType lock2(audioProcessor.effectsLock);

    const auto currentFileName = files.getCurrentFileName();
    const bool binaryFile = !editingCustomFunction && isBinaryFile(currentFileName);

    // When disabling the pencil icon, don't close the editor if it was already open.
    // Instead, switch back to the currently-open file (unless it's a binary file).
    const bool shouldOpenEditor = enable || (codeEditorWasVisibleBeforeEditingCustomFunction && !binaryFile);

    codeEditors[0]->setVisible(enable);
    updateCodeEditor(binaryFile, shouldOpenEditor);
}

void OscirenderAudioProcessorEditor::commitCodeModel(osci::LuaScriptEditorModel& model) {
    auto& files = audioProcessor.getFileController();
    const auto code = model.getCode();
    if (model.getScriptId() == LuaEffectState::UNIQUE_ID) {
        juce::SpinLock::ScopedLockType effectsLock(audioProcessor.effectsLock);
        if (audioProcessor.luaEffectState != nullptr) {
            audioProcessor.luaEffectState->updateCode(code);
        }
        return;
    }

    files.updateFileById(model.getScriptId(), std::make_shared<juce::MemoryBlock>(code.toRawUTF8(), code.getNumBytesAsUTF8() + 1));
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
    if ((textCharacter == 'j' || textCharacter == 'k') && (isTextureInputActive() || audioProcessor.getFileController().isTextureInputActive())) {
        stopTextureInput();
    }

    int targetFile = -1;
    {
        auto& files = audioProcessor.getFileController();
        juce::SpinLock::ScopedLockType parserLock(files.lock);
        int numFiles = files.size();
        const auto selectedFile = files.getCurrentFileIndex();
        int currentFile = selectedFile.value_or(-1);

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
        auto& files = audioProcessor.getFileController();
        files.clearPendingSelection();
        files.selectFile(targetFile);
    }

    if (OscilloscopePluginEditorBase::keyPressed(key))
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

#if OSCI_PREMIUM
void OscirenderAudioProcessorEditor::openLaserWorkspace() {
    if (laserWorkspaceOverlay != nullptr) {
        laserWorkspaceOverlay->toFront(true);
        return;
    }
    std::unique_ptr<juce::Component> workspace = std::make_unique<ExternalLaserLinkComponent>(audioProcessor.getLaserAdapter());
    auto overlay = std::make_unique<osci::ComponentOverlay>(std::move(workspace), "External Laser Output", juce::Point<int> {680, 390}, true);
    laserWorkspaceOverlay = overlay.get();
    overlay->onDismissRequested = [this] {
        laserWorkspaceOverlay = nullptr;
    };
    showOverlay(std::move(overlay));
}
#endif

void OscirenderAudioProcessorEditor::openRecordingSettings() {
#if OSCI_PREMIUM
    OscilloscopePluginEditorBase::openRecordingSettings();
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

    auto& files = audioProcessor.getFileController();
    {
        juce::SpinLock::ScopedLockType fileLock(files.lock);
        const auto currentFileIndex = files.getCurrentFileIndex();
        if (currentFileIndex.has_value()) {
            auto parser = files.getParser(*currentFileIndex);

            // Check if it's an animatable file (gpla, gif, video)
            if (parser->isAnimatable) {
                controller = animationTimelineController;
            }
            // Check if it's an audio file (FileParser contains a WavParser)
            else if (parser->getWav() != nullptr) {
                controller = audioTimelineController;
            }
        }
    }

    visualiser.setTimelineController(controller);
}

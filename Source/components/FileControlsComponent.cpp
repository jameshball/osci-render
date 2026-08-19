#include "FileControlsComponent.h"
#include "../PluginEditor.h"
#include "../parser/FileFormatRegistry.h"

namespace {

int getTextWidth(const juce::Font& font, const juce::String& text) {
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText(font, text, 0.0f, 0.0f);
    return juce::roundToInt(glyphs.getBoundingBox(0, glyphs.getNumGlyphs(), true).getWidth());
}

} // namespace

FileControlsComponent::FileControlsComponent(OscirenderAudioProcessor& p, OscirenderAudioProcessorEditor& editor)
    : audioProcessor(p), pluginEditor(editor)
{
    // Open Files panel button
    addAndMakeVisible(openFileButton);
    openFileButton.setTooltip("Open files and examples");
    openFileButton.onClick = [this] {
        pluginEditor.settings.showExamples(true);
    };

    // File navigation
    addAndMakeVisible(leftArrow);
    leftArrow.setTooltip("Change to previous file (k).");
    leftArrow.onClick = [this] {
        audioProcessor.getFileController().selectAdjacentFile(-1);
    };

    addAndMakeVisible(rightArrow);
    rightArrow.setTooltip("Change to next file (j).");
    rightArrow.onClick = [this] {
        audioProcessor.getFileController().selectAdjacentFile(1);
    };

    // Close current file
    addAndMakeVisible(closeFileButton);
    closeFileButton.setTooltip("Close the currently open file.");
    closeFileButton.onClick = [this] {
        if (pluginEditor.isTextureInputActive() || audioProcessor.getFileController().isTextureInputActive()) {
            pluginEditor.stopTextureInput();
            updateFileLabel();
            return;
        }

        const auto currentFile = audioProcessor.getFileController().getCurrentFileIndex();
        if (currentFile.has_value()) {
            removeFile(*currentFile);
        }
    };

    // microphone icon
    addAndMakeVisible(inputEnabled);
    inputEnabled.onClick = [this] {
        audioProcessor.inputEnabled->setBoolValueNotifyingHost(!audioProcessor.inputEnabled->getBoolValue());
        updateFileLabel();
    };

    // Current file label
    addAndMakeVisible(fileLabel);
    fileLabel.setJustificationType(juce::Justification::centred);
    fileLabel.onContextMenu = [this](juce::Point<int> screenPosition) {
        showFileMenu(screenPosition);
    };

    addChildComponent(renameEditor);
    renameEditor.setMultiLine(false);
    renameEditor.setReturnKeyStartsNewLine(false);
    renameEditor.setJustification(juce::Justification::centred);
    renameEditor.setFont(fileLabel.getFont());
    renameEditor.setIndents(8, 0);
    renameEditor.setColour(juce::TextEditor::backgroundColourId, osci::Colours::veryDark());
    renameEditor.setColour(juce::TextEditor::textColourId, juce::Colours::white);
    renameEditor.setColour(juce::TextEditor::outlineColourId, osci::Colours::grey().withAlpha(0.45f));
    renameEditor.setColour(juce::TextEditor::focusedOutlineColourId, osci::Colours::accentColor());
    renameEditor.setColour(juce::TextEditor::highlightColourId, osci::Colours::accentColor().withAlpha(0.45f));
    renameEditor.onReturnKey = [this] { finishRenameFile(true); };
    renameEditor.onEscapeKey = [this] { finishRenameFile(false); };
    renameEditor.onFocusLost = [this] { finishRenameFile(true); };
    renameEditor.onTextChange = [this] { layoutRenameEditor(); };

    addChildComponent(renameExtensionLabel);
    renameExtensionLabel.setFont(fileLabel.getFont());
    renameExtensionLabel.setJustificationType(juce::Justification::centredLeft);
    renameExtensionLabel.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.72f));
    updateFileLabel();

    addAndMakeVisible(fileNumberLabel);
    fileNumberLabel.setJustificationType(juce::Justification::right);
}

void FileControlsComponent::showFileMenu(juce::Point<int> screenPosition) {
    constexpr int editFileId = 1;
    constexpr int renameFileId = 2;
    constexpr int duplicateFileId = 3;
    constexpr int exportFileId = 4;
    constexpr int removeFileId = 5;
    constexpr int offId = 100;
    constexpr int omniId = 101;
    constexpr int firstChannelId = 200;

    int fileIndex = -1;
    juce::String fileName;
    {
        auto& files = audioProcessor.getFileController();
        juce::SpinLock::ScopedLockType lock(files.lock);
        const auto currentFile = files.getCurrentFileIndex();
        fileIndex = currentFile.value_or(-1);
        if (files.contains(fileIndex)) {
            fileName = files.getFileName(fileIndex);
        }
    }
    const bool hasFile = fileName.isNotEmpty();

    auto& files = audioProcessor.getFileController();
    const int currentChannel = files.getProgramChangeChannel();
    juce::PopupMenu channelMenu;
    for (int channel = 1; channel <= 16; channel++) {
        channelMenu.addItem(firstChannelId + channel, "Channel " + juce::String(channel), true, currentChannel == channel);
    }

    juce::PopupMenu programChangeMenu;
    programChangeMenu.addItem(offId, "Disabled", true, currentChannel == FileController::programChangeOff);
    programChangeMenu.addItem(omniId, "Omni", true, currentChannel == FileController::programChangeOmni);
    programChangeMenu.addSubMenu("Channel", channelMenu, true, nullptr, currentChannel > 0);

    juce::PopupMenu menu;
    if (hasFile) {
        if (osci::files::isCodeEditable(fileName)) {
            menu.addItem(editFileId, "Edit file");
        }
        menu.addItem(renameFileId, "Rename file...");
        menu.addItem(duplicateFileId, "Duplicate file");
        menu.addItem(exportFileId, "Export file...");
        menu.addSeparator();
    }
    menu.addSubMenu("MIDI Program Change", programChangeMenu);
    if (hasFile) {
        menu.addSeparator();
        menu.addItem(removeFileId, "Remove file");
    }

    auto safeThis = juce::Component::SafePointer<FileControlsComponent>(this);
    osci::showContextMenuAsync(std::move(menu), screenPosition, this,
        [safeThis, fileIndex, editFileId, renameFileId, duplicateFileId, exportFileId, removeFileId, offId, omniId, firstChannelId](int result) {
        if (safeThis == nullptr || result == 0) {
            return;
        }

        auto& files = safeThis->audioProcessor.getFileController();
        if (result == editFileId) {
            safeThis->pluginEditor.editFile(fileIndex);
        } else if (result == renameFileId) {
            safeThis->beginRenameFile(fileIndex);
        } else if (result == duplicateFileId) {
            safeThis->pluginEditor.duplicateFile(fileIndex);
        } else if (result == exportFileId) {
            safeThis->pluginEditor.exportFile(fileIndex);
        } else if (result == removeFileId) {
            safeThis->removeFile(fileIndex);
        } else if (result == offId) {
            files.setProgramChangeChannel(FileController::programChangeOff);
        } else if (result == omniId) {
            files.setProgramChangeChannel(FileController::programChangeOmni);
        } else if (result > firstChannelId && result <= firstChannelId + 16) {
            files.setProgramChangeChannel(result - firstChannelId);
        }
        safeThis->updateFileLabel();
    });
}

void FileControlsComponent::beginRenameFile(int index) {
    juce::String fileName;
    {
        auto& files = audioProcessor.getFileController();
        juce::SpinLock::ScopedLockType lock(files.lock);
        if (!files.contains(index) || files.getCurrentFileIndex() != index) {
            return;
        }
        fileName = files.getFileName(index);
    }

    const int extensionStart = fileName.lastIndexOfChar('.');
    renameExtension = extensionStart > 0 ? fileName.substring(extensionStart) : juce::String();
    const juce::String baseName = renameExtension.isNotEmpty() ? fileName.dropLastCharacters(renameExtension.length()) : fileName;
    renameFileIndex = index;
    renamingFile = true;
    renameEditor.setText(baseName, false);
    renameEditor.selectAll();
    renameExtensionLabel.setText(renameExtension, juce::dontSendNotification);
    fileLabel.setVisible(false);
    renameEditor.setVisible(true);
    renameExtensionLabel.setVisible(renameExtension.isNotEmpty());
    layoutRenameEditor();
    renameEditor.toFront(false);
    renameEditor.grabKeyboardFocus();
}

void FileControlsComponent::finishRenameFile(bool commit) {
    if (!renamingFile) {
        return;
    }

    juce::String requestedName = renameEditor.getText().trim();
    const int fileIndex = renameFileIndex;
    renamingFile = false;
    renameFileIndex = -1;
    renameEditor.setVisible(false);
    renameExtensionLabel.setVisible(false);
    fileLabel.setVisible(true);

    if (commit && requestedName.isNotEmpty()) {
        requestedName += renameExtension;
        pluginEditor.renameFile(fileIndex, std::move(requestedName));
    }
    renameExtension.clear();
    updateFileLabel();
}

void FileControlsComponent::layoutRenameEditor() {
    if (!renamingFile) {
        return;
    }

    const auto area = fileLabel.getBounds().reduced(2, 0);
    const auto font = renameEditor.getFont();
    const int extensionWidth = renameExtension.isNotEmpty()
        ? getTextWidth(font, renameExtension) + 6
        : 0;
    const int maximumEditorWidth = juce::jmax(1, area.getWidth() - extensionWidth);
    const int minimumEditorWidth = juce::jmin(80, maximumEditorWidth);
    const int editorWidth = juce::jlimit(minimumEditorWidth, maximumEditorWidth,
        getTextWidth(font, renameEditor.getText()) + 28);
    const int totalWidth = editorWidth + extensionWidth;
    const int left = area.getCentreX() - totalWidth / 2;
    renameEditor.setBounds(left, area.getY(), editorWidth, area.getHeight());
    renameExtensionLabel.setBounds(left + editorWidth, area.getY(), extensionWidth, area.getHeight());
}

void FileControlsComponent::removeFile(int index) {
    if (index < 0) {
        return;
    }
    audioProcessor.getFileController().removeFile(index);
    updateFileLabel();
}

void FileControlsComponent::paint(juce::Graphics& g)
{
    // Rounded veryDark background
    auto b = getLocalBounds().toFloat();
    auto bg = osci::Colours::veryDark();
    g.setColour(bg);
    g.fillRoundedRectangle(b, osci::LookAndFeel::RECT_RADIUS);
}

void FileControlsComponent::resized()
{
    auto bounds = getLocalBounds().reduced(8, 2);
    const int h = bounds.getHeight();
    const int icon = juce::jmin(h, 22);
    const int gap = 8;

    // Layout: [Mic] [<] [Label expands] [>] [Close] [Open]
    inputEnabled.setBounds(bounds.removeFromLeft(icon));
    bounds.removeFromLeft(gap);

    // Always remove bounds to keep label consistently positioned
    auto leftArea = bounds.removeFromLeft(icon);
    bounds.removeFromLeft(gap);
    if (leftArrow.isVisible()) {
        leftArrow.setBounds(leftArea.withSizeKeepingCentre(icon, icon));
    }

    if (openFileButton.isVisible()) {
        openFileButton.setBounds(bounds.removeFromRight(icon).withSizeKeepingCentre(icon, icon));
        bounds.removeFromRight(gap);
    }

    if (closeFileButton.isVisible()) {
        auto closeArea = bounds.removeFromRight(icon);
        closeFileButton.setBounds(closeArea.withSizeKeepingCentre(icon, icon));
        bounds.removeFromRight(gap);
    }
    
    // Always remove bounds to keep label consistently positioned
    auto rightArea = bounds.removeFromRight(icon);
    bounds.removeFromRight(gap);
    if (rightArrow.isVisible()) {
        rightArrow.setBounds(rightArea.withSizeKeepingCentre(icon, icon));
    }

    if (fileNumberLabel.isVisible()) {
        fileNumberLabel.setBounds(bounds.removeFromRight(45));
    }
    
    fileLabel.setBounds(bounds);
    layoutRenameEditor();
}

void FileControlsComponent::updateFileLabel()
{
    auto& files = audioProcessor.getFileController();
    const bool textureInputActive = pluginEditor.isTextureInputActive() || files.isTextureInputActive();
    bool ableToOpenFiles = !files.isObjectServerActive() && !audioProcessor.inputEnabled->getBoolValue() && !textureInputActive;
    const auto currentFile = files.getCurrentFileIndex();
    bool fileOpen = currentFile.has_value() && ableToOpenFiles;
    bool showLeftArrow = currentFile.has_value() && *currentFile > 0 && fileOpen;
    bool showRightArrow = files.getAdjacentFileIndex(1).has_value() && fileOpen;

    if (renamingFile && (!fileOpen || currentFile != renameFileIndex)) {
        finishRenameFile(false);
        return;
    }

    openFileButton.setVisible(ableToOpenFiles);
    closeFileButton.setVisible(fileOpen || textureInputActive);
    leftArrow.setVisible(showLeftArrow);
    rightArrow.setVisible(showRightArrow);
    fileNumberLabel.setVisible(showLeftArrow || showRightArrow);
    fileLabel.setVisible(!renamingFile);
    renameEditor.setVisible(renamingFile && fileOpen);
    renameExtensionLabel.setVisible(renamingFile && fileOpen && renameExtension.isNotEmpty());

    if (files.isObjectServerActive()) {
        fileLabel.setText("Rendering from Blender", juce::dontSendNotification);
    } else if (audioProcessor.inputEnabled->getBoolValue()) {
        fileLabel.setText("Using external audio", juce::dontSendNotification);
    } else if (textureInputActive) {
        fileLabel.setText("Using texture input: " + pluginEditor.getTextureInputName(), juce::dontSendNotification);
    } else if (!currentFile.has_value()) {
        fileLabel.setText("No file open", juce::dontSendNotification);
    } else {
        fileNumberLabel.setText(" (" + juce::String(*currentFile + 1) + "/" + juce::String(files.size()) + ")", juce::dontSendNotification);
        fileLabel.setText(files.getCurrentFileName(), juce::dontSendNotification);
    }

    resized();
}

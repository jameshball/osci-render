#include "FileControlsComponent.h"
#include "../PluginEditor.h"

FileControlsComponent::FileControlsComponent(OscirenderAudioProcessor& p, OscirenderAudioProcessorEditor& editor)
    : audioProcessor(p), pluginEditor(editor)
{
    // Open Files panel button
    addAndMakeVisible(openPanelButton);
    openPanelButton.setTooltip("Open files and examples");
    openPanelButton.onClick = [this] {
        pluginEditor.settings.showExamples(true);
    };

    // File navigation
    addAndMakeVisible(leftArrow);
    leftArrow.setTooltip("Change to previous file (k).");
    leftArrow.onClick = [this] {
        juce::SpinLock::ScopedLockType parserLock(audioProcessor.parsersLock);
        juce::SpinLock::ScopedLockType effectsLock(audioProcessor.effectsLock);
        int index = audioProcessor.getCurrentFileIndex();
        if (index > 0) {
            audioProcessor.changeCurrentFile(index - 1);
            pluginEditor.fileUpdated(audioProcessor.getCurrentFileName());
        }
    };

    addAndMakeVisible(rightArrow);
    rightArrow.setTooltip("Change to next file (j).");
    rightArrow.onClick = [this] {
        juce::SpinLock::ScopedLockType parserLock(audioProcessor.parsersLock);
        juce::SpinLock::ScopedLockType effectsLock(audioProcessor.effectsLock);
        int index = audioProcessor.getCurrentFileIndex();
        if (index < audioProcessor.numFiles() - 1) {
            audioProcessor.changeCurrentFile(index + 1);
            pluginEditor.fileUpdated(audioProcessor.getCurrentFileName());
        }
    };

    // Close current file
    addAndMakeVisible(closeFileButton);
    closeFileButton.setTooltip("Close the currently open file.");
    closeFileButton.onClick = [this] {
        juce::SpinLock::ScopedLockType lock(audioProcessor.parsersLock);
        int index = audioProcessor.getCurrentFileIndex();
        if (index == -1) return;
        audioProcessor.removeFile(audioProcessor.getCurrentFileIndex());
        updateFileLabel();
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
    updateFileLabel();
}

void FileControlsComponent::paint(juce::Graphics& g)
{
    // Rounded veryDark background
    auto b = getLocalBounds().toFloat();
    auto bg = Colours::veryDark;
    g.setColour(bg);
    g.fillRoundedRectangle(b, OscirenderLookAndFeel::RECT_RADIUS);
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

    if (leftArrow.isVisible()) {
        auto leftArea = bounds.removeFromLeft(icon);
        leftArrow.setBounds(leftArea.withSizeKeepingCentre(icon, icon));
        bounds.removeFromLeft(gap);
    }

    if (openPanelButton.isVisible()) {
        openPanelButton.setBounds(bounds.removeFromRight(icon).withSizeKeepingCentre(icon, icon));
        bounds.removeFromRight(gap);
    }

    if (closeFileButton.isVisible()) {
        auto closeArea = bounds.removeFromRight(icon);
        closeFileButton.setBounds(closeArea.withSizeKeepingCentre(icon, icon));
        bounds.removeFromRight(gap);
    }
    
    if (rightArrow.isVisible()) {
        auto rightArea = bounds.removeFromRight(icon);
        rightArrow.setBounds(rightArea.withSizeKeepingCentre(icon, icon));
        bounds.removeFromRight(gap);
    }

    fileLabel.setBounds(bounds);
}

void FileControlsComponent::updateFileLabel()
{
    bool fileOpen = audioProcessor.getCurrentFileIndex() != -1 && !audioProcessor.objectServerRendering && !audioProcessor.inputEnabled->getBoolValue();
    bool showLeftArrow  = audioProcessor.getCurrentFileIndex() > 0 && fileOpen;
    bool showRightArrow = audioProcessor.getCurrentFileIndex() < audioProcessor.numFiles() - 1 && fileOpen;

    openPanelButton.setVisible(fileOpen);
    closeFileButton.setVisible(fileOpen);
    leftArrow.setVisible(showLeftArrow);
    rightArrow.setVisible(showRightArrow);

#if (JUCE_MAC || JUCE_WINDOWS) && OSCI_PREMIUM
    if (audioProcessor.syphonInputActive) {
        fileLabel.setText(pluginEditor.getSyphonSourceName(), juce::dontSendNotification);
    } else
#endif
    if (audioProcessor.objectServerRendering) {
        fileLabel.setText("Rendering from Blender", juce::dontSendNotification);
    } else if (audioProcessor.inputEnabled->getBoolValue()) {
        fileLabel.setText("Using external audio", juce::dontSendNotification);
    } else if (audioProcessor.getCurrentFileIndex() == -1) {
        fileLabel.setText("No file open", juce::dontSendNotification);
    } else {
        fileLabel.setText(audioProcessor.getCurrentFileName(), juce::dontSendNotification);
    }

    resized();
}

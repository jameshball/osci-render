#include "FileControlsComponent.h"
#include "../PluginEditor.h"

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
        int targetIndex = -1;
        {
            juce::SpinLock::ScopedLockType parserLock(audioProcessor.parsersLock);
            const int currentIndex = audioProcessor.getCurrentFileIndex();
            if (currentIndex > 0) {
                targetIndex = currentIndex - 1;
            }
        }
        if (targetIndex >= 0) {
            audioProcessor.selectFile(targetIndex);
        }
    };

    addAndMakeVisible(rightArrow);
    rightArrow.setTooltip("Change to next file (j).");
    rightArrow.onClick = [this] {
        int targetIndex = -1;
        {
            juce::SpinLock::ScopedLockType parserLock(audioProcessor.parsersLock);
            const int currentIndex = audioProcessor.getCurrentFileIndex();
            if (currentIndex < audioProcessor.numFiles() - 1) {
                targetIndex = currentIndex + 1;
            }
        }
        if (targetIndex >= 0) {
            audioProcessor.selectFile(targetIndex);
        }
    };

    // Close current file
    addAndMakeVisible(closeFileButton);
    closeFileButton.setTooltip("Close the currently open file.");
    closeFileButton.onClick = [this] {
        if (pluginEditor.isTextureInputActive() || audioProcessor.isTextureInputActive()) {
            pluginEditor.stopTextureInput();
            updateFileLabel();
            return;
        }

        juce::SpinLock::ScopedLockType parserLock(audioProcessor.parsersLock);
        juce::SpinLock::ScopedLockType effectsLock(audioProcessor.effectsLock);
        int index = audioProcessor.getCurrentFileIndex();
        if (index == -1) {
            return;
        }
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
    fileLabel.onContextMenu = [this](juce::Point<int> screenPosition) {
        showProgramChangeMenu(screenPosition);
    };
    updateFileLabel();

    addAndMakeVisible(fileNumberLabel);
    fileNumberLabel.setJustificationType(juce::Justification::right);
}

void FileControlsComponent::showProgramChangeMenu(juce::Point<int> screenPosition) {
    constexpr int offId = 1;
    constexpr int omniId = 2;
    constexpr int firstChannelId = 100;

    const int currentChannel = audioProcessor.getProgramChangeChannel();
    juce::PopupMenu channelMenu;
    for (int channel = 1; channel <= 16; channel++) {
        channelMenu.addItem(firstChannelId + channel, "Channel " + juce::String(channel), true, currentChannel == channel);
    }

    juce::PopupMenu menu;
    menu.addSectionHeader("Program Change File Selection");
    menu.addItem(offId, "Off", true, currentChannel == OscirenderAudioProcessor::kProgramChangeOff);
    menu.addItem(omniId, "Omni (all channels)", true, currentChannel == OscirenderAudioProcessor::kProgramChangeOmni);
    menu.addSeparator();
    menu.addSubMenu("MIDI Channel", channelMenu);

    auto safeThis = juce::Component::SafePointer<FileControlsComponent>(this);
    osci::showContextMenuAsync(std::move(menu), screenPosition, this, [safeThis, offId, omniId, firstChannelId](int result) {
        if (safeThis == nullptr || result == 0) {
            return;
        }

        if (result == offId) {
            safeThis->audioProcessor.setProgramChangeChannel(OscirenderAudioProcessor::kProgramChangeOff);
        } else if (result == omniId) {
            safeThis->audioProcessor.setProgramChangeChannel(OscirenderAudioProcessor::kProgramChangeOmni);
        } else if (result > firstChannelId && result <= firstChannelId + 16) {
            safeThis->audioProcessor.setProgramChangeChannel(result - firstChannelId);
        }
        safeThis->updateFileLabel();
    });
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
}

void FileControlsComponent::updateFileLabel()
{
    const bool textureInputActive = pluginEditor.isTextureInputActive() || audioProcessor.isTextureInputActive();
    bool ableToOpenFiles = !audioProcessor.objectServerRendering && !audioProcessor.inputEnabled->getBoolValue() && !textureInputActive;
    bool fileOpen = audioProcessor.getCurrentFileIndex() != -1 && ableToOpenFiles;
    bool showLeftArrow  = audioProcessor.getCurrentFileIndex() > 0 && fileOpen;
    bool showRightArrow = audioProcessor.getCurrentFileIndex() < audioProcessor.numFiles() - 1 && fileOpen;

    openFileButton.setVisible(ableToOpenFiles);
    closeFileButton.setVisible(fileOpen || textureInputActive);
    leftArrow.setVisible(showLeftArrow);
    rightArrow.setVisible(showRightArrow);
    fileNumberLabel.setVisible(showLeftArrow || showRightArrow);

    if (audioProcessor.objectServerRendering) {
        fileLabel.setText("Rendering from Blender", juce::dontSendNotification);
    } else if (audioProcessor.inputEnabled->getBoolValue()) {
        fileLabel.setText("Using external audio", juce::dontSendNotification);
    } else if (textureInputActive) {
        fileLabel.setText("Using texture input: " + pluginEditor.getTextureInputName(), juce::dontSendNotification);
    } else if (audioProcessor.getCurrentFileIndex() == -1) {
        fileLabel.setText("No file open", juce::dontSendNotification);
    } else {
        fileNumberLabel.setText(" (" + juce::String(audioProcessor.getCurrentFileIndex() + 1) + "/" + juce::String(audioProcessor.numFiles()) + ")", juce::dontSendNotification); 
        fileLabel.setText(audioProcessor.getCurrentFileName(), juce::dontSendNotification);
    }

    resized();
}

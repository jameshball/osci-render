#pragma once

#include <JuceHeader.h>

#include "../../modules/juce_sharedtexture/SharedTexture.h"

class SyphonInputSelectorComponent : public juce::Component, private juce::Button::Listener {
public:
    SyphonInputSelectorComponent(SharedTextureManager& manager, std::function<void(const juce::String&, const juce::String&)> onConnect, std::function<void()> onDisconnect, juce::String currentSource = "")
        : sharedTextureManager(manager), onConnectCallback(onConnect), onDisconnectCallback(onDisconnect), currentSourceName(currentSource) {
        addAndMakeVisible(sourceLabel);
        sourceLabel.setText("Syphon/Spout Source:", juce::dontSendNotification);

        addAndMakeVisible(sourceDropdown);

        addAndMakeVisible(connectButton);
        connectButton.setButtonText("Connect");
        connectButton.addListener(this);

        refreshSources();

        // Auto-select first item if no current source specified
        if (!currentSourceName.isEmpty()) {
            sourceDropdown.setText(currentSourceName, juce::dontSendNotification);
        } else if (sourceDropdown.getNumItems() > 0) {
            sourceDropdown.setSelectedItemIndex(0, juce::sendNotificationSync);
        }
    }

    void refreshSources() {
        sourceDropdown.clear();
        serverNames.clear();
        appNames.clear();
        sharedTextureManager.getAvailableSenderDetails(serverNames, appNames);
        for (int i = 0; i < serverNames.size(); i++) {
            juce::String display = serverNames[i];
            if (appNames[i].isNotEmpty())
                display += " - " + appNames[i];
            sourceDropdown.addItem(display, i + 1); // ComboBox IDs are 1-based
        }
    }

    void resized() override {
        auto area = getLocalBounds().reduced(10);
        sourceLabel.setBounds(area.removeFromTop(24));
        sourceDropdown.setBounds(area.removeFromTop(28));

        // Make the button not take up the full width
        auto buttonArea = area.removeFromTop(40).reduced(0, 8);
        connectButton.setBounds(buttonArea.withSizeKeepingCentre(120, buttonArea.getHeight()));
    }

    void buttonClicked(juce::Button* b) override {
        int selectedIndex = sourceDropdown.getSelectedItemIndex();
        if (selectedIndex >= 0 && selectedIndex < serverNames.size()) {
            onConnectCallback(serverNames[selectedIndex], appNames[selectedIndex]);

            if (auto* window = findParentComponentOfClass<juce::DialogWindow>())
                window->exitModalState(0);
        }
    }

private:
    SharedTextureManager& sharedTextureManager;
    std::function<void(const juce::String&, const juce::String&)> onConnectCallback;
    std::function<void()> onDisconnectCallback;
    juce::String currentSourceName;

    juce::StringArray serverNames;
    juce::StringArray appNames;

    juce::Label sourceLabel;
    juce::ComboBox sourceDropdown;
    juce::TextButton connectButton;
};

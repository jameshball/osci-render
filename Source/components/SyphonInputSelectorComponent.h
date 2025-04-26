#pragma once

#include <JuceHeader.h>
#include "../modules/juce_sharedtexture/SharedTexture.h"

class SyphonInputSelectorComponent : public juce::Component, private juce::Button::Listener {
public:
    SyphonInputSelectorComponent(SharedTextureManager& manager, std::function<void(const juce::String&, const juce::String&)> onConnect, std::function<void()> onDisconnect, bool isConnected, juce::String currentSource)
        : sharedTextureManager(manager), onConnectCallback(onConnect), onDisconnectCallback(onDisconnect), connected(isConnected), currentSourceName(currentSource) {
        addAndMakeVisible(sourceLabel);
        sourceLabel.setText("Syphon/Spout Source:", juce::dontSendNotification);

        addAndMakeVisible(sourceDropdown);
        sourceDropdown.onChange = [this] {
            selectedSource = sourceDropdown.getText();
        };

        addAndMakeVisible(connectButton);
        connectButton.setButtonText(connected ? "Disconnect" : "Connect");
        connectButton.addListener(this);

        refreshSources();
        if (!currentSourceName.isEmpty()) {
            sourceDropdown.setText(currentSourceName, juce::dontSendNotification);
        }
    }

    void refreshSources() {
        sourceDropdown.clear();
        auto sources = sharedTextureManager.getAvailableSenders();
        for (const auto& s : sources)
            sourceDropdown.addItem(s, sourceDropdown.getNumItems() + 1);
    }

    void resized() override {
        auto area = getLocalBounds().reduced(10);
        sourceLabel.setBounds(area.removeFromTop(24));
        sourceDropdown.setBounds(area.removeFromTop(28));
        connectButton.setBounds(area.removeFromTop(28).reduced(0, 8));
    }

    void buttonClicked(juce::Button* b) override {
        if (connected) {
            onDisconnectCallback();
        } else {
            auto selected = sourceDropdown.getText();
            if (selected.isNotEmpty()) {
                // Syphon: "ServerName - AppName"
                auto parts = juce::StringArray::fromTokens(selected, "-", "");
                juce::String server = parts[0].trim();
                juce::String app = parts.size() > 1 ? parts[1].trim() : juce::String();
                onConnectCallback(server, app);
            }
        }
    }

private:
    SharedTextureManager& sharedTextureManager;
    std::function<void(const juce::String&, const juce::String&)> onConnectCallback;
    std::function<void()> onDisconnectCallback;
    bool connected;
    juce::String currentSourceName;
    juce::String selectedSource;

    juce::Label sourceLabel;
    juce::ComboBox sourceDropdown;
    juce::TextButton connectButton;
};

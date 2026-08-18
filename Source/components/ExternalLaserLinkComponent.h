#pragma once

#include "../laser/OscirenderLaserAdapter.h"

class ExternalLaserLinkComponent final : public juce::Component, private juce::Timer {
public:
    explicit ExternalLaserLinkComponent(OscirenderLaserAdapter& adapterToUse) : adapter(adapterToUse) {
        setComponentID("externalLaserLink");
        setTitle("External laser output");
        setDescription("Streams post-effects XYRGB to the separately installed osci-laser application over local IDN.");

        launchButton.setComponentID("launchOsciLaser");
        launchButton.onClick = [this] {
            const auto current = adapter.getSnapshot();
            if (current.installed) {
                adapter.launchApplication();
            } else {
                juce::URL("https://osci-render.com/osci-laser").launchInDefaultBrowser();
            }
            refresh();
        };
        addAndMakeVisible(launchButton);

        streamButton.setComponentID("toggleLaserStreaming");
        streamButton.onClick = [this] {
            if (adapter.getSnapshot().streamingRequested) {
                adapter.stopStreaming();
            } else {
                adapter.startStreaming();
            }
            refresh();
        };
        addAndMakeVisible(streamButton);

        refreshButton.setComponentID("refreshLaserDetection");
        refreshButton.setButtonText("REFRESH");
        refreshButton.onClick = [this] {
            adapter.refreshInstallationStatus();
            refresh();
        };
        addAndMakeVisible(refreshButton);

        statusLabel.setComponentID("externalLaserStatus");
        statusLabel.setTitle("External laser stream status");
        statusLabel.setDescription("Current state of the local IDN link to osci-laser.");
        statusLabel.setJustificationType(juce::Justification::centredLeft);
        statusLabel.setFont(juce::FontOptions(12.0f));
        statusLabel.setColour(juce::Label::textColourId, juce::Colour(0xffd5d6da));
        statusLabel.setInterceptsMouseClicks(false, false);
        addAndMakeVisible(statusLabel);

        startTimer(250);
        refresh();
    }

    ~ExternalLaserLinkComponent() override {
        stopTimer();
    }

    void paint(juce::Graphics& graphics) override {
        const auto bounds = getLocalBounds().toFloat();
        graphics.setColour(juce::Colour(0xff191a1d));
        graphics.fillRoundedRectangle(bounds, 12.0f);
        graphics.setColour(juce::Colour(0xff35373c));
        graphics.drawRoundedRectangle(bounds.reduced(0.5f), 12.0f, 1.0f);

        auto content = getLocalBounds().reduced(26);
        graphics.setColour(juce::Colours::white);
        graphics.setFont(juce::FontOptions(24.0f, juce::Font::bold));
        graphics.drawText("osci-render -> osci-laser", content.removeFromTop(34), juce::Justification::centredLeft);
        content.removeFromTop(8);
        graphics.setColour(juce::Colour(0xffb5b7bd));
        graphics.setFont(juce::FontOptions(14.0f));
        graphics.drawFittedText("osci-render sends its live post-effects XYRGB stream. osci-laser owns device setup, conditioning, calibration, licensing and all arming controls.",
                                content.removeFromTop(52), juce::Justification::topLeft, 3);

        auto statusArea = content.removeFromTop(68).reduced(0, 8);
        graphics.setColour(statusColour);
        graphics.fillRoundedRectangle(statusArea.toFloat(), 7.0f);

        graphics.setColour(juce::Colour(0xff858891));
        graphics.setFont(juce::FontOptions(11.0f));
        graphics.drawText("Streaming never launches on project restore and can never arm hardware remotely.",
                          getLocalBounds().reduced(26).removeFromBottom(58).removeFromTop(18), juce::Justification::centredLeft);
    }

    void resized() override {
        auto content = getLocalBounds().reduced(26);
        content.removeFromTop(102);
        statusLabel.setBounds(content.removeFromTop(52).reduced(12, 0));
        auto controls = getLocalBounds().reduced(26).removeFromBottom(38);
        refreshButton.setBounds(controls.removeFromRight(104));
        controls.removeFromRight(10);
        streamButton.setBounds(controls.removeFromRight(170));
        controls.removeFromRight(10);
        launchButton.setBounds(controls.removeFromRight(170));
    }

private:
    void timerCallback() override {
        refresh();
    }

    void refresh() {
        const auto current = adapter.getSnapshot();
        statusMessage = current.message;
        launchButton.setButtonText(current.installed ? "LAUNCH OSCI-LASER" : "GET OSCI-LASER");
        streamButton.setButtonText(current.streamingRequested ? "STOP STREAMING" : "START STREAMING");
        streamButton.setEnabled(current.installed && current.state != OscirenderLaserLinkState::launching);
        switch (current.state) {
            case OscirenderLaserLinkState::streaming:
                statusTitle = "STREAMING";
                statusColour = juce::Colour(0xff174d2c);
                break;
            case OscirenderLaserLinkState::fault:
                statusTitle = "CONNECTION FAULT";
                statusColour = juce::Colour(0xff5b2026);
                break;
            case OscirenderLaserLinkState::connecting:
            case OscirenderLaserLinkState::launching:
                statusTitle = "CONNECTING";
                statusColour = juce::Colour(0xff55431f);
                break;
            case OscirenderLaserLinkState::notInstalled:
                statusTitle = "NOT INSTALLED";
                statusColour = juce::Colour(0xff34363b);
                break;
            default:
                statusTitle = "READY";
                statusColour = juce::Colour(0xff243b45);
                break;
        }
        const auto statusText = statusTitle + ": " + statusMessage;
        if (statusLabel.getText() != statusText) {
            statusLabel.setText(statusText, juce::dontSendNotification);
        }
        repaint();
    }

    OscirenderLaserAdapter& adapter;
    juce::TextButton launchButton;
    juce::TextButton streamButton;
    juce::TextButton refreshButton;
    juce::Label statusLabel;
    juce::String statusTitle;
    juce::String statusMessage;
    juce::Colour statusColour {juce::Colour(0xff34363b)};
};

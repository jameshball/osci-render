#pragma once

#include <JuceHeader.h>
#include "../LookAndFeel.h"
#include <osci_gui/osci_gui.h>

class AboutComponent : public juce::Component {
public:
    struct CreditEntry {
        juce::String name;
        juce::String contribution;
    };

    struct Info {
        const void* imageData = nullptr;
        size_t imageSize = 0;
        juce::String productName;
        juce::String companyName;
        juce::String versionString;
        bool isPremium = false;
        bool betaUpdatesEnabled = false;
        juce::String websiteUrl;
        juce::String githubUrl;
        std::vector<CreditEntry> credits;
        int blenderPort = -1;
        std::function<void(bool enabled)> onBetaUpdatesChanged;
    };

    explicit AboutComponent(const Info& info);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseUp(const juce::MouseEvent& event) override;

    static juce::Point<int> preferredSize(const Info& info);

    static std::unique_ptr<osci::OverlayComponent> createOverlay(const Info& info, juce::String closeButtonSvg);

private:
    Info info;
    juce::Image logo;
    juce::ImageComponent logoComponent;
    juce::TextButton websiteBtn, discordBtn, issuesBtn;
    juce::Rectangle<float> versionStatusBounds;
    juce::String betaUnlockMessage;
    int betaUnlockClicks = 0;

    void paintCard(juce::Graphics& g, juce::Rectangle<float> area) const;
    void showTemporaryBetaMessage(const juce::String& message);

    static constexpr float kPad = 16.0f;
    static constexpr float kCardPad = 12.0f;
    static constexpr float kCardRadius = 6.0f;
    static constexpr float kGap = 8.0f;
    static constexpr float kLogoH = 100.0f;
    static constexpr float kRowH = 34.0f;
    static constexpr float kBtnH = 28.0f;
    static constexpr float kBtnGap = 8.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AboutComponent)
};

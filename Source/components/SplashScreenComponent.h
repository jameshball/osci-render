#pragma once

#include <JuceHeader.h>

#include <osci_gui/osci_gui.h>

class SplashScreenComponent : public osci::OverlayComponent {
public:
    SplashScreenComponent();
    ~SplashScreenComponent() override = default;

    std::function<void()> onUpgradeClicked;

protected:
    juce::Point<int> getPreferredPanelSize() const override;
    void resizeContent(juce::Rectangle<int> contentArea) override;

private:
    void buildBenefitTiles();

    juce::Label subtitleLabel;
    osci::GridComponent benefitsGrid;
    juce::Label supportLabel;

    juce::TextButton upgradeButton{"Upgrade to Premium"};
    juce::TextButton continueButton{"Maybe later"};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SplashScreenComponent)
};

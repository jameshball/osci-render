#pragma once

#include <JuceHeader.h>

#include "OverlayComponent.h"
#include "GridComponent.h"

class SplashScreenComponent : public OverlayComponent {
public:
    SplashScreenComponent();
    ~SplashScreenComponent() override = default;

    std::function<void()> onUpgradeClicked;

protected:
    void resizeContent(juce::Rectangle<int> contentArea) override;

private:
    void buildBenefitTiles();

    juce::Label subtitleLabel;
    GridComponent benefitsGrid;
    juce::Label supportLabel;

    juce::TextButton upgradeButton{"Upgrade to Premium"};
    juce::TextButton continueButton{"Maybe later"};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SplashScreenComponent)
};

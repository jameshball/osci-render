#pragma once

#include <JuceHeader.h>

#include "../LookAndFeel.h"
#include "GridComponent.h"

class SplashScreenComponent : public juce::SplashScreen {
public:
    SplashScreenComponent();
    ~SplashScreenComponent() override = default;

    std::function<void()> onUpgradeClicked;
    std::function<void()> onDismissRequested;

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    void configureLabel(juce::Label& label, const juce::Font& font, juce::Justification justification);
    void dismiss();
    void buildBenefitTiles();

    juce::Rectangle<int> panelBounds;

    juce::Label titleLabel;
    juce::Label subtitleLabel;
    GridComponent benefitsGrid;
    juce::Label supportLabel;

    juce::TextButton upgradeButton{"Upgrade to Premium"};
    juce::TextButton continueButton{"Maybe later"};
    
    // Analytics button trackers
    std::unique_ptr<juce::ButtonTracker> upgradeButtonTracker;
    std::unique_ptr<juce::ButtonTracker> continueButtonTracker;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SplashScreenComponent)
};

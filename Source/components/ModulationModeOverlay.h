#pragma once

#include <JuceHeader.h>
#include "OverlayComponent.h"
#include "GridItemComponent.h"
#include "../ModulationMode.h"

// First-launch overlay that asks the user to choose between Simple and Standard
// modulation modes.  Reusable via the Interface menu or the Simple Mode button.
class ModulationModeOverlay : public OverlayComponent {
public:
    ModulationModeOverlay(ModulationMode currentMode);
    ~ModulationModeOverlay() override = default;

    // Fired when the user makes a choice.  The callback receives the chosen mode.
    std::function<void(ModulationMode)> onModeSelected;

protected:
    void resizeContent(juce::Rectangle<int> contentArea) override;
    void resized() override;

    // Override to prevent dismiss on background click (force a choice).
    void mouseDown(const juce::MouseEvent& e) override;

private:
    void selectMode(ModulationMode mode);

    static constexpr int MIN_CARD_WIDTH = 250;
    static constexpr int CARD_HEIGHT = 120;
    static constexpr int PANEL_HEIGHT_SINGLE_ROW = 380;
    static constexpr int PANEL_HEIGHT_TWO_ROWS = 520;

    juce::Label titleLabel;
    juce::Label subtitleLabel;

    GridItemComponent simpleCard;
    GridItemComponent standardCard;

    juce::TextButton confirmButton{"Continue"};

    ModulationMode selectedMode;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ModulationModeOverlay)
};

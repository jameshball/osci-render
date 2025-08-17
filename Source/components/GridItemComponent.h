#pragma once
#include <JuceHeader.h>
#include "../LookAndFeel.h"
#include "HoverAnimationMixin.h"
#include "SvgButton.h"

// Generic grid item with name, icon, and id
class GridItemComponent : public HoverAnimationMixin
{
public:
    GridItemComponent(const juce::String& name, const juce::String& icon, const juce::String& id);
    ~GridItemComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseEnter(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent& event) override;

    const juce::String& getId() const { return itemId; }
    const juce::String& getName() const { return itemName; }

    std::function<void(const juce::String& id)> onItemSelected;
    std::function<void(const juce::String& id)> onHoverStart;
    std::function<void()> onHoverEnd;

private:
    juce::String itemName;
    juce::String itemId;

    // Icon for the item
    std::unique_ptr<SvgButton> iconButton;

    static constexpr int CORNER_RADIUS = 8;
    static constexpr float HOVER_LIFT_AMOUNT = 2.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GridItemComponent)
};

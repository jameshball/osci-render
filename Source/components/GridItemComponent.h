#pragma once
#include <JuceHeader.h>
#include "../LookAndFeel.h"
#include "HoverAnimationMixin.h"
#include "SvgButton.h"

// Generic grid item with name, icon, and id
class GridItemComponent : public HoverAnimationMixin, public juce::SettableTooltipClient
{
public:
    GridItemComponent(const juce::String& name, const juce::String& icon, const juce::String& id, juce::Colour iconTint = juce::Colours::white.withAlpha(0.7f));
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
    std::function<void()> onLockedClick;

    void setInteractive(bool shouldBeInteractive);
    bool isInteractive() const { return interactive; }

    void setDescription(const juce::String& text);
    const juce::String& getDescription() const { return description; }

    void setLocked(bool shouldBeLocked);
    bool isLocked() const { return locked; }

    int getPreferredHeight(int width) const;

private:
    struct TextLayoutMetrics
    {
        juce::TextLayout headingLayout;
        juce::TextLayout bodyLayout;
        float headingHeight = 0.0f;
        float bodyHeight = 0.0f;
        float totalHeight = 0.0f;
        bool hasDescription = false;
    };

    [[nodiscard]] TextLayoutMetrics computeTextLayouts(int textWidth) const;

    juce::String itemName;
    juce::String itemId;

    // Icon for the item
    std::unique_ptr<SvgButton> iconButton;
    SvgButton lockIcon;

    bool interactive { true };
    bool locked { false };
    juce::String description;
    juce::Colour iconColour;

    static constexpr int CORNER_RADIUS = 8;
    static constexpr int TEXT_HORIZONTAL_PADDING = 40;
    static constexpr int TITLE_DESCRIPTION_SPACING = 5;
    static constexpr float HOVER_LIFT_AMOUNT = 2.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GridItemComponent)
};

#pragma once
#include <JuceHeader.h>
#include "ScrollFadeMixin.h"
#include "GridItemComponent.h"

// Generic grid component that owns and lays out GridItemComponent children
class GridComponent : public juce::Component, private ScrollFadeMixin
{
public:
    GridComponent();
    ~GridComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void clearItems();
    void addItem(GridItemComponent* item); // takes ownership
    juce::OwnedArray<GridItemComponent>& getItems() { return items; }
    int calculateRequiredHeight(int availableWidth) const;

private:
    juce::Viewport viewport; // scroll container
    juce::Component content; // holds the grid items
    juce::OwnedArray<GridItemComponent> items;
    juce::FlexBox flexBox;

    static constexpr int ITEM_HEIGHT = 80;
    static constexpr int MIN_ITEM_WIDTH = 180;

    void layoutScrollFadeIfNeeded();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GridComponent)
};

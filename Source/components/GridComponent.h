#pragma once
#include <JuceHeader.h>
#include "ScrollFadeViewport.h"
#include "GridItemComponent.h"

// Generic grid component that owns and lays out GridItemComponent children
class GridComponent : public juce::Component
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

    // Configuration: when true (default), pad the final row with placeholders so it's centered.
    // When false, rows are left-aligned with no placeholders.
    void setUseCenteringPlaceholders(bool shouldCenter) { useCenteringPlaceholders = shouldCenter; resized(); }

    // Configuration: when true (default), GridComponent uses its own internal Viewport.
    // When false, the grid lays out directly without an internal scroll container (for embedding
    // inside a parent Viewport).
    void setUseViewport(bool shouldUseViewport);

private:
    ScrollFadeViewport viewport; // scroll container with fades
    juce::Component content; // holds the grid items
    juce::OwnedArray<GridItemComponent> items;
    juce::FlexBox flexBox;

    static constexpr int ITEM_HEIGHT = 80;
    static constexpr int MIN_ITEM_WIDTH = 180;

    bool useCenteringPlaceholders { true };
    bool useInternalViewport { true };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GridComponent)
};

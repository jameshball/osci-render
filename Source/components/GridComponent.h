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

    // When false, items will not respond to clicks or pointer cursor changes.
    void setItemsInteractive(bool shouldBeInteractive);

    // Adjust the fixed height allocated to each grid tile (default 80px).
    void setItemHeight(int newHeight);
    int getItemHeight() const { return itemHeight; }

    void setMinItemWidth(int newWidth);
    int getMinItemWidth() const { return minItemWidth; }
    int getItemWidthFor(int availableWidth) const;

private:
    ScrollFadeViewport viewport; // scroll container with fades
    juce::Component content; // holds the grid items
    juce::OwnedArray<GridItemComponent> items;
    juce::FlexBox flexBox;

    static constexpr int DEFAULT_ITEM_HEIGHT = 80;
    static constexpr int DEFAULT_MIN_ITEM_WIDTH = 200;

    bool useCenteringPlaceholders { true };
    bool useInternalViewport { true };
    bool itemsInteractive { true };
    int itemHeight { DEFAULT_ITEM_HEIGHT };
    int minItemWidth { DEFAULT_MIN_ITEM_WIDTH };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GridComponent)
};

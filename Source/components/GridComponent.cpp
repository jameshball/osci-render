#include "GridComponent.h"

GridComponent::GridComponent()
{
    // Setup scrollable viewport and content
    addAndMakeVisible(viewport);
    viewport.setViewedComponent(&content, false);
    viewport.setScrollBarsShown(true, false); // vertical only
    // Setup reusable bottom fade
    initScrollFade(*this);
    attachToViewport(viewport);
}

GridComponent::~GridComponent() = default;

void GridComponent::clearItems()
{
    items.clear();
    content.removeAllChildren();
}

void GridComponent::addItem(GridItemComponent* item)
{
    items.add(item);
    content.addAndMakeVisible(item);
}

void GridComponent::paint(juce::Graphics& g)
{
    // transparent background
}

void GridComponent::resized()
{
    auto bounds = getLocalBounds();
    viewport.setBounds(bounds);
    auto contentArea = viewport.getLocalBounds();
    // Lock content width to viewport width to avoid horizontal scrolling
    content.setSize(contentArea.getWidth(), content.getHeight());

    // Create FlexBox for responsive grid layout within content
    flexBox = juce::FlexBox();
    flexBox.flexWrap = juce::FlexBox::Wrap::wrap;
    flexBox.justifyContent = juce::FlexBox::JustifyContent::spaceBetween;
    flexBox.alignContent = juce::FlexBox::AlignContent::flexStart;
    flexBox.flexDirection = juce::FlexBox::Direction::row;

    // Determine fixed per-item width for this viewport width
    const int viewW = contentArea.getWidth();
    const int viewH = contentArea.getHeight();
    const int itemsPerRow = juce::jmax(1, viewW / MIN_ITEM_WIDTH);
    const int fixedItemWidth = (itemsPerRow > 0 ? viewW / itemsPerRow : viewW);

    // Add each item with a fixed width, and pad the final row with placeholders so it's centered
    const int total = items.size();
    const int fullRows = (itemsPerRow > 0 ? total / itemsPerRow : 0);
    const int remainder = (itemsPerRow > 0 ? total % itemsPerRow : 0);

    auto addItemFlex = [&](juce::Component* c)
    {
        flexBox.items.add(juce::FlexItem(*c)
                              .withMinWidth((float) fixedItemWidth)
                              .withMaxWidth((float) fixedItemWidth)
                              .withHeight(80.0f)
                              .withFlex(1.0f)
                              .withMargin(juce::FlexItem::Margin(0)));
    };

    auto addPlaceholder = [&]()
    {
        // Placeholder occupies a slot visually but has no component; ensures last row is centered
        juce::FlexItem placeholder((float) fixedItemWidth, 80.0f);
        placeholder.flexGrow = 1.0f; // match item flex for consistent spacing
        placeholder.margin = juce::FlexItem::Margin(0);
        flexBox.items.add(std::move(placeholder));
    };

    int index = 0;
    // Add complete rows
    for (int r = 0; r < fullRows; ++r)
        for (int c = 0; c < itemsPerRow; ++c)
            addItemFlex(items.getUnchecked(index++));

    // Add last row centered with balanced placeholders
    if (remainder > 0)
    {
        const int missing = itemsPerRow - remainder;
        const int leftPad = missing / 2;
        const int rightPad = missing - leftPad;

        for (int i = 0; i < leftPad; ++i) addPlaceholder();
        for (int i = 0; i < remainder; ++i) addItemFlex(items.getUnchecked(index++));
        for (int i = 0; i < rightPad; ++i) addPlaceholder();
    }

    // Compute required content height
    const int requiredHeight = calculateRequiredHeight(viewW);

    // If content is shorter than viewport, make content at least as tall as viewport
    int yOffset = 0;
    if (requiredHeight < viewH) {
        content.setSize(viewW, viewH);
        yOffset = (viewH - requiredHeight) / 2;
    } else {
        content.setSize(viewW, requiredHeight);
    }
    // Layout items within content at the computed offset
    flexBox.performLayout(juce::Rectangle<float>(0.0f, (float) yOffset, (float) viewW, (float) requiredHeight));

    // Layout bottom scroll fade over the viewport area
    layoutScrollFadeIfNeeded();
}

int GridComponent::calculateRequiredHeight(int availableWidth) const
{
    if (items.isEmpty())
        return 80; // ITEM_HEIGHT

    // Calculate how many items can fit per row
    int itemsPerRow = juce::jmax(1, availableWidth / MIN_ITEM_WIDTH);

    // Calculate number of rows needed
    int numRows = (items.size() + itemsPerRow - 1) / itemsPerRow; // Ceiling division

    return numRows * 80; // ITEM_HEIGHT
}

void GridComponent::layoutScrollFadeIfNeeded()
{
    layoutScrollFade(viewport.getBounds(), true, 48);
}

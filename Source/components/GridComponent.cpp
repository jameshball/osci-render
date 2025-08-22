#include "GridComponent.h"

GridComponent::GridComponent()
{
    // Default: use internal viewport
    addAndMakeVisible(viewport);
    viewport.setViewedComponent(&content, false);
    viewport.setScrollBarsShown(true, false); // vertical only
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
    juce::Rectangle<int> contentArea;
    if (useInternalViewport)
    {
        viewport.setBounds(bounds);
    viewport.setFadeVisible(true);
        contentArea = viewport.getLocalBounds();
        // Lock content width to viewport width to avoid horizontal scrolling
        content.setSize(contentArea.getWidth(), content.getHeight());
    }
    else
    {
        // No internal viewport: lay out content directly within our bounds
    viewport.setBounds(0, 0, 0, 0);
    viewport.setFadeVisible(false);
        contentArea = bounds;
        content.setBounds(contentArea);
        content.setSize(contentArea.getWidth(), contentArea.getHeight());
    }

    // Create FlexBox for responsive grid layout within content
    flexBox = juce::FlexBox();
    flexBox.flexWrap = juce::FlexBox::Wrap::wrap;
    flexBox.justifyContent = useCenteringPlaceholders
        ? juce::FlexBox::JustifyContent::spaceBetween
        : juce::FlexBox::JustifyContent::flexStart;
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

    // Add last row; optionally centered with placeholders or left-aligned
    if (remainder > 0)
    {
        if (useCenteringPlaceholders)
        {
            const int missing = itemsPerRow - remainder;
            const int leftPad = missing / 2;
            const int rightPad = missing - leftPad;

            for (int i = 0; i < leftPad; ++i) addPlaceholder();
            for (int i = 0; i < remainder; ++i) addItemFlex(items.getUnchecked(index++));
            for (int i = 0; i < rightPad; ++i) addPlaceholder();
        }
        else
        {
            for (int i = 0; i < remainder; ++i) addItemFlex(items.getUnchecked(index++));
        }
    }

    // Compute required content height
    const int requiredHeight = calculateRequiredHeight(viewW);

    // If content is shorter than container, fill height; otherwise, set to required height
    int yOffset = 0;
    if (useInternalViewport)
    {
        const int viewH = contentArea.getHeight();
        if (requiredHeight < viewH) {
            content.setSize(viewW, viewH);
            yOffset = (viewH - requiredHeight) / 2;
        } else {
            content.setSize(viewW, requiredHeight);
        }
        // Layout items within content at the computed offset
        flexBox.performLayout(juce::Rectangle<float>(0.0f, (float) yOffset, (float) viewW, (float) requiredHeight));
    }
    else
    {
        content.setSize(viewW, requiredHeight);
        flexBox.performLayout(juce::Rectangle<float>(0.0f, 0.0f, (float) viewW, (float) requiredHeight));
    }
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


void GridComponent::setUseViewport(bool shouldUseViewport)
{
    if (useInternalViewport == shouldUseViewport)
        return;

    useInternalViewport = shouldUseViewport;

    if (useInternalViewport)
    {
        // Reattach content to viewport and attach fade listeners
        if (viewport.getViewedComponent() != &content)
            viewport.setViewedComponent(&content, false);
    }
    else
    {
    // Hide viewport and lay out items directly
        viewport.setViewedComponent(nullptr, false);
        if (content.getParentComponent() != this)
            addAndMakeVisible(content);
    }
    resized();
}

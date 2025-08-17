#include "EffectTypeGridComponent.h"
#include "../LookAndFeel.h"
#include <unordered_set>
#include <algorithm>
#include <numeric>

EffectTypeGridComponent::EffectTypeGridComponent(OscirenderAudioProcessor& processor)
    : audioProcessor(processor)
{
    // Setup scrollable viewport and content
    addAndMakeVisible(viewport);
    viewport.setViewedComponent(&content, false);
    viewport.setScrollBarsShown(true, false); // vertical only
    // Setup reusable bottom fade
    initScrollFade(*this);
    attachToViewport(viewport);
    setupEffectItems();
    setSize(400, 200);
    addAndMakeVisible(cancelButton);
    cancelButton.onClick = [this]() {
        if (onCanceled) onCanceled();
    };
    refreshDisabledStates();
}

EffectTypeGridComponent::~EffectTypeGridComponent() = default;

void EffectTypeGridComponent::setupEffectItems()
{
    // Clear existing items
    effectItems.clear();
    content.removeAllChildren();
    
    // Get effect types directly from the audio processor's toggleableEffects
    juce::SpinLock::ScopedLockType lock(audioProcessor.effectsLock);
    const int n = (int) audioProcessor.toggleableEffects.size();
    std::vector<int> order(n);
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [this](int a, int b) {
        auto ea = audioProcessor.toggleableEffects[a];
        auto eb = audioProcessor.toggleableEffects[b];
        const int cmp = ea->getName().compareIgnoreCase(eb->getName());
        if (cmp != 0)
            return cmp < 0; // ascending alphabetical, case-insensitive
        // Stable tie-breaker to ensure deterministic layout
        return ea->getId().compare(eb->getId()) < 0;
    });

    for (int idx : order)
    {
        auto effect = audioProcessor.toggleableEffects[idx];
        // Extract effect name from the effect
        juce::String effectName = effect->getName();

        // Create new item component
        auto* item = new EffectTypeItemComponent(effectName, effect->getIcon(), effect->getId());

        // Set up callback to forward effect selection
        item->onEffectSelected = [this](const juce::String& effectId) {
            if (onEffectSelected)
                onEffectSelected(effectId);
        };
        // Hover preview: request temporary preview of this effect while hovered
        item->onHoverStart = [this](const juce::String& effectId) {
            if (audioProcessor.getGlobalBoolValue("previewEffectOnHover", true)) {
                juce::SpinLock::ScopedLockType lock(audioProcessor.effectsLock);
                audioProcessor.setPreviewEffectId(effectId);
            }
        };
        item->onHoverEnd = [this]() {
            if (audioProcessor.getGlobalBoolValue("previewEffectOnHover", true)) {
                juce::SpinLock::ScopedLockType lock(audioProcessor.effectsLock);
                audioProcessor.clearPreviewEffect();
            }
        };

        effectItems.add(item);
        content.addAndMakeVisible(item);
    }
}

void EffectTypeGridComponent::refreshDisabledStates()
{
    juce::SpinLock::ScopedLockType lock(audioProcessor.effectsLock);
    // Build a quick lookup of selected ids
    std::unordered_set<std::string> selectedIds;
    selectedIds.reserve((size_t) audioProcessor.toggleableEffects.size());
    bool anySelected = false;
    for (const auto& eff : audioProcessor.toggleableEffects) {
        const bool isSelected = (eff->selected == nullptr) ? true : eff->selected->getBoolValue();
        if (isSelected) {
            anySelected = true;
            selectedIds.insert(eff->getId().toStdString());
        }
    }
    for (auto* item : effectItems) {
        const bool disable = selectedIds.find(item->getEffectId().toStdString()) != selectedIds.end();
        item->setEnabled(! disable);
    }
    cancelButton.setVisible(anySelected);
    // Update fade visibility/layout in case scrollability changed
    layoutScrollFade(viewport.getBounds(), true, 48);
}

void EffectTypeGridComponent::paint(juce::Graphics& g)
{
    // No background - make component transparent
}

void EffectTypeGridComponent::resized()
{
    auto bounds = getLocalBounds();
    auto topBar = bounds.removeFromTop(30);
    cancelButton.setBounds(topBar.removeFromRight(80).reduced(4));
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

    // Add each effect item with a fixed width, and pad the final row with placeholders so it's centered
    const int total = effectItems.size();
    const int fullRows = (itemsPerRow > 0 ? total / itemsPerRow : 0);
    const int remainder = (itemsPerRow > 0 ? total % itemsPerRow : 0);

    auto addItemFlex = [&](juce::Component* c)
    {
        flexBox.items.add(juce::FlexItem(*c)
                              .withMinWidth((float) fixedItemWidth)
                              .withMaxWidth((float) fixedItemWidth)
                              .withHeight((float) ITEM_HEIGHT)
                              .withFlex(1.0f) // keep existing flex behaviour; fixed max width holds size
                              .withMargin(juce::FlexItem::Margin(0)));
    };

    auto addPlaceholder = [&]()
    {
        // Placeholder occupies a slot visually but has no component; ensures last row is centered
        juce::FlexItem placeholder((float) fixedItemWidth, (float) ITEM_HEIGHT);
        placeholder.flexGrow = 1.0f; // match item flex for consistent spacing
        placeholder.margin = juce::FlexItem::Margin(0);
        flexBox.items.add(std::move(placeholder));
    };

    int index = 0;
    // Add complete rows
    for (int r = 0; r < fullRows; ++r)
        for (int c = 0; c < itemsPerRow; ++c)
            addItemFlex(effectItems.getUnchecked(index++));

    // Add last row centered with balanced placeholders
    if (remainder > 0)
    {
        const int missing = itemsPerRow - remainder;
        const int leftPad = missing / 2;
        const int rightPad = missing - leftPad;

        for (int i = 0; i < leftPad; ++i) addPlaceholder();
        for (int i = 0; i < remainder; ++i) addItemFlex(effectItems.getUnchecked(index++));
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
    layoutScrollFade(viewport.getBounds(), true, 48);
}

int EffectTypeGridComponent::calculateRequiredHeight(int availableWidth) const
{
    if (effectItems.isEmpty())
        return ITEM_HEIGHT;
    
    // Calculate how many items can fit per row
    int itemsPerRow = juce::jmax(1, availableWidth / MIN_ITEM_WIDTH);
    
    // Calculate number of rows needed
    int numRows = (effectItems.size() + itemsPerRow - 1) / itemsPerRow; // Ceiling division
    
    return numRows * ITEM_HEIGHT;
}

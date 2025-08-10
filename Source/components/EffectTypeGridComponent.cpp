#include "EffectTypeGridComponent.h"
#include "../LookAndFeel.h"
#include <unordered_set>

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
    for (const auto& effect : audioProcessor.toggleableEffects)
    {
        // Extract effect name from the effect ID or first parameter name
        juce::String effectName = effect->getName();
        
        // Create new item component
        auto* item = new EffectTypeItemComponent(effectName, effect->getIcon(), effect->getId());
        
        // Set up callback to forward effect selection
        item->onEffectSelected = [this](const juce::String& effectId) {
            if (onEffectSelected)
                onEffectSelected(effectId);
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
    
    // Add each effect item as a FlexItem with flex-grow to fill available space
    for (auto* item : effectItems)
    {
        flexBox.items.add(juce::FlexItem(*item)
                         .withMinWidth(MIN_ITEM_WIDTH)
                         .withHeight(ITEM_HEIGHT)
                         .withFlex(1.0f)  // Allow items to grow to fill available space
                         .withMargin(juce::FlexItem::Margin(0)));
    }

    // Compute required content height
    const int viewW = contentArea.getWidth();
    const int viewH = contentArea.getHeight();
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

#include "EffectTypeGridComponent.h"
#include "../LookAndFeel.h"

EffectTypeGridComponent::EffectTypeGridComponent(OscirenderAudioProcessor& processor)
    : audioProcessor(processor)
{
    setupEffectItems();
    setSize(400, 200); // Default size, will be resized by parent
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

EffectTypeGridComponent::~EffectTypeGridComponent() = default;

void EffectTypeGridComponent::setupEffectItems()
{
    // Clear existing items
    effectItems.clear();
    
    // Get effect types directly from the audio processor's toggleableEffects
    juce::SpinLock::ScopedLockType lock(audioProcessor.effectsLock);
    for (const auto& effect : audioProcessor.toggleableEffects)
    {
        // Extract effect name from the effect ID or first parameter name
        juce::String effectName = effect->getName();
        
        // Create new item component
        auto* item = new EffectTypeItemComponent(effectName, effect->getId());
        
        // Set up callback to forward effect selection
        item->onEffectSelected = [this](const juce::String& effectId) {
            if (onEffectSelected)
                onEffectSelected(effectId);
        };
        
        effectItems.add(item);
        addAndMakeVisible(item);
    }
}

void EffectTypeGridComponent::paint(juce::Graphics& g)
{
    // No background - make component transparent
}

void EffectTypeGridComponent::resized()
{
    auto bounds = getLocalBounds();
    
    // Create FlexBox for responsive grid layout
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
    
    flexBox.performLayout(bounds.toFloat());
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

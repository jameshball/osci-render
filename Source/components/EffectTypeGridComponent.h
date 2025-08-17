#pragma once
#include <JuceHeader.h>
#include "../PluginProcessor.h"
#include "EffectTypeItemComponent.h"
#include "ScrollFadeMixin.h"

class EffectTypeGridComponent : public juce::Component, private ScrollFadeMixin
{
public:
    EffectTypeGridComponent(OscirenderAudioProcessor& processor);
    ~EffectTypeGridComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    int calculateRequiredHeight(int availableWidth) const;
    std::function<void(const juce::String& effectId)> onEffectSelected;
    std::function<void()> onCanceled; // optional cancel handler
    void refreshDisabledStates(); // grey-out items that are already selected

private:
    OscirenderAudioProcessor& audioProcessor;
    juce::Viewport viewport; // scroll container
    juce::Component content; // holds the grid items
    juce::OwnedArray<EffectTypeItemComponent> effectItems;
    juce::FlexBox flexBox;
    juce::TextButton cancelButton { "Cancel" };
    
    static constexpr int ITEM_HEIGHT = 80;
    static constexpr int MIN_ITEM_WIDTH = 180;
    
    void setupEffectItems();
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EffectTypeGridComponent)
};

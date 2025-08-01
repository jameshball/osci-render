#pragma once
#include <JuceHeader.h>
#include "../PluginProcessor.h"
#include "EffectTypeItemComponent.h"

class EffectTypeGridComponent : public juce::Component
{
public:
    EffectTypeGridComponent(OscirenderAudioProcessor& processor);
    ~EffectTypeGridComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    int calculateRequiredHeight(int availableWidth) const;
    std::function<void(const juce::String& effectId)> onEffectSelected;

private:
    OscirenderAudioProcessor& audioProcessor;
    juce::OwnedArray<EffectTypeItemComponent> effectItems;
    juce::FlexBox flexBox;
    
    static constexpr int ITEM_HEIGHT = 80;
    static constexpr int MIN_ITEM_WIDTH = 180;
    
    void setupEffectItems();
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EffectTypeGridComponent)
};

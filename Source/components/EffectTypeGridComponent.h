#pragma once
#include <JuceHeader.h>
#include "../PluginProcessor.h"
#include "GridComponent.h"

// Effect-specific wrapper that declares which items appear in the grid
class EffectTypeGridComponent : public juce::Component
{
public:
    EffectTypeGridComponent(OscirenderAudioProcessor& processor);
    ~EffectTypeGridComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    std::function<void(const juce::String& effectId)> onEffectSelected;
    std::function<void()> onCanceled; // optional cancel handler
    void refreshDisabledStates(); // grey-out items that are already selected

private:
    OscirenderAudioProcessor& audioProcessor;
    GridComponent grid;
    juce::TextButton cancelButton { "Cancel" };

    void setupEffectItems();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EffectTypeGridComponent)
};

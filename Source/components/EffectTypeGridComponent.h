#pragma once
#include <JuceHeader.h>
#include "../PluginProcessor.h"
#include "GridComponent.h"

class OscirenderAudioProcessorEditor;

// Effect-specific wrapper that declares which items appear in the grid
class EffectTypeGridComponent : public juce::Component
{
public:
    EffectTypeGridComponent(OscirenderAudioProcessor& processor, OscirenderAudioProcessorEditor& editor);
    ~EffectTypeGridComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    std::function<void(const juce::String& effectId)> onEffectSelected;
    std::function<void()> onCanceled; // optional cancel handler
    void refreshDisabledStates(); // grey-out items that are already selected

private:
    OscirenderAudioProcessor& audioProcessor;
    OscirenderAudioProcessorEditor& editor;
    GridComponent grid;
    juce::TextButton cancelButton { "Cancel" };

    void setupEffectItems();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EffectTypeGridComponent)
};

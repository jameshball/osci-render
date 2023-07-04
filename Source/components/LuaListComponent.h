#pragma once
#include <JuceHeader.h>
#include "../PluginProcessor.h"
#include "../audio/Effect.h"
#include "EffectComponent.h"

class LuaListComponent : public juce::Component
{
public:
    LuaListComponent(OscirenderAudioProcessor& p, Effect& effect);
    ~LuaListComponent();

    void resized() override;

protected:
    std::shared_ptr<EffectComponent> effectComponent;
private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LuaListComponent)
};

class LuaListBoxModel : public juce::ListBoxModel
{
public:
    LuaListBoxModel(juce::ListBox& lb, OscirenderAudioProcessor& p) : listBox(lb), audioProcessor(p) {}

    int getNumRows() override;
    void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override;
    juce::Component* refreshComponentForRow(int sliderNum, bool isRowSelected, juce::Component *existingComponentToUpdate) override;

private:
    int numSliders = 5;
    juce::ListBox& listBox;
    OscirenderAudioProcessor& audioProcessor;
};

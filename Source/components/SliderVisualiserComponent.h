#pragma once

#include <JuceHeader.h>
#include "../visualiser/VisualiserRenderer.h"
#include "../EffectPluginProcessor.h"
#include "../txt/TextParser.h"

class SliderVisualiserComponent : public juce::Component
{
public:
    SliderVisualiserComponent(EffectAudioProcessor& processor);
    ~SliderVisualiserComponent() override;

    void resized() override;
    void setValue(double newValue);
    double getValue() const;

    // Forwards to underlying slider
    void setRange(double newMinimum, double newMaximum, double newInterval = 0.0);
    void addListener(juce::Slider::Listener* listener);
    void removeListener(juce::Slider::Listener* listener);
    void onValueChange(std::function<void()> callback);
    
    // Set the label text to be displayed next to the slider
    void setLabel(const juce::String& labelText);
    
    // Update the visualiser display
    void updateVisualiser();

private:
    EffectAudioProcessor& audioProcessor;
    
    VisualiserRenderer sliderVisualiser{
        audioProcessor.visualiserParameters,
        audioProcessor.threadManager,
        512,
        60.0,
        "Slider"
    };

    juce::Slider slider;
    juce::String label;
    std::vector<std::unique_ptr<osci::Shape>> labelShapes;
    juce::Font labelFont{0.7f};
    
    // Proportion of the width allocated for the label
    const double labelWidthProportion = 0.35;
    
    void updateLabelShapes();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SliderVisualiserComponent)
};

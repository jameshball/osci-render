#pragma once

#include <JuceHeader.h>
#include "EffectPluginProcessor.h"
#include "visualiser/VisualiserRenderer.h"
#include "LookAndFeel.h"
#include "components/EffectComponent.h"

class EffectPluginEditor : public juce::AudioProcessorEditor {
public:
    EffectPluginEditor(EffectAudioProcessor&);
    ~EffectPluginEditor() override;

    void resized() override;

private:
    EffectAudioProcessor& audioProcessor;
    bool fullScreen = false;
public:
    OscirenderLookAndFeel lookAndFeel;

    VisualiserRenderer visualiser{
        audioProcessor.visualiserParameters,
        audioProcessor.threadManager,
        512,
        60.0,
        "Main"
    };
    
    VisualiserRenderer titleVisualiser{
        audioProcessor.visualiserParameters,
        audioProcessor.threadManager,
        512,
        60.0,
        "Title"
    };

    juce::SharedResourcePointer<juce::TooltipWindow> tooltipWindow;
    juce::DropShadower tooltipDropShadow{juce::DropShadow(juce::Colours::black.withAlpha(0.5f), 6, {0,0})};

    EffectComponent bitCrush{*audioProcessor.bitCrush};

#if JUCE_LINUX
    juce::OpenGLContext openGlContext;
#endif

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EffectPluginEditor)
};

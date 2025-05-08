#include "EffectPluginProcessor.h"
#include "EffectPluginEditor.h"

EffectPluginEditor::EffectPluginEditor(EffectAudioProcessor& p)
	: AudioProcessorEditor(&p), audioProcessor(p)
{
#if JUCE_LINUX
    // use OpenGL on Linux for much better performance. The default on Mac is CoreGraphics, and on Window is Direct2D which is much faster.
    openGlContext.attachTo(*getTopLevelComponent());
#endif

    setLookAndFeel(&lookAndFeel);
    
    audioProcessor.visualiserParameters.ambientEffect->setValue(0);
    
    addAndMakeVisible(visualiser);
    addAndMakeVisible(titleVisualiser);
    addAndMakeVisible(bitCrush);
    
    titleVisualiser.setCropRectangle(juce::Rectangle<float>(-0.1f, 0.35f, 1.2f, 0.3f));
    
    bitCrush.slider.onValueChange = [this] {
        audioProcessor.bitCrush->parameters[0]->setUnnormalisedValueNotifyingHost(bitCrush.slider.getValue());
    };

    setSize(600, 200);
    setResizable(false, false);

    tooltipDropShadow.setOwner(&tooltipWindow.get());
    tooltipWindow->setMillisecondsBeforeTipAppears(0);
}

void EffectPluginEditor::resized() {
    auto bounds = getLocalBounds();
    visualiser.setBounds(bounds.removeFromLeft(bounds.getHeight()));
    
    // Set bounds for titleVisualiser
    auto titleBounds = bounds.removeFromTop(100);
    titleVisualiser.setBounds(titleBounds);
    
    bitCrush.setBounds(bounds);
}

EffectPluginEditor::~EffectPluginEditor() {
    setLookAndFeel(nullptr);
    juce::Desktop::getInstance().setDefaultLookAndFeel(nullptr);
}

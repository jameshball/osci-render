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
    audioProcessor.visualiserParameters.shutterSync->setBoolValue(true);
    audioProcessor.visualiserParameters.upsamplingEnabled->setBoolValue(false);
    
    addAndMakeVisible(visualiser);
    addAndMakeVisible(titleVisualiser);
    addAndMakeVisible(sliderVisualiser);
    
    titleVisualiser.setCropRectangle(juce::Rectangle<float>(-0.1f, 0.35f, 1.2f, 0.3f));
      // Configure the slider visualiser component
    sliderVisualiser.onValueChange([this]() {
        audioProcessor.bitCrush->parameters[0]->setUnnormalisedValueNotifyingHost(sliderVisualiser.getValue());
    });
    
    // Set the label for the slider
    sliderVisualiser.setLabel("bit crush");

    setSize(600, 200);
    setResizable(false, false);

    tooltipDropShadow.setOwner(&tooltipWindow.get());
    tooltipWindow->setMillisecondsBeforeTipAppears(0);
    
    audioProcessor.bitCrush->addListener(0, this);
}

EffectPluginEditor::~EffectPluginEditor() {
    audioProcessor.bitCrush->removeListener(0, this);
    setLookAndFeel(nullptr);
    juce::Desktop::getInstance().setDefaultLookAndFeel(nullptr);
}

void EffectPluginEditor::resized() {
    auto bounds = getLocalBounds();
    visualiser.setBounds(bounds.removeFromLeft(bounds.getHeight()));
    
    // Set bounds for titleVisualiser
    auto titleBounds = bounds.removeFromTop(100);
    titleVisualiser.setBounds(titleBounds);
    
    // Set bounds for sliderVisualiser
    sliderVisualiser.setBounds(bounds);
}

void EffectPluginEditor::parameterValueChanged(int parameterIndex, float newValue) {
    if (parameterIndex == 0) {
        juce::MessageManager::getInstance()->callAsync([this, newValue]() {
            sliderVisualiser.setValue(newValue);
            // Update visualizer directly as setValue doesn't always trigger onChange
            sliderVisualiser.updateVisualiser();
        });
    }
}

void EffectPluginEditor::parameterGestureChanged(int parameterIndex, bool gestureIsStarting) {}

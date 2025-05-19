#include "SliderVisualiserComponent.h"

SliderVisualiserComponent::SliderVisualiserComponent(EffectAudioProcessor& processor)
    : audioProcessor(processor)
{
    sliderVisualiser.setCropRectangle(juce::Rectangle<float>(0.0f, 0.35f, 1.0f, 0.25f));
    sliderVisualiser.setInterceptsMouseClicks(false, false);
    
    // Configure slider to be invisible but functional
    slider.setLookAndFeel(nullptr); // Remove any look and feel
    slider.setColour(juce::Slider::trackColourId, juce::Colours::transparentBlack);
    slider.setColour(juce::Slider::backgroundColourId, juce::Colours::transparentBlack);
    slider.setColour(juce::Slider::thumbColourId, juce::Colours::transparentBlack);
    slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    slider.setColour(juce::Slider::textBoxTextColourId, juce::Colours::transparentBlack);
    slider.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    slider.setRange(0.0, 1.0, 0.001);
    
    // Setup default callback
    slider.onValueChange = [this] { updateVisualiser(); };
    
    // Set default label
    setLabel("Param");
    
    addAndMakeVisible(sliderVisualiser);
    addAndMakeVisible(slider);
}

SliderVisualiserComponent::~SliderVisualiserComponent()
{
}

void SliderVisualiserComponent::resized()
{
    auto bounds = getLocalBounds();
    sliderVisualiser.setBounds(bounds);
    
    // Scale down the slider to match the visual representation
    // It should only occupy the portion where the slider visuals appear (not the label area)
    int labelWidth = bounds.getWidth() * labelWidthProportion;
    auto sliderBounds = bounds.withTrimmedLeft(labelWidth - 5);
    slider.setBounds(sliderBounds.withTrimmedRight(10)); // Add some padding on the right
}

void SliderVisualiserComponent::setValue(double newValue)
{
    slider.setValue(newValue);
}

double SliderVisualiserComponent::getValue() const
{
    return slider.getValue();
}

void SliderVisualiserComponent::setRange(double newMinimum, double newMaximum, double newInterval)
{
    slider.setRange(newMinimum, newMaximum, newInterval);
}

void SliderVisualiserComponent::addListener(juce::Slider::Listener* listener)
{
    slider.addListener(listener);
}

void SliderVisualiserComponent::removeListener(juce::Slider::Listener* listener)
{
    slider.removeListener(listener);
}

void SliderVisualiserComponent::onValueChange(std::function<void()> callback)
{
    slider.onValueChange = [this, callback]() {
        updateVisualiser();
        callback();
    };
}

void SliderVisualiserComponent::setLabel(const juce::String& labelText)
{
    // Only update if the label has changed
    if (label != labelText) {
        label = labelText;
        updateLabelShapes();
        updateVisualiser();
    }
}

void SliderVisualiserComponent::updateLabelShapes()
{
    // Create text shapes using TextParser
    if (label.isNotEmpty()) {
        TextParser labelParser{label, labelFont};
        labelShapes = labelParser.draw();
        
        // Calculate the center of the label area
        double labelCenter = -1.0 + (labelWidthProportion * 1.0); // Center of the label area in oscilloscope space
        
        // Scale and position the label shapes to appear on the left side
        for (auto& shape : labelShapes) {
            // Scale down the text to fit nicely
            shape->scale(0.3, 0.3, 1.0);
            // Position centered in the label area
            shape->translate(labelCenter, 0.0, 0.0);
        }
    } else {
        labelShapes.clear();
    }
}

void SliderVisualiserComponent::updateVisualiser()
{
    // Create shapes for the slider indicator
    std::vector<std::unique_ptr<osci::Shape>> combinedShapes;
    
    // First add the label shapes
    for (auto& shape : labelShapes) {
        combinedShapes.push_back(shape->clone());
    }
    
    // Calculate the visualizer coordinates that exactly match our component layout
    // Convert component coordinates to oscilloscope coordinates (-1.0 to 1.0)
    double sliderStart = -1.0 + (labelWidthProportion * 2.0); // Scaled from component space to oscilloscope space
    double sliderEnd = 1.0;
    double sliderRange = sliderEnd - sliderStart;
    
    double value = slider.getValue();
    double circleRadius = 0.07;
    
    // Map the normalized slider value (0-1) to our visual slider range
    double xPos = sliderStart + (value * sliderRange);
    
    // Add the left line only if there's space for it (circle not at left edge)
    if (xPos - circleRadius > sliderStart) {
        combinedShapes.push_back(std::make_unique<osci::Line>(sliderStart, 0.0, xPos - circleRadius, 0.0));  // Left line
    }
    
    // Add the right line only if there's space for it (circle not at right edge)
    if (xPos + circleRadius < sliderEnd) {
        combinedShapes.push_back(std::make_unique<osci::Line>(xPos + circleRadius, 0.0, sliderEnd, 0.0));  // Right line
    }
    
    // Add circle
    combinedShapes.push_back(std::make_unique<osci::CircleArc>(xPos, 0.0, circleRadius, circleRadius, 0.0, 2.0 * M_PI));
    
    juce::SpinLock::ScopedLockType lock(audioProcessor.sliderLock);
    // Set the shapes in the renderer
    audioProcessor.sliderRenderer.setShapes(std::move(combinedShapes));
}

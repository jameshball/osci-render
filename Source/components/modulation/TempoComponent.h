#pragma once

#include <JuceHeader.h>
#include "../../PluginProcessor.h"
#include "../LabelledBarComponent.h"

// A compact draggable BPM display for standalone mode.
// Drag vertically to change tempo; double-click to type a value.
// Uses LabelledBarComponent for consistent dark-bar styling.
class TempoComponent : public LabelledBarComponent, public juce::AudioProcessorParameter::Listener, public juce::AsyncUpdater {
public:
    TempoComponent(OscirenderAudioProcessor& p);
    ~TempoComponent() override;

    juce::String getLabelText() const override { return "BPM"; }
    void paintContent(juce::Graphics& g, juce::Rectangle<int> area) override;
    void resizedContent(juce::Rectangle<int> area) override;

    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;

private:
    void parameterValueChanged(int, float) override { triggerAsyncUpdate(); }
    void parameterGestureChanged(int, bool) override {}
    void handleAsyncUpdate() override;
    void showInlineEditor();
    void commitInlineEditor();

    OscirenderAudioProcessor& audioProcessor;

    float dragStartValue = 120.0f;
    juce::Point<int> dragStartPos;
    bool dragging = false;

    std::unique_ptr<juce::TextEditor> inlineEditor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TempoComponent)
};

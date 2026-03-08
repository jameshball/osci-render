#pragma once

#include <JuceHeader.h>
#include "../../PluginProcessor.h"

// A compact draggable BPM display for standalone mode.
// Drag vertically to change tempo; double-click to type a value.
class TempoComponent : public juce::Component, public juce::AudioProcessorParameter::Listener, public juce::AsyncUpdater {
public:
    TempoComponent(OscirenderAudioProcessor& p);
    ~TempoComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

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

#pragma once

#include <JuceHeader.h>

#include "LookAndFeel.h"
#include "EffectsComponent.h"
#include "FrameSettingsComponent.h"
#include "LuaComponent.h"
#include "MidiComponent.h"
#include "PerspectiveComponent.h"
#include "PluginProcessor.h"
#include "TxtComponent.h"
#include "components/ExampleFilesGridComponent.h"
#include "components/FileControlsComponent.h"

class OscirenderAudioProcessorEditor;
class SettingsComponent : public juce::Component, public juce::AudioProcessorParameter::Listener {
public:
    SettingsComponent(OscirenderAudioProcessor&, OscirenderAudioProcessorEditor&);
    ~SettingsComponent() override;

    void resized() override;
    void paint(juce::Graphics& g) override;
    void parameterValueChanged(int parameterIndex, float newValue) override;
	void parameterGestureChanged(int parameterIndex, bool gestureIsStarting) override;
    void fileUpdated(juce::String fileName);
    void update();
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseDown(const juce::MouseEvent& event) override;
    // Show or hide the example files grid panel on the right-hand side
    void showExamples(bool shouldShow);

private:
    OscirenderAudioProcessor& audioProcessor;
    OscirenderAudioProcessorEditor& pluginEditor;

    FileControlsComponent fileControls{audioProcessor, pluginEditor};
    PerspectiveComponent perspective{audioProcessor, pluginEditor};
    TxtComponent txt{audioProcessor, pluginEditor};
    FrameSettingsComponent frame{audioProcessor, pluginEditor};
    EffectsComponent effects{audioProcessor, pluginEditor};
    MidiComponent midi{audioProcessor, pluginEditor};
    ExampleFilesGridComponent examples{audioProcessor};

    bool examplesVisible = false;

    juce::StretchableLayoutManager midiLayout;
    juce::StretchableLayoutResizerBar midiResizerBar{&midiLayout, 1, false};
    juce::StretchableLayoutManager mainLayout;
    juce::StretchableLayoutResizerBar mainResizerBar{&mainLayout, 1, true};

    juce::Component* toggleComponents[1] = {&midi};
    juce::StretchableLayoutManager* toggleLayouts[1] = {&midiLayout};
    double prefSizes[1] = {300};

    juce::Rectangle<int> volumeVisualiserBounds;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsComponent)
};

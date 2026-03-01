#pragma once

#include <JuceHeader.h>

#include "LookAndFeel.h"
#include "EffectsComponent.h"
#include "FrameSettingsComponent.h"
#include "LuaComponent.h"
#include "MidiComponent.h"
#include "PerspectiveComponent.h"
#include "PluginProcessor.h"
#include "components/EnvelopeComponent.h"
#include "components/OpenFileComponent.h"
#include "components/FileControlsComponent.h"
#include "components/lfo/LfoComponent.h"

class OscirenderAudioProcessorEditor;
class SettingsComponent : public juce::Component, public juce::AudioProcessorParameter::Listener, private juce::Timer {
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
    FrameSettingsComponent frame{audioProcessor, pluginEditor};
    EffectsComponent effects{audioProcessor, pluginEditor};
    MidiComponent midi{audioProcessor, pluginEditor};
    OpenFileComponent examples{audioProcessor};

    // Free-standing components (previously inside MidiComponent)
    EnvelopeContainerComponent envelope;
    LfoComponent lfo;
    juce::MidiKeyboardComponent keyboard;

    bool examplesVisible = false;

    // Three-column horizontal layout: visColumn | resizer | effectsColumn | resizer2 | rightColumn
    juce::StretchableLayoutManager mainLayout;
    juce::StretchableLayoutResizerBar mainResizerBar{&mainLayout, 1, true};
    juce::StretchableLayoutResizerBar midiResizerBar{&mainLayout, 3, true};

    juce::Rectangle<int> volumeVisualiserBounds;

    // Envelope flow-marker animation timer
    void timerCallback() override;

    // DAHDSR parameter listener helper
    struct DahdsrListener : public juce::AudioProcessorParameter::Listener, public juce::AsyncUpdater {
        SettingsComponent& owner;
        DahdsrListener(SettingsComponent& o) : owner(o) {}
        void parameterValueChanged(int, float) override { triggerAsyncUpdate(); }
        void parameterGestureChanged(int, bool) override {}
        void handleAsyncUpdate() override;
    };
    DahdsrListener dahdsrListener{*this};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsComponent)
};

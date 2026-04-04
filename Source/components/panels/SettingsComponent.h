#pragma once

#include <JuceHeader.h>

#include "../CustomMidiKeyboardComponent.h"
#include "../ScrollFadeViewport.h"

#include "../../LookAndFeel.h"
#include "EffectsComponent.h"
#include "FrameSettingsComponent.h"
#include "LuaComponent.h"
#include "MidiComponent.h"
#include "QuickControlsBar.h"
#include "../../PluginProcessor.h"
#include "../modulation/EnvelopeComponent.h"
#include "../OpenFileComponent.h"
#include "../FileControlsComponent.h"
#if OSCI_PREMIUM
#include "../FractalComponent.h"
#endif
#include "../modulation/LfoComponent.h"
#include "../modulation/RandomComponent.h"
#include "../modulation/SidechainComponent.h"

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
    QuickControlsBar quickControls{audioProcessor, pluginEditor};
    FrameSettingsComponent frame{audioProcessor, pluginEditor};
    EffectsComponent effects{audioProcessor, pluginEditor};
    MidiComponent midi{audioProcessor, pluginEditor};
#if OSCI_PREMIUM
    FractalComponent fractalEditor{audioProcessor, pluginEditor};
#endif
    OpenFileComponent examples{audioProcessor};

    // Free-standing components (previously inside MidiComponent)
    EnvelopeComponent envelope;
    std::unique_ptr<LfoComponent> lfo;
    std::unique_ptr<RandomComponent> random;
    std::unique_ptr<SidechainComponent> sidechain;
    ScrollFadeViewport keyboardViewport;
    juce::CustomMidiKeyboardComponent keyboard;

    bool examplesVisible = false;

    // Three-column horizontal layout: visColumn | resizer | effectsColumn | resizer2 | rightColumn
    juce::StretchableLayoutManager mainLayout;
    juce::StretchableLayoutResizerBar mainResizerBar{&mainLayout, 1, true};
    juce::StretchableLayoutResizerBar midiResizerBar{&mainLayout, 3, true};

    juce::Rectangle<int> volumeVisualiserBounds;
    juce::Rectangle<int> keyboardPanelBounds;

    // Envelope flow-marker animation timer
    void timerCallback() override;

    // Proxy components whose bounds are set by layOutComponents() in resized().
    // They persist into the deferred layoutChildren() call so the async path
    // reads the same column geometry that was computed synchronously.
    juce::Component layoutVisColumnProxy, layoutEffectsColumnProxy, layoutRightColumnProxy;

    // Deferred child-layout updater – coalesces rapid resizer-bar drag events
    // so that the expensive child setBounds cascade runs at most once per
    // message-loop iteration (≤60fps) while the resizerbar itself tracks
    // the cursor synchronously at full rate.
    struct ChildLayoutUpdater : public juce::AsyncUpdater {
        SettingsComponent& owner;
        explicit ChildLayoutUpdater(SettingsComponent& o) : owner(o) {}
        void handleAsyncUpdate() override;
    };
    ChildLayoutUpdater childLayoutUpdater{*this};

    void layoutChildren();

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

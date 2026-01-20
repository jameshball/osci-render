#include "MidiComponent.h"
#include "PluginEditor.h"
#include "audio/DahdsrEnvelope.h"

MidiComponent::MidiComponent(OscirenderAudioProcessor& p, OscirenderAudioProcessorEditor& editor) : audioProcessor(p), pluginEditor(editor), envelope(p) {
    setText("MIDI Settings");

    addAndMakeVisible(midiToggle);
    addAndMakeVisible(voicesSlider);
    addAndMakeVisible(voicesLabel);
    addAndMakeVisible(keyboard);

    voicesSlider.setRange(1, 16, 1);
    voicesSlider.setValue(audioProcessor.voices->getValueUnnormalised(), juce::dontSendNotification);
    voicesSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);

    voicesLabel.setText("Voices", juce::dontSendNotification);
    voicesLabel.attachToComponent(&voicesSlider, true);
    voicesLabel.setTooltip("Number of voices for the synth to use. Larger numbers will use more CPU, and may cause audio glitches.");

    voicesSlider.onValueChange = [this]() {
        audioProcessor.voices->setUnnormalisedValueNotifyingHost(voicesSlider.getValue());
    };

    audioProcessor.voices->addListener(this);

    addAndMakeVisible(envelope);
    envelope.setDahdsrParams(audioProcessor.getCurrentDahdsrParams());
    envelope.setGrid(EnvelopeComponent::GridBoth, EnvelopeComponent::GridNone, 0.1, 0.25);

    // UI-only animation for note flow markers.
    startTimerHz(60);

    if (juce::JUCEApplicationBase::isStandaloneApp()) {
        addAndMakeVisible(midiSettingsButton);
        midiSettingsButton.onClick = [this]() {
            pluginEditor.openAudioSettings();
        };
    }

    audioProcessor.attackTime->addListener(this);
    audioProcessor.delayTime->addListener(this);
    audioProcessor.attackShape->addListener(this);
    audioProcessor.holdTime->addListener(this);
    audioProcessor.decayTime->addListener(this);
    audioProcessor.decayShape->addListener(this);
    audioProcessor.sustainLevel->addListener(this);
    audioProcessor.releaseTime->addListener(this);
    audioProcessor.releaseShape->addListener(this);

    handleAsyncUpdate();
}

MidiComponent::~MidiComponent() {
    stopTimer();
    audioProcessor.attackTime->removeListener(this);
    audioProcessor.delayTime->removeListener(this);
    audioProcessor.attackShape->removeListener(this);
    audioProcessor.holdTime->removeListener(this);
    audioProcessor.decayTime->removeListener(this);
    audioProcessor.decayShape->removeListener(this);
    audioProcessor.sustainLevel->removeListener(this);
    audioProcessor.releaseTime->removeListener(this);
    audioProcessor.releaseShape->removeListener(this);

    audioProcessor.voices->removeListener(this);
}

void MidiComponent::timerCallback() {
    // Only show flow markers when MIDI is enabled
    if (!audioProcessor.midiEnabled->getBoolValue()) {
        envelope.getEnvelopeComponent()->resetFlowPersistenceForUi();
        return;
    }


    // Fixed slots (one per voice) so the envelope can draw seamless motion streaks.
    constexpr int kMax = OscirenderAudioProcessor::kMaxUiVoices;
    double times[kMax];
    bool anyActive = false;
    for (int i = 0; i < kMax; ++i) {
        if (audioProcessor.uiVoiceActive[i].load(std::memory_order_relaxed)) {
            times[i] = audioProcessor.uiVoiceEnvelopeTimeSeconds[i].load(std::memory_order_relaxed);
            anyActive = true;
        } else {
            times[i] = -1.0;
        }
    }

    if (!anyActive) {
        // Stop pushing markers; the envelope component will keep repainting to fade the existing trail.
        envelope.getEnvelopeComponent()->clearFlowMarkerTimesForUi();
        return;
    }

    envelope.getEnvelopeComponent()->setFlowMarkerTimesForUi(times, kMax);
}

void MidiComponent::parameterValueChanged(int parameterIndex, float newValue) {
    triggerAsyncUpdate();
}

void MidiComponent::parameterGestureChanged(int parameterIndex, bool gestureIsStarting) {}

void MidiComponent::handleAsyncUpdate() {
    voicesSlider.setValue(audioProcessor.voices->getValueUnnormalised(), juce::dontSendNotification);

    envelope.setDahdsrParams(audioProcessor.getCurrentDahdsrParams());
}

void MidiComponent::resized() {
    auto area = getLocalBounds().withTrimmedTop(20).reduced(20);
    auto topRow = area.removeFromTop(30);
    midiToggle.setBounds(topRow.removeFromLeft(120).translated(0, 1));
    topRow.removeFromLeft(80);
    voicesSlider.setBounds(topRow.removeFromLeft(250));
    if (midiSettingsButton.isVisible()) {
        midiSettingsButton.setBounds(topRow.removeFromRight(160));
    }
    area.removeFromTop(5);
    keyboard.setBounds(area.removeFromBottom(50));
    envelope.setBounds(area);
}

void MidiComponent::paint(juce::Graphics& g) {
    juce::GroupComponent::paint(g);
}

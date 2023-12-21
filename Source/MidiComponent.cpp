#include "MidiComponent.h"
#include "PluginEditor.h"

MidiComponent::MidiComponent(OscirenderAudioProcessor& p, OscirenderAudioProcessorEditor& editor) : audioProcessor(p), pluginEditor(editor) {
    setText("MIDI Settings");

    addAndMakeVisible(midiToggle);
    addAndMakeVisible(voicesSlider);
    addAndMakeVisible(voicesLabel);
    addAndMakeVisible(keyboard);

    midiToggle.setToggleState(audioProcessor.midiEnabled->getBoolValue(), juce::dontSendNotification);
    midiToggle.setTooltip("Enable MIDI input for the synth. If disabled, the synth will play a constant tone, as controlled by the frequency slider.");
    
    midiToggle.onClick = [this]() {
        audioProcessor.midiEnabled->setBoolValueNotifyingHost(midiToggle.getToggleState());
    };

    audioProcessor.midiEnabled->addListener(this);

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
    envelope.setAdsrMode(true);
    envelope.setEnv(audioProcessor.adsrEnv);
    envelope.addListener(&audioProcessor);
    envelope.setGrid(EnvelopeComponent::GridBoth, EnvelopeComponent::GridNone, 0.1, 0.25);

    audioProcessor.attackTime->addListener(this);
    audioProcessor.attackLevel->addListener(this);
    audioProcessor.attackShape->addListener(this);
    audioProcessor.decayTime->addListener(this);
    audioProcessor.decayShape->addListener(this);
    audioProcessor.sustainLevel->addListener(this);
    audioProcessor.releaseTime->addListener(this);
    audioProcessor.releaseShape->addListener(this);

    handleAsyncUpdate();
}

MidiComponent::~MidiComponent() {
    envelope.removeListener(&audioProcessor);
    audioProcessor.attackTime->removeListener(this);
    audioProcessor.attackLevel->removeListener(this);
    audioProcessor.attackShape->removeListener(this);
    audioProcessor.decayTime->removeListener(this);
    audioProcessor.decayShape->removeListener(this);
    audioProcessor.sustainLevel->removeListener(this);
    audioProcessor.releaseTime->removeListener(this);
    audioProcessor.releaseShape->removeListener(this);

    audioProcessor.midiEnabled->removeListener(this);
    audioProcessor.voices->removeListener(this);
}

void MidiComponent::parameterValueChanged(int parameterIndex, float newValue) {
    triggerAsyncUpdate();
}

void MidiComponent::parameterGestureChanged(int parameterIndex, bool gestureIsStarting) {}

void MidiComponent::handleAsyncUpdate() {
    midiToggle.setToggleState(audioProcessor.midiEnabled->getBoolValue(), juce::dontSendNotification);
    voicesSlider.setValue(audioProcessor.voices->getValueUnnormalised(), juce::dontSendNotification);

    Env newEnv = Env(
        { 
            0.0,
            audioProcessor.attackLevel->getValueUnnormalised(),
            audioProcessor.sustainLevel->getValueUnnormalised(),
            0.0
        },
        {
            audioProcessor.attackTime->getValueUnnormalised(),
            audioProcessor.decayTime->getValueUnnormalised(),
            audioProcessor.releaseTime->getValueUnnormalised()
        },
        std::vector<EnvCurve>{ audioProcessor.attackShape->getValueUnnormalised(), audioProcessor.decayShape->getValueUnnormalised(), audioProcessor.releaseShape->getValueUnnormalised() },
        2
    );

    {
        juce::SpinLock::ScopedLockType lock(audioProcessor.effectsLock);
        audioProcessor.adsrEnv = newEnv;
    }

    envelope.setEnv(newEnv);
}

void MidiComponent::resized() {
    auto area = getLocalBounds().withTrimmedTop(20).reduced(20);
    auto topRow = area.removeFromTop(30);
    midiToggle.setBounds(topRow.removeFromLeft(120));
    topRow.removeFromLeft(80);
    voicesSlider.setBounds(topRow.removeFromLeft(250));
    area.removeFromTop(5);
    keyboard.setBounds(area.removeFromBottom(50));
    envelope.setBounds(area);
}

void MidiComponent::paint(juce::Graphics& g) {
    juce::GroupComponent::paint(g);
}

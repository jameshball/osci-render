#include "SosciPluginProcessor.h"
#include "SosciPluginEditor.h"
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

SosciPluginEditor::SosciPluginEditor(SosciAudioProcessor& p) : CommonPluginEditor(p, "sosci", "sosci", 1180, 750), audioProcessor(p) {
    initialiseMenuBar(model);
    addAndMakeVisible(volume);
    addAndMakeVisible(visualiserSettingsWrapper);

    resized();
}

SosciPluginEditor::~SosciPluginEditor() {
    menuBar.setModel(nullptr);
}

void SosciPluginEditor::paint(juce::Graphics& g) {
    g.fillAll(Colours::veryDark);
}

void SosciPluginEditor::resized() {
    auto area = getLocalBounds();

    menuBar.setBounds(area.removeFromTop(25));

    auto volumeArea = area.removeFromLeft(30);
    volume.setBounds(volumeArea.withSizeKeepingCentre(volumeArea.getWidth(), juce::jmin(volumeArea.getHeight(), 300)));

    auto settingsArea = area.removeFromRight(juce::jmax(juce::jmin(0.4 * getWidth(), 550.0), 350.0));
    visualiserSettings.setSize(settingsArea.getWidth(), 550);
    visualiserSettingsWrapper.setBounds(settingsArea);

    visualiser.setBounds(area);
}

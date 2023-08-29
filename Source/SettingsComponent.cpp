#include "SettingsComponent.h"
#include "PluginEditor.h"

SettingsComponent::SettingsComponent(OscirenderAudioProcessor& p, OscirenderAudioProcessorEditor& editor) : audioProcessor(p), pluginEditor(editor) {
    addAndMakeVisible(effects);
    addAndMakeVisible(main);
    addChildComponent(lua);
    addChildComponent(obj);
    addChildComponent(txt);
}


void SettingsComponent::resized() {
    auto area = getLocalBounds();
    auto effectsSection = area.removeFromRight(1.2 * pluginEditor.getWidth() / sections);
    area.removeFromLeft(5);
    area.removeFromRight(3);
    area.removeFromTop(5);
    area.removeFromBottom(5);

    main.setBounds(area);
    if (lua.isVisible() || obj.isVisible() || txt.isVisible()) {
        int height = txt.isVisible() ? 150 : 300;
        auto altEffectsSection = effectsSection.removeFromBottom(juce::jmin(effectsSection.getHeight() / 2, height));
        altEffectsSection.removeFromTop(3);
        altEffectsSection.removeFromLeft(2);
        altEffectsSection.removeFromRight(5);
        altEffectsSection.removeFromBottom(5);

        lua.setBounds(altEffectsSection);
        obj.setBounds(altEffectsSection);
        txt.setBounds(altEffectsSection);

        effectsSection.removeFromBottom(2);
    } else {
        effectsSection.removeFromBottom(5);
    }

    effectsSection.removeFromLeft(2);
    effectsSection.removeFromRight(5);
    effectsSection.removeFromTop(5);
    effects.setBounds(effectsSection);
}

void SettingsComponent::fileUpdated(juce::String fileName) {
    juce::String extension = fileName.fromLastOccurrenceOf(".", true, false);
    lua.setVisible(false);
    obj.setVisible(false);
    txt.setVisible(false);
    if (fileName.isEmpty()) {
        // do nothing
    } else if (extension == ".lua") {
        lua.setVisible(true);
    } else if (extension == ".obj") {
        obj.setVisible(true);
    } else if (extension == ".txt") {
        txt.setVisible(true);
    }
    main.updateFileLabel();
    resized();
}

void SettingsComponent::update() {
    txt.update();
}

void SettingsComponent::disableMouseRotation() {
    obj.disableMouseRotation();
}

#include "SettingsComponent.h"
#include "PluginEditor.h"

SettingsComponent::SettingsComponent(OscirenderAudioProcessor& p, OscirenderAudioProcessorEditor& editor) : audioProcessor(p), pluginEditor(editor) {
    addAndMakeVisible(effects);
    addAndMakeVisible(main);
    addAndMakeVisible(columnResizerBar);
    addAndMakeVisible(rowResizerBar);
    addChildComponent(lua);
    addChildComponent(obj);
    addChildComponent(txt);

    columnLayout.setItemLayout(0, -0.1, -0.9, -0.4);
    columnLayout.setItemLayout(1, 7, 7, 7);
    columnLayout.setItemLayout(2, -0.1, -0.9, -0.6);

    rowLayout.setItemLayout(0, -0.1, -1.0, -0.63);
    rowLayout.setItemLayout(1, 7, 7, 7);
    rowLayout.setItemLayout(2, -0.1, -0.9, -0.37);
}


void SettingsComponent::resized() {
    auto area = getLocalBounds();
    area.removeFromLeft(5);
    area.removeFromRight(5);
    area.removeFromTop(5);
    area.removeFromBottom(5);

    juce::Component dummy;

    juce::Component* columns[] = { &main, &columnResizerBar, &dummy };
    columnLayout.layOutComponents(columns, 3, area.getX(), area.getY(), area.getWidth(), area.getHeight(), false, true);

    juce::Component* effectSettings = nullptr;

    if (lua.isVisible()) {
        effectSettings = &lua;
    } else if (obj.isVisible()) {
        effectSettings = &obj;
    } else if (txt.isVisible()) {
        effectSettings = &txt;
    }

    juce::Component* rows[] = { &effects, &rowResizerBar, effectSettings };

    // use the dummy component to work out the bounds of the rows
    if (effectSettings != nullptr) {
        rowLayout.layOutComponents(rows, 3, dummy.getX(), dummy.getY(), dummy.getWidth(), dummy.getHeight(), true, true);
    } else {
        effects.setBounds(dummy.getBounds());
    }
}

void SettingsComponent::fileUpdated(juce::String fileName) {
    juce::String extension = fileName.fromLastOccurrenceOf(".", true, false);
    lua.setVisible(false);
    obj.setVisible(false);
    txt.setVisible(false);
    if (fileName.isEmpty() || audioProcessor.objectServerRendering) {
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

#include "SettingsComponent.h"
#include "PluginEditor.h"

SettingsComponent::SettingsComponent(OscirenderAudioProcessor& p, OscirenderAudioProcessorEditor& editor) : audioProcessor(p), pluginEditor(editor) {
    addAndMakeVisible(effects);
    addAndMakeVisible(main);
    addAndMakeVisible(midiResizerBar);
    addAndMakeVisible(mainResizerBar);
    addAndMakeVisible(effectResizerBar);
    addAndMakeVisible(midi);
    addChildComponent(lua);
    addChildComponent(obj);
    addChildComponent(txt);

    midiLayout.setItemLayout(0, -0.1, -1.0, -1.0);
    midiLayout.setItemLayout(1, 7, 7, 7);
    midiLayout.setItemLayout(2, CLOSED_PREF_SIZE, -0.9, CLOSED_PREF_SIZE);

    mainLayout.setItemLayout(0, -0.1, -0.9, -0.4);
    mainLayout.setItemLayout(1, 7, 7, 7);
    mainLayout.setItemLayout(2, -0.1, -0.9, -0.6);

    effectLayout.setItemLayout(0, -0.1, -1.0, -0.63);
    effectLayout.setItemLayout(1, 7, 7, 7);
    effectLayout.setItemLayout(2, -0.1, -0.9, -0.37);
}


void SettingsComponent::resized() {
    auto area = getLocalBounds();
    area.removeFromLeft(5);
    area.removeFromRight(5);
    area.removeFromTop(5);
    area.removeFromBottom(5);

    juce::Component dummy;

    juce::Component* midiComponents[] = { &dummy, &midiResizerBar, &midi };
    midiLayout.layOutComponents(midiComponents, 3, area.getX(), area.getY(), area.getWidth(), area.getHeight(), true, true);

    juce::Component* columns[] = { &main, &mainResizerBar, &dummy };
    mainLayout.layOutComponents(columns, 3, dummy.getX(), dummy.getY(), dummy.getWidth(), dummy.getHeight(), false, true);

    juce::Component* effectSettings = nullptr;

    if (lua.isVisible()) {
        effectSettings = &lua;
    } else if (obj.isVisible()) {
        effectSettings = &obj;
    } else if (txt.isVisible()) {
        effectSettings = &txt;
    }

    juce::Component* rows[] = { &effects, &effectResizerBar, effectSettings };

    // use the dummy component to work out the bounds of the rows
    if (effectSettings != nullptr) {
        effectLayout.layOutComponents(rows, 3, dummy.getX(), dummy.getY(), dummy.getWidth(), dummy.getHeight(), true, true);
    } else {
        effects.setBounds(dummy.getBounds());
    }

    repaint();
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

void SettingsComponent::toggleMidiComponent() {
    double minSize, maxSize, preferredSize;
    midiLayout.getItemLayout(2, minSize, maxSize, preferredSize);
    if (preferredSize == CLOSED_PREF_SIZE) {
        midiLayout.setItemLayout(0, -0.1, -1.0, -0.7);
        midiLayout.setItemLayout(2, CLOSED_PREF_SIZE, -0.9, -0.3);
    } else {
        midiLayout.setItemLayout(0, -0.1, -1.0, -1.0);
        midiLayout.setItemLayout(2, CLOSED_PREF_SIZE, -0.9, CLOSED_PREF_SIZE);
    }
    
    resized();
}

void SettingsComponent::mouseMove(const juce::MouseEvent& event) {
    // if mouse over midi component, change cursor to link cursor
    if (midi.getBounds().removeFromTop(CLOSED_PREF_SIZE).contains(event.getPosition())) {
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    } else {
        setMouseCursor(juce::MouseCursor::NormalCursor);
    }
}

void SettingsComponent::mouseDown(const juce::MouseEvent& event) {
    // if mouse over midi component, toggle midi component
    if (midi.getBounds().removeFromTop(CLOSED_PREF_SIZE).contains(event.getPosition())) {
        toggleMidiComponent();
    }
}

void SettingsComponent::paint(juce::Graphics& g) {
    // add drop shadow to each component
    auto dc = juce::DropShadow(juce::Colours::black, 5, juce::Point<int>(0, 0));
    dc.drawForRectangle(g, main.getBounds());
    dc.drawForRectangle(g, effects.getBounds());
    dc.drawForRectangle(g, midi.getBounds());

    if (lua.isVisible()) {
        dc.drawForRectangle(g, lua.getBounds());
    } else if (obj.isVisible()) {
        dc.drawForRectangle(g, obj.getBounds());
    } else if (txt.isVisible()) {
        dc.drawForRectangle(g, txt.getBounds());
    }
}

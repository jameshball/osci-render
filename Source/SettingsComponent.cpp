#include "SettingsComponent.h"
#include "PluginEditor.h"

SettingsComponent::SettingsComponent(OscirenderAudioProcessor& p, OscirenderAudioProcessorEditor& editor) : audioProcessor(p), pluginEditor(editor) {
    addAndMakeVisible(effects);
    addAndMakeVisible(main);
    addAndMakeVisible(perspective);
    addAndMakeVisible(midiResizerBar);
    addAndMakeVisible(mainResizerBar);
    addAndMakeVisible(mainPerspectiveResizerBar);
    addAndMakeVisible(effectResizerBar);
    addAndMakeVisible(midi);
    addChildComponent(lua);
    addChildComponent(txt);

    midiLayout.setItemLayout(0, -0.1, -1.0, -1.0);
    midiLayout.setItemLayout(1, RESIZER_BAR_SIZE, RESIZER_BAR_SIZE, RESIZER_BAR_SIZE);
    midiLayout.setItemLayout(2, CLOSED_PREF_SIZE, -0.9, CLOSED_PREF_SIZE);

    mainLayout.setItemLayout(0, -0.1, -0.9, -0.4);
    mainLayout.setItemLayout(1, RESIZER_BAR_SIZE, RESIZER_BAR_SIZE, RESIZER_BAR_SIZE);
    mainLayout.setItemLayout(2, -0.1, -0.9, -0.6);

    mainPerspectiveLayout.setItemLayout(0, RESIZER_BAR_SIZE, -1.0, -1.0);
    mainPerspectiveLayout.setItemLayout(1, RESIZER_BAR_SIZE, RESIZER_BAR_SIZE, RESIZER_BAR_SIZE);
    mainPerspectiveLayout.setItemLayout(2, CLOSED_PREF_SIZE, -1.0, CLOSED_PREF_SIZE);

    effectLayout.setItemLayout(0, CLOSED_PREF_SIZE, -1.0, -0.6);
    effectLayout.setItemLayout(1, RESIZER_BAR_SIZE, RESIZER_BAR_SIZE, RESIZER_BAR_SIZE);
    effectLayout.setItemLayout(2, CLOSED_PREF_SIZE, -1.0, -0.4);
}


void SettingsComponent::resized() {
    auto area = getLocalBounds();
    area.removeFromLeft(5);
    area.removeFromRight(5);
    area.removeFromTop(5);
    area.removeFromBottom(5);

    juce::Component dummy;
    juce::Component dummy2;

    juce::Component* midiComponents[] = { &dummy, &midiResizerBar, &midi };
    midiLayout.layOutComponents(midiComponents, 3, area.getX(), area.getY(), area.getWidth(), area.getHeight(), true, true);

    juce::Component* columns[] = { &dummy2, &mainResizerBar, &dummy };
    mainLayout.layOutComponents(columns, 3, dummy.getX(), dummy.getY(), dummy.getWidth(), dummy.getHeight(), false, true);

    juce::Component* rows1[] = { &main, &mainPerspectiveResizerBar, &perspective };
    mainPerspectiveLayout.layOutComponents(rows1, 3, dummy2.getX(), dummy2.getY(), dummy2.getWidth(), dummy2.getHeight(), true, true);

    juce::Component* effectSettings = nullptr;

    if (lua.isVisible()) {
        effectSettings = &lua;
    } else if (txt.isVisible()) {
        effectSettings = &txt;
    }

    juce::Component* rows2[] = {&effects, &effectResizerBar, effectSettings};

    // use the dummy component to work out the bounds of the rows
    if (effectSettings != nullptr) {
        effectLayout.layOutComponents(rows2, 3, dummy.getX(), dummy.getY(), dummy.getWidth(), dummy.getHeight(), true, true);
    } else {
        effects.setBounds(dummy.getBounds());
    }

    repaint();
}

void SettingsComponent::fileUpdated(juce::String fileName) {
    juce::String extension = fileName.fromLastOccurrenceOf(".", true, false);
    lua.setVisible(false);
    txt.setVisible(false);
    if (fileName.isEmpty() || audioProcessor.objectServerRendering) {
        // do nothing
    } else if (extension == ".lua") {
        lua.setVisible(true);
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
    perspective.disableMouseRotation();
}

void SettingsComponent::toggleLayout(juce::StretchableLayoutManager& layout, double prefSize) {
    double minSize, maxSize, preferredSize;
    layout.getItemLayout(2, minSize, maxSize, preferredSize);

    if (preferredSize == CLOSED_PREF_SIZE) {
        double otherPrefSize = -(1 + prefSize);
        layout.setItemLayout(2, CLOSED_PREF_SIZE, -1.0, prefSize);
        layout.setItemLayout(0, CLOSED_PREF_SIZE, -1.0, otherPrefSize);
    } else {
        layout.setItemLayout(2, CLOSED_PREF_SIZE, -1.0, CLOSED_PREF_SIZE);
        layout.setItemLayout(0, CLOSED_PREF_SIZE, -1.0, -1.0);
    }
    
    resized();
}

void SettingsComponent::mouseMove(const juce::MouseEvent& event) {
    for (int i = 0; i < 4; i++) {
        if (toggleComponents[i]->getBounds().removeFromTop(CLOSED_PREF_SIZE).contains(event.getPosition())) {
            setMouseCursor(juce::MouseCursor::PointingHandCursor);
            return;
        }
    }
    setMouseCursor(juce::MouseCursor::NormalCursor);
}

void SettingsComponent::mouseDown(const juce::MouseEvent& event) {
    for (int i = 0; i < 4; i++) {
        if (toggleComponents[i]->getBounds().removeFromTop(CLOSED_PREF_SIZE).contains(event.getPosition())) {
            toggleLayout(*toggleLayouts[i], prefSizes[i]);
        }
    }
}

void SettingsComponent::paint(juce::Graphics& g) {
    // add drop shadow to each component
    auto dc = juce::DropShadow(juce::Colours::black, 5, juce::Point<int>(0, 0));
    dc.drawForRectangle(g, main.getBounds());
    dc.drawForRectangle(g, effects.getBounds());
    dc.drawForRectangle(g, midi.getBounds());
    dc.drawForRectangle(g, perspective.getBounds());

    if (lua.isVisible()) {
        dc.drawForRectangle(g, lua.getBounds());
    } else if (txt.isVisible()) {
        dc.drawForRectangle(g, txt.getBounds());
    }
}

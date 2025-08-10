#include "SettingsComponent.h"

#include "PluginEditor.h"

SettingsComponent::SettingsComponent(OscirenderAudioProcessor& p, OscirenderAudioProcessorEditor& editor) : audioProcessor(p), pluginEditor(editor) {
    addAndMakeVisible(effects);
    addAndMakeVisible(main);
    addAndMakeVisible(perspective);
    addAndMakeVisible(midiResizerBar);
    addAndMakeVisible(mainResizerBar);
    addAndMakeVisible(midi);
    addChildComponent(txt);
    addChildComponent(frame);

    double midiLayoutPreferredSize = std::any_cast<double>(audioProcessor.getProperty("midiLayoutPreferredSize", pluginEditor.CLOSED_PREF_SIZE));
    double mainLayoutPreferredSize = std::any_cast<double>(audioProcessor.getProperty("mainLayoutPreferredSize", -0.5));

    midiLayout.setItemLayout(0, -0.1, -1.0, -(1.0 + midiLayoutPreferredSize));
    midiLayout.setItemLayout(1, pluginEditor.RESIZER_BAR_SIZE, pluginEditor.RESIZER_BAR_SIZE, pluginEditor.RESIZER_BAR_SIZE);
    midiLayout.setItemLayout(2, pluginEditor.CLOSED_PREF_SIZE, -0.9, midiLayoutPreferredSize);

    mainLayout.setItemLayout(0, -0.1, -0.9, mainLayoutPreferredSize);
    mainLayout.setItemLayout(1, pluginEditor.RESIZER_BAR_SIZE, pluginEditor.RESIZER_BAR_SIZE, pluginEditor.RESIZER_BAR_SIZE);
    mainLayout.setItemLayout(2, -0.1, -0.9, -(1.0 + mainLayoutPreferredSize));
}

void SettingsComponent::resized() {
    auto area = getLocalBounds();
    area.removeFromLeft(5);
    area.removeFromRight(5);
    area.removeFromTop(5);
    area.removeFromBottom(5);

    if (area.getWidth() <= 0 || area.getHeight() <= 0) {
        return;
    }

    juce::Component dummy;
    juce::Component dummy2;

    juce::Component* midiComponents[] = {&dummy, &midiResizerBar, &midi};
    midiLayout.layOutComponents(midiComponents, 3, area.getX(), area.getY(), area.getWidth(), area.getHeight(), true, true);
    midi.setBounds(midi.getBounds());

    juce::Component* columns[] = {&dummy2, &mainResizerBar, &dummy};
    mainLayout.layOutComponents(columns, 3, dummy.getX(), dummy.getY(), dummy.getWidth(), dummy.getHeight(), false, true);

    auto bounds = dummy2.getBounds();
    main.setBounds(bounds);

    juce::Component* effectSettings = nullptr;

    if (txt.isVisible()) {
        effectSettings = &txt;
    } else if (frame.isVisible()) {
        effectSettings = &frame;
    }

    auto dummyBounds = dummy.getBounds();

    if (effectSettings != nullptr) {
        effectSettings->setBounds(dummyBounds.removeFromBottom(160));
        dummyBounds.removeFromBottom(pluginEditor.RESIZER_BAR_SIZE);
    }

    perspective.setBounds(dummyBounds.removeFromBottom(120));
    dummyBounds.removeFromBottom(pluginEditor.RESIZER_BAR_SIZE);

    effects.setBounds(dummyBounds);

    if (isVisible() && getWidth() > 0 && getHeight() > 0) {
        audioProcessor.setProperty("midiLayoutPreferredSize", midiLayout.getItemCurrentRelativeSize(2));
        audioProcessor.setProperty("mainLayoutPreferredSize", mainLayout.getItemCurrentRelativeSize(0));
    }

    repaint();
}

// syphonLock must be held when calling this function
void SettingsComponent::fileUpdated(juce::String fileName) {
    juce::String extension = fileName.fromLastOccurrenceOf(".", true, false).toLowerCase();
    txt.setVisible(false);
    frame.setVisible(false);

    // Check if the file is an image based on extension or Syphon/Spout input
    bool isSyphonActive = false;
#if (JUCE_MAC || JUCE_WINDOWS) && OSCI_PREMIUM
    isSyphonActive = audioProcessor.syphonInputActive;
#endif

    bool isImage = isSyphonActive ||
                   (extension == ".gif" ||
                    extension == ".png" ||
                    extension == ".jpg" ||
                    extension == ".jpeg" ||
                    extension == ".mov" ||
                    extension == ".mp4");

    // Skip processing if object server is rendering or if no file is selected and no Syphon input
    bool skipProcessing = audioProcessor.objectServerRendering || (fileName.isEmpty() && !isSyphonActive);

    if (skipProcessing) {
        // do nothing
    } else if (extension == ".txt") {
        txt.setVisible(true);
    } else if (extension == ".gpla" || isImage) {
        frame.setVisible(true);
        frame.setAnimated(extension == ".gpla" || extension == ".gif" || extension == ".mov" || extension == ".mp4");
        frame.setImage(isImage);
        frame.resized();
    }
    main.updateFileLabel();
    resized();
}

void SettingsComponent::update() {
    txt.update();
    frame.update();
}

void SettingsComponent::mouseMove(const juce::MouseEvent& event) {
    for (int i = 0; i < 1; i++) {
        if (toggleComponents[i]->getBounds().removeFromTop(pluginEditor.CLOSED_PREF_SIZE).contains(event.getPosition())) {
            setMouseCursor(juce::MouseCursor::PointingHandCursor);
            return;
        }
    }
    setMouseCursor(juce::MouseCursor::NormalCursor);
}

void SettingsComponent::mouseDown(const juce::MouseEvent& event) {
    for (int i = 0; i < 1; i++) {
        if (toggleComponents[i]->getBounds().removeFromTop(pluginEditor.CLOSED_PREF_SIZE).contains(event.getPosition())) {
            pluginEditor.toggleLayout(*toggleLayouts[i], prefSizes[i]);
            resized();
            return;
        }
    }
}

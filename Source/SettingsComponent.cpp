#include "SettingsComponent.h"

#include "PluginEditor.h"

SettingsComponent::SettingsComponent(OscirenderAudioProcessor& p, OscirenderAudioProcessorEditor& editor) : audioProcessor(p), pluginEditor(editor) {
    addAndMakeVisible(effects);
    addAndMakeVisible(fileControls);
    addAndMakeVisible(perspective);
    addAndMakeVisible(midiResizerBar);
    addAndMakeVisible(mainResizerBar);
    addAndMakeVisible(midi);
    addChildComponent(txt);
    addChildComponent(frame);
    addChildComponent(examples);

    examples.onClosed = [this]() {
        showExamples(false);
    };
    examples.onExampleOpened = [this](const juce::String& fileName, bool shouldOpenEditor) {
        pluginEditor.addCodeEditor(audioProcessor.getCurrentFileIndex());
        pluginEditor.fileUpdated(fileName, shouldOpenEditor);
    };

    double midiLayoutPreferredSize = std::any_cast<double>(audioProcessor.getProperty("midiLayoutPreferredSize", pluginEditor.CLOSED_PREF_SIZE));
    double mainLayoutPreferredSize = std::any_cast<double>(audioProcessor.getProperty("mainLayoutPreferredSize", -0.5));

    midiLayout.setItemLayout(0, -0.1, -1.0, -(1.0 + midiLayoutPreferredSize));
    midiLayout.setItemLayout(1, pluginEditor.RESIZER_BAR_SIZE, pluginEditor.RESIZER_BAR_SIZE, pluginEditor.RESIZER_BAR_SIZE);
    midiLayout.setItemLayout(2, pluginEditor.CLOSED_PREF_SIZE, -0.9, midiLayoutPreferredSize);

    mainLayout.setItemLayout(0, -0.1, -0.9, mainLayoutPreferredSize);
    mainLayout.setItemLayout(1, pluginEditor.RESIZER_BAR_SIZE, pluginEditor.RESIZER_BAR_SIZE, pluginEditor.RESIZER_BAR_SIZE);
    mainLayout.setItemLayout(2, -0.1, -0.9, -(1.0 + mainLayoutPreferredSize));

    addAndMakeVisible(editor.volume);

    osci::BooleanParameter* visualiserFullScreen = audioProcessor.visualiserParameters.visualiserFullScreen;
    pluginEditor.visualiser.setFullScreen(visualiserFullScreen->getBoolValue());

    addAndMakeVisible(pluginEditor.visualiser);
    pluginEditor.visualiser.setFullScreenCallback([this, visualiserFullScreen](FullScreenMode mode) {
        if (mode == FullScreenMode::TOGGLE) {
            visualiserFullScreen->setBoolValueNotifyingHost(!visualiserFullScreen->getBoolValue());
        } else if (mode == FullScreenMode::FULL_SCREEN) {
            visualiserFullScreen->setBoolValueNotifyingHost(true);
        } else if (mode == FullScreenMode::MAIN_COMPONENT) {
            visualiserFullScreen->setBoolValueNotifyingHost(false);
        }

        pluginEditor.visualiser.setFullScreen(visualiserFullScreen->getBoolValue());

        pluginEditor.resized();
        pluginEditor.repaint();
        resized();
        repaint();
    });

    pluginEditor.visualiser.setColour(VisualiserComponent::buttonRowColourId, juce::Colours::black);

    visualiserFullScreen->addListener(this);
}

SettingsComponent::~SettingsComponent() {
    audioProcessor.visualiserParameters.visualiserFullScreen->removeListener(this);
}

void SettingsComponent::parameterValueChanged(int parameterIndex, float newValue) {
    juce::MessageManager::callAsync([this] {
        pluginEditor.resized();
        pluginEditor.repaint();
        resized();
        repaint();
    });
}

void SettingsComponent::parameterGestureChanged(int parameterIndex, bool gestureIsStarting) {}

void SettingsComponent::resized() {
    auto padding = 7;

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
    auto row = bounds.removeFromTop(30);
    fileControls.setBounds(row.removeFromLeft(bounds.getWidth()));
    bounds.removeFromTop(padding);

    volumeVisualiserBounds = bounds;
    bounds.reduce(5, 5);

    auto volumeArea = bounds.removeFromLeft(30);
    pluginEditor.volume.setBounds(volumeArea.withSizeKeepingCentre(volumeArea.getWidth(), juce::jmin(volumeArea.getHeight(), 300)));

    if (!audioProcessor.visualiserParameters.visualiserFullScreen->getBoolValue()) {
        auto minDim = juce::jmin(bounds.getWidth(), bounds.getHeight());
        juce::Point<int> localTopLeft = {bounds.getX(), bounds.getY()};
        juce::Point<int> topLeft = pluginEditor.getLocalPoint(this, localTopLeft);
        auto shiftedBounds = bounds;
        shiftedBounds.setX(topLeft.getX());
        shiftedBounds.setY(topLeft.getY());
        pluginEditor.visualiser.setBounds(shiftedBounds);
    }

    juce::Component* effectSettings = nullptr;
    auto dummyBounds = dummy.getBounds();

    // Only reserve space for effect settings panel when not showing the Open Files panel
    if (!examplesVisible) {
        if (txt.isVisible()) {
            effectSettings = &txt;
        } else if (frame.isVisible()) {
            effectSettings = &frame;
        }

        if (effectSettings != nullptr) {
            effectSettings->setBounds(dummyBounds.removeFromBottom(160));
            dummyBounds.removeFromBottom(pluginEditor.RESIZER_BAR_SIZE);
        }
    }

    if (examplesVisible) {
        // Hide other panels while examples are visible
        perspective.setVisible(false);
        effects.setVisible(false);
        txt.setVisible(false);
        frame.setVisible(false);
        examples.setVisible(true);
        examples.setBounds(dummyBounds);
    } else {
        examples.setVisible(false);
        perspective.setVisible(true);
        effects.setVisible(true);
        perspective.setBounds(dummyBounds.removeFromBottom(120));
        dummyBounds.removeFromBottom(pluginEditor.RESIZER_BAR_SIZE);
        effects.setBounds(dummyBounds);
    }

    if (isVisible() && getWidth() > 0 && getHeight() > 0) {
        audioProcessor.setProperty("midiLayoutPreferredSize", midiLayout.getItemCurrentRelativeSize(2));
        audioProcessor.setProperty("mainLayoutPreferredSize", mainLayout.getItemCurrentRelativeSize(0));
    }

    repaint();
}

void SettingsComponent::paint(juce::Graphics& g) {
    g.setColour(juce::Colours::black);
    g.fillRoundedRectangle(volumeVisualiserBounds.toFloat(), OscirenderLookAndFeel::RECT_RADIUS);
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
    fileControls.updateFileLabel();
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

void SettingsComponent::showExamples(bool shouldShow) {
    examplesVisible = shouldShow;
    resized();
    if (examplesVisible) {
        // Force layout so the OpenFileComponent sizes its viewport/content right away
        examples.resized();
        examples.repaint();
    }
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

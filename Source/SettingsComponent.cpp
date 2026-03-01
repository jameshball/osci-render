#include "SettingsComponent.h"

#include "PluginEditor.h"

SettingsComponent::SettingsComponent(OscirenderAudioProcessor& p, OscirenderAudioProcessorEditor& editor) : audioProcessor(p), pluginEditor(editor) {
    addAndMakeVisible(effects);
    addAndMakeVisible(fileControls);
    addAndMakeVisible(perspective);
    addAndMakeVisible(mainResizerBar);
    addAndMakeVisible(midiResizerBar);
    addAndMakeVisible(midi);
    addChildComponent(frame);
    addChildComponent(examples);

    examples.onClosed = [this]() {
        showExamples(false);
    };
    examples.onFileOpened = [this](const juce::String& fileName, bool shouldOpenEditor, int fileIndex) {
        pluginEditor.addCodeEditor(fileIndex);
        pluginEditor.fileUpdated(fileName, shouldOpenEditor);
    };

    double mainLayoutVisSize = std::any_cast<double>(audioProcessor.getProperty("mainLayoutVisSize", -0.25));
    double mainLayoutMidiSize = std::any_cast<double>(audioProcessor.getProperty("mainLayoutMidiSize", -0.35));

    // 5-component horizontal layout: vis | resizer | effects | resizer | midi
    mainLayout.setItemLayout(0, -0.1, -0.5, mainLayoutVisSize);                    // vis column
    mainLayout.setItemLayout(1, pluginEditor.RESIZER_BAR_SIZE, pluginEditor.RESIZER_BAR_SIZE, pluginEditor.RESIZER_BAR_SIZE); // resizer
    mainLayout.setItemLayout(2, -0.1, -0.6, -(1.0 + mainLayoutVisSize + mainLayoutMidiSize)); // effects column
    mainLayout.setItemLayout(3, pluginEditor.RESIZER_BAR_SIZE, pluginEditor.RESIZER_BAR_SIZE, pluginEditor.RESIZER_BAR_SIZE); // resizer2
    mainLayout.setItemLayout(4, -0.1, -0.6, mainLayoutMidiSize);                   // midi column

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

    // 5-component horizontal layout: visColumn | resizer | effectsColumn | resizer2 | midiColumn
    juce::Component visColumn, effectsColumn;
    juce::Component* columns[] = { &visColumn, &mainResizerBar, &effectsColumn, &midiResizerBar, &midi };
    mainLayout.layOutComponents(columns, 5, area.getX(), area.getY(), area.getWidth(), area.getHeight(), false, true);

    // --- Left column: file controls, volume, visualiser ---
    auto visBounds = visColumn.getBounds();
    auto row = visBounds.removeFromTop(30);
    fileControls.setBounds(row.removeFromLeft(visBounds.getWidth()));
    visBounds.removeFromTop(padding);

    volumeVisualiserBounds = visBounds;
    visBounds.reduce(5, 5);

    auto volumeArea = visBounds.removeFromLeft(30);
    pluginEditor.volume.setBounds(volumeArea.withSizeKeepingCentre(volumeArea.getWidth(), juce::jmin(volumeArea.getHeight(), 300)));

    if (!audioProcessor.visualiserParameters.visualiserFullScreen->getBoolValue()) {
        auto minDim = juce::jmin(visBounds.getWidth(), visBounds.getHeight());
        juce::Point<int> localTopLeft = {visBounds.getX(), visBounds.getY()};
        juce::Point<int> topLeft = pluginEditor.getLocalPoint(this, localTopLeft);
        auto shiftedBounds = visBounds;
        shiftedBounds.setX(topLeft.getX());
        shiftedBounds.setY(topLeft.getY());
        pluginEditor.visualiser.setBounds(shiftedBounds);
    }

    // --- Middle column: effects, perspective, frame settings ---
    auto effectsBounds = effectsColumn.getBounds();

    if (!examplesVisible) {
        if (frame.isVisible()) {
            int preferredHeight = frame.getPreferredHeight();
            frame.setBounds(effectsBounds.removeFromBottom(preferredHeight));
            effectsBounds.removeFromBottom(pluginEditor.RESIZER_BAR_SIZE);
        }
    }

    if (examplesVisible) {
        perspective.setVisible(false);
        effects.setVisible(false);
        frame.setVisible(false);
        examples.setVisible(true);
        examples.setBounds(effectsBounds);
    } else {
        examples.setVisible(false);
        perspective.setVisible(true);
        effects.setVisible(true);
        perspective.setBounds(effectsBounds.removeFromBottom(120));
        effectsBounds.removeFromBottom(pluginEditor.RESIZER_BAR_SIZE);
        effects.setBounds(effectsBounds);
    }

    // --- Right column: midi (already positioned by layOutComponents) ---
    midi.setBounds(midi.getBounds());

    if (isVisible() && getWidth() > 0 && getHeight() > 0) {
        audioProcessor.setProperty("mainLayoutVisSize", mainLayout.getItemCurrentRelativeSize(0));
        audioProcessor.setProperty("mainLayoutMidiSize", mainLayout.getItemCurrentRelativeSize(4));
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
    frame.update();
}

void SettingsComponent::mouseMove(const juce::MouseEvent& event) {
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
}

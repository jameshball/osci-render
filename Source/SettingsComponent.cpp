#include "SettingsComponent.h"

#include "PluginEditor.h"
#include "components/EffectComponent.h"
#include "audio/EnvState.h"
#include "components/CustomMidiKeyboardComponent.h"

SettingsComponent::SettingsComponent(OscirenderAudioProcessor& p, OscirenderAudioProcessorEditor& editor)
    : audioProcessor(p), pluginEditor(editor),
      envelope(p), lfo(p),
    keyboard(p.keyboardState, juce::CustomMidiKeyboardComponent::horizontalKeyboard) {
    addAndMakeVisible(effects);
    addAndMakeVisible(fileControls);
    addAndMakeVisible(perspective);
    addAndMakeVisible(mainResizerBar);
    addAndMakeVisible(midiResizerBar);
    addAndMakeVisible(midi);
    addChildComponent(frame);
    addChildComponent(examples);

    // Envelope setup
    addAndMakeVisible(envelope);
    envelope.setDahdsrParams(0, audioProcessor.getCurrentDahdsrParams(0));
    envelope.setGrid(EnvelopeComponent::GridBoth, EnvelopeComponent::GridNone, 0.1, 0.25);

    // Shared drag-active handler: just set the atomic flag.
    // The central 60Hz ModulationUpdateBroadcaster handles highlight repaints
    // while dragging. On drag-end, one repaint clears residual highlights.
    auto dragChanged = [this](bool isDragging) {
        EffectComponent::modAnyDragActive.store(isDragging, std::memory_order_relaxed);
        if (!isDragging)
            repaint();
    };
    envelope.onDragActiveChanged = dragChanged;

    // LFO setup
    addAndMakeVisible(lfo);
    lfo.onDragActiveChanged = dragChanged;

    // Keyboard
    addAndMakeVisible(keyboardViewport);
    keyboardViewport.setViewedComponent(&keyboard, false);
    keyboardViewport.setScrollBarsShown(false, false, false, true);
    keyboardViewport.setColour(scrollFadeOverlayBackgroundColourId,
                               findColour(juce::ResizableWindow::backgroundColourId));
    keyboardViewport.setSidesEnabled(false, false, true, true);
    keyboardViewport.setFadeWidth(20);
    keyboardViewport.setOpaque(false);
    keyboard.setBlackNoteLengthProportion(0.6f);
    keyboard.setBlackNoteWidthProportion(0.68f);
    keyboard.setScrollButtonsVisible(false);

    // DAHDSR parameter listeners (for envelope display updates)
    for (int e = 0; e < NUM_ENVELOPES; ++e) {
        const auto& ep = audioProcessor.envParams[e];
        ep.delayTime->addListener(&dahdsrListener);
        ep.attackTime->addListener(&dahdsrListener);
        ep.holdTime->addListener(&dahdsrListener);
        ep.decayTime->addListener(&dahdsrListener);
        ep.sustainLevel->addListener(&dahdsrListener);
        ep.releaseTime->addListener(&dahdsrListener);
        ep.attackShape->addListener(&dahdsrListener);
        ep.decayShape->addListener(&dahdsrListener);
        ep.releaseShape->addListener(&dahdsrListener);
    }

    // Envelope flow-marker animation
    startTimerHz(60);

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
    stopTimer();
    for (int e = 0; e < NUM_ENVELOPES; ++e) {
        const auto& ep = audioProcessor.envParams[e];
        ep.delayTime->removeListener(&dahdsrListener);
        ep.attackTime->removeListener(&dahdsrListener);
        ep.holdTime->removeListener(&dahdsrListener);
        ep.decayTime->removeListener(&dahdsrListener);
        ep.sustainLevel->removeListener(&dahdsrListener);
        ep.releaseTime->removeListener(&dahdsrListener);
        ep.attackShape->removeListener(&dahdsrListener);
        ep.decayShape->removeListener(&dahdsrListener);
        ep.releaseShape->removeListener(&dahdsrListener);
    }
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

    // 5-component horizontal layout: visColumn | resizer | effectsColumn | resizer2 | rightColumn
    juce::Component visColumn, effectsColumn, rightColumn;
    juce::Component* columns[] = { &visColumn, &mainResizerBar, &effectsColumn, &midiResizerBar, &rightColumn };
    mainLayout.layOutComponents(columns, 5, area.getX(), area.getY(), area.getWidth(), area.getHeight(), false, true);

    // --- Keyboard at bottom, spanning effects + resizer + right columns ---
    static constexpr int keyboardHeight = 34;
    auto effectsBounds = effectsColumn.getBounds();
    auto rightBounds = rightColumn.getBounds();

    // Clip both resizer bars so they stop above the keyboard area.
    // The keyboard sits at area.getBottom() - keyboardHeight; leave a padding gap above it.
    {
        int resizerBottom = area.getBottom() - keyboardHeight - padding;
        mainResizerBar.setBounds(mainResizerBar.getBounds().withBottom(resizerBottom));
        midiResizerBar.setBounds(midiResizerBar.getBounds().withBottom(resizerBottom));
    }

    keyboardPanelBounds = juce::Rectangle<int>(
        effectsBounds.getX(),
        area.getBottom() - keyboardHeight,
        rightBounds.getRight() - effectsBounds.getX(),
        keyboardHeight);
    keyboardViewport.setBounds(keyboardPanelBounds);

    const auto viewportBounds = keyboardViewport.getLocalBounds();
    const auto whiteKeyCount = juce::CustomMidiKeyboardComponent::getWhiteKeyCount(keyboard.getRangeStart(), keyboard.getRangeEnd());
    const auto compactKeyWidth = juce::jlimit(9.0f, 14.0f, viewportBounds.getHeight() * 0.38f);
    const auto stretchedKeyWidth = whiteKeyCount > 0 ? (float) viewportBounds.getWidth() / (float) whiteKeyCount : compactKeyWidth;
    const auto keyWidth = juce::jmax(compactKeyWidth, stretchedKeyWidth);
    keyboard.setKeyWidth(keyWidth);

    const auto keyboardContentWidth = juce::jmax(
        viewportBounds.getWidth(),
        juce::roundToInt((float) whiteKeyCount * keyboard.getKeyWidth()));
    keyboard.setBounds(0, 0, keyboardContentWidth, viewportBounds.getHeight());

    // Trim columns to exclude keyboard and restore the gap above it.
    effectsBounds.removeFromBottom(keyboardHeight + padding);
    rightBounds.removeFromBottom(keyboardHeight + padding);

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

    // --- Right column: MIDI settings (compact), envelope, LFO ---
    static constexpr int midiGroupHeight = 60;
    midi.setBounds(rightBounds.removeFromTop(midiGroupHeight));
    rightBounds.removeFromTop(padding);

    // Split remaining: top 55% envelope, bottom 45% LFO
    auto lfoArea = rightBounds.removeFromBottom(rightBounds.getHeight() * 45 / 100);
    rightBounds.removeFromBottom(5);
    envelope.setBounds(rightBounds);
    lfo.setBounds(lfoArea);

    if (isVisible() && getWidth() > 0 && getHeight() > 0) {
        audioProcessor.setProperty("mainLayoutVisSize", mainLayout.getItemCurrentRelativeSize(0));
        audioProcessor.setProperty("mainLayoutMidiSize", mainLayout.getItemCurrentRelativeSize(4));
    }

    repaint();
}

void SettingsComponent::paint(juce::Graphics& g) {
    g.setColour(juce::Colours::black);
    g.fillRoundedRectangle(volumeVisualiserBounds.toFloat(), OscirenderLookAndFeel::RECT_RADIUS);

    if (! keyboardPanelBounds.isEmpty()) {
        g.setColour(findColour(juce::ResizableWindow::backgroundColourId));
        g.fillRoundedRectangle(keyboardPanelBounds.toFloat(), (float) OscirenderLookAndFeel::RECT_RADIUS);

    }
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

// --- Envelope flow-marker animation (moved from MidiComponent) ---

void SettingsComponent::timerCallback() {
    if (!audioProcessor.midiEnabled->getBoolValue()) {
        envelope.resetFlowPersistenceForUi();
        return;
    }

    // Send flow markers for the currently active envelope tab
    int activeEnv = envelope.getActiveEnvIndex();
    constexpr int kMax = OscirenderAudioProcessor::kMaxUiVoices;
    double times[kMax];
    bool anyActive = false;
    for (int i = 0; i < kMax; ++i) {
        if (audioProcessor.uiVoiceEnvActive[activeEnv][i].load(std::memory_order_relaxed)) {
            times[i] = audioProcessor.uiVoiceEnvTimeSeconds[activeEnv][i].load(std::memory_order_relaxed);
            anyActive = true;
        } else {
            times[i] = -1.0;
        }
    }

    if (!anyActive) {
        envelope.clearFlowMarkerTimesForUi();
        return;
    }

    envelope.setFlowMarkerTimesForUi(times, kMax);
}

void SettingsComponent::DahdsrListener::handleAsyncUpdate() {
    for (int e = 0; e < NUM_ENVELOPES; ++e)
        owner.envelope.setDahdsrParams(e, owner.audioProcessor.getCurrentDahdsrParams(e));
}

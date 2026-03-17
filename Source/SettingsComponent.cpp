#include "SettingsComponent.h"

#include "PluginEditor.h"
#include "components/EffectComponent.h"
#include "audio/EnvState.h"
#include "components/CustomMidiKeyboardComponent.h"

SettingsComponent::SettingsComponent(OscirenderAudioProcessor& p, OscirenderAudioProcessorEditor& editor)
    : audioProcessor(p), pluginEditor(editor),
      beginnerMode(p.isBeginnerMode()),
      envelope(p),
    keyboard(p.keyboardState, juce::CustomMidiKeyboardComponent::horizontalKeyboard) {
    addAndMakeVisible(effects);
    addAndMakeVisible(fileControls);
    addAndMakeVisible(perspective);
    addAndMakeVisible(mainResizerBar);
    if (!beginnerMode) {
        addAndMakeVisible(midiResizerBar);
    }
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

    // LFO setup — only in advanced mode
    if (!beginnerMode) {
        lfo = std::make_unique<LfoComponent>(p);
        addAndMakeVisible(*lfo);
        lfo->onDragActiveChanged = dragChanged;

        random = std::make_unique<RandomComponent>(p);
        addAndMakeVisible(*random);
        random->onDragActiveChanged = dragChanged;
    }

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

    // Listen to midiEnabled so we can show/hide keyboard+envelope in beginner mode
    audioProcessor.midiEnabled->addListener(this);

    // DAHDSR parameter listeners (for envelope display updates)
    const int numEnvListeners = beginnerMode ? 1 : NUM_ENVELOPES;
    for (int e = 0; e < numEnvListeners; ++e) {
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

    if (beginnerMode) {
        // 3-component horizontal layout: vis | resizer | effects
        mainLayout.setItemLayout(0, -0.1, -0.5, mainLayoutVisSize);
        mainLayout.setItemLayout(1, pluginEditor.RESIZER_BAR_SIZE, pluginEditor.RESIZER_BAR_SIZE, pluginEditor.RESIZER_BAR_SIZE);
        mainLayout.setItemLayout(2, -0.1, -0.9, -(1.0 + mainLayoutVisSize));
    } else {
        // 5-component horizontal layout: vis | resizer | effects | resizer | midi
        mainLayout.setItemLayout(0, -0.1, -0.5, mainLayoutVisSize);
        mainLayout.setItemLayout(1, pluginEditor.RESIZER_BAR_SIZE, pluginEditor.RESIZER_BAR_SIZE, pluginEditor.RESIZER_BAR_SIZE);
        mainLayout.setItemLayout(2, -0.1, -0.6, -(1.0 + mainLayoutVisSize + mainLayoutMidiSize));
        mainLayout.setItemLayout(3, pluginEditor.RESIZER_BAR_SIZE, pluginEditor.RESIZER_BAR_SIZE, pluginEditor.RESIZER_BAR_SIZE);
        mainLayout.setItemLayout(4, -0.1, -0.6, mainLayoutMidiSize);
    }

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
    const int numEnvListeners = beginnerMode ? 1 : NUM_ENVELOPES;
    for (int e = 0; e < numEnvListeners; ++e) {
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
    audioProcessor.midiEnabled->removeListener(this);
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
    childLayoutUpdater.triggerAsyncUpdate();
}

void SettingsComponent::layoutChildren() {
    auto padding = 7;

    auto area = getLocalBounds();
    area.removeFromLeft(5);
    area.removeFromRight(5);
    area.removeFromTop(5);
    area.removeFromBottom(5);

    if (area.getWidth() <= 0 || area.getHeight() <= 0) {
        return;
    }

    static constexpr int keyboardHeight = 34;

    // --- Left column (shared by both modes) ---
    auto layoutVisColumn = [&](juce::Rectangle<int> visBounds) {
        auto row = visBounds.removeFromTop(30);
        fileControls.setBounds(row.removeFromLeft(visBounds.getWidth()));
        visBounds.removeFromTop(padding);

        volumeVisualiserBounds = visBounds;
        visBounds.reduce(5, 5);

        auto volumeArea = visBounds.removeFromLeft(30);
        pluginEditor.volume.setBounds(volumeArea.withSizeKeepingCentre(volumeArea.getWidth(), juce::jmin(volumeArea.getHeight(), 300)));

        if (!audioProcessor.visualiserParameters.visualiserFullScreen->getBoolValue()) {
            juce::Point<int> localTopLeft = {visBounds.getX(), visBounds.getY()};
            juce::Point<int> topLeft = pluginEditor.getLocalPoint(this, localTopLeft);
            auto shiftedBounds = visBounds;
            shiftedBounds.setX(topLeft.getX());
            shiftedBounds.setY(topLeft.getY());
            pluginEditor.visualiser.setBounds(shiftedBounds);
        }
    };

    // --- Effects column (shared by both modes) ---
    auto layoutEffectsColumn = [&](juce::Rectangle<int> effectsBounds) {
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
    };

    if (beginnerMode) {
        // ============================================================
        // BEGINNER: 3-column layout — vis | resizer | effects
        // Envelope, MIDI, and keyboard stacked at bottom of effects column
        // ============================================================
        juce::Component* columns[] = { &layoutVisColumnProxy, &mainResizerBar, &layoutEffectsColumnProxy };
        mainLayout.layOutComponents(columns, 3, area.getX(), area.getY(), area.getWidth(), area.getHeight(), false, true);

        if (audioProcessor.midiEnabled->getBoolValue()) {
            int resizerBottom = area.getBottom() - keyboardHeight - padding;
            mainResizerBar.setBounds(mainResizerBar.getBounds().withBottom(resizerBottom));
        }

        auto effectsBounds = layoutEffectsColumnProxy.getBounds();

        const bool midiOn = audioProcessor.midiEnabled->getBoolValue();

        // Bottom stack (from bottom up): keyboard, envelope, MIDI
        static constexpr int envelopeHeight = 90;
        static constexpr int midiGroupHeight = 30;

        if (midiOn) {
            keyboardPanelBounds = juce::Rectangle<int>(
                effectsBounds.getX(),
                effectsBounds.getBottom() - keyboardHeight,
                effectsBounds.getWidth(),
                keyboardHeight);
            keyboardViewport.setBounds(keyboardPanelBounds);
            keyboardViewport.setVisible(true);
            effectsBounds.removeFromBottom(keyboardHeight + padding);

            envelope.setBounds(effectsBounds.removeFromBottom(envelopeHeight));
            envelope.setVisible(true);
            effectsBounds.removeFromBottom(padding);

            // Keyboard sizing
            const auto viewportBounds = keyboardViewport.getLocalBounds();
            const auto whiteKeyCount = juce::CustomMidiKeyboardComponent::getWhiteKeyCount(keyboard.getRangeStart(), keyboard.getRangeEnd());
            const auto compactKeyWidth = juce::jlimit(9.0f, 14.0f, viewportBounds.getHeight() * 0.38f);
            const auto stretchedKeyWidth = whiteKeyCount > 0 ? (float) viewportBounds.getWidth() / (float) whiteKeyCount : compactKeyWidth;
            keyboard.setKeyWidth(juce::jmax(compactKeyWidth, stretchedKeyWidth));
            const auto keyboardContentWidth = juce::jmax(
                viewportBounds.getWidth(),
                juce::roundToInt((float) whiteKeyCount * keyboard.getKeyWidth()));
            keyboard.setBounds(0, 0, keyboardContentWidth, viewportBounds.getHeight());
        } else {
            keyboardPanelBounds = {};
            keyboardViewport.setVisible(false);
            envelope.setVisible(false);
        }

        midi.setBounds(effectsBounds.removeFromBottom(midiGroupHeight));
        effectsBounds.removeFromBottom(padding);

        layoutVisColumn(layoutVisColumnProxy.getBounds());
        layoutEffectsColumn(effectsBounds);

        if (isVisible() && getWidth() > 0 && getHeight() > 0) {
            audioProcessor.setProperty("mainLayoutVisSize", mainLayout.getItemCurrentRelativeSize(0));
        }
    } else {
        // ============================================================
        // ADVANCED: 5-column layout — vis | resizer | effects | resizer2 | right
        // ============================================================
        juce::Component* columns[] = { &layoutVisColumnProxy, &mainResizerBar, &layoutEffectsColumnProxy, &midiResizerBar, &layoutRightColumnProxy };
        mainLayout.layOutComponents(columns, 5, area.getX(), area.getY(), area.getWidth(), area.getHeight(), false, true);

        const bool midiOn = audioProcessor.midiEnabled->getBoolValue();

        if (midiOn) {
            int resizerBottom = area.getBottom() - keyboardHeight - padding;
            mainResizerBar.setBounds(mainResizerBar.getBounds().withBottom(resizerBottom));
            midiResizerBar.setBounds(midiResizerBar.getBounds().withBottom(resizerBottom));
        }

        auto effectsBounds = layoutEffectsColumnProxy.getBounds();
        auto rightBounds   = layoutRightColumnProxy.getBounds();

        if (midiOn) {
            // Keyboard spans effects + resizer + right columns
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
            keyboard.setKeyWidth(juce::jmax(compactKeyWidth, stretchedKeyWidth));
            const auto keyboardContentWidth = juce::jmax(
                viewportBounds.getWidth(),
                juce::roundToInt((float) whiteKeyCount * keyboard.getKeyWidth()));
            keyboard.setBounds(0, 0, keyboardContentWidth, viewportBounds.getHeight());

            // Trim columns to exclude keyboard
            effectsBounds.removeFromBottom(keyboardHeight + padding);
            rightBounds.removeFromBottom(keyboardHeight + padding);
        }

        keyboardViewport.setVisible(midiOn);

        layoutVisColumn(layoutVisColumnProxy.getBounds());
        layoutEffectsColumn(effectsBounds);

        // --- Right column: MIDI settings (compact), envelope, LFO, random ---
        static constexpr int midiGroupHeight = 60;
        midi.setBounds(rightBounds.removeFromTop(midiGroupHeight));
        rightBounds.removeFromBottom(padding);

        lfo->setMidiEnabled(midiOn);

        int modAreaHeight = rightBounds.getHeight();
        int randomPct = midiOn ? 15 : 25;
        auto randomArea = rightBounds.removeFromBottom(modAreaHeight * randomPct / 100);
        rightBounds.removeFromBottom(3);

        if (midiOn) {
            auto envelopeArea = rightBounds.removeFromBottom(modAreaHeight * 25 / 100);
            rightBounds.removeFromBottom(3);
            envelope.setBounds(envelopeArea);
            envelope.setVisible(true);
        } else {
            envelope.setVisible(false);
        }

        lfo->setBounds(rightBounds);
        random->setBounds(randomArea);

        if (isVisible() && getWidth() > 0 && getHeight() > 0) {
            audioProcessor.setProperty("mainLayoutVisSize", mainLayout.getItemCurrentRelativeSize(0));
            audioProcessor.setProperty("mainLayoutMidiSize", mainLayout.getItemCurrentRelativeSize(4));
        }
    }

    repaint();
}

void SettingsComponent::ChildLayoutUpdater::handleAsyncUpdate() {
    owner.layoutChildren();
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
    const int numEnvs = owner.beginnerMode ? 1 : NUM_ENVELOPES;
    for (int e = 0; e < numEnvs; ++e)
        owner.envelope.setDahdsrParams(e, owner.audioProcessor.getCurrentDahdsrParams(e));
}

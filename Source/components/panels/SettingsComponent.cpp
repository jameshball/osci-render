#include "SettingsComponent.h"

#include "../../PluginEditor.h"
#include "../effects/EffectComponent.h"
#include "../../audio/modulation/EnvState.h"
#include "../../parser/FileParser.h"
#include "../CustomMidiKeyboardComponent.h"

SettingsComponent::SettingsComponent(OscirenderAudioProcessor& p, OscirenderAudioProcessorEditor& editor)
    : audioProcessor(p), pluginEditor(editor),
      envelope(p),
    keyboard(p.keyboardState, juce::CustomMidiKeyboardComponent::horizontalKeyboard) {
    addAndMakeVisible(effects);
    addAndMakeVisible(fileControls);
    addAndMakeVisible(quickControls);
    addAndMakeVisible(mainResizerBar);
#if OSCI_PREMIUM
    addAndMakeVisible(midiResizerBar);
#endif
    addAndMakeVisible(midi);
    addChildComponent(frame);
#if OSCI_PREMIUM
    addChildComponent(fractalEditor);
#endif
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

    // LFO setup — only in premium
#if OSCI_PREMIUM
    {
        lfo = std::make_unique<LfoComponent>(p);
        addAndMakeVisible(*lfo);
        lfo->onDragActiveChanged = dragChanged;

        random = std::make_unique<RandomComponent>(p);
        addAndMakeVisible(*random);
        random->onDragActiveChanged = dragChanged;

        sidechain = std::make_unique<SidechainComponent>(p);
        addAndMakeVisible(*sidechain);
        sidechain->onDragActiveChanged = dragChanged;
    }
#endif

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

    // Listen to midiEnabled so we can show/hide keyboard+envelope in free version
    audioProcessor.midiEnabled->addListener(this);

    // DAHDSR parameter listeners (for envelope display updates)
    for (int e = 0; e < NUM_ACTIVE_ENVELOPES; ++e)
        audioProcessor.envelopeParameters.params[e].addListenerToAll(&dahdsrListener);

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

#if !OSCI_PREMIUM
    {
        // 3-component horizontal layout: vis | resizer | effects
        mainLayout.setItemLayout(0, -0.1, -0.5, mainLayoutVisSize);
        mainLayout.setItemLayout(1, pluginEditor.RESIZER_BAR_SIZE, pluginEditor.RESIZER_BAR_SIZE, pluginEditor.RESIZER_BAR_SIZE);
        mainLayout.setItemLayout(2, -0.1, -0.9, -(1.0 + mainLayoutVisSize));
    }
#else
    {
        // 5-component horizontal layout: vis | resizer | effects | resizer | midi
        mainLayout.setItemLayout(0, -0.1, -0.5, mainLayoutVisSize);
        mainLayout.setItemLayout(1, pluginEditor.RESIZER_BAR_SIZE, pluginEditor.RESIZER_BAR_SIZE, pluginEditor.RESIZER_BAR_SIZE);
        mainLayout.setItemLayout(2, -0.1, -0.6, -(1.0 + mainLayoutVisSize + mainLayoutMidiSize));
        mainLayout.setItemLayout(3, pluginEditor.RESIZER_BAR_SIZE, pluginEditor.RESIZER_BAR_SIZE, pluginEditor.RESIZER_BAR_SIZE);
        mainLayout.setItemLayout(4, -0.1, -0.6, mainLayoutMidiSize);
    }
#endif

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
    for (int e = 0; e < NUM_ACTIVE_ENVELOPES; ++e)
        audioProcessor.envelopeParameters.params[e].removeListenerFromAll(&dahdsrListener);
    audioProcessor.visualiserParameters.visualiserFullScreen->removeListener(this);
    audioProcessor.midiEnabled->removeListener(this);
}

void SettingsComponent::parameterValueChanged(int parameterIndex, float newValue) {
    auto safeThis = juce::Component::SafePointer<SettingsComponent>(this);
    juce::MessageManager::callAsync([safeThis] {
        if (safeThis == nullptr) return;
        safeThis->pluginEditor.resized();
        safeThis->pluginEditor.repaint();
        safeThis->resized();
        safeThis->repaint();
    });
}

void SettingsComponent::parameterGestureChanged(int parameterIndex, bool gestureIsStarting) {}

void SettingsComponent::resized() {
    childLayoutUpdater.triggerAsyncUpdate();
}

void SettingsComponent::layoutChildren() {
    auto padding = 7;
    static constexpr int kGap = 3; // small gap between stacked panels

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
        visBounds.removeFromTop(kGap);

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
#if OSCI_PREMIUM
            else if (fractalEditor.isVisible()) {
                int preferredHeight = juce::jmin(210, effectsBounds.getHeight() / 2);
                fractalEditor.setBounds(effectsBounds.removeFromBottom(preferredHeight));
                effectsBounds.removeFromBottom(pluginEditor.RESIZER_BAR_SIZE);
            }
#endif
        }

        if (examplesVisible) {
            quickControls.setVisible(false);
            effects.setVisible(false);
            frame.setVisible(false);
            examples.setVisible(true);
            examples.setBounds(effectsBounds);
        } else {
            examples.setVisible(false);
            quickControls.setVisible(true);
            effects.setVisible(true);
            static constexpr int quickControlsHeight = 60;
            quickControls.setBounds(effectsBounds.removeFromBottom(quickControlsHeight));
            effectsBounds.removeFromBottom(kGap);
            effects.setBounds(effectsBounds);
        }
    };

#if !OSCI_PREMIUM
    {
        // ============================================================
        // FREE: 3-column layout — vis | resizer | effects
        // Envelope and keyboard stacked at bottom of effects column
        // MIDI toggle + voices live in QuickControlsBar
        // ============================================================
        juce::Component* columns[] = { &layoutVisColumnProxy, &mainResizerBar, &layoutEffectsColumnProxy };
        mainLayout.layOutComponents(columns, 3, area.getX(), area.getY(), area.getWidth(), area.getHeight(), false, true);

        auto effectsBounds = layoutEffectsColumnProxy.getBounds();

        const bool midiOn = audioProcessor.midiEnabled->getBoolValue();

        midi.setVisible(false);

        if (midiOn) {
            // Keyboard at the bottom
            keyboardPanelBounds = juce::Rectangle<int>(
                effectsBounds.getX(),
                effectsBounds.getBottom() - keyboardHeight,
                effectsBounds.getWidth(),
                keyboardHeight);
            keyboardViewport.setBounds(keyboardPanelBounds);
            keyboardViewport.setVisible(true);
            effectsBounds.removeFromBottom(keyboardHeight + kGap);

            // Envelope above the keyboard
            static constexpr int envelopeHeight = 90;
            envelope.setBounds(effectsBounds.removeFromBottom(envelopeHeight));
            envelope.setVisible(true);
            effectsBounds.removeFromBottom(kGap);

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

        layoutVisColumn(layoutVisColumnProxy.getBounds());
        layoutEffectsColumn(effectsBounds);

        if (isVisible() && getWidth() > 0 && getHeight() > 0) {
            audioProcessor.setProperty("mainLayoutVisSize", mainLayout.getItemCurrentRelativeSize(0));
        }
    }
#else
    {
        // ============================================================
        // PREMIUM: 5-column layout — vis | resizer | effects | resizer2 | right
        // ============================================================
        juce::Component* columns[] = { &layoutVisColumnProxy, &mainResizerBar, &layoutEffectsColumnProxy, &midiResizerBar, &layoutRightColumnProxy };
        mainLayout.layOutComponents(columns, 5, area.getX(), area.getY(), area.getWidth(), area.getHeight(), false, true);

        const bool midiOn = audioProcessor.midiEnabled->getBoolValue();

        if (midiOn) {
            int resizerBottom = area.getBottom() - keyboardHeight - padding;
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
            effectsBounds.removeFromBottom(keyboardHeight + kGap);
            rightBounds.removeFromBottom(keyboardHeight + kGap);
        }

        keyboardViewport.setVisible(midiOn);

        layoutVisColumn(layoutVisColumnProxy.getBounds());
        layoutEffectsColumn(effectsBounds);

        // --- Right column: MIDI settings (compact), LFO, envelope, random + sidechain ---
        static constexpr int midiGroupHeight = 60;
        midi.setBounds(rightBounds.removeFromTop(midiGroupHeight));
        rightBounds.removeFromTop(kGap);

        lfo->setMidiEnabled(midiOn);

        int modAreaHeight = rightBounds.getHeight();

        int bottomPct = midiOn ? 20 : 30;
        auto bottomRow = rightBounds.removeFromBottom(juce::jmax(130, modAreaHeight * bottomPct / 100));
        rightBounds.removeFromBottom(kGap);

        // Split bottom row: random (wider, left) | gap | sidechain (narrower, right)
        int scW = (bottomRow.getWidth() - kGap) * (2.0 / 5.0);
        auto sidechainArea = bottomRow.removeFromRight(scW);
        bottomRow.removeFromRight(kGap);
        auto randomArea = bottomRow;

        if (midiOn) {
            auto envelopeArea = rightBounds.removeFromBottom(modAreaHeight * 25 / 100);
            rightBounds.removeFromBottom(kGap);
            envelope.setBounds(envelopeArea);
            envelope.setVisible(true);
        } else {
            envelope.setVisible(false);
        }

        lfo->setBounds(rightBounds);
        random->setBounds(randomArea);
        sidechain->setBounds(sidechainArea);

        if (isVisible() && getWidth() > 0 && getHeight() > 0) {
            audioProcessor.setProperty("mainLayoutVisSize", mainLayout.getItemCurrentRelativeSize(0));
            audioProcessor.setProperty("mainLayoutMidiSize", mainLayout.getItemCurrentRelativeSize(4));
        }
    }
#endif

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
#if OSCI_PREMIUM
    fractalEditor.setVisible(false);
#endif

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
    } else if (extension == ".lsystem") {
#if OSCI_PREMIUM
        int fileIndex = audioProcessor.getCurrentFileIndex();
        if (fileIndex >= 0) {
            auto parser = audioProcessor.getCurrentFileParser();
            if (parser != nullptr) {
                fractalEditor.setParser(parser->getFractal(), fileIndex);
                fractalEditor.setVisible(true);
            }
        }
#endif
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
    for (int e = 0; e < NUM_ACTIVE_ENVELOPES; ++e)
        owner.envelope.setDahdsrParams(e, owner.audioProcessor.getCurrentDahdsrParams(e));
}

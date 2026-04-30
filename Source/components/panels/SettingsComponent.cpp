#include "SettingsComponent.h"

#include "../../PluginEditor.h"
#include "../effects/EffectComponent.h"
#include "../../audio/modulation/EnvState.h"
#include "../../audio/modulation/LfoState.h"
#include "../../parser/FileParser.h"
#include "../CustomMidiKeyboardComponent.h"
#if OSCI_PREMIUM
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#endif

// ============================================================================
// VerticalDragBar implementation
// ============================================================================
#if OSCI_PREMIUM
SettingsComponent::VerticalDragBar::VerticalDragBar() {
    setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
}

void SettingsComponent::VerticalDragBar::paint(juce::Graphics& g) {
    if (isMouseOver() || isDragging) {
        g.setColour(Colours::accentColor().withAlpha(0.5f));
        g.fillRoundedRectangle(getLocalBounds().toFloat(), 4.0f);
    }
}

void SettingsComponent::VerticalDragBar::mouseEnter(const juce::MouseEvent&) { repaint(); }
void SettingsComponent::VerticalDragBar::mouseExit(const juce::MouseEvent&) { repaint(); }

void SettingsComponent::VerticalDragBar::mouseDown(const juce::MouseEvent&) {
    isDragging = true;
    dragStartPanelHeight = currentPanelHeight;
    repaint();
}

void SettingsComponent::VerticalDragBar::mouseDrag(const juce::MouseEvent& e) {
    int newHeight = dragStartPanelHeight - e.getDistanceFromDragStartY();
    if (onDrag) onDrag(newHeight);
}

void SettingsComponent::VerticalDragBar::mouseUp(const juce::MouseEvent&) {
    isDragging = false;
    repaint();
    if (onDragEnd) onDragEnd();
}
#endif

SettingsComponent::SettingsComponent(OscirenderAudioProcessor& p, OscirenderAudioProcessorEditor& editor)
    : audioProcessor(p), pluginEditor(editor),
      envelope(p),
    keyboard(p.keyboardState, juce::CustomMidiKeyboardComponent::horizontalKeyboard) {
    addAndMakeVisible(effects);
    addAndMakeVisible(fileControls);
    addAndMakeVisible(quickControls);
    addAndMakeVisible(mainResizerBar);
#if OSCI_PREMIUM
    addAndMakeVisible(verticalResizerBar);
    verticalResizerBar.onDrag = [this](int newHeight) {
        newHeight = juce::jmax(kCollapsedModHeight, newHeight);
        // Snap in/out of collapsed state
        if (!modPanelCollapsed && newHeight < kCollapseThreshold) {
            modPanelCollapsed = true;
            modPanelHeight = kCollapsedModHeight;
        } else if (modPanelCollapsed && newHeight >= kCollapseThreshold) {
            modPanelCollapsed = false;
            modPanelHeight = newHeight;
        } else if (!modPanelCollapsed) {
            modPanelHeight = newHeight;
        }
        resized();
    };
    verticalResizerBar.onDragEnd = [this]() {
        // Enforce minimum expanded height when drag ends
        if (!modPanelCollapsed && modPanelHeight < kMinExpandedModHeight)
            modPanelHeight = kMinExpandedModHeight;
        // Save the current size
        if (!modPanelCollapsed)
            audioProcessor.setProperty("mainLayoutModSize", (double)modPanelHeight);
        resized();
    };
#endif
    addAndMakeVisible(midi);
    addChildComponent(quickControls);
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
        sidechain->onDisabledClicked = [this]() {
            pluginEditor.openAudioSettings();
        };

        // In standalone mode, monitor device changes to update sidechain disabled state
        if (juce::JUCEApplicationBase::isStandaloneApp()) {
            if (auto* holder = juce::StandalonePluginHolder::getInstance())
                holder->deviceManager.addChangeListener(this);
            updateSidechainDisabledState();
        }

        // Uncollapse callback for all modulation sources
        auto uncollapse = [safeThis = juce::Component::SafePointer<SettingsComponent>(this)]() {
            if (safeThis == nullptr) return;
            if (safeThis->modPanelCollapsed) {
                safeThis->modPanelCollapsed = false;
                double saved = std::any_cast<double>(safeThis->audioProcessor.getProperty("mainLayoutModSize", 250.0));
                safeThis->modPanelHeight = (int)(saved < 0 ? 250 : saved);
                safeThis->modPanelHeight = juce::jmax(kMinExpandedModHeight, safeThis->modPanelHeight);
                safeThis->resized();
            }
        };
        lfo->onUncollapseRequested = uncollapse;
        envelope.onUncollapseRequested = uncollapse;
        random->onUncollapseRequested = uncollapse;
        sidechain->onUncollapseRequested = uncollapse;

        // Auto-uncollapse and select LFO tab when an LFO assignment is added
        audioProcessor.lfoParameters.onAssignmentAdded = [this, uncollapse]() {
            auto safeThis = juce::Component::SafePointer<SettingsComponent>(this);
            juce::MessageManager::callAsync([safeThis, uncollapse]() {
                if (safeThis == nullptr) return;
                uncollapse();
                // Switch to the source that was just assigned and refresh
                auto assignments = safeThis->audioProcessor.lfoParameters.getAssignments();
                if (!assignments.empty()) {
                    int srcIdx = assignments.back().sourceIndex;
                    safeThis->lfo->switchToSource(srcIdx);
                }
                safeThis->lfo->refreshAllDepthIndicators();
            });
        };
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
    double mainLayoutModSize = std::any_cast<double>(audioProcessor.getProperty("mainLayoutModSize", -0.35));

#if !OSCI_PREMIUM
    {
        // 3-component horizontal layout: vis | resizer | effects
        mainLayout.setItemLayout(0, -0.1, -0.5, mainLayoutVisSize);
        mainLayout.setItemLayout(1, pluginEditor.RESIZER_BAR_SIZE, pluginEditor.RESIZER_BAR_SIZE, pluginEditor.RESIZER_BAR_SIZE);
        mainLayout.setItemLayout(2, -0.1, -0.9, -(1.0 + mainLayoutVisSize));
    }
#else
    {
        // 3-component horizontal layout: vis | resizer | effects
        mainLayout.setItemLayout(0, -0.1, -0.5, mainLayoutVisSize);
        mainLayout.setItemLayout(1, pluginEditor.RESIZER_BAR_SIZE, pluginEditor.RESIZER_BAR_SIZE, pluginEditor.RESIZER_BAR_SIZE);
        mainLayout.setItemLayout(2, -0.1, -0.9, -(1.0 + mainLayoutVisSize));
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
#if OSCI_PREMIUM
    if (juce::JUCEApplicationBase::isStandaloneApp()) {
        if (auto* holder = juce::StandalonePluginHolder::getInstance())
            holder->deviceManager.removeChangeListener(this);
    }
#endif
    for (int e = 0; e < NUM_ACTIVE_ENVELOPES; ++e)
        audioProcessor.envelopeParameters.params[e].removeListenerFromAll(&dahdsrListener);
    audioProcessor.visualiserParameters.visualiserFullScreen->removeListener(this);
    audioProcessor.midiEnabled->removeListener(this);
}

void SettingsComponent::parameterValueChanged(int parameterIndex, float newValue) {
    auto safeThis = juce::Component::SafePointer<SettingsComponent>(this);
    bool midiJustEnabled = (parameterIndex == audioProcessor.midiEnabled->getParameterIndex()
                            && newValue >= 0.5f);
    juce::MessageManager::callAsync([safeThis, midiJustEnabled] {
        if (safeThis == nullptr) return;
#if OSCI_PREMIUM
        if (midiJustEnabled) {
            safeThis->audioProcessor.lfoParameters.switchUnmodifiedFreeToTrigger();
        }
#endif
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
            frame.setVisible(frameSettingsVisible);
            if (frameSettingsVisible) {
                frame.resized();
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
#if !OSCI_PREMIUM
            quickControls.setVisible(false);
            midi.setVisible(false);
#endif
            effects.setVisible(false);
            frame.setVisible(false);
            examples.setVisible(true);
            examples.setBounds(effectsBounds);
        } else {
            examples.setVisible(false);
            effects.setVisible(true);
#if OSCI_PREMIUM
            // MIDI and quick controls are laid out outside this lambda in PREMIUM mode
            quickControls.setVisible(true);
            midi.setVisible(true);
#else
            quickControls.setVisible(true);
            static constexpr int controlsRowHeight = 60;
            quickControls.setBounds(effectsBounds.removeFromBottom(controlsRowHeight));
            effectsBounds.removeFromBottom(kGap);
#endif
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
        const bool showKeyboard = midiOn
                          && audioProcessor.getGlobalBoolValue("showMidiKeyboard", true);

        midi.setVisible(false);

        if (showKeyboard) {
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

            if (midiOn) {
                // Envelope visible even when keyboard is hidden
                static constexpr int envelopeHeight = 90;
                envelope.setBounds(effectsBounds.removeFromBottom(envelopeHeight));
                envelope.setVisible(true);
                effectsBounds.removeFromBottom(kGap);
            } else {
                envelope.setVisible(false);
            }
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
        // PREMIUM: top (vis | resizer | effects) / vertical resizer / bottom modulation row
        // Keyboard at the very bottom spanning full width when MIDI enabled
        // ============================================================

        const bool midiOn = audioProcessor.midiEnabled->getBoolValue();
        const bool showKeyboard = midiOn
                          && audioProcessor.getGlobalBoolValue("showMidiKeyboard", true);

        // Reserve space for keyboard at the very bottom if MIDI is on
        if (showKeyboard) {
            keyboardPanelBounds = juce::Rectangle<int>(
                area.getX(),
                area.getBottom() - keyboardHeight,
                area.getWidth(),
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

            area.removeFromBottom(keyboardHeight + kGap);
        } else {
            keyboardPanelBounds = {};
        }

        keyboardViewport.setVisible(showKeyboard);

        // Clamp to available space
        int resizerH = pluginEditor.RESIZER_BAR_SIZE;
        int maxModH = area.getHeight() - resizerH - 100; // Leave at least 100 for top
        modPanelHeight = juce::jmin(modPanelHeight, maxModH);

        int effectiveModH = modPanelCollapsed ? kCollapsedModHeight : modPanelHeight;

        auto bottomArea = area.removeFromBottom(effectiveModH);
        auto resizerArea = area.removeFromBottom(resizerH);
        auto topArea = area;

        verticalResizerBar.setBounds(resizerArea);
        verticalResizerBar.setVisible(true);
        verticalResizerBar.setCurrentPanelHeight(effectiveModH);
        layoutTopProxy.setBounds(topArea);
        layoutBottomProxy.setBounds(bottomArea);

        // --- Top section: vis | resizer | effects ---
        juce::Component* columns[] = { &layoutVisColumnProxy, &mainResizerBar, &layoutEffectsColumnProxy };
        mainLayout.layOutComponents(columns, 3, topArea.getX(), topArea.getY(), topArea.getWidth(), topArea.getHeight(), false, true);

        auto effectsBounds = layoutEffectsColumnProxy.getBounds();
        auto visBounds = layoutVisColumnProxy.getBounds();

        // Reserve space for MIDI + quick controls below vis/effects columns
        {
            static constexpr int controlsRowHeight = 60;
            quickControls.setBounds(visBounds.removeFromBottom(controlsRowHeight));
            visBounds.removeFromBottom(kGap);
            midi.setBounds(effectsBounds.removeFromBottom(controlsRowHeight));
            effectsBounds.removeFromBottom(kGap);
        }

        layoutVisColumn(visBounds);
        layoutEffectsColumn(effectsBounds);

        lfo->setMidiEnabled(midiOn);

        // Set collapsed state on all modulation components
        lfo->setCollapsed(modPanelCollapsed);
        envelope.setCollapsed(modPanelCollapsed);
        random->setCollapsed(modPanelCollapsed);
        sidechain->setCollapsed(modPanelCollapsed);

        // --- Bottom modulation panel: LFO | Envelope | Random | Sidechain in a row ---
        static constexpr int kMaxRandW = 250;
        static constexpr int kMaxScW = 200;
        static constexpr int kMinScW = 130;

        int numGaps = midiOn ? 3 : 2;
        int totalW = bottomArea.getWidth() - kGap * numGaps;
        int randW = juce::jmin(kMaxRandW, totalW * 25 / 100);
        int scW = juce::jlimit(kMinScW, kMaxScW, totalW * 15 / 100);
        int flexW = totalW - randW - scW;
        int lfoW, envW;
        if (midiOn) {
            lfoW = flexW / 2;
            envW = flexW - lfoW;
        } else {
            lfoW = flexW;
            envW = 0;
        }

        auto modRow = bottomArea;
        lfo->setBounds(modRow.removeFromLeft(lfoW));
        modRow.removeFromLeft(kGap);

        if (midiOn) {
            envelope.setBounds(modRow.removeFromLeft(envW));
            envelope.setVisible(true);
            modRow.removeFromLeft(kGap);
        } else {
            envelope.setVisible(false);
        }

        random->setBounds(modRow.removeFromLeft(randW));
        modRow.removeFromLeft(kGap);
        sidechain->setBounds(modRow.removeFromLeft(scW));

        if (isVisible() && getWidth() > 0 && getHeight() > 0) {
            audioProcessor.setProperty("mainLayoutVisSize", mainLayout.getItemCurrentRelativeSize(0));
            if (!modPanelCollapsed)
                audioProcessor.setProperty("mainLayoutModSize", (double)modPanelHeight);
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
    frameSettingsVisible = false;
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

    bool isAnimated = extension == ".gpla" || extension == ".gif" || extension == ".mov" || extension == ".mp4"
#if OSCI_PREMIUM
            || isLottieExtension(extension)
#endif
            ;
    quickControls.setAnimated(isAnimated);

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
    } else if (isImage) {
        frameSettingsVisible = true;
        frame.setAnimated(isAnimated);
        frame.setImage(true);
        frame.resized();
    }
    fileControls.updateFileLabel();
    resized();
}

void SettingsComponent::update() {
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

void SettingsComponent::changeListenerCallback(juce::ChangeBroadcaster*) {
#if OSCI_PREMIUM
    updateSidechainDisabledState();
#endif
}

#if OSCI_PREMIUM
void SettingsComponent::updateSidechainDisabledState() {
    if (!sidechain) return;
    bool hasInput = false;
    if (auto* holder = juce::StandalonePluginHolder::getInstance()) {
        if (auto* device = holder->deviceManager.getCurrentAudioDevice())
            hasInput = device->getActiveInputChannels().countNumberOfSetBits() > 0;
    }
    sidechain->setSidechainDisabled(!hasInput);
}
#endif

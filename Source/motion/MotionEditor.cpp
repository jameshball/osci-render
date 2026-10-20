#include "MotionEditor.h"
#include "ui/MenuIcons.h"
#include "../components/AboutComponent.h"
#include <cstdlib>

MotionEditor::MotionEditor(MotionProcessor& ownerProcessor)
    : CommonPluginEditor(ownerProcessor, "osci-motion", "osci-motion", 1440, 900), processor(ownerProcessor), timeline(ownerProcessor), composition(ownerProcessor), assetLibrary(ownerProcessor.document), curveEditor(ownerProcessor), cameraRig(ownerProcessor), clipTimingPanel(ownerProcessor), effectStack(ownerProcessor), routingPanel(ownerProcessor), modulatorLibrary(ownerProcessor), propertyInspector(ownerProcessor) {
    lookAndFeel.setControlCornerRadius(3.0f);
    motionLookAndFeel.setControlCornerRadius(3.0f);
    setLookAndFeel(&motionLookAndFeel);
    visualiserSettings.setSurfaceColours(osci::Colours::veryDark(), osci::Colours::surface());
    scopePanel.onXYOnly = [this] { offerHatchingForXY(); };
    // The Scope's cog selects the Scope, whose beam and display properties
    // animate in Properties and the Graph like any other; keeping
    // openSettings set also keeps the cog in the strip.
    visualiser.openSettings = [this] { select(processor.document.project().beam.id); };
    visualiser.closeSettings = [] {};
    // The scope's record, settings, popout and full-screen controls live in
    // the Scope panel header rather than on top of the picture.
    visualiserControls = &visualiser.detachControls(*this);
    visualiser.onControlsChanged = [this] { resized(); };
    visualiser.setControlStyle(osci::Colours::text().withAlpha(.78f), 5);
    // The Scope shows the project as it plays: Space plays and pauses that.
    visualiser.setPausable(false);
    // Children in paint order: each draws over those added before it.
    for (auto* header : { &viewportHeader, &outputHeader, &inspectorHeader, &timelineHeader }) {
        addAndMakeVisible(header);
    }
    for (auto* component : std::initializer_list<juce::Component*> { &timeline, &composition, &assetLibrary, &importButton, &playButton, &startButton, &endButton, &timeLabel, &propertyInspector, &curveEditor, &timelineTabs, &timelineDivider, &previewDivider, &statusBar }) {
        addAndMakeVisible(component);
    }
    addChildComponent(scopeBack);
    addChildComponent(scopeLabel);
    addChildComponent(scopeShared);
    for (const auto& [label, text] : std::initializer_list<std::pair<juce::Label*, const char*>> {{&compositionTitle, "Scene"}, {&outputTitle, "Scope"}}) {
        addAndMakeVisible(label);
        label->setText(text, juce::dontSendNotification);
        label->setFont(motion::style::title());
        label->setBorderSize(juce::BorderSize<int>(0));
    }
    // Tools live inside the Scene, Blender style; the header keeps its title
    // and the view presets.
    addAndMakeVisible(sceneTools);
    addChildComponent(exportBar);
    addAndMakeVisible(libraryTabs);
    addChildComponent(graphSideViewport);
    addChildComponent(modulatorLibrary);
    addAndMakeVisible(tempoValue);
    addAndMakeVisible(tempoLabel);
    // The Scope's controls float over its top right, like the Scene's tools:
    // inside it, which draws them into its picture, as over an OpenGL view on
    // Windows nothing else would show.
    visualiser.addAndMakeVisible(scopeTools);
    addAndMakeVisible(outputButton);
    addAndMakeVisible(outputLabel);
    addAndMakeVisible(loopButton);
    addAndMakeVisible(tapButton);
    removeChildComponent(&undoRedoControls);
    for (auto* button : {&undoButton, &redoButton}) { addAndMakeVisible(*button); }
    addAndMakeVisible(undoDescription);
    undoDescription.setFont(motion::style::caption());
    undoDescription.setColour(juce::Label::textColourId, osci::Colours::textMuted());
    undoDescription.setJustificationType(juce::Justification::centredRight);
    undoDescription.setMinimumHorizontalScale(1.0f);
    undoButton.onClick = [this] { undoRedoControls.undo(); refreshUndo(); };
    redoButton.onClick = [this] { undoRedoControls.redo(); refreshUndo(); };
    undoButton.onPopupMenu = [this] { osci::showUndoHistory(processor.getUndoManager(), false, undoButton, [this] { refreshUndo(); }); };
    redoButton.onPopupMenu = [this] { osci::showUndoHistory(processor.getUndoManager(), true, redoButton, [this] { refreshUndo(); }); };
    addChildComponent(effectLibrary);
    addChildComponent(cancelExport);
    addAndMakeVisible(inspectorTitle);
    addChildComponent(curveList);
    setUpMenus();
    setUpCompositionNavigation();
    setUpScene();
    setUpScope();
    setUpTransport();
    setUpLibrary();
    setUpProperties();
    setUpTimeline();
    setUpGraph();
    processor.document.addChangeListener(this);
    auto* holder = juce::StandalonePluginHolder::getInstance();
    if (holder != nullptr) { holder->deviceManager.addChangeListener(this); }
    keepOutputPlayable();
    sliderBakes.onStatus = [this](const juce::String& text, bool error) {
        statusBar.show(text, error ? MotionStatusBar::Kind::error : MotionStatusBar::Kind::notice);
        // An export asked for while they baked goes ahead once they're done.
        if (afterSliderBakes == nullptr || (!error && sliderBakes.busy())) { return; }
        auto resume = std::exchange(afterSliderBakes, nullptr);
        if (error) { return; }
        const juce::Component::SafePointer<MotionEditor> self(this);
        juce::MessageManager::callAsync([self, resume] { if (self != nullptr) { resume(); } });
    };
    // Beam overloaded: why, and the next rate up to draw it whole.
    statusBar.onOverload = [this](juce::Component& mark) {
        const auto rate = processor.document.mainProject().sampleRate;
        // Up to the default from below it; past that, twice as fast.
        std::optional<double> higher;
        const auto wanted = rate < motion::Project::defaultSampleRate - .5 ? motion::Project::defaultSampleRate : rate * 2;
        for (const auto candidate : projectSampleRates) {
            if (candidate > wanted - .5) { higher = candidate; break; }
        }
        auto popover = std::make_unique<motion::ui::BeamOverloadPopover>(rate, higher);
        auto* raw = popover.get();
        raw->onChoose = [this, raw](std::optional<double> chosen) {
            if (chosen.has_value() && processor.document.mainProject().sampleRate != *chosen) {
                const auto value = *chosen;
                processor.document.edit("Change the sample rate", [value](motion::Project& project) { project.sampleRate = value; });
                statusBar.show("Sample rate " + motion::ui::BeamOverloadPopover::kilohertz(value), MotionStatusBar::Kind::notice);
            }
            dismissPopover(raw);
        };
        showPopover(std::move(popover), getLocalArea(&statusBar, mark.getBounds()));
    };
    sliderBakes.update();
    composition.refresh();
    timeline.refreshTracks();
    refreshInspector();
    startTimerHz(30);
    setResizeLimits(1100, 700, 4096, 2160);
    loadLayout();
    juce::Desktop::getInstance().addFocusChangeListener(this);
    resized();
    // After the command line is read: a file or render opened with it goes
    // straight there.
    juce::MessageManager::callAsync([owner = juce::Component::SafePointer<MotionEditor>(this)] {
        if (owner != nullptr) { owner->atLaunch(); }
    });
}

void MotionEditor::setUpMenus() {
    // File is built when opened (it lists recent projects); the rest are
    // command lists.
    menus.addTopLevelMenu("File");
    menus.customMenuLogic = [this](juce::PopupMenu& menu, int index) {
        if (index == fileMenu) { buildFileMenu(menu); }
        if (index == audioMenu) {
            menu.addItem(importAudioMenuId, "Import audio...");
            menu.addSeparator();
            // Named as the field beside the transport is.
            menu.addItem(outputMenuId, "Output...");
            // The project's own rate: what it is drawn and exported at.
            juce::PopupMenu rates;
            const auto current = processor.document.mainProject().sampleRate;
            for (std::size_t index = 0; index < projectSampleRates.size(); ++index) {
                const auto rate = projectSampleRates[index];
                rates.addItem(sampleRateMenuId + static_cast<int>(index), juce::String(rate / 1000.0, rate == std::floor(rate / 1000.0) * 1000.0 ? 0 : 1) + " kHz" + (rate == motion::Project::defaultSampleRate ? juce::String(" (default)") : juce::String()), true, std::abs(rate - current) < .5);
            }
            menu.addSubMenu("Sample rate", rates);
            menu.addSeparator();
        }
        if (index == timingMenuIndex) {
            menu = timingMenu();
            menu.setLookAndFeel(&motionLookAndFeel);
        }
        // Undo and redo name the step and are off with nothing to undo, as
        // the buttons beside the transport are.
        if (index == editMenu) {
            auto& undoManager = processor.getUndoManager();
            const auto undo = undoManager.getUndoDescription(), redo = undoManager.getRedoDescription();
            juce::PopupMenu::Item undoItem(undo.isNotEmpty() ? "Undo " + undo : juce::String("Undo"));
            undoItem.itemID = undoMenuId;
            undoItem.isEnabled = undoManager.canUndo();
            undoItem.shortcutKeyDescription = motion::style::shortcutText("Cmd+Z");
            menu.addItem(undoItem);
            juce::PopupMenu::Item redoItem(redo.isNotEmpty() ? "Redo " + redo : juce::String("Redo"));
            redoItem.itemID = redoMenuId;
            redoItem.isEnabled = undoManager.canRedo();
            redoItem.shortcutKeyDescription = motion::style::shortcutText("Cmd+Shift+Z");
            menu.addItem(redoItem);
        }
    };
    menus.customMenuSelectedLogic = [this](int id, int index) {
        if (index == audioMenu && id == importAudioMenuId) {
            chooseAudioFile();
            return true;
        }
        if (index == audioMenu && id == outputMenuId) {
            showOutputPanel();
            return true;
        }
        // The rate is the project's: an edit, undone as one.
        if (index == audioMenu && id >= sampleRateMenuId && id < sampleRateMenuId + static_cast<int>(projectSampleRates.size())) {
            const auto rate = projectSampleRates[static_cast<std::size_t>(id - sampleRateMenuId)];
            if (processor.document.mainProject().sampleRate != rate) {
                processor.document.edit("Change the sample rate", [rate](motion::Project& project) { project.sampleRate = rate; });
            }
            return true;
        }
        if (index == editMenu && (id == undoMenuId || id == redoMenuId)) {
            if (id == undoMenuId) { undoRedoControls.undo(); } else { undoRedoControls.redo(); }
            refreshUndo();
            return true;
        }
        return (index == fileMenu && fileMenuItemSelected(id)) || (index == timingMenuIndex && applyTiming(id));
    };
    menus.addTopLevelMenu("Edit");
    menus.addTopLevelMenu("Clip");
    menus.addTopLevelMenu("Transport");
    menus.addTopLevelMenu("Audio");
    menus.addStandaloneAudioSettingsMenuItem(audioMenu, processor, *this);
    menus.addTopLevelMenu("View");
    menus.addTopLevelMenu("Timing");
    menus.addTopLevelMenu("Help");
    registerCommands();
    menus.addMenuItem(helpMenu, "Start screen...", [this] { showStartScreen(); });
    menus.addMenuItem(helpMenu, "Keyboard shortcuts...", [this] { showShortcuts(); }, motion::style::shortcutText("Cmd+/"));
    menus.addMenuSeparator(helpMenu);
    menus.addMenuItem(helpMenu, "About osci-motion", [this] {
        AboutComponent::Info about;
        about.imageData = BinaryData::motion_logo_png;
        about.imageSize = BinaryData::motion_logo_pngSize;
        about.productName = ProjectInfo::projectName;
        about.companyName = ProjectInfo::companyName;
        about.versionString = ProjectInfo::versionString;
        about.isPremium = true;
        about.betaUpdatesEnabled = osci::UpdateSettings(processor.getProductSlug()).betaUpdatesEnabled();
        about.onBetaUpdatesChanged = [this](bool enabled) {
            osci::UpdateSettings(processor.getProductSlug()).setReleaseTrack(enabled ? osci::ReleaseTrack::Beta : osci::ReleaseTrack::Stable);
            refreshBetaUpdatesButton();
            resized();
        };
        about.websiteUrl = "https://osci-render.com";
        about.onPrivacyAndTerms = [this] { openPrivacyAndTerms(); };
        showOverlay(AboutComponent::createOverlay(about));
    });
    menus.addSupportMenuItems(helpMenu, processor, *this);
    initialiseMenuBar(menus);
    menuBar.setLookAndFeel(&motionLookAndFeel);
}

void MotionEditor::setUpCompositionNavigation() {
    scopeShared.setText("Shared composition", juce::dontSendNotification);
    scopeShared.setFont(motion::style::body());
    scopeShared.setColour(juce::Label::textColourId, osci::Colours::text().withAlpha(0.55f));
    scopeShared.setJustificationType(juce::Justification::centredRight);
    scopeBack.setName("Back to parent composition");
    scopeBack.onClick = [this] { leaveComposition(); };
    scopeLabel.setName("Composition name");
    scopeLabel.setEditable(false, true);
    scopeLabel.setTooltip("Double-click to rename");
    scopeLabel.onEditorShow = [this] { scopeNameGeneration = processor.document.scopeGeneration(); };
    scopeLabel.onTextChange = [this] {
        const auto name = scopeLabel.getText().trim();
        if (processor.document.editingComposition() == 0 || processor.document.scopeGeneration() != scopeNameGeneration || name.isEmpty() || name.length() > 200) { return; }
        processor.document.edit("Rename composition", [name](motion::Project& project) { project.name = name; });
    };
}

void MotionEditor::setUpTransport() {
    outputLabel.setText("Output", juce::dontSendNotification);
    outputLabel.setFont(motion::style::caption());
    outputLabel.setColour(juce::Label::textColourId, osci::Colours::textMuted());
    outputLabel.setJustificationType(juce::Justification::centredRight);
    outputButton.setRouting(processor.getRouting(), outputDevice().outputs > 0);
    outputButton.onClick = [this] { showOutputPanel(); };
    playButton.setTooltip("Play / pause (Space)");
    startButton.setTooltip("Go to start (Home)");
    endButton.setTooltip("Go to end (End)");
    loopButton.setTitle("Loop playback");
    loopButton.setTooltip("Loop playback (L). I and O set the loop at the playhead.");
    loopButton.onClick = [this] { toggleLoop(); };
    addAndMakeVisible(autoKeyButton);
    autoKeyButton.setClickingTogglesState(false);
    autoKeyButton.onColour = motion::style::error().withAlpha(.2f);
    autoKeyButton.setTooltip("Auto-key: every change is keyed at the playhead");
    autoKeyButton.onClick = [this] { setAutoKey(!autoKeyButton.getToggleState()); };
    processor.document.onEdited = [this](const motion::Project& before) { editRecorded(before); };
    startButton.onClick = [this] { seekAndReveal(0); };
    endButton.onClick = [this] { seekAndReveal(processor.document.project().duration); };
    timeLabel.setFont(motion::style::mono());
    timeLabel.setJustificationType(juce::Justification::centred);
    tempoValue.setFont(motion::style::body());
    tempoLabel.setFont(motion::style::caption());
    tempoLabel.setColour(juce::Label::textColourId, osci::Colours::textMuted());
    refreshTiming();
    tempoLabel.setText("BPM", juce::dontSendNotification);
    timeLabel.setName("Timeline position");
    timeLabel.setEditable(false, true);
    timeLabel.setColour(juce::Label::backgroundColourId, osci::Colours::surfaceRaised());
    timeLabel.setTooltip("Double-click to go to a time: as the ruler shows it, or with s for seconds or f for frames");
    timeLabel.onEditorShow = [this] {
        positionEditGrid = processor.document.project().timeGrid();
        positionEditGeneration = processor.document.scopeGeneration();
        positionEditRevision = processor.document.revision();
    };
    timeLabel.onTextChange = [this] {
        const auto& project = processor.document.project();
        const auto requested = positionEditGrid.parsePosition(timeLabel.getText().toStdString());
        const bool current = positionEditGeneration == processor.document.scopeGeneration() && positionEditRevision == processor.document.revision();
        timeLabel.setText(juce::String(project.timeGrid().positionLabel(processor.position.load())), juce::dontSendNotification);
        if (!current) { return; }
        if (!requested.has_value() || *requested > project.duration) {
            statusBar.show("Enter a position from 0 to " + juce::String(project.duration, 3)
                + " seconds: seconds (90s or 1:30s), frames (240f), or bar.beat.tick in the musical display.");
            return;
        }
        seekAndReveal(*requested);
    };
    tapButton.setName("Tap tempo");
    tapButton.setTitle("Tap tempo");
    tapButton.setTooltip("Tap on the beat, four times or more, to set the tempo");
    tapButton.setWantsKeyboardFocus(false);
    tapButton.onClick = [this] { tap(); };
    tempoValue.setName("Project tempo");
    tempoValue.setEditable(false, true);
    tempoValue.setColour(juce::Label::backgroundColourId, osci::Colours::surfaceRaised());
    tempoValue.setJustificationType(juce::Justification::centred);
    tempoValue.onTextChange = [this] {
        const auto typed = motion::ui::parseNumber(tempoValue.getText());
        const auto value = typed.value_or(0);
        if (value < 1 || value > 1000) {
            tempoValue.setText(juce::String(processor.document.project().bpm, 1), juce::dontSendNotification);
            statusBar.show("Enter a tempo between 1 and 1000 BPM.", MotionStatusBar::Kind::notice);
            return;
        }
        if (value != processor.document.project().bpm) {
            const auto result = processor.document.changeTempo(value);
            if (result.failed()) {
                tempoValue.setText(juce::String(processor.document.project().bpm, 1), juce::dontSendNotification);
                statusBar.show("Couldn't change the tempo: " + result.getErrorMessage());
            }
        }
    };
    exportBar.setName("Signal export progress");
    cancelExport.onClick = [this] { if (exportState != nullptr) { exportState->cancelled.store(true); } };
    playButton.onClick = [this] { togglePlayback(); };
}

MotionEditor::~MotionEditor() {
    if (luaPreviewCancel != nullptr) { luaPreviewCancel->store(true); }
    luaPreviews.removeAllJobs(true, 10000);
    processor.document.onEdited = nullptr;
    juce::Desktop::getInstance().removeFocusChangeListener(this);
    // Closed as it should be: the session is kept by the app on quitting.
    if (recovery != nullptr) { recovery->finish(); }
    // A popover is parented here and deletes itself only when dismissed.
    if (popover != nullptr) {
        popover->exitModalState(0);
        delete popover.getComponent();
    }
    visualiser.setFullScreenCallback(nullptr);
    for (auto* child : getChildren()) { child->removeComponentListener(&dialogStyle); }
    motion::style::unstyleDialogs(*this, dialogStyle.look);
    saveLayout();
    menuBar.setLookAndFeel(nullptr);
    processor.blenderInputs().cancelAllCaptures();
    processor.textureInputs().cancelAllCaptures();
    stopTimer();
    visualiser.openSettings = {};
    visualiser.closeSettings = {};
    visualiser.onControlsChanged = {};
    curveEditor.onPreview = {};
    processor.prepareComposition(processor.document.project());
    processor.document.removeChangeListener(this);
    auto* holder = juce::StandalonePluginHolder::getInstance();
    if (holder != nullptr) { holder->deviceManager.removeChangeListener(this); }
    for (const auto& task : pendingImports) { task->cancelled.store(true); }
    if (projectLoad != nullptr) { projectLoad->cancelled.store(true); }
    imports.removeAllJobs(true, -1);
    if (exportState != nullptr) { exportState->cancelled.store(true); }
    exports.removeAllJobs(true, -1);
    menuBar.setModel(nullptr);
    setLookAndFeel(nullptr);
}

void MotionEditor::showPopover(std::unique_ptr<juce::Component> content, juce::Rectangle<int> anchor, Placement placement) {
    auto* panel = content.get();
    motion::style::styleFields(*panel);
    auto& box = juce::CallOutBox::launchAsynchronously(std::move(content), anchor, this);
    box.setArrowSize(9.0f);
    if (placement == Placement::below) { box.updatePosition(anchor, getLocalBounds().withTop(anchor.getBottom())); }
    if (placement == Placement::left) { box.updatePosition(anchor, getLocalBounds().withRight(anchor.getX())); }
    popover = &box;
    // Escape in a field closes the popover, as it does anywhere else in it.
    std::vector<juce::TextEditor*> fields;
    for (auto* child : panel->getChildren()) {
        auto* field = dynamic_cast<juce::TextEditor*>(child);
        if (field == nullptr) { continue; }
        fields.push_back(field);
        if (!field->onEscapeKey) { field->onEscapeKey = [panel] { dismissPopover(panel); }; }
    }
    // The first field takes the keyboard, its text selected; without one the
    // popover itself does, so Escape still closes it.
    juce::MessageManager::callAsync([first = juce::Component::SafePointer<juce::TextEditor>(fields.empty() ? nullptr : fields.front()), popover = juce::Component::SafePointer<juce::CallOutBox>(&box)] {
        if (first != nullptr && first->isShowing()) {
            first->grabKeyboardFocus();
            first->selectAll();
        } else if (popover != nullptr) {
            popover->setWantsKeyboardFocus(true);
            popover->grabKeyboardFocus();
        }
    });
}

// A popover's Apply edits after the native event has returned, and only the
// project state the popover opened on; otherwise it just closes. A failed
// edit shows its error in the panel (which stays open) or the status bar.
std::function<void(MotionEditor::PopoverEdit)> MotionEditor::popoverEdit(juce::Component* panel, std::function<void(const juce::String&)> showError) {
    const juce::Component::SafePointer<MotionEditor> owner(this);
    const juce::Component::SafePointer<juce::Component> popover(panel);
    const auto generation = processor.document.generation();
    const auto revision = processor.document.revision();
    return [owner, popover, generation, revision, showError](PopoverEdit edit) {
        juce::MessageManager::callAsync([owner, popover, generation, revision, showError, edit] {
            if (owner == nullptr || popover == nullptr) { return; }
            auto& document = owner->processor.document;
            if (document.generation() == generation && document.revision() == revision) {
                const auto result = edit(document);
                if (result.failed() && showError) {
                    showError(result.getErrorMessage());
                    return;
                }
                if (result.failed()) { owner->statusBar.show(result.getErrorMessage()); }
            } else {
                owner->statusBar.show("The project changed while this was open, so nothing was changed. Try again.", MotionStatusBar::Kind::warning);
            }
            dismissPopover(popover.getComponent());
        });
    };
}

void MotionEditor::dismissPopover(juce::Component* content) {
    if (content == nullptr) { return; }
    auto* box = content->findParentComponentOfClass<juce::CallOutBox>();
    if (box != nullptr) { box->dismiss(); }
}

// Full screen, the Scope covers the window and keeps Motion's strip; the
// visualiser's own full-screen mode would bring back its built-in row.
void MotionEditor::setScopeFullScreen(bool value) {
    if (value == scopeFullScreen) { return; }
    scopeFullScreen = value;
    scopeTools.fullScreen.setIcon(value ? motion::icons::Icon::fullscreenExit : motion::icons::Icon::fullscreen);
    scopeTools.fullScreen.setTooltip(value ? "Exit full screen (Esc)" : "Full screen");
    resized();
    if (value) { visualiser.grabKeyboardFocus(); }
}

void MotionEditor::showOverlay(std::unique_ptr<osci::OverlayComponent> overlay) {
    auto& shown = *overlay;
    shown.setControlLookAndFeel(&dialogStyle.look);
    CommonPluginEditor::showOverlay(std::move(overlay));
    shown.addComponentListener(&dialogStyle);
    motion::style::restyleDialog(shown, dialogStyle.look);
}

void MotionEditor::refreshUndo() {
    auto& undoManager = processor.getUndoManager();
    const auto undo = undoManager.getUndoDescription(), redo = undoManager.getRedoDescription();
    undoButton.setEnabled(undoManager.canUndo());
    redoButton.setEnabled(undoManager.canRedo());
    const auto undoKeys = " (" + motion::style::shortcutText("Cmd+Z") + ")", redoKeys = " (" + motion::style::shortcutText("Cmd+Shift+Z") + ")";
    // Right-click lists the steps, so several go at once.
    const auto history = motion::style::dot() + "right-click for history";
    undoButton.setTooltip((undo.isNotEmpty() ? "Undo " + undo : juce::String("Undo")) + undoKeys + (undoManager.canUndo() ? history : juce::String()));
    redoButton.setTooltip((redo.isNotEmpty() ? "Redo " + redo : juce::String("Redo")) + redoKeys + (undoManager.canRedo() ? history : juce::String()));
    undoDescription.setText(undo.isNotEmpty() ? "Undo " + undo : juce::String(), juce::dontSendNotification);
    motion::style::tooltipWhenCut(undoDescription);
}

void MotionEditor::timerCallback() {
    continueCommandLineRender();
    keepRecovery();
    const auto now = juce::Time::getMillisecondCounterHiRes();
    if (now - recentFrameRequested > 2000.0) {
        recentFrameRequested = now;
        const juce::Component::SafePointer<MotionEditor> owner(this);
        visualiser.captureFrame([owner](juce::Image frame) { if (owner != nullptr && frame.isValid()) { owner->recentFrame = frame; } });
    }
    refreshUndo();
    if (textPreviewDue > 0 && juce::Time::getMillisecondCounterHiRes() >= textPreviewDue) { previewText(); }
    if (luaPreviewDue > 0 && juce::Time::getMillisecondCounterHiRes() >= luaPreviewDue) { previewLua(); }
    processor.showIdleSeek(processor.document.mainProject().duration);
    auto& previewRate = processor.recordingParameters.frameRate;
    // Keep playback and export cadence aligned within the live renderer's supported range.
    const auto projectFrameRate = static_cast<float>(std::clamp<double>(processor.document.mainProject().frameRate, previewRate.min, previewRate.max));
    if (std::abs(previewRate.getValueUnnormalised() - projectFrameRate) > 0.005f) {
        previewRate.setUnnormalisedValueNotifyingHost(projectFrameRate);
    }
    scopeTools.canvas.setEnabled(exportState == nullptr);
    {
        using Control = VisualiserComponent::Control;
        // The strip shows what the visualiser is doing, however it started.
        scopeTools.textureOutput.setToggleState(visualiser.isControlOn(Control::textureOutput), juce::dontSendNotification);
        scopeTools.popout.setToggleState(visualiser.isControlOn(Control::popout), juce::dontSendNotification);
        const auto settingsShown = visualiser.hasControl(Control::settings), popoutShown = visualiser.hasControl(Control::popout);
        if (scopeTools.settings.isVisible() != settingsShown || scopeTools.popout.isVisible() != popoutShown) {
            scopeTools.settings.setVisible(settingsShown);
            scopeTools.popout.setVisible(popoutShown);
            resized();
        }
    }
    {
        const auto rate = processor.exportSampleRate();
        const auto beamRate = motion::beamCycleRate(processor.document.mainProject().frameRate);
        const auto layers = processor.beamLayers.load(std::memory_order_relaxed);
        const auto simplified = processor.beamSimplified.load(std::memory_order_relaxed);
        const auto dot = motion::style::dot();
        // A short line of what is out of the ordinary, beside the rates; its
        // tooltip says the rest. The timeline's ruler shows the bake itself.
        const auto rates = juce::String(rate / 1000.0, rate == std::floor(rate / 1000.0) * 1000.0 ? 0 : 1) + " kHz" + dot + juce::String(beamRate, beamRate == std::floor(beamRate) ? 0 : 2) + " Hz beam";
        juce::StringArray line {rates}, details {rates};
        if (processor.document.project().scope.xyOnly) {
            line.add("XY only");
            details.add("XY only: the Scope has no brightness input, so the beam never blanks");
        }
        if (processor.playing.load() || layers > 0) {
            details.add("Drawing " + juce::String(layers) + (layers == 1 ? " object" : " objects"));
            // Too much to draw in a cycle: its shortest strokes are left out.
            if (simplified) {
                overloadSeenAt = juce::Time::getMillisecondCounterHiRes();
                details.add("Simplified to fit: too much to draw in one cycle of the beam, so the shortest strokes are left out (a higher sample rate draws more)");
            }
        }
        // What the frames are baked for, and how many are left: a count,
        // which a percentage of a bake taking over most of its frames isn't.
        juce::StringArray baked;
        if (processor.hidesHiddenLines()) { baked.add("hidden lines removed"); }
        if (processor.lightsFrames()) { baked.add("lit"); }
        if (processor.reflectsFrames()) { baked.add("reflections"); }
        if (processor.restylesFrames()) { baked.add("lines drawn on surfaces"); }
        if (processor.hatchesFrames()) { baked.add("hatched"); }
        const auto remaining = processor.hiddenLinesRemaining();
        if (!baked.isEmpty()) {
            if (remaining.has_value()) {
                const auto count = juce::String(static_cast<juce::int64>(*remaining));
                juce::String grouped;
                for (int index = 0; index < count.length(); ++index) { grouped << (index > 0 && (count.length() - index) % 3 == 0 ? "," : "") << count[index]; }
                line.add("baking " + grouped + (*remaining == 1 ? " frame" : " frames"));
                details.add("Baking " + grouped + (*remaining == 1 ? " frame" : " frames") + " (" + baked.joinIntoString(", ") + "), from the playhead on; a frame not yet baked draws every line");
            } else {
                details.add("Every frame baked: " + baked.joinIntoString(", "));
            }
        }
        // Overloaded until a moment has passed with nothing simplified, so
        // the mark doesn't flicker as frames come and go.
        statusBar.setOverloaded(juce::Time::getMillisecondCounterHiRes() - overloadSeenAt < 1500);
        statusBar.setStatistics(line.joinIntoString(dot), details.joinIntoString("\n"));
    }
    processor.collectPreparedState();
    processor.followPlayhead();
    assetLibrary.updateLiveStatus();
    const auto restoreError = processor.takeRestoreError();
    if (restoreError.isNotEmpty()) { statusBar.show(restoreError, MotionStatusBar::Kind::warning); }
    const auto preparationError = processor.getPreparationError();
    if (preparationError != lastPreparationError) {
        lastPreparationError = preparationError;
        if (preparationError.isNotEmpty()) {
            statusBar.show("The latest edit cannot play: " + preparationError + " Playing the last valid version.", MotionStatusBar::Kind::warning);
        } else if (statusBar.text().startsWith("The latest edit cannot play")) {
            statusBar.clear();
        }
    }
    if (pendingImports.empty()) {
        assetLibrary.setImportStatus({});
    } else {
        const auto& task = pendingImports.front();
        assetLibrary.setImportStatus(task->cancelled.load() ? "Cancelling import..." : "Preparing " + task->name + "\n" + juce::String(juce::roundToInt(task->progress.load() * 100)) + "%"
            + (pendingImports.size() > 1 ? "  (" + juce::String(static_cast<int>(pendingImports.size() - 1)) + " queued)" : ""));
    }
    if (exportState != nullptr) {
        exportProgress = exportState->videoWithAudio
            ? (exportState->progress.load() + exportState->soundtrackProgress.load()) * 0.5
            : exportState->progress.load();
    }
    const auto playing = processor.playing.load();
    playButton.setIcon(playing ? motion::icons::Icon::pause : motion::icons::Icon::play);
    {
        const auto& project = processor.document.project();
        loopButton.setToggleState(project.looping && project.hasLoop(), juce::dontSendNotification);
    }
    if (playButton.getName() != (playing ? "Pause" : "Play")) {
        playButton.setName(playing ? "Pause" : "Play");
        playButton.setTitle(playButton.getName());
    }
    if (timeLabel.isBeingEdited() && (positionEditGeneration != processor.document.generation() || positionEditRevision != processor.document.revision())) { timeLabel.hideEditor(true); }
    if (!timeLabel.isBeingEdited()) { timeLabel.setText(juce::String(processor.document.project().timeGrid().positionLabel(processor.position.load())), juce::dontSendNotification); }
    if (!tempoValue.isBeingEdited() && !tappedBpm.has_value()) { tempoValue.setText(juce::String(processor.document.project().bpm, 1), juce::dontSendNotification); }
    // A moving playhead repaints only its own columns of the time views, and a
    // slow full refresh (about 3 Hz) catches anything that changes without an
    // edit or a playhead move. Live Blender capture shows in the Scene, which
    // repaints for it while stopped too.
    const auto position = processor.position.load();
    const auto liveFrames = processor.liveSourcePreview();
    // The snapshot is held, so a new one can never reuse the old address.
    const auto live = liveFrames != lastLiveFrames;
    lastLiveFrames = liveFrames;
    const auto slowTick = ++ticks % 10 == 0;
    const auto moved = position != lastPaintedPosition || playing;
    lastPaintedPosition = position;
    timeline.followPlayhead(position, playing);
    timeline.updateBake();
    if (curveEditor.isVisible() && timeline.layout().follow) { curveEditor.followPlayhead(position, playing); }
    if (slowTick) {
        timeline.repaint();
        if (curveEditor.isVisible()) { curveEditor.repaint(); }
    } else if (moved) {
        timeline.repaintPlayhead();
        if (curveEditor.isVisible()) { curveEditor.repaintPlayhead(); }
    }
    if (slowTick || moved || live) { composition.repaint(); }
    // The playhead moves values; the slow tick also refreshes structure.
    if (slowTick) {
        keepEndedCaptures();
        refreshInspector();
    } else if (moved) {
        propertyInspector.refreshValues();
        cameraRig.refresh();
    }
    if (moved || slowTick) {
        refreshCameraTools();
        effectStack.updateValues();
    }
}

void MotionEditor::changeListenerCallback(juce::ChangeBroadcaster* source) {
    // The audio device decides which outputs the sound and the beam can use.
    if (source == &processor.document) {
        refreshFromDocument();
    } else {
        refreshOutputChoices();
        keepOutputPlayable();
        if (outputPanel != nullptr) { outputPanel->setDevice(outputDevice(), processor.getRouting()); }
    }
}

void MotionEditor::refreshFromDocument() {
    // A drawing belongs to the project it was started in.
    if (sceneEditor != nullptr && processor.document.generation() != sceneEditorGeneration) { closeSceneEditor(); }
    // So does an export waiting for its Lua sliders.
    if (afterSliderBakes != nullptr && processor.document.generation() != afterSliderBakesGeneration) {
        afterSliderBakes = nullptr;
        if (statusBar.text() == waitingForSliderBakes) { statusBar.clear(); }
    }
    // An undo or a delete can remove what was selected. Undoing an extraction
    // removes the parts it made and selects the clips they came from again.
    if (selection != 0 && !selectionExists()) {
        const auto& project = processor.document.project();
        const auto restorable = !selectionBeforeExtract.empty() && std::all_of(selectionBeforeExtract.begin(), selectionBeforeExtract.end(), [&project](motion::Id id) { return motion::findClip(project, id) != nullptr; });
        if (restorable) {
            timeline.selectClips(selectionBeforeExtract);
        } else {
            select(0);
        }
        selectionBeforeExtract.clear();
    }
    sliderBakes.requestUpdate();
    refreshOutputChoices();
    if (curveList.isVisible()) { refreshCurveList(); }
    if (processor.document.editingComposition() == 0) { scopeHistory.clear(); }
    if (scopeLabel.isBeingEdited() && scopeNameGeneration != processor.document.generation()) { scopeLabel.hideEditor(true); }
    if (!scopeLabel.isBeingEdited()) { scopeLabel.setText(processor.document.project().name, juce::dontSendNotification); }
    juce::String parentName = "Main";
    if (!scopeHistory.empty() && scopeHistory.back().scope != 0) {
        const auto parent = motion::findDefinition(processor.document.mainProject(), scopeHistory.back().scope);
        if (parent != nullptr) { parentName = parent->name; }
    }
    scopeBack.setButtonText("Back to " + parentName);
    for (const auto& task : pendingImports) {
        if (task->generation != processor.document.generation()) { task->cancelled.store(true); }
    }
    assetLibrary.refresh();
    refreshTiming();
    timeline.refreshTracks();
    curveEditor.refresh();
    composition.refresh();
    refreshInspector();
    refreshCameraTools();
    resized();
    repaint();
}

// The camera the Scene's camera tools act on: the one looked through, or else
// the selected one.
motion::Id MotionEditor::toolCamera() const {
    const auto& cameras = processor.document.project().cameras;
    const auto driven = composition.drivenCamera();
    // A deleted camera stays driven until the Scene's next sync.
    if (driven != 0 && std::any_of(cameras.begin(), cameras.end(), [driven](const auto& camera) { return camera.id == driven; })) { return driven; }
    return selectionIsCamera() ? selection : 0;
}

// Look through follows the selection, the playhead and edits; a camera that
// cannot be looked through says why.
void MotionEditor::refreshCameraTools() {
    // The cog is lit while the Scope's properties are shown.
    scopeTools.settings.setToggleState(selection != 0 && selection == processor.document.project().beam.id, juce::dontSendNotification);
    const auto camera = toolCamera();
    const auto driven = camera != 0 && camera == composition.drivenCamera();
    const auto blocker = composition.driveBlocker(camera);
    sceneTools.lookThrough.setEnabled(driven || blocker.isEmpty());
    sceneTools.lookThrough.setTooltip(driven || blocker.isEmpty() ? "Look through the selected camera: moving the view moves the camera" : blocker);
}

void MotionEditor::enterComposition(motion::Id id, bool fromLibrary) {
    const auto& project = processor.document.project();
    motion::Id definition = 0;
    double time = 0;
    for (const auto& track : project.tracks) {
        for (const auto& clip : track.clips) {
            if (clip.id != id || clip.composition == 0) { continue; }
            definition = clip.composition;
            const auto clock = clip.timing(project.tempo());
            const auto position = processor.position.load();
            time = clock.localTime(position >= clock.start && position < clock.end() ? position : clock.start);
        }
    }
    if (fromLibrary) {
        for (const auto& source : project.definitions) {
            if (source->id != id) { continue; }
            definition = id;
            time = motion::Document::makeCompositionClip(0, *source, 0).offset;
        }
    }
    if (definition == 0 || definition == processor.document.editingComposition()) { return; }
    ScopeView previous;
    previous.scope = processor.document.editingComposition();
    previous.selection = fromLibrary ? selection : id;
    previous.position = processor.position.load();
    previous.timelineFraction = timelineFraction;
    previous.timelineTab = timelineTabs.getCurrentTabIndex();
    previous.curveTarget = curveTarget;
    previous.property = curvePropertyName;
    previous.cameraCurve = cameraCurve;
    previous.timeline = timeline.viewState();
    previous.preview = composition.viewState();
    previous.graph = curveEditor.viewState();

    composition.setNavigating(false);
    composition.setDrivenCamera(0);
    const auto entered = processor.document.enterComposition(definition);
    if (entered.failed()) { statusBar.show(entered.getErrorMessage()); return; }
    scopeHistory.push_back(previous);
    statusBar.clear();
    processor.playing.store(false);
    processor.seek(std::clamp(time, 0.0, processor.document.project().duration));
    composition.restoreView({});
    select(0);
    timeline.scrollToStart();
    timeline.revealTime(time);
    timelineTabs.setSelectedIndex(0);
    refreshFromDocument();
}

void MotionEditor::leaveComposition() {
    composition.setDrivenCamera(0);
    ScopeView previous;
    if (!scopeHistory.empty()) { previous = scopeHistory.back(); scopeHistory.pop_back(); }
    auto entered = processor.document.enterComposition(previous.scope);
    if (entered.failed()) { previous = {}; processor.document.enterComposition(0); scopeHistory.clear(); }
    statusBar.clear();
    processor.playing.store(false);
    processor.seek(previous.position);
    select(previous.selection);
    timelineTabs.setSelectedIndex(previous.timelineTab);
    selectCurveTarget(previous.curveTarget, previous.property, previous.cameraCurve);
    timelineFraction = previous.timelineFraction;
    timeline.restoreView(previous.timeline);
    composition.restoreView(previous.preview);
    curveEditor.restoreView(previous.graph);
    refreshFromDocument();
}

void MotionEditor::select(motion::Id id) {
    selection = id;
    clipTimingPanel.setSelection(id);
    textAnimation.setSelection(id);
    modelSize.setSelection(id);
    // Cameras have no effects, so selecting one shows its Properties.
    const auto camera = selectionIsCamera();
    timeline.setSelection(id);
    timeline.revealSelection();
    composition.setSelection(id);
    // A free camera can be driven from the Scene until its button is released
    // or another camera is chosen.
    if (camera && composition.drivenCamera() != 0 && composition.drivenCamera() != id) { composition.setDrivenCamera(0); }
    // Always in the strip, so they keep its size; usable with a camera selected.
    refreshCameraTools();
    selectCurveTarget(id, curvePropertyName, camera);
    refreshInspector();
    resized();
    repaint();
}

void MotionEditor::previewEffect(const std::string& type, std::optional<motion::Id> owner) {
    const auto* definition = motion::effectDefinition(type);
    auto project = processor.document.project();
    auto* effects = owner.has_value() && definition != nullptr ? motion::findEffectOwner(project, *owner) : nullptr;
    if (effects == nullptr) {
        processor.prepareComposition(processor.document.project());
        composition.refresh();
        return;
    }
    effects->push_back(motion::makeEffect(std::numeric_limits<motion::Id>::max(), *definition));
    processor.prepareComposition(project);
    composition.preview(project);
}

void MotionEditor::addEffectTo(const std::string& type, motion::Id owner) {
    const auto* definition = motion::effectDefinition(type);
    const auto* effects = motion::findEffectOwner(processor.document.project(), owner);
    if (definition == nullptr || effects == nullptr || effects->size() >= motion::maximumEffectsPerOwner) { return; }
    const auto effect = motion::makeEffect(processor.document.newId(), *definition);
    processor.document.edit("Add " + juce::String(definition->name), [owner, effect](motion::Project& project) {
        auto* list = motion::findEffectOwner(project, owner);
        if (list != nullptr) { list->push_back(effect); }
    });
    select(owner);
}

void MotionEditor::dragOperationStarted(const juce::DragAndDropTarget::SourceDetails& details) {
    const auto description = details.description.toString();
    // Library effects and effect cards both land on clips, tracks and the Scene.
    const auto effect = description.startsWith("motion-effect:") || description.startsWith("motion-effect-instance:");
    propertyInspector.setModulatorDrag(description.startsWith("motion-modulator:"));
    timeline.setEffectDragActive(effect);
    composition.setEffectDragActive(effect);
    effectStack.setDragActive(description.startsWith("motion-effect:"));
}

// Moves an effect card's effect to another owner (Alt: a copy), and shows
// that owner so the effect is seen where it went.
void MotionEditor::moveEffectTo(motion::Id effect, motion::Id owner, bool copy) {
    const auto* from = motion::findEffectList(processor.document.project(), effect);
    if (from != nullptr && from == motion::findEffectOwner(processor.document.project(), owner) && !copy) { return; }
    const auto result = processor.document.moveEffect(effect, owner, copy);
    if (result.failed()) {
        statusBar.show(result.getErrorMessage());
        return;
    }
    select(owner);
}

void MotionEditor::dragOperationEnded(const juce::DragAndDropTarget::SourceDetails&) {
    propertyInspector.setModulatorDrag(false);
    timeline.setEffectDragActive(false);
    composition.setEffectDragActive(false);
    effectStack.setDragActive(false);
}

void MotionEditor::addCamera(double at) {
    const auto frame = processor.document.project().frameTime(at);
    motion::Id id = 0;
    const auto result = processor.document.addCamera(frame, id);
    if (result.failed()) {
        statusBar.show(result.getErrorMessage());
        return;
    }
    select(id);
}

bool MotionEditor::selectionExists() const {
    const auto& project = processor.document.project();
    const auto isTrack = std::any_of(project.tracks.begin(), project.tracks.end(), [this](const auto& track) { return track.id == selection; });
    return isTrack || motion::findPropertyTarget(project, selection).has_value();
}

bool MotionEditor::selectionIsCamera() const {
    const auto& cameras = processor.document.project().cameras;
    return selection != 0 && std::any_of(cameras.begin(), cameras.end(), [this](const auto& camera) { return camera.id == selection; });
}

void MotionEditor::refreshInspector() {
    propertyInspector.setSelectionCount(std::max<std::size_t>(1, timeline.selectedClipIds().size()));
    clipTimingPanel.refresh();
    textAnimation.refresh();
    modelSize.refresh();
    compositionSettings.setShown(selection == 0);
    compositionSettings.refresh();
    inspectorLead.resized();
    // A camera's rig leads its Properties where a clip's timing would.
    const auto camera = selectionIsCamera();
    cameraRig.setCamera(camera ? selection : 0);
    const auto scopeSelected = selection != 0 && selection == processor.document.project().beam.id;
    if (camera) {
        propertyInspector.setLead(&cameraRig, [this] { return cameraRig.preferredHeight(); });
    } else if (scopeSelected) {
        propertyInspector.setLead(nullptr, {});
    } else {
        propertyInspector.setLead(&inspectorLead, [this] { return inspectorLead.preferredHeight(); });
    }
    // The Scope has no effects; its fixed options follow its rows instead.
    if (scopeSelected) {
        propertyInspector.setTrail(&scopePanel, [] { return MotionScopePanel::preferredHeight(); }, {}, true);
    } else {
        propertyInspector.setTrail(&effectStack, [this] { return effectStack.preferredHeight(); }, "Effects");
    }
    const auto& project = processor.document.project();
    const auto target = motion::findPropertyTarget(project, selection);
    const bool editable = target.has_value() && !target->isEffect;
    // Effects belong to visual clips, groups, tracks and the composition;
    // with nothing selected Properties shows the composition's.
    std::optional<motion::Id> owner;
    std::optional<std::pair<juce::String, juce::String>> heading;
    const auto track = std::find_if(project.tracks.begin(), project.tracks.end(), [this](const auto& item) { return item.id == selection; });
    if (selection == 0) {
        owner = 0;
        heading = std::pair<juce::String, juce::String>("Composition", juce::String());
    } else if (track != project.tracks.end()) {
        if (track->kind == motion::TrackKind::visual) { owner = selection; }
        heading = std::pair<juce::String, juce::String>(juce::String(track->name), "Track");
    } else if (editable && !target->camera && !target->beam && !target->isAudio) {
        owner = selection;
    }
    propertyInspector.setHeading(heading);
    propertyInspector.setTarget(editable ? selection : 0);
    effectStack.setOwner(owner, selection);
}

// Light on an XY-only scope shows only as how slowly each line is drawn,
// which a busy frame has little time to spare for. Hatching shows it by how
// densely lines cover what is lit, which X and Y alone draw well.
void MotionEditor::offerHatchingForXY() {
    const auto& project = processor.document.mainProject();
    if (!project.scope.xyOnly || processor.document.editingComposition() != 0 || project.cameras.empty()) { return; }
    const auto lit = std::any_of(project.tracks.begin(), project.tracks.end(), [&](const auto& track) {
        return std::any_of(track.clips.begin(), track.clips.end(), [&](const auto& clip) {
            const auto asset = motion::findAsset(project.assets, clip.asset);
            const auto* generator = asset != nullptr ? motion::generatorOf(*asset) : nullptr;
            return generator != nullptr && generator->category == motion::GeneratorCategory::light;
        });
    });
    const auto hatches = std::any_of(project.cameras.begin(), project.cameras.end(), [](const auto& camera) {
        const auto found = camera.properties.find("hatching");
        return found != camera.properties.end() && (found->second.animated() || found->second.base > 0);
    });
    if (!lit || hatches) { return; }
    statusBar.show("On an XY-only scope, light shows only by how slowly each line is drawn. Hatching shows it by how densely lines cover what's lit.", MotionStatusBar::Kind::notice, "Shade by hatching", [this] {
        processor.document.edit("Shade by hatching", [](motion::Project& project) {
            for (auto& camera : project.cameras) {
                auto found = camera.properties.find("hatching");
                if (found != camera.properties.end() && !found->second.animated()) { found->second.base = 100; }
            }
        });
    });
}

bool MotionEditor::keyPressed(const juce::KeyPress& key) {
    // While the start screen is up, keys are its alone.
    if (startScreen != nullptr) { return startScreen->handleKey(key); }
    if (scopeFullScreen && key == juce::KeyPress::escapeKey) {
        setScopeFullScreen(false);
        return true;
    }
    for (const auto& command : commands) {
        if (command.key.isValid() && command.key == key) {
            command.action();
            return true;
        }
    }
    return CommonPluginEditor::keyPressed(key);
}

void MotionEditor::selectCurveTarget(motion::Id id, const std::string& property, bool camera, bool chosen) {
    const auto hadTarget = curveTarget != 0;
    const juce::ScopeGuard relayout {[this, hadTarget] { if ((curveTarget != 0) != hadTarget) { resized(); } }};
    curveTarget = id;
    cameraCurve = camera;
    curveProperties.clear();
    const auto* effect = motion::findEffect(processor.document.project(), id);
    const auto parameterless = effect != nullptr && motion::effectDefinition(effect->type) != nullptr && motion::effectDefinition(effect->type)->parameters.empty();
    // Nothing selected, or nothing of it to graph (an effect without parameters).
    const auto nothingToGraph = [this] {
        curveTarget = 0;
        curveEditor.setSelection(0, {});
        routingPanel.setTarget(0, {});
        refreshCurveList();
        layoutGraphSide();
    };
    if (parameterless || !motion::findPropertyTarget(processor.document.project(), id).has_value()) {
        nothingToGraph();
        return;
    }
    const auto* definition = effect == nullptr ? nullptr : motion::effectDefinition(effect->type);
    const auto target = motion::findPropertyTarget(processor.document.project(), id);
    std::size_t selectedIndex = 0;
    const auto add = [&](std::string_view name) {
        if (property == name) { selectedIndex = curveProperties.size(); }
        curveProperties.emplace_back(name);
    };
    if (definition != nullptr) {
        for (const auto& parameter : definition->parameters) { add(parameter.id); }
    } else {
        // A generator's own numbers first, as Properties lists them.
        const auto* generator = motion::generatorFor(processor.document.project(), id);
        if (generator != nullptr) {
            for (const auto& spec : generator->parameters) { add(spec.id); }
        }
        const auto surface = motion::hasSurface(processor.document.project(), id);
        for (const auto& spec : motion::propertySpecs(*target)) {
            if (motion::hasProperty(generator, spec.id, surface)) { add(spec.id); }
        }
        // A Lua clip's slider curves, and an L-system's depth, are graphable once they exist.
        for (const auto& spec : motion::luaSliderSpecs) {
            if (target->properties != nullptr && target->properties->contains(spec.id)) { add(spec.id); }
        }
        for (const auto& spec : motion::fractalPropertySpecs) {
            if (target->properties != nullptr && target->properties->contains(spec.id)) { add(spec.id); }
        }
    }
    if (curveProperties.empty()) {
        nothingToGraph();
        return;
    }
    const auto previousTarget = curveEditor.viewState().target;
    // Opening a new target shows its first animated channel unless one was asked for.
    const auto animated = [&](const std::string& name) {
        const auto* curve = target.has_value() ? target->curve(name) : nullptr;
        const auto routed = std::any_of(processor.document.project().routes.begin(), processor.document.project().routes.end(), [&](const auto& route) { return route.target == id && route.property == name; });
        return routed || (curve != nullptr && (!curve->keyframes().empty() || curve->link.has_value()));
    };
    if (!chosen && previousTarget != id && !animated(curveProperties[selectedIndex])) {
        const auto found = std::find_if(curveProperties.begin(), curveProperties.end(), animated);
        if (found != curveProperties.end()) { selectedIndex = static_cast<std::size_t>(found - curveProperties.begin()); }
    }
    curvePropertyName = curveProperties[selectedIndex];
    if (previousTarget != id) { shownCurves.clear(); hiddenCurves.clear(); }
    curveEditor.setSelection(id, curvePropertyName);
    routingPanel.setTarget(id, curvePropertyName);
    refreshCurveList();
    layoutGraphSide();
}

// The channel list mirrors the target's properties, marking keyed and driven
// ones; shown channels are drawn behind the edited curve.
void MotionEditor::refreshCurveList() {
    const auto& project = processor.document.project();
    const auto target = motion::findPropertyTarget(project, curveTarget);
    if (!target.has_value()) {
        // Nothing selected: an empty list, like the graph beside it.
        curveList.setOwner({}, {});
        curveList.setChannels({}, {}, shownCurves);
        curveEditor.setContextCurves({});
        return;
    }
    const auto kind = target->camera ? "Camera" : target->isEffect ? "Effect" : target->isGroup ? "Group" : target->beam ? "Scope" : target->isAudio ? "Audio" : "Clip";
    curveList.setOwner(kind, juce::String(target->name.data(), target->name.size()));
    std::vector<MotionCurveList::Channel> channels;
    std::map<std::string, juce::Colour> colours;
    for (const auto& name : curveProperties) {
        MotionCurveList::Channel channel;
        channel.id = name;
        const auto spec = motion::specFor(project, curveTarget, name);
        channel.label = spec.has_value() ? juce::String(spec->label.data(), spec->label.size()) : juce::String(name).replace(".", " ");
        channel.group = spec.has_value() ? juce::String(spec->group.data(), spec->group.size()) : juce::String();
        channel.colour = motion::style::axisColour(spec.has_value() ? spec->axis : std::string_view());
        const auto* curve = target->curve(name);
        channel.keyed = curve != nullptr && !curve->keyframes().empty();
        const auto routed = std::any_of(project.routes.begin(), project.routes.end(), [&](const auto& route) { return route.target == curveTarget && route.property == name; });
        channel.driven = routed || (curve != nullptr && curve->link.has_value());
        colours[name] = channel.colour;
        channels.push_back(std::move(channel));
    }
    // The edited channel's siblings (its other axes) are drawn and editable
    // unless hidden; other channels are drawn faintly once shown.
    juce::String primaryGroup;
    for (const auto& channel : channels) { if (channel.id == curvePropertyName) { primaryGroup = channel.group; } }
    auto shown = shownCurves;
    shown.insert(curvePropertyName);
    for (const auto& channel : channels) {
        const auto sibling = channel.id != curvePropertyName && primaryGroup.isNotEmpty() && channel.group == primaryGroup;
        if (sibling && !hiddenCurves.contains(channel.id)) { shown.insert(channel.id); }
        if (sibling && hiddenCurves.contains(channel.id)) { shown.erase(channel.id); }
    }
    curveList.setChannels(std::move(channels), curvePropertyName, shown);
    curveEditor.setHiddenCurves(hiddenCurves);
    std::map<std::string, juce::Colour> context;
    for (const auto& name : shownCurves) { if (colours.contains(name)) { context[name] = colours[name]; } }
    curveEditor.setContextCurves(std::move(context));
}

void MotionEditor::refreshKeyPanel() {
    const auto selection = curveEditor.keySelection();
    const auto key = curveEditor.activeKey();
    const auto height = keyPanel.preferredHeight();
    if (selection.count > 0) {
        keyPanel.setTimeGrid(processor.document.project().timeGrid());
        const auto spec = key.has_value() ? motion::specFor(processor.document.project(), curveTarget, key->property) : std::nullopt;
        keyPanel.show(selection.count, selection.present, selection.editable, key.has_value() ? std::optional<std::pair<double, double>>({key->time, key->value}) : std::nullopt, spec.value_or(motion::ui::amountSpec));
    }
    if (keyPanel.isVisible() != (selection.count > 0) || keyPanel.preferredHeight() != height) {
        keyPanel.setVisible(selection.count > 0);
        layoutGraphSide();
    }
}

void MotionEditor::keepOutputPlayable() {
    auto* holder = juce::StandalonePluginHolder::getInstance();
    auto* device = holder != nullptr ? holder->deviceManager.getCurrentAudioDevice() : nullptr;
    if (device == nullptr) { return; }
    // Another device starts from the sound alone, or from what was chosen
    // for it last.
    if (device->getName() != routingDevice) {
        routingDevice = device->getName();
        const auto previous = processor.getRouting();
        if (!restoreRouting()) {
            const motion::OutputRouting plain {device->getOutputChannelNames().size() >= 2 ? 0 : motion::OutputRouting::off, motion::OutputMode::soundtrack, 0};
            processor.setRouting(plain);
            outputButton.setRouting(plain);
        }
        const auto now = processor.getRouting();
        if (now != previous) { statusBar.show("Output on " + routingDevice + ": " + motion::ui::output::summary(now) + ".", MotionStatusBar::Kind::notice); }
    }
    const auto routing = processor.getRouting();
    const auto active = device->getActiveOutputChannels().countNumberOfSetBits();
    const auto playable = motion::fitRouting(routing, active);
    if (playable == routing) { return; }
    processor.setRouting(playable);
    outputButton.setRouting(playable);
    statusBar.show(device->getName() + " plays " + juce::String(active) + " outputs, too few for " + motion::ui::output::summary(routing) + ": now " + motion::ui::output::summary(playable) + ".");
}

void MotionEditor::refreshOutputChoices() {
    outputButton.setRouting(processor.getRouting(), outputDevice().outputs > 0);
}

MotionOutputPanel::Device MotionEditor::outputDevice() const {
    auto* holder = juce::StandalonePluginHolder::getInstance();
    auto* device = holder != nullptr ? holder->deviceManager.getCurrentAudioDevice() : nullptr;
    return device != nullptr ? MotionOutputPanel::Device {device->getName(), device->getOutputChannelNames().size()} : MotionOutputPanel::Device {};
}

void MotionEditor::showOutputPanel() {
    auto panel = std::make_unique<MotionOutputPanel>(processor.getRouting(), outputDevice(), soundtrackClip() != 0);
    auto* raw = panel.get();
    outputPanel = raw;
    raw->onChange = [this](motion::OutputRouting routing) { return applyRouting(routing); };
    raw->onDone = [raw] { dismissPopover(raw); };
    raw->onAudioSettings = [this, raw] {
        dismissPopover(raw);
        auto* holder = juce::StandalonePluginHolder::getInstance();
        if (holder != nullptr) { holder->showAudioSettingsDialog(); }
    };
    showPopover(std::move(panel), getLocalArea(&outputButton, outputButton.getLocalBounds()), Placement::below);
}

// The routing chosen last, when the same device is open: another (the
// speakers while the interface is unplugged) plays the sound as usual, and
// the choice waits for its device.
bool MotionEditor::restoreRouting() {
    const auto saved = static_cast<std::uint32_t>(processor.globalSettings.getInt("outputRouting", 0));
    const auto device = outputDevice();
    if (saved == 0 || device.outputs <= 0 || device.name != processor.globalSettings.getString("outputRoutingDevice")) { return false; }
    auto routing = motion::OutputRouting::unpack(saved);
    if (static_cast<int>(routing.beam) > static_cast<int>(motion::OutputMode::xyzInverted)) { routing.beam = motion::OutputMode::soundtrack; }
    routing = motion::fitRouting(routing, device.outputs);
    auto* holder = juce::StandalonePluginHolder::getInstance();
    if (routing.channels() > 2) {
        const auto opened = holder != nullptr ? holder->configureOutputOnlyChannels(routing.channels()) : juce::Result::fail({});
        if (opened.failed()) { routing = motion::fitRouting(routing, 2); }
    }
    processor.setRouting(routing);
    outputButton.setRouting(routing);
    return true;
}

juce::Result MotionEditor::applyRouting(motion::OutputRouting routing) {
    const auto previous = processor.getRouting();
    // More than a pair opens the outputs it needs; a pair is always open.
    if (routing.channels() > 2) {
        auto* holder = juce::StandalonePluginHolder::getInstance();
        const auto opened = holder != nullptr ? holder->configureOutputOnlyChannels(routing.channels()) : juce::Result::fail("The beam on more than two outputs needs the standalone app's audio device.");
        if (opened.failed()) { return opened; }
    }
    processor.setRouting(routing);
    outputButton.setRouting(routing);
    processor.globalSettings.set("outputRouting", static_cast<int>(routing.pack()));
    processor.globalSettings.set("outputRoutingDevice", outputDevice().name);
    processor.globalSettings.save();
    // A scope driven by X and Y alone can't show brightness: offer to draw for
    // it, light shown by the beam's time instead; and the other way when the
    // output carries brightness again.
    if (routing.beam != previous.beam || routing.beamOn() != previous.beamOn()) {
        const auto xyOnly = processor.document.project().scope.xyOnly;
        const auto suggest = [this](bool on, const juce::String& why, const juce::String& action, const juce::String& done) {
            statusBar.show(why, MotionStatusBar::Kind::notice, action, [this, on, done] {
                if (processor.document.project().scope.xyOnly == on) { return; }
                processor.document.edit(on ? "Set the scope to XY only" : "Give the scope brightness", [on](motion::Project& project) { project.scope.xyOnly = on; });
                statusBar.show(done, MotionStatusBar::Kind::notice);
                if (on) { offerHatchingForXY(); }
            });
        };
        if (routing.beamOn() && routing.beam == motion::OutputMode::xy && !xyOnly) {
            suggest(true, "An XY output carries no brightness, so lighting and fades won't show.", "Draw for XY only",
                "Drawing for XY only: brighter parts are drawn more slowly. Scope settings change it back.");
        } else if (routing.beamChannels() > 2 && xyOnly) {
            suggest(false, "This output carries brightness, but the scope is set to XY only.", "Use brightness", "The scope uses brightness again.");
        }
    }
    return juce::Result::ok();
}

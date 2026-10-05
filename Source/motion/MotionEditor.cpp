#include "MotionEditor.h"
#include "ui/CanvasSizeEditor.h"
#include "ui/MarkerPanel.h"
#include "ui/MidiEnvelopePanel.h"
#include "../components/OverlayDialogHelpers.h"
#include <cstdlib>

namespace {
// Panel tabs at their natural width, in the panel title style.
void styleTabs(osci::TabBar& tabs) {
    tabs.setTabSpacing(1, 8);
    tabs.setFont(motion::style::title());
    tabs.setColour(osci::TabBar::backgroundColourId, motion::style::background());
    tabs.setColour(osci::TabBar::textColourId, motion::style::muted());
    tabs.setColour(osci::TabBar::selectedTextColourId, motion::style::text());
    tabs.setColour(osci::TabBar::indicatorColourId, motion::style::accent());
}
}


MotionEditor::MotionEditor(MotionProcessor& ownerProcessor)
    : CommonPluginEditor(ownerProcessor, "osci-motion", "osci-motion", 1440, 900), processor(ownerProcessor), timeline(ownerProcessor), composition(ownerProcessor), assetLibrary(ownerProcessor.document), curveEditor(ownerProcessor), notesEditor(ownerProcessor), cameraRig(ownerProcessor), clipTimingPanel(ownerProcessor), effectStack(ownerProcessor), routingPanel(ownerProcessor), modulatorLibrary(ownerProcessor), propertyInspector(ownerProcessor) {
    lookAndFeel.setControlCornerRadius(3.0f);
    motionLookAndFeel.setControlCornerRadius(3.0f);
    setLookAndFeel(&motionLookAndFeel);
    visualiserSettings.setSurfaceColours(osci::Colours::veryDark(), osci::Colours::surface());
    // The Scope's cog selects the Scope, whose beam and display properties
    // animate in Properties and the Graph like any other; keeping
    // openSettings set also keeps the cog in the strip.
    visualiser.openSettings = [this] { select(processor.document.project().beam.id); };
    visualiser.closeSettings = [] {};
    // The scope's record, settings, popout and full-screen controls live in
    // the Scope panel header rather than on top of the picture.
    visualiserControls = &visualiser.detachControls(*this);
    visualiser.onControlsChanged = [this] { resized(); };
    visualiser.setControlStyle(motion::style::text().withAlpha(.78f), 5);
    // Children in paint order: each draws over those added before it.
    for (auto* header : { &viewportHeader, &outputHeader, &inspectorHeader, &timelineHeader }) {
        addAndMakeVisible(header);
    }
    for (auto* component : std::initializer_list<juce::Component*> { &timeline, &composition, &assetLibrary, &importButton, &playButton, &startButton, &endButton, &timeLabel, &propertyInspector, &curveEditor, &notesEditor, &timelineTabs, &timelineDivider, &previewDivider, &statusBar }) {
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
    addAndMakeVisible(sceneView);
    addChildComponent(exportBar);
    addAndMakeVisible(libraryTabs);
    addChildComponent(graphSideViewport);
    addChildComponent(modulatorLibrary);
    addAndMakeVisible(tempoValue);
    addAndMakeVisible(tempoLabel);
    // The Scope's controls float over its top right, like the Scene's tools.
    addAndMakeVisible(scopeTools);
    addAndMakeVisible(monitorOutput);
    addAndMakeVisible(outputLabel);
    addAndMakeVisible(loopButton);
    addAndMakeVisible(tapButton);
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
    sliderBakes.onStatus = [this](const juce::String& text, bool error) { statusBar.show(text, error ? MotionStatusBar::Kind::error : MotionStatusBar::Kind::notice); };
    sliderBakes.update();
    composition.refresh();
    timeline.refreshTracks();
    refreshInspector();
    startTimerHz(30);
    setResizeLimits(1100, 700, 4096, 2160);
    loadLayout();
    juce::Desktop::getInstance().addFocusChangeListener(this);
    resized();
}

void MotionEditor::setUpMenus() {
    // File is built when opened (it lists recent projects); the rest are
    // command lists.
    menus.addTopLevelMenu("File");
    menus.customMenuLogic = [this](juce::PopupMenu& menu, int index) {
        if (index == 0) { buildFileMenu(menu); }
        if (index == 4) {
            juce::PopupMenu output;
            for (int id = 1; id <= 3; ++id) { output.addItem(2000 + id, monitorOutput.getItemText(id - 1), monitorOutput.isItemEnabled(id), monitorOutput.getSelectedId() == id); }
            menu.addSubMenu("Audio interface plays", output);
            menu.addSeparator();
        }
        if (index == timingMenuIndex) {
            menu = timingMenu();
            menu.setLookAndFeel(&motionLookAndFeel);
        }
    };
    menus.customMenuSelectedLogic = [this](int id, int index) {
        if (index == 4 && id > 2000 && id <= 2003) {
            monitorOutput.setSelectedId(id - 2000);
            return true;
        }
        return (index == 0 && fileMenuItemSelected(id)) || (index == timingMenuIndex && applyTiming(id));
    };
    menus.addTopLevelMenu("Edit");
    // Undo and redo keys are handled by the shared editor; these list them.
    menus.addMenuItem(1, "Undo", [this] { processor.getUndoManager().undo(); }, motion::style::shortcutText("Cmd+Z"));
    menus.addMenuItem(1, "Redo", [this] { processor.getUndoManager().redo(); }, motion::style::shortcutText("Cmd+Shift+Z"));
    menus.addTopLevelMenu("Clip");
    menus.addTopLevelMenu("Transport");
    menus.addTopLevelMenu("Audio");
    menus.addStandaloneAudioSettingsMenuItem(4, processor, *this);
    menus.addTopLevelMenu("View");
    menus.addTopLevelMenu("Timing");
    registerCommands();
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
    scopeLabel.setTooltip("Shared composition: changes affect every instance. Double-click to rename.");
    scopeLabel.onEditorShow = [this] { scopeNameGeneration = processor.document.generation(); };
    scopeLabel.onTextChange = [this] {
        const auto name = scopeLabel.getText().trim();
        if (processor.document.editingComposition() == 0 || processor.document.generation() != scopeNameGeneration || name.isEmpty() || name.length() > 200) { return; }
        processor.document.edit("Rename composition", [name](motion::Project& project) { project.name = name; });
    };
}

void MotionEditor::setUpScene() {
    sceneTools.move.setToggleState(true, juce::dontSendNotification);
    sceneTools.move.onClick = [this] { composition.setTool(MotionTransformTool::move); };
    sceneTools.rotate.onClick = [this] { composition.setTool(MotionTransformTool::rotate); };
    sceneTools.scale.onClick = [this] { composition.setTool(MotionTransformTool::scale); };
    composition.onToolChanged = [this](MotionTransformTool tool) {
        sceneTools.move.setToggleState(tool == MotionTransformTool::move, juce::dontSendNotification);
        sceneTools.rotate.setToggleState(tool == MotionTransformTool::rotate, juce::dontSendNotification);
        sceneTools.scale.setToggleState(tool == MotionTransformTool::scale, juce::dontSendNotification);
    };
    sceneTools.path.onClick = [this] { composition.setMotionPathVisible(sceneTools.path.getToggleState()); };
    composition.onMotionPathChanged = [this](bool visible) { sceneTools.path.setToggleState(visible, juce::dontSendNotification); };
    sceneTools.fly.onClick = [this] { composition.setNavigating(!composition.isNavigating()); };
    composition.onNavigationChanged = [this](bool active) { sceneTools.fly.setToggleState(active, juce::dontSendNotification); };
    sceneTools.frame.onClick = [this] { composition.frameSelection(); };
    sceneTools.lookThrough.onClick = [this] {
        composition.setDrivenCamera(sceneTools.lookThrough.getToggleState() ? selection : 0);
        // A camera that cannot be driven leaves the button off.
        sceneTools.lookThrough.setToggleState(composition.drivenCamera() != 0, juce::dontSendNotification);
        resized();
    };
    sceneTools.keyCamera.onClick = [this] {
        composition.toggleCameraKey(toolCamera());
        refreshCameraTools();
    };
    composition.onDrivenCameraChanged = [this](motion::Id id) {
        sceneTools.lookThrough.setToggleState(id != 0, juce::dontSendNotification);
        refreshCameraTools();
        resized();
    };
    sceneView.setName("Scene view");
    sceneView.setTooltip("Look along an axis (numpad 1, 3, 7), frame the selection (F) or reset the view (0)");
    sceneView.setColour(juce::TextButton::buttonColourId, osci::Colours::surfaceRaised());
    sceneView.onClick = [this] { showSceneViewMenu(false); };
    composition.onContextMenu = [this] { showSceneViewMenu(true); };
    composition.onPropertyEdited = [this](motion::Id id, const std::string& property) { selectCurveTarget(id, property, false, true); };
    composition.isSelected = [this](motion::Id id) { return timeline.selectedClipIds().contains(id); };
    composition.onOpenSource = [this](motion::Id id) {
        const auto& project = processor.document.project();
        const auto* clip = motion::findClip(project, id);
        if (clip == nullptr) { return; }
        if (clip->composition != 0) { enterComposition(clip->id); return; }
        const auto asset = motion::findAsset(project.assets, clip->asset);
        if (asset == nullptr) { return; }
        if (asset->extension.equalsIgnoreCase(".svg") && motion::drawing::isDrawing(juce::String::fromUTF8(static_cast<const char*>(asset->data.getData()), static_cast<int>(asset->data.getSize())))) {
            showDrawingEditor(asset->id);
        } else if (asset->extension.equalsIgnoreCase(".txt") || asset->extension.equalsIgnoreCase(".lua")) {
            assetLibrary.onBake(asset->id);
        }
    };
    composition.onEffectPreview = [this](const std::string& type, std::optional<motion::Id> owner) { previewEffect(type, owner); };
    composition.onEffectDropped = [this](const std::string& type, motion::Id owner) { addEffectTo(type, owner); };
    composition.onSelection = [this](motion::Id id) { select(id); };
}

void MotionEditor::setUpScope() {
    outputHeader.setName("Scope");
    visualiser.setControlButtonsHidden(true);
    using Control = VisualiserComponent::Control;
    scopeTools.record.onClick = [this] { visualiser.clickControl(Control::record, &scopeTools.record); };
    scopeTools.textureOutput.onClick = [this] { visualiser.clickControl(Control::textureOutput, &scopeTools.textureOutput); };
    scopeTools.settings.onClick = [this] { visualiser.clickControl(Control::settings, &scopeTools.settings); };
    scopeTools.popout.onClick = [this] { visualiser.clickControl(Control::popout, &scopeTools.popout); };
    scopeTools.fullScreen.onClick = [this] { visualiser.clickControl(Control::fullScreen, &scopeTools.fullScreen); };
    visualiser.setFullScreenCallback([this](FullScreenMode mode) {
        const auto next = mode == FullScreenMode::TOGGLE ? !scopeFullScreen : mode == FullScreenMode::FULL_SCREEN;
        setScopeFullScreen(next);
    });
    scopeTools.canvas.onClick = [this] {
        auto panel = std::make_unique<MotionCanvasSettings>(processor.recordingParameters.getCanvasSize(), processor.document.mainProject().frameRate);
        auto* controls = panel.get();
        panel->setSize(360, 150);
        const juce::Component::SafePointer<MotionEditor> owner(this);
        const juce::Component::SafePointer<juce::Component> popover(controls);
        controls->onApply = [owner, popover](VisualiserRenderSize size) {
            juce::MessageManager::callAsync([owner, popover, size] {
                if (owner == nullptr || popover == nullptr) { return; }
                owner->processor.recordingParameters.setCanvasSize(size);
                dismissPopover(popover.getComponent());
            });
        };
        showPopover(std::move(panel), getLocalArea(&scopeTools, scopeTools.canvas.getBounds()));
    };
}

void MotionEditor::setUpTransport() {
    outputLabel.setText("Output", juce::dontSendNotification);
    outputLabel.setFont(motion::style::caption());
    outputLabel.setColour(juce::Label::textColourId, motion::style::muted());
    outputLabel.setJustificationType(juce::Justification::centredRight);
    monitorOutput.setName("Audio output mode");
    monitorOutput.setColour(juce::ComboBox::backgroundColourId, osci::Colours::surfaceRaised());
    monitorOutput.setColour(juce::ComboBox::arrowColourId, osci::Colours::textMuted());
    monitorOutput.addItem("Soundtrack", 1);
    monitorOutput.addItem("Beam X/Y", 2);
    monitorOutput.addItem("Beam XYRGB (5 ch)", 3);
    refreshOutputChoices();
    monitorOutput.setSelectedId(static_cast<int>(processor.getOutputMode()) + 1, juce::dontSendNotification);
    monitorOutput.setTooltip("What your audio interface plays: the soundtrack, or the beam itself to drive a real oscilloscope (XYRGB needs five output channels). The Scope always shows the beam.");
    monitorOutput.onChange = [this] {
        const auto mode = static_cast<MotionProcessor::OutputMode>(monitorOutput.getSelectedId() - 1);
        if (mode == MotionProcessor::OutputMode::xyrgb) {
            auto* holder = juce::StandalonePluginHolder::getInstance();
            const auto result = holder != nullptr ? holder->configureOutputOnlyChannels(5) : juce::Result::fail("Five-channel output requires the standalone audio device.");
            if (result.failed()) {
                statusBar.show(result.getErrorMessage());
                monitorOutput.setSelectedId(static_cast<int>(processor.getOutputMode()) + 1, juce::dontSendNotification);
                return;
            }
        }
        processor.setOutputMode(mode);
    };
    playButton.setTooltip("Play / pause (Space)");
    startButton.setTooltip("Go to start (Home)");
    endButton.setTooltip("Go to end (End)");
    loopButton.setTitle("Loop playback");
    loopButton.setTooltip("Loop playback (L). I and O set the loop at the playhead; drag the brace in the ruler.");
    loopButton.onClick = [this] { toggleLoop(); };
    startButton.onClick = [this] { processor.seek(0); timeline.revealTime(0); };
    endButton.onClick = [this] {
        const auto end = processor.document.project().duration;
        processor.seek(end); timeline.revealTime(end);
    };
    timeLabel.setFont(motion::style::mono());
    timeLabel.setJustificationType(juce::Justification::centred);
    tempoValue.setFont(motion::style::body());
    tempoLabel.setFont(motion::style::caption());
    tempoLabel.setColour(juce::Label::textColourId, motion::style::muted());
    refreshTiming();
    tempoLabel.setText("BPM", juce::dontSendNotification);
    timeLabel.setName("Timeline position");
    timeLabel.setEditable(false, true);
    timeLabel.setColour(juce::Label::backgroundColourId, osci::Colours::surfaceRaised());
    timeLabel.setTooltip("Double-click to go to a position. Use the current display, or append s for seconds / f for frames. Musical positions use bar.beat.tick (960 ticks per beat).");
    timeLabel.onEditorShow = [this] {
        positionEditGrid = processor.document.project().timeGrid();
        positionEditGeneration = processor.document.generation();
        positionEditRevision = processor.document.revision();
    };
    timeLabel.onTextChange = [this] {
        const auto& project = processor.document.project();
        const auto requested = positionEditGrid.parsePosition(timeLabel.getText().toStdString());
        const bool current = positionEditGeneration == processor.document.generation() && positionEditRevision == processor.document.revision();
        timeLabel.setText(juce::String(project.timeGrid().positionLabel(processor.position.load())), juce::dontSendNotification);
        if (!current) { return; }
        if (!requested.has_value() || *requested > project.duration) {
            osci::showOverlayMessage(*this, "Cannot go to position", "Enter a position from 0 to " + juce::String(project.duration, 3)
                + " seconds. Use seconds (90s or 1:30s), frames (240f), or bar.beat.tick in the musical display.");
            return;
        }
        processor.seek(*requested);
        timeline.revealTime(*requested);
    };
    tapButton.setName("Tap tempo");
    tapButton.setTitle("Tap tempo");
    tapButton.setTooltip("Tap on the beat (four taps or more). The tempo is set when you stop tapping. Detect it from the soundtrack in the timing menu.");
    tapButton.setWantsKeyboardFocus(false);
    tapButton.setColour(juce::TextButton::buttonColourId, osci::Colours::surfaceRaised());
    tapButton.onClick = [this] { tap(); };
    tempoValue.setName("Project tempo");
    tempoValue.setEditable(false, true);
    tempoValue.setColour(juce::Label::backgroundColourId, osci::Colours::surfaceRaised());
    tempoValue.setJustificationType(juce::Justification::centred);
    tempoValue.onTextChange = [this] {
        const auto text = tempoValue.getText().trim();
        char* end = nullptr;
        const auto value = std::strtod(text.toRawUTF8(), &end);
        if (text.isEmpty() || end == nullptr || *end != '\0' || !std::isfinite(value) || value < 1 || value > 1000) {
            tempoValue.setText(juce::String(processor.document.project().bpm, 1), juce::dontSendNotification);
            return;
        }
        if (value != processor.document.project().bpm) {
            const auto result = processor.document.changeTempo(value);
            if (result.failed()) {
                tempoValue.setText(juce::String(processor.document.project().bpm, 1), juce::dontSendNotification);
                osci::showOverlayMessage(*this, "Cannot change tempo", result.getErrorMessage());
            }
        }
    };
    exportBar.setName("Signal export progress");
    cancelExport.onClick = [this] { if (exportState != nullptr) { exportState->cancelled.store(true); } };
    playButton.onClick = [this] { processor.playing.store(!processor.playing.load()); };
}

void MotionEditor::setUpLibrary() {
    modulatorLibrary.onError = [this](const juce::String& text) { statusBar.show(text); };
    libraryTabs.setName("Library tabs");
    styleTabs(libraryTabs);
    libraryTabs.addTab("Assets");
    libraryTabs.addTab("Effects");
    libraryTabs.addTab("Modulators");
    libraryTabs.onSelectionChanged = [this](int index) {
        assetLibrary.setVisible(index == 0);
        importButton.setVisible(index == 0);
        effectLibrary.setVisible(index == 1);
        modulatorLibrary.setVisible(index == 2);
        if (index == 2) { modulatorLibrary.refresh(); }
        resized();
    };
    // Double-clicking an effect adds it to what Properties shows.
    effectLibrary.onInsert = [this](const std::string& type) { effectStack.addEffect(type); };
    importButton.onClick = [this] {
        juce::PopupMenu menu;
        menu.addItem(3, "Draw a shape...");
        menu.addItem(1, "Import file...");
        menu.addItem(2, "Blender live source...");
        // The examples that ship with osci-render, by kind.
        juce::PopupMenu examples;
        std::vector<juce::String> resources;
        for (const auto& [title, extension] : std::initializer_list<std::pair<const char*, const char*>> {{"3D models", ".obj"}, {"Lua scripts", ".lua"}, {"Lottie animations", ".lottie"}, {"Fractals", ".lsystem"}, {"Text", ".txt"}}) {
            juce::PopupMenu kind;
            for (int index = 0; index < BinaryData::namedResourceListSize; ++index) {
                const juce::String file = BinaryData::getNamedResourceOriginalFilename(BinaryData::namedResourceList[index]);
                if (!file.endsWithIgnoreCase(extension)) { continue; }
                resources.push_back(BinaryData::namedResourceList[index]);
                kind.addItem(100 + static_cast<int>(resources.size()) - 1, file.upToLastOccurrenceOf(".", false, false).replaceCharacter('_', ' '));
            }
            examples.addSubMenu(title, kind);
        }
        menu.addSubMenu("Examples", examples);
        const juce::Component::SafePointer<MotionEditor> owner(this);
        menu.setLookAndFeel(&getLookAndFeel());
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&importButton), [owner, resources](int choice) {
            if (owner == nullptr) { return; }
            if (choice == 1) { owner->chooseSourceFile(); }
            if (choice == 2) { owner->showBlenderSettings(); }
            if (choice == 3) { owner->showDrawingEditor(0); }
            if (choice >= 100 && choice - 100 < static_cast<int>(resources.size())) { owner->importExample(resources[static_cast<std::size_t>(choice - 100)]); }
        });
    };
    assetLibrary.onReplace = [this](motion::Id id) { replaceSourceFile(id); };
    assetLibrary.onEditDrawing = [this](motion::Id id) { showDrawingEditor(id); };
    assetLibrary.onRemoveComposition = [this](motion::Id id) {
        const auto& definitions = processor.document.mainProject().definitions;
        const auto found = std::find_if(definitions.begin(), definitions.end(), [id](const auto& value) { return value->id == id; });
        if (found == definitions.end()) { return; }
        const auto expected = *found;
        const auto generation = processor.document.generation();
        const juce::Component::SafePointer<MotionEditor> owner(this);
        osci::showOverlayConfirmationOrAlert(this, "Remove unused composition?",
            "Remove \"" + expected->name + "\" from the library? Shared media and child compositions remain available. You can undo this.",
            "Remove composition", "Cancel", [owner, id, generation, expected] {
                if (owner == nullptr || owner->processor.document.generation() != generation) { return; }
                const auto& current = owner->processor.document.mainProject().definitions;
                if (std::find(current.begin(), current.end(), expected) == current.end()) { return; }
                const auto result = owner->processor.document.removeComposition(id);
                if (result.failed()) { owner->statusBar.show(result.getErrorMessage()); }
            });
    };
    assetLibrary.onOpenComposition = [this](motion::Id id) { enterComposition(id, true); };
    assetLibrary.onMessage = [this](const juce::String& message) { statusBar.show(message, message.startsWith("Removed") || message.startsWith("Tempo now") ? MotionStatusBar::Kind::notice : MotionStatusBar::Kind::warning); };
    assetLibrary.onSelectUses = [this](motion::Id id) {
        std::vector<motion::Id> clips;
        for (const auto& track : processor.document.project().tracks) {
            for (const auto& clip : track.clips) { if (clip.asset == id || clip.midiAsset == id) { clips.push_back(clip.id); } }
        }
        timelineTabs.setSelectedIndex(0);
        timeline.selectClips(clips);
        if (clips.empty()) { statusBar.show("This source is only used inside other compositions.", MotionStatusBar::Kind::notice); }
    };
    assetLibrary.onInsert = [this](motion::Id id) { timeline.insertAsset(id, -1, -1); };
    assetLibrary.liveStatus = [this](motion::Id id) { return processor.blenderInputs().statusText(id); };
    assetLibrary.onBake = [this](motion::Id id) {
        for (const auto& asset : processor.document.mainProject().assets) {
            if (asset->id == id && asset->liveIdentity != nullptr) { showBlenderSettings(id); return; }
        }
        const auto& assets = processor.document.project().assets;
        const auto found = motion::findAsset(assets, id);
        if (found == nullptr || (!found->extension.equalsIgnoreCase(".lua") && !found->extension.equalsIgnoreCase(".txt")
                && !found->extension.equalsIgnoreCase(".lsystem") && !osci::files::isImage(found->extension))) { return; }
        preparationRequests.push_back({{}, processor.position.load(), processor.document.generation(), found});
        showNextPreparationSettings();
    };
    assetLibrary.onCancelImport = [this] {
        for (const auto& task : pendingImports) { task->cancelled.store(true); }
    };
}

void MotionEditor::setUpProperties() {
    effectStack.onPropertySelected = [this](motion::Id id, std::string property) { selectCurveTarget(id, property, false, true); };
    effectStack.onShowOwner = [this](motion::Id id) { select(id); };
    propertyInspector.onRouteModulator = [this](motion::Id modulator, motion::Id target, std::vector<std::string> properties) {
        const auto result = processor.document.routeModulator(modulator, target, properties);
        if (result.failed()) { statusBar.show(result.getErrorMessage()); return; }
        modulatorLibrary.refresh();
        if (!properties.empty()) { selectCurveTarget(target, properties.front(), selectionIsCamera(), true); }
    };
    effectStack.onHeightChanged = [this] { propertyInspector.relayout(); };
    effectStack.onReveal = [this](juce::Component& card) { propertyInspector.reveal(card); };
    // A clip's timing leads its Properties, as layer timing does in other editors.
    inspectorLead.add(compositionSettings, [this] { return compositionSettings.preferredHeight(); });
    inspectorLead.add(clipTimingPanel, [this] { return clipTimingPanel.preferredHeight(); });
    compositionSettings.onTiming = [this](int command) { applyTiming(command); };
    compositionSettings.onError = [this](const juce::String& message) { statusBar.show(message); };
    // Panels in the lead resize it, and Properties lays out again.
    const auto relayoutLead = [this] { inspectorLead.resized(); propertyInspector.relayout(); };
    compositionSettings.onHeightChanged = relayoutLead;
    inspectorLead.add(textAnimation, [this] { return textAnimation.preferredHeight(); });
    propertyInspector.setLead(&inspectorLead, [this] { return inspectorLead.preferredHeight(); });
    clipTimingPanel.onHeightChanged = relayoutLead;
    textAnimation.onHeightChanged = relayoutLead;
    // A change to the characters prepares the text source again in place.
    textAnimation.onApply = [this](motion::Id id, motion::TextSettings settings) {
        const auto& assets = processor.document.project().assets;
        const auto found = motion::findAsset(assets, id);
        if (found == nullptr) { return; }
        // One preparation at a time: a later change waits for the current one.
        if (!pendingImports.empty()) {
            queuedTextAnimation = QueuedTextAnimation {id, settings, processor.document.generation()};
            return;
        }
        // Only the animation is this section's; the text and its type stay
        // as the source has them now.
        auto merged = found->textSettings;
        merged.animation = settings.animation;
        merged.characterDelay = settings.characterDelay;
        merged.characterDuration = settings.characterDuration;
        merged.hold = settings.hold;
        merged.amount = settings.amount;
        SourceRequest request {{}, processor.position.load(), processor.document.generation(), found};
        request.textSettings = merged;
        beginSourceImport(request);
    };
    // The owner's effects follow its properties.
    propertyInspector.setTrail(&effectStack, [this] { return effectStack.preferredHeight(); });
    inspectorTitle.setText("Properties", juce::dontSendNotification);
    inspectorTitle.setFont(motion::style::title());
    inspectorTitle.setBorderSize(juce::BorderSize<int>(0));
    propertyInspector.onPropertySelected = [this](motion::Id id, const std::string& property) { selectCurveTarget(id, property, selectionIsCamera(), true); };
    propertyInspector.onModulate = [this](motion::Id id, const std::string& property) {
        selectCurveTarget(id, property, selectionIsCamera(), true);
        timelineTabs.setSelectedIndex(1);
    };
    propertyInspector.onRename = [this](motion::Id id, const juce::String& text) {
        processor.document.tryEdit("Rename camera", [id, name = text.toStdString()](motion::Project& project) {
            for (auto& camera : project.cameras) {
                if (camera.id == id && camera.name != name) { camera.name = name; return true; }
            }
            return false;
        });
    };
    propertyInspector.selectedKeyTime = [this](motion::Id id) { return composition.selectedKeyContentTime(id); };
    propertyInspector.onKeyTimeEdited = [this] { composition.retainSelectedKeyAfterEdit(); };
    propertyInspector.onShowPopover = [this](std::unique_ptr<juce::Component> content, juce::Component& anchor) { showPopover(std::move(content), getLocalArea(&anchor, anchor.getLocalBounds())); };
}

void MotionEditor::setUpTimeline() {
    timelineDivider.setName("Resize timeline");
    previewDivider.setName("Resize preview panels");
    styleTabs(timelineTabs);
    timeline.onEffectAdded = [this](motion::Id owner, motion::Id) { select(owner); };
    timelineTabs.setName("Timeline tabs");
    timelineTabs.addTab("Timeline");
    timelineTabs.addTab("Graph");
    timelineTabs.addTab("Notes");
    timelineTabs.onSelectionChanged = [this](int index) {
        // All three share the panel height the user chose: switching never
        // moves the Scene or Scope.
        timeline.setVisible(index == 0);
        curveEditor.setVisible(index == 1);
        notesEditor.setVisible(index == 2);
        if (index == 1) { refreshCurveList(); }
        resized();
        if (index == 2) { notesEditor.fitContents(); }
    };
    notesEditor.setVisible(false);
    timelineDivider.onStart = [this] { dividerStart = timelineFraction; };
    timelineDivider.onDrag = [this](int delta) {
        timelineFraction = std::clamp(dividerStart - static_cast<double>(delta) / workspaceHeight, 0.25, 0.65);
        resized();
    };
    timelineDivider.onReset = [this] { timelineFraction = 0.34; resized(); };
    previewDivider.onStart = [this] { dividerStart = previewFraction; };
    previewDivider.onDrag = [this](int delta) {
        previewFraction = std::clamp(dividerStart + static_cast<double>(delta) / previewWidth, 0.25, 0.75);
        resized();
    };
    previewDivider.onReset = [this] { previewFraction = 0.5; resized(); };
    timeline.onOpenSource = [this](motion::Id id) { composition.onOpenSource(id); };
    notesEditor.onEditInstrument = [this](motion::Id id) {
        const auto* clip = motion::findClip(processor.document.project(), id);
        if (clip == nullptr) { return; }
        auto panel = std::make_unique<MotionMidiEnvelopePanel>(clip->instrument);
        panel->setSize(380, 294);
        const auto apply = popoverEdit(panel.get());
        panel->onApply = [apply, id](motion::MidiInstrument settings) {
            apply([id, settings](motion::Document& document) { return document.setMidiInstrument(id, settings); });
        };
        showPopover(std::move(panel), getLocalArea(&notesEditor, notesEditor.envelopeAnchor().getBounds()));
    };
    timeline.onEditTempo = [this](double beat, double bpm, std::optional<double> replacing) {
        const auto& changes = processor.document.project().tempoChanges;
        const auto existing = changes != nullptr && replacing.has_value() ? std::find_if(changes->begin(), changes->end(), [&](const auto& change) { return change.beat == *replacing; }) : std::vector<motion::TempoChange>::const_iterator();
        const auto ramped = changes != nullptr && replacing.has_value() && existing != changes->end() && existing->ramp;
        auto panel = std::make_unique<MotionTempoPanel>(beat, bpm, processor.document.project().beatsPerBar, ramped);
        panel->setSize(300, 160);
        const juce::Component::SafePointer<MotionTempoPanel> tempoPanel(panel.get());
        const auto apply = popoverEdit(panel.get(), [tempoPanel](const juce::String& error) { if (tempoPanel != nullptr) { tempoPanel->setError(error); } });
        panel->onApply = [apply, beat, replacing](double value, bool glide) {
            apply([=](motion::Document& document) { return document.setTempoChange(beat, value, replacing, glide); });
        };
        showPopover(std::move(panel), getLocalArea(&timeline, timeline.rulerAnchor(processor.document.project().tempo().seconds(beat))));
    };
    timeline.onEditMarker = [this](motion::Id id, double time) {
        const auto& project = processor.document.project();
        juce::String name = "Marker";
        if (id == 0) {
            const auto found = std::find_if(project.markers.begin(), project.markers.end(), [time](const auto& marker) { return std::abs(marker.time - time) < 1.0e-9; });
            if (found != project.markers.end()) { id = found->id; }
        }
        if (id != 0) {
            const auto found = std::find_if(project.markers.begin(), project.markers.end(), [id](const auto& marker) { return marker.id == id; });
            if (found == project.markers.end()) { return; }
            name = found->name; time = found->time;
        }
        auto panel = std::make_unique<MotionMarkerPanel>(name, time, project.timeGrid(), project.duration);
        panel->setSize(300, 140);
        const juce::Component::SafePointer<MotionMarkerPanel> markerPanel(panel.get());
        const auto apply = popoverEdit(panel.get(), [markerPanel](const juce::String& error) { if (markerPanel != nullptr) { markerPanel->setError(error); } });
        panel->onApply = [apply, id](juce::String name, double time) {
            apply([=](motion::Document& document) { return document.setMarker(id, time, name); });
        };
        showPopover(std::move(panel), getLocalArea(&timeline, timeline.rulerAnchor(time)));
    };
    timeline.onEnterComposition = [this](motion::Id id) { enterComposition(id); };
    timeline.onSelection = [this](motion::Id id) { select(id); };
    timeline.onAddCamera = [this](double time) { addCamera(time); };
    timeline.onMidiAssigned = [this](motion::Id id) { select(id); timelineTabs.setSelectedIndex(2); notesEditor.fitContents(); };
    timeline.onMakeUnique = [this](motion::Id id) {
        libraryTabs.setSelectedIndex(0);
        const auto& project = processor.document.project();
        const auto* clip = motion::findClip(project, id);
        const auto asset = clip != nullptr ? motion::findAsset(project.assets, clip->asset) : nullptr;
        if (asset == nullptr) { return; }
        SourceRequest request {{}, processor.position.load(), processor.document.generation(), asset};
        request.uniqueClip = id;
        beginSourceImport(std::move(request));
    };
    timeline.onTimingRequested = [this](motion::Id id) { select(id); };
    timeline.onLoopSelection = [this] { loopSelection(); };
    timeline.onCommand = [this](const juce::String& name) {
        for (const auto& command : commands) {
            if (command.name == name) { command.action(); return; }
        }
    };
    timeline.onRevealSource = [this](motion::Id asset) {
        libraryTabs.setSelectedIndex(0);
        assetLibrary.refresh();
        assetLibrary.selectAsset(asset);
    };
    timeline.onError = [this](const juce::String& message) { osci::showOverlayMessage(*this, "Cannot edit timeline", message); };
    timeline.onPreview = [this](const motion::Project* project) {
        processor.previewComposition(project != nullptr ? *project : processor.document.project());
        if (project != nullptr) { composition.preview(*project); } else { composition.refresh(); }
    };
}

void MotionEditor::setUpGraph() {
    graphSide.addAndMakeVisible(routingPanel);
    graphSideViewport.setViewedComponent(&graphSide, false);
    graphSideViewport.setScrollBarsShown(true, false);
    graphSideViewport.setScrollBarThickness(6);
    routingPanel.onLayoutChanged = [this] { layoutGraphSide(); };
    routingPanel.onError = [this](const juce::String& text) { statusBar.show(text); };
    curveEditor.setVisible(false);
    for (const auto& spec : motion::objectPropertySpecs) { curveProperties.emplace_back(spec.id); }
    curveList.onChoose = [this](const std::string& property) { selectCurveTarget(curveTarget, property, cameraCurve, true); };
    curveList.onShow = [this](const std::string& property, bool show) {
        // Siblings of the edited channel are shown by default, so their eye hides them.
        if (show) { shownCurves.insert(property); hiddenCurves.erase(property); } else { shownCurves.erase(property); hiddenCurves.insert(property); }
        refreshCurveList();
        curveEditor.repaint();
    };
    curveEditor.onPropertyChosen = [this](const std::string& property) { selectCurveTarget(curveTarget, property, cameraCurve, true); };
    curveEditor.onPreview = [this](const motion::PropertyMap* curves) {
        auto preview = processor.document.project();
        if (curves != nullptr) {
            for (const auto& [name, curve] : *curves) {
                auto* target = motion::findPropertyCurve(preview, curveTarget, name);
                if (target != nullptr) { *target = curve; }
            }
        }
        processor.previewComposition(preview);
        composition.preview(preview);
    };
}

// The workspace layout is a per-user preference, not part of a project.
void MotionEditor::loadLayout() {
    auto& settings = processor.globalSettings;
    timelineFraction = std::clamp(settings.getDouble("motion.layout.timeline", timelineFraction), 0.25, 0.65);
    previewFraction = std::clamp(settings.getDouble("motion.layout.preview", previewFraction), 0.25, 0.75);
    timeline.namesWidth = std::clamp(settings.getInt("motion.layout.names", timeline.namesWidth), 140, 420);
    timeline.defaultTrackHeight = std::clamp(settings.getInt("motion.layout.trackHeight", timeline.defaultTrackHeight), motion::Track::minimumHeight, 120);
    timeline.followEnabled = settings.getBool("motion.layout.follow", true);
    timeline.onDefaultTrackHeight = [this](int) { saveLayout(); };
    timeline.refreshTracks();
}

void MotionEditor::saveLayout() {
    auto& settings = processor.globalSettings;
    settings.set("motion.layout.timeline", timelineFraction);
    settings.set("motion.layout.preview", previewFraction);
    settings.set("motion.layout.names", timeline.namesWidth);
    settings.set("motion.layout.trackHeight", timeline.defaultTrackHeight);
    settings.set("motion.layout.follow", timeline.followEnabled);
    settings.save();
}

MotionEditor::~MotionEditor() {
    juce::Desktop::getInstance().removeFocusChangeListener(this);
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
    stopTimer();
    visualiser.openSettings = {};
    visualiser.closeSettings = {};
    visualiser.onControlsChanged = {};
    curveEditor.onPreview = {};
    processor.previewComposition(processor.document.project());
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

void MotionEditor::resized() {
    CommonPluginEditor::resized();
    auto area = getLocalBounds().reduced(3);
    auto top = area.removeFromTop(30);
    // Menus keep their natural width; the transport follows them.
    int menuWidth = 0;
    const auto names = static_cast<juce::MenuBarModel&>(menus).getMenuBarNames();
    for (int index = 0; index < names.size(); ++index) { menuWidth += menuBar.getLookAndFeel().getMenuBarItemWidth(menuBar, index, names[index]); }
    // Narrow windows keep the Output picker: the undo description shortens first.
    // Without room for the whole description only the buttons stay; a clipped
    // description would wrap onto two lines.
    const auto spare = top.getWidth() - menuWidth - 16 - 390 - 12 - (46 + 160) - 8;
    const auto wide = spare >= undoRedoControls.getPreferredWidth();
    undoRedoControls.setBounds(top.removeFromRight(wide ? undoRedoControls.getPreferredWidth() : 54));
    top.removeFromRight(8);
    // Transport sits centred in the menu row, leaving the full height below
    // for the workspace.
    // The transport goes compact (no BPM caption, tighter readout) before the
    // Output picker would have to hide.
    const auto transportWidth = top.getWidth() - menuWidth - 16 - 12 - 160 >= 390 ? 390 : 310;
    auto transport = top.withSizeKeepingCentre(std::min(top.getWidth() - menuWidth - 16, transportWidth), 30).withX(std::max(top.getX() + menuWidth + 16, top.getCentreX() - transportWidth / 2));
    // What the audio interface plays sits with the other audio state (the
    // DSP meter), not in the Scope; without room it lives in the Audio menu.
    {
        // Narrower windows drop the label, then shrink the picker.
        auto output = top.withLeft(transport.getRight() + 12);
        const auto labelled = output.getWidth() >= 46 + 160;
        const auto pickerWidth = labelled ? 160 : std::min(160, output.getWidth());
        outputLabel.setVisible(labelled);
        monitorOutput.setVisible(pickerWidth >= 100);
        output = output.removeFromRight(pickerWidth + (labelled ? 46 : 0));
        if (labelled) { outputLabel.setBounds(output.removeFromLeft(46).withTrimmedRight(6)); }
        monitorOutput.setBounds(output.reduced(0, 4));
    }
    menuBar.setBounds(top.withRight(transport.getX()));
    startButton.setBounds(transport.removeFromLeft(28).reduced(1, 3));
    playButton.setBounds(transport.removeFromLeft(32).reduced(1, 3));
    endButton.setBounds(transport.removeFromLeft(28).reduced(1, 3));
    loopButton.setBounds(transport.removeFromLeft(28).reduced(1, 3));
    // Narrow windows drop the BPM caption and tighten the readouts so the
    // timing menu never collapses to nothing.
    const auto compact = transport.getWidth() < 258;
    if (compact != compactTransport) {
        compactTransport = compact;
        refreshTiming();
    }
    transport.removeFromLeft(compact ? 4 : 8);
    timeLabel.setBounds(transport.removeFromLeft(compact ? 92 : 118).reduced(0, 3));
    transport.removeFromLeft(compact ? 4 : 8);
    tempoValue.setBounds(transport.removeFromLeft(52).reduced(0, 4));
    tempoLabel.setVisible(!compact);
    tempoLabel.setBounds(transport.removeFromLeft(compact ? 4 : 34).withTrimmedLeft(compact ? 0 : 4));
    tapButton.setBounds(transport.removeFromLeft(38).reduced(1, 4));
    area.removeFromTop(3);
    statusBar.setBounds(area.removeFromBottom(20));
    area.removeFromBottom(2);
    workspaceHeight = area.getHeight();
    timelineBounds = area.removeFromBottom(std::clamp(juce::roundToInt(workspaceHeight * timelineFraction), 240, workspaceHeight - 370));
    auto timeline = timelineBounds;
    auto header = timeline.removeFromTop(30);
    timelineHeader.setBounds(header);
    timelineTabs.setBounds(header.removeFromLeft(timelineTabs.preferredWidth()));
    cancelExport.setBounds(header.removeFromRight(62).reduced(2));
    exportBar.setBounds(header.removeFromRight(180).reduced(2));
    const bool nested = processor.document.editingComposition() != 0;
    scopeBack.setVisible(nested); scopeLabel.setVisible(nested); scopeShared.setVisible(nested);
    if (nested) {
        // Inset like the tool row below it.
        auto breadcrumb = timeline.removeFromTop(28).reduced(motion::style::padding, 2);
        scopeBack.setBounds(breadcrumb.removeFromLeft(scopeBack.getBestWidthForHeight(24) + 16));
        breadcrumb.removeFromLeft(motion::style::padding);
        scopeShared.setBounds(breadcrumb.removeFromRight(140));
        scopeLabel.setBounds(breadcrumb);
    }
    this->timeline.setBounds(timeline.withTrimmedTop(3));
    notesEditor.setBounds(timeline.withTrimmedTop(3));
    auto graph = timeline.withTrimmedTop(3);
    // The channel list and routing need a target; without one the graph takes the room.
    curveList.setVisible(timelineTabs.getCurrentTabIndex() == 1 && curveTarget != 0);
    if (curveTarget != 0) {
        curveList.setBounds(graph.removeFromLeft(std::clamp(graph.getWidth() / 7, 150, 210)));
        graph.removeFromLeft(2);
    }
    graphSideViewport.setVisible(timelineTabs.getCurrentTabIndex() == 1 && curveTarget != 0);
    if (curveTarget != 0) {
        graphSideViewport.setBounds(graph.removeFromRight(std::clamp(graph.getWidth() / 4, 230, 285)));
        layoutGraphSide();
        graph.removeFromRight(3);
    }
    curveEditor.setBounds(graph);
    timelineDivider.setBounds(area.removeFromBottom(7));
    // Side panels widen on large windows; the preview keeps the rest.
    const auto extra = std::max(0, getWidth() - 1440);
    libraryBounds = area.removeFromLeft(std::clamp(190 + extra / 10, 190, 280));
    auto library = libraryBounds;
    libraryTabs.setBounds(library.removeFromTop(30));
    effectLibrary.setBounds(library.reduced(4, 0));
    modulatorLibrary.setBounds(library);
    importButton.setBounds(library.removeFromTop(42).reduced(8, 6));
    assetLibrary.setBounds(library.reduced(4, 0));
    area.removeFromLeft(3);
    inspectorBounds = area.removeFromRight(std::clamp(300 + extra * 3 / 20, 300, 420));
    auto inspector = inspectorBounds;
    inspectorHeader.setBounds(inspector.removeFromTop(30));
    inspectorTitle.setBounds(inspectorHeader.getBounds().reduced(8, 3));
    propertyInspector.setBounds(inspector);
    area.removeFromRight(3);
    previewWidth = area.getWidth();
    auto editing = area.removeFromLeft(juce::roundToInt((previewWidth - 7) * previewFraction));
    previewDivider.setBounds(area.removeFromLeft(7));
    auto output = area;
    outputHeader.setBounds(output.removeFromTop(30));
    // The header keeps the title; what remains of the visualiser's own bar
    // (the recording stopwatch, an ffmpeg download) sits at its right.
    const auto controlsWidth = visualiserControls != nullptr && visualiserControls->getParentComponent() == this ? visualiser.controlsPreferredWidth() + 4 : 0;
    const auto titled = outputHeader.getWidth() - 8 >= 68 + controlsWidth;
    outputTitle.setVisible(titled);
    outputTitle.setBounds(outputHeader.getBounds().reduced(8, 3).withWidth(60));
    auto monitorBounds = outputHeader.getBounds().withTrimmedLeft(titled ? 68 : 0).reduced(4, 3);
    if (visualiserControls != nullptr && visualiserControls->getParentComponent() == this) {
        const auto width = std::min(visualiser.controlsPreferredWidth(), std::max(0, monitorBounds.getWidth()));
        visualiserControls->setBounds(monitorBounds.removeFromRight(width).withSizeKeepingCentre(width, 24));
        visualiserControls->toFront(false);
    }

    output.removeFromTop(1);
    visualiser.setBounds(output);
    // Mirrors the Scene's strip: 8 px in from the Scope's top right.
    {
        const auto room = output.getHeight() - 16;
        scopeTools.setVisible(room >= scopeTools.preferredHeight() && output.getWidth() >= scopeTools.preferredWidth() + 16);
        scopeTools.setBounds(output.getRight() - 8 - scopeTools.preferredWidth(), output.getY() + 8, scopeTools.preferredWidth(), scopeTools.preferredHeight());
        scopeTools.toFront(false);
    }
    viewportBounds = editing;
    viewportHeader.setBounds(editing.removeFromTop(30));
    auto viewControls = viewportHeader.getBounds().reduced(8, 3);
    compositionTitle.setVisible(viewControls.getWidth() >= 160);
    sceneView.setBounds(viewControls.removeFromRight(std::min(64, viewControls.getWidth())));
    if (compositionTitle.isVisible()) { compositionTitle.setBounds(viewControls.removeFromLeft(100)); }
    // The tool strip floats at the Scene's top left.
    const auto room = editing.getHeight() - 20;
    sceneTools.setCompact(room < sceneTools.fullHeight());
    sceneTools.setVisible(room >= sceneTools.preferredHeight());
    sceneTools.setBounds(editing.getX() + 8, editing.getY() + 9, sceneTools.preferredWidth(), sceneTools.preferredHeight());
    composition.setBounds(editing.withTrimmedTop(1));
    sceneTools.toFront(false);
    // A drawing or text being edited takes over the Scene.
    juce::Component* sceneEditor = drawingEditor != nullptr ? static_cast<juce::Component*>(drawingEditor.get()) : textEditor != nullptr ? static_cast<juce::Component*>(textEditor.get()) : luaEditor.get();
    if (sceneEditor != nullptr) {
        sceneEditor->setBounds(viewportBounds);
        for (auto* component : std::initializer_list<juce::Component*> {&composition, &sceneTools, &sceneView, &compositionTitle, &viewportHeader}) { component->setVisible(false); }
    }
    // Full screen, the Scope covers everything; its strip stays at the top
    // right, with the recording stopwatch beside it.
    if (scopeFullScreen) {
        visualiser.setBounds(getLocalBounds());
        visualiser.toFront(false);
        scopeTools.setVisible(true);
        scopeTools.setBounds(getWidth() - 8 - scopeTools.preferredWidth(), 8, scopeTools.preferredWidth(), scopeTools.preferredHeight());
        scopeTools.toFront(false);
        if (visualiserControls != nullptr && visualiserControls->getParentComponent() == this) {
            const auto width = visualiser.controlsPreferredWidth();
            visualiserControls->setBounds(scopeTools.getX() - 8 - width, 8, width, 24);
            visualiserControls->toFront(false);
        }
    }
}

void MotionEditor::paintOverChildren(juce::Graphics& graphics) {
    if (scopeFullScreen) { return; }
    // Every panel has the same rounded corners, whatever its content paints.
    const auto scope = outputHeader.getBounds().getUnion(visualiser.getBounds());
    graphics.setColour(motion::style::background());
    for (const auto& panel : {libraryBounds, viewportBounds, scope, inspectorBounds, timelineBounds}) {
        if (panel.isEmpty()) { continue; }
        juce::Path corners;
        corners.addRectangle(panel.toFloat());
        corners.addRoundedRectangle(panel.toFloat(), motion::style::panelRadius);
        corners.setUsingNonZeroWinding(false);
        graphics.fillPath(corners);
    }
    if (findActiveOverlay<osci::OverlayComponent>() != nullptr) { return; }
    graphics.setColour(osci::Colours::outlineSubtle());
    const auto panel = focusedPanel();
    if (!panel.isEmpty()) {
        graphics.setColour(motion::style::accent().withAlpha(.45f));
        graphics.drawRoundedRectangle(panel.toFloat().reduced(.5f), 5.0f, 1.0f);
    }
}

juce::Rectangle<int> MotionEditor::focusedPanel() const {
    const auto* focused = juce::Component::getCurrentlyFocusedComponent();
    if (focused == nullptr) { return {}; }
    const auto within = [focused](const juce::Component& component) { return &component == focused || component.isParentOf(focused); };
    if (within(timeline) || within(curveEditor) || within(notesEditor) || within(curveList) || within(graphSideViewport)) { return timelineBounds; }
    if (within(composition) || within(sceneTools)) { return viewportBounds; }
    if (within(assetLibrary) || within(effectLibrary) || within(modulatorLibrary)) { return libraryBounds; }
    if (within(propertyInspector) || within(clipTimingPanel)) { return inspectorBounds; }
    return {};
}

void MotionEditor::paint(juce::Graphics& graphics) {
    graphics.fillAll(osci::Colours::veryDark());
    graphics.setColour(osci::Colours::surface());
    for (const auto& panel : { libraryBounds, viewportBounds, inspectorBounds, timelineBounds }) {
        graphics.fillRoundedRectangle(panel.toFloat(), 5.0f);
    }
}

// Small edits (a marker, a tempo change, the canvas) open in a panel that
// points at what was clicked instead of covering the window.
void MotionEditor::showPopover(std::unique_ptr<juce::Component> content, juce::Rectangle<int> anchor) {
    auto* panel = content.get();
    motion::style::styleFields(*panel);
    auto& box = juce::CallOutBox::launchAsynchronously(std::move(content), anchor, this);
    box.setArrowSize(9.0f);
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
    CommonPluginEditor::showOverlay(std::move(overlay));
    shown.addComponentListener(&dialogStyle);
    motion::style::restyleDialog(shown, dialogStyle.look);
}

void MotionEditor::timerCallback() {
    continueCommandLineRender();
    if (textPreviewDue > 0 && juce::Time::getMillisecondCounterHiRes() >= textPreviewDue) { previewText(); }
    processor.showIdleSeek(processor.document.mainProject().duration);
    auto& previewRate = processor.recordingParameters.frameRate;
    // Keep playback and export cadence aligned within the live renderer's supported range.
    const auto projectFrameRate = static_cast<float>(std::clamp<double>(processor.document.mainProject().frameRate, previewRate.min, previewRate.max));
    if (!visualiser.isRecording() && std::abs(previewRate.getValueUnnormalised() - projectFrameRate) > 0.005f) {
        previewRate.setUnnormalisedValueNotifyingHost(projectFrameRate);
    }
    scopeTools.canvas.setEnabled(!visualiser.isRecording() && exportState == nullptr);
    {
        using Control = VisualiserComponent::Control;
        // The strip shows what the visualiser is doing, however it started.
        scopeTools.record.setToggleState(visualiser.isControlOn(Control::record), juce::dontSendNotification);
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
        const auto interleave = processor.beamInterleave.load(std::memory_order_relaxed);
        const juce::String dot(juce::CharPointer_UTF8("  \xc2\xb7  "));
        auto statistics = juce::String(rate / 1000.0, rate == std::floor(rate / 1000.0) * 1000.0 ? 0 : 1) + " kHz" + dot + juce::String(beamRate, beamRate == std::floor(beamRate) ? 0 : 2) + " Hz beam";
        if (processor.playing.load() || layers > 0) {
            statistics += dot + "drawing " + juce::String(layers) + (layers == 1 ? " object" : " objects");
            if (interleave > 1) { statistics += dot + "dense: each object every " + juce::String(interleave) + " cycles"; }
        }
        statusBar.setStatistics(statistics);
    }
    processor.collectPreparedState();
    assetLibrary.updateLiveStatus();
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
    // edit or a playhead move. Live inputs (armed MIDI tracks, Blender capture)
    // show in the Scene, which repaints for them while stopped too.
    const auto position = processor.position.load();
    const auto liveFrames = processor.liveSourcePreview();
    const auto isArmed = [](const auto& track) { return track.midiInput != 0; };
    const auto& mainTracks = processor.document.mainProject().tracks;
    const auto& scopeTracks = processor.document.project().tracks;
    const auto armed = std::any_of(mainTracks.begin(), mainTracks.end(), isArmed) || std::any_of(scopeTracks.begin(), scopeTracks.end(), isArmed);
    // The snapshot is held, so a new one can never reuse the old address.
    const auto live = armed || liveFrames != lastLiveFrames;
    lastLiveFrames = liveFrames;
    const auto slowTick = ++ticks % 10 == 0;
    const auto moved = position != lastPaintedPosition || playing;
    lastPaintedPosition = position;
    timeline.followPlayhead(position, playing);
    if (curveEditor.isVisible() && timeline.followEnabled) { curveEditor.followPlayhead(position, playing); }
    if (slowTick) {
        timeline.repaint();
        if (notesEditor.isVisible()) { notesEditor.repaint(); }
        if (curveEditor.isVisible()) { curveEditor.repaint(); }
    } else if (moved) {
        timeline.repaintPlayhead();
        if (notesEditor.isVisible()) { notesEditor.repaintPlayhead(); }
        if (curveEditor.isVisible()) { curveEditor.repaintPlayhead(); }
    }
    if (slowTick || moved || live) { composition.repaint(); }
    // The playhead moves values; the slow tick also refreshes structure.
    if (slowTick) {
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
    // The audio device decides whether the 5-channel output can be chosen.
    if (source == &processor.document) {
        refreshFromDocument();
    } else {
        refreshOutputChoices();
    }
}

void MotionEditor::refreshFromDocument() {
    // A drawing belongs to the project it was started in.
    if (drawingEditor != nullptr && processor.document.generation() != drawingGeneration) { closeDrawingEditor(); }
    if (textEditor != nullptr && processor.document.generation() != textGeneration) { closeTextEditor(); }
    if (luaEditor != nullptr && processor.document.generation() != luaGeneration) { closeLuaEditor(); }
    // An undo or a delete can remove what was selected.
    if (selection != 0 && !selectionExists()) { select(0); }
    sliderBakes.requestUpdate();
    refreshOutputChoices();
    if (curveList.isVisible()) { refreshCurveList(); }
    if (processor.document.editingComposition() == 0) { scopeHistory.clear(); }
    if (scopeLabel.isBeingEdited() && scopeNameGeneration != processor.document.generation()) { scopeLabel.hideEditor(true); }
    if (!scopeLabel.isBeingEdited()) { scopeLabel.setText(processor.document.project().name, juce::dontSendNotification); }
    juce::String parentName = "Main";
    if (!scopeHistory.empty() && scopeHistory.back().scope != 0) {
        for (const auto& definition : processor.document.mainProject().definitions) {
            if (definition->id == scopeHistory.back().scope) { parentName = definition->name; }
        }
    }
    scopeBack.setButtonText("Back to " + parentName);
    for (const auto& task : pendingImports) {
        if (task->generation != processor.document.generation()) { task->cancelled.store(true); }
    }
    assetLibrary.refresh();
    refreshTiming();
    timeline.refreshTracks();
    curveEditor.refresh();
    notesEditor.refresh();
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

// Look through and Key camera follow the selection, the playhead and edits;
// a camera that cannot be looked through says why.
void MotionEditor::refreshCameraTools() {
    // The cog is lit while the Scope's properties are shown.
    scopeTools.settings.setToggleState(selection != 0 && selection == processor.document.project().beam.id, juce::dontSendNotification);
    const auto camera = toolCamera();
    const auto driven = camera != 0 && camera == composition.drivenCamera();
    const auto blocker = composition.driveBlocker(camera);
    sceneTools.lookThrough.setEnabled(driven || blocker.isEmpty());
    sceneTools.lookThrough.setTooltip(driven || blocker.isEmpty() ? "Look through the selected camera: moving the view moves the camera" : blocker);
    sceneTools.keyCamera.setEnabled(camera != 0);
    sceneTools.keyCamera.setToggleState(camera != 0 && composition.cameraKeyed(camera), juce::dontSendNotification);
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
            definition = id; time = motion::Document::makeCompositionClip(0, *source, 0).offset;
        }
    }
    if (definition == 0 || definition == processor.document.editingComposition()) { return; }
    ScopeView previous;
    previous.scope = processor.document.editingComposition(); previous.selection = fromLibrary ? selection : id;
    previous.position = processor.position.load(); previous.timelineFraction = timelineFraction;
    previous.timelineTab = timelineTabs.getCurrentTabIndex();
    previous.curveTarget = curveTarget; previous.property = curvePropertyName; previous.cameraCurve = cameraCurve;
    previous.timeline = timeline.viewState(); previous.preview = composition.viewState();
    previous.graph = curveEditor.viewState(); previous.notes = notesEditor.viewState();

    composition.setNavigating(false);
    composition.setDrivenCamera(0);
    const auto entered = processor.document.enterComposition(definition);
    if (entered.failed()) { statusBar.show(entered.getErrorMessage()); return; }
    scopeHistory.push_back(previous);
    statusBar.clear();
    processor.playing.store(false);
    processor.seek(std::clamp(time, 0.0, processor.document.project().duration));
    composition.restoreView({});
    select(0); timeline.scrollY = 0; timeline.scrollTime = 0; timeline.revealTime(time);
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
    processor.playing.store(false); processor.seek(previous.position);
    select(previous.selection);
    timelineTabs.setSelectedIndex(previous.timelineTab);
    selectCurveTarget(previous.curveTarget, previous.property, previous.cameraCurve);
    timelineFraction = previous.timelineFraction;
    timeline.restoreView(previous.timeline);
    composition.restoreView(previous.preview);
    curveEditor.restoreView(previous.graph);
    notesEditor.restoreView(previous.notes);
    refreshFromDocument();
}

void MotionEditor::select(motion::Id id) {
    selection = id;
    notesEditor.setSelection(id);
    clipTimingPanel.setSelection(id);
    textAnimation.setSelection(id);
    // Cameras have no effects, so selecting one shows its Properties.
    const auto camera = selectionIsCamera();
    timeline.setSelection(id);
    timeline.revealSelection();
    composition.selected = id;
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
        processor.previewComposition(processor.document.project());
        composition.refresh();
        return;
    }
    effects->push_back(motion::makeEffect(std::numeric_limits<motion::Id>::max(), *definition));
    processor.previewComposition(project);
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
    const auto effect = details.description.toString().startsWith("motion-effect:");
    propertyInspector.setModulatorDrag(details.description.toString().startsWith("motion-modulator:"));
    timeline.setEffectDragActive(effect);
    composition.setEffectDragActive(effect);
    effectStack.setDragActive(effect);
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

bool MotionEditor::audioSelected() const {
    const auto target = motion::findPropertyTarget(processor.document.project(), selection);
    return target.has_value() && target->isAudio;
}

void MotionEditor::refreshInspector() {
    propertyInspector.setSelectionCount(std::max<std::size_t>(1, timeline.selectedClipIds().size()));
    clipTimingPanel.refresh();
    textAnimation.refresh();
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
        propertyInspector.setLead(&scopeHeading, [] { return MotionScopeHeading::preferredHeight(); });
    } else {
        propertyInspector.setLead(&inspectorLead, [this] { return inspectorLead.preferredHeight(); });
    }
    // The Scope has no effects; its fixed options follow its rows instead.
    if (scopeSelected) {
        propertyInspector.setTrail(&scopePanel, [] { return MotionScopePanel::preferredHeight(); });
    } else {
        propertyInspector.setTrail(&effectStack, [this] { return effectStack.preferredHeight(); });
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

bool MotionEditor::keyPressed(const juce::KeyPress& key) {
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
    if (parameterless || !motion::findPropertyTarget(processor.document.project(), id).has_value()) {
        // Nothing selected, or an effect without parameters: nothing to graph.
        curveTarget = 0;
        curveEditor.setSelection(0, {});
        routingPanel.setTarget(0, {});
        refreshCurveList();
        layoutGraphSide();
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
        for (const auto& spec : motion::propertySpecs(*target)) { add(spec.id); }
        // A Lua clip's slider curves are graphable once they exist.
        for (const auto& spec : motion::luaSliderSpecs) {
            if (target->properties != nullptr && target->properties->contains(spec.id)) { add(spec.id); }
        }
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
        curveList.setChannels({}, {}, shownCurves);
        curveEditor.setContextCurves({});
        return;
    }
    const auto* effect = motion::findEffect(project, curveTarget);
    const auto* definition = effect == nullptr ? nullptr : motion::effectDefinition(effect->type);
    std::vector<MotionCurveList::Channel> channels;
    std::map<std::string, juce::Colour> colours;
    for (const auto& name : curveProperties) {
        MotionCurveList::Channel channel;
        channel.id = name;
        const auto* spec = target.has_value() && definition == nullptr ? motion::findPropertySpec(motion::propertySpecs(*target), name) : nullptr;
        if (spec == nullptr && target.has_value() && name.rfind("slider.", 0) == 0) { spec = motion::findPropertySpec(motion::luaSliderSpecs, name); }
        if (definition != nullptr) {
            for (const auto& parameter : definition->parameters) { if (parameter.id == name) { channel.label = juce::String(parameter.name); } }
            channel.group = "Effect";
        } else if (spec != nullptr) {
            channel.label = juce::String(spec->label.data(), spec->label.size());
            channel.group = juce::String(spec->group.data(), spec->group.size());
        } else {
            channel.label = juce::String(name).replace(".", " ");
        }
        const auto axis = spec != nullptr && !spec->axis.empty() ? spec->axis[0] : ' ';
        channel.colour = axis == 'X' || axis == 'R' ? motion::style::axisX() : axis == 'Y' || axis == 'G' ? motion::style::axisY() : axis == 'Z' || axis == 'B' ? motion::style::axisZ() : motion::style::key();
        const auto* curve = target.has_value() ? target->curve(name) : nullptr;
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

void MotionEditor::layoutGraphSide() {
    const auto width = graphSideViewport.getWidth() - 8;
    routingPanel.setBounds(0, 0, width, routingPanel.preferredHeight());
    graphSide.setSize(width, routingPanel.getBottom());
}


void MotionEditor::refreshOutputChoices() {
    // Say so when "Soundtrack" would play silence.
    const juce::String soundtrackChoice = soundtrackClip() != 0 ? "Soundtrack" : "Soundtrack (none yet)";
    if (monitorOutput.getItemText(0) != soundtrackChoice) {
        const auto chosen = monitorOutput.getSelectedId();
        monitorOutput.changeItemText(1, soundtrackChoice);
        monitorOutput.setSelectedId(chosen, juce::dontSendNotification);
    }
    auto* holder = juce::StandalonePluginHolder::getInstance();
    auto* device = holder != nullptr ? holder->deviceManager.getCurrentAudioDevice() : nullptr;
    monitorOutput.setItemEnabled(3, device != nullptr && device->getOutputChannelNames().size() >= 5);
}

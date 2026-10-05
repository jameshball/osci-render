#include "live/BlenderCaptureArchive.h"
#include "import/SourceDecoding.h"
#include "ui/BlenderSourcePanel.h"
#include "MotionEditor.h"
#include "export/SignalExporter.h"
#include "export/SoundtrackExporter.h"
#include "ui/VideoExportSettings.h"
#include "ui/BakeSettingsPanel.h"
#include "ui/FractalSettingsPanel.h"
#include "ui/RasterSettingsPanel.h"

#include "ui/MarkerPanel.h"
#include "ui/MidiEnvelopePanel.h"
#include "../components/OverlayDialogHelpers.h"
#include <cstdlib>
#include <iostream>

namespace {
class MotionProjectLoading final : public juce::Component {
public:
    explicit MotionProjectLoading(std::function<void()> cancel) {
        status.setText("Preparing sources. Your current project stays open until loading succeeds.", juce::dontSendNotification);
        status.setJustificationType(juce::Justification::centred);
        cancelButton.onClick = std::move(cancel);
        addAndMakeVisible(status);
        addAndMakeVisible(cancelButton);
    }
    void resized() override {
        auto bounds = getLocalBounds().reduced(12);
        cancelButton.setBounds(bounds.removeFromBottom(30).withSizeKeepingCentre(100, 30));
        status.setBounds(bounds);
    }
private:
    juce::Label status;
    juce::TextButton cancelButton {"Cancel loading"};
};

class MotionVideoPreparation final : public juce::Component, private juce::Timer {
public:
    MotionVideoPreparation(std::function<double()> readProgress, std::function<void()> cancel, bool includeAudio)
        : readProgress(std::move(readProgress)), cancelWork(std::move(cancel)) {
        addAndMakeVisible(status);
        addAndMakeVisible(bar);
        addAndMakeVisible(cancelButton);
        status.setText(includeAudio ? "Preparing beam signal and soundtrack..." : "Preparing beam signal...", juce::dontSendNotification);
        status.setJustificationType(juce::Justification::centred);
        bar.setName("Preparing video media");
        cancelButton.setName("Cancel video preparation");
        cancelButton.onClick = [this] {
            cancelWork();
            cancelButton.setEnabled(false);
            status.setText("Cancelling...", juce::dontSendNotification);
        };
        startTimerHz(20);
    }
    void resized() override {
        auto area = getLocalBounds().reduced(12);
        status.setBounds(area.removeFromTop(28));
        area.removeFromTop(8);
        bar.setBounds(area.removeFromTop(24));
        area.removeFromTop(16);
        cancelButton.setBounds(area.removeFromTop(30).withSizeKeepingCentre(100, 30));
    }
private:
    void timerCallback() override { progress = readProgress(); }
    std::function<double()> readProgress;
    std::function<void()> cancelWork;
    double progress = 0;
    juce::ProgressBar bar { progress };
    juce::Label status;
    juce::TextButton cancelButton { "Cancel" };
};

struct MotionVideoTemporaryFiles {
    const juce::File directory = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("osci-motion-video-" + juce::Uuid().toString());
    ~MotionVideoTemporaryFiles() { directory.deleteRecursively(); }
    juce::File signal() const { return directory.getChildFile("beam.wav"); }
    juce::File soundtrack() const { return directory.getChildFile("soundtrack.wav"); }
};
bool canSplitClip(const motion::Clip* clip, double time, const motion::Tempo& bpm) {
    return clip != nullptr && clip->valid() && std::isfinite(time) && time > clip->timing(bpm).start && time < clip->timing(bpm).end();
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
    for (auto* header : { &libraryHeader, &viewportHeader, &outputHeader, &inspectorHeader, &timelineHeader }) {
        addAndMakeVisible(header);
    }
    for (auto* component : std::initializer_list<juce::Component*> { &timeline, &composition, &assetLibrary, &importButton, &playButton, &startButton, &endButton, &timeLabel, &propertyInspector, &curveEditor, &notesEditor, &timelineTabs, &timelineDivider, &previewDivider, &statusBar }) {
        addAndMakeVisible(component);
    }
    addChildComponent(scopeBack);
    addChildComponent(scopeLabel);
    addChildComponent(scopeShared);
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
    for (const auto& [label, text] : std::initializer_list<std::pair<juce::Label*, const char*>> {{&compositionTitle, "Scene"}, {&outputTitle, "Scope"}}) {
        addAndMakeVisible(label);
        label->setText(text, juce::dontSendNotification);
        label->setFont(motion::style::title());
        label->setBorderSize(juce::BorderSize<int>(0));
    }
    outputHeader.setName("Scope");
    // Tools live inside the Scene, Blender style; the header keeps its title
    // and the view presets.
    addAndMakeVisible(sceneTools);
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
    addAndMakeVisible(sceneView);
    sceneView.setButtonText("Views");
    sceneView.setName("Scene view");
    sceneView.setTooltip("Look along an axis (numpad 1, 3, 7), frame the selection (F) or reset the view (0)");
    sceneView.setColour(juce::TextButton::buttonColourId, osci::Colours::surfaceRaised());
    sceneView.onClick = [this] { showSceneViewMenu(false); };
    composition.onContextMenu = [this] { showSceneViewMenu(true); };
    composition.onPropertyEdited = [this](motion::Id id, const std::string& property) { selectCurveTarget(id, property, false, true); };
    composition.isSelected = [this](motion::Id id) { return timeline.selectedClipIds().contains(id); };
    addChildComponent(exportBar);
    addAndMakeVisible(libraryTabs);
    graphSide.addAndMakeVisible(routingPanel);
    graphSideViewport.setViewedComponent(&graphSide, false);
    graphSideViewport.setScrollBarsShown(true, false);
    graphSideViewport.setScrollBarThickness(6);
    addChildComponent(graphSideViewport);
    routingPanel.onLayoutChanged = [this] { layoutGraphSide(); };
    addChildComponent(modulatorLibrary);
    routingPanel.onError = [this](const juce::String& text) { statusBar.show(text); };
    modulatorLibrary.onError = [this](const juce::String& text) { statusBar.show(text); };
    routingPanel.onShowModulator = [this](motion::Id id) {
        modulatorLibrary.select(id);
        libraryTabs.setSelectedIndex(2);
    };
    addAndMakeVisible(tempoValue);
    addAndMakeVisible(tempoLabel);
    // The Scope's controls float over its top right, like the Scene's tools.
    addAndMakeVisible(scopeTools);
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
    addAndMakeVisible(monitorOutput);
    outputLabel.setText("Output", juce::dontSendNotification);
    outputLabel.setFont(motion::style::caption());
    outputLabel.setColour(juce::Label::textColourId, motion::style::muted());
    outputLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(outputLabel);
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
            const auto result = holder != nullptr ? holder->configureOutputChannels(5) : juce::Result::fail("Five-channel output requires the standalone audio device.");
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
    addAndMakeVisible(loopButton);
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
    addAndMakeVisible(tapButton);
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
    addChildComponent(effectLibrary);
    libraryHeader.setVisible(false);
    libraryTabs.setName("Library tabs");
    libraryTabs.setTabPadding(8);
    timelineTabs.setTabPadding(8);
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
    timeline.onEffectAdded = [this](motion::Id owner, motion::Id) { select(owner); };
    addChildComponent(cancelExport);
    exportBar.setName("Signal export progress");
    cancelExport.onClick = [this] { if (exportState != nullptr) { exportState->cancelled.store(true); } };
    // A clip's timing leads its Properties, as layer timing does in other editors.
    inspectorLead.add(compositionSettings, [this] { return compositionSettings.preferredHeight(); });
    inspectorLead.add(clipTimingPanel, [this] { return clipTimingPanel.preferredHeight(); });
    compositionSettings.onTiming = [this](int command) { applyTiming(command); };
    compositionSettings.onError = [this](const juce::String& message) { statusBar.show(message); };
    compositionSettings.onHeightChanged = [this] { inspectorLead.resized(); propertyInspector.relayout(); };
    inspectorLead.add(textAnimation, [this] { return textAnimation.preferredHeight(); });
    propertyInspector.setLead(&inspectorLead, [this] { return inspectorLead.preferredHeight(); });
    clipTimingPanel.onHeightChanged = [this] { inspectorLead.resized(); propertyInspector.relayout(); };
    textAnimation.onHeightChanged = [this] { inspectorLead.resized(); propertyInspector.relayout(); };
    // A change to the characters prepares the text source again in place.
    textAnimation.onApply = [this](motion::Id id, motion::TextSettings settings) {
        const auto& assets = processor.document.project().assets;
        const auto found = std::find_if(assets.begin(), assets.end(), [id](const auto& asset) { return asset->id == id; });
        if (found == assets.end()) { return; }
        // One preparation at a time: a later change waits for the current one.
        if (!pendingImports.empty()) {
            queuedTextAnimation = QueuedTextAnimation {id, settings, processor.document.generation()};
            return;
        }
        // Only the animation is this section's; the text and its type stay
        // as the source has them now.
        auto merged = (*found)->textSettings;
        merged.animation = settings.animation;
        merged.characterDelay = settings.characterDelay;
        merged.characterDuration = settings.characterDuration;
        merged.hold = settings.hold;
        merged.amount = settings.amount;
        SourceRequest request {{}, processor.position.load(), processor.document.generation(), *found};
        request.textSettings = merged;
        beginSourceImport(request);
    };
    // The owner's effects follow its properties.
    propertyInspector.setTrail(&effectStack, [this] { return effectStack.preferredHeight(); });
    addAndMakeVisible(inspectorTitle);
    inspectorTitle.setText("Properties", juce::dontSendNotification);
    inspectorTitle.setFont(motion::style::title());
    inspectorTitle.setBorderSize(juce::BorderSize<int>(0));
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
        graphSideViewport.setVisible(index == 1 && curveTarget != 0);
        curveList.setVisible(index == 1 && curveTarget != 0);
        if (index == 1) { refreshCurveList(); }
        resized();
        if (index == 2) { notesEditor.fitContents(); }
    };
    curveEditor.setVisible(false);
    notesEditor.setVisible(false);
    addChildComponent(curveList);
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
    playButton.onClick = [this] { processor.playing.store(!processor.playing.load()); };
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
        const auto found = std::find_if(assets.begin(), assets.end(), [id](const auto& asset) { return asset->id == id; });
        if (found == assets.end() || (!(*found)->extension.equalsIgnoreCase(".lua") && !(*found)->extension.equalsIgnoreCase(".txt")
                && !(*found)->extension.equalsIgnoreCase(".lsystem") && !osci::files::isImage((*found)->extension))) { return; }
        preparationRequests.push_back({{}, processor.position.load(), processor.document.generation(), *found});
        showNextPreparationSettings();
    };
    composition.onOpenSource = [this](motion::Id id) {
        const auto& project = processor.document.project();
        for (const auto& track : project.tracks) {
            for (const auto& clip : track.clips) {
                if (clip.id != id) { continue; }
                if (clip.composition != 0) { enterComposition(clip.id); return; }
                const auto found = std::find_if(project.assets.begin(), project.assets.end(), [&](const auto& asset) { return asset->id == clip.asset; });
                if (found == project.assets.end()) { return; }
                const auto& asset = **found;
                if (asset.extension.equalsIgnoreCase(".svg") && motion::drawing::isDrawing(juce::String::fromUTF8(static_cast<const char*>(asset.data.getData()), static_cast<int>(asset.data.getSize())))) {
                    showDrawingEditor(asset.id);
                } else if (asset.extension.equalsIgnoreCase(".txt") || asset.extension.equalsIgnoreCase(".lua")) {
                    assetLibrary.onBake(asset.id);
                }
                return;
            }
        }
    };
    timeline.onOpenSource = [this](motion::Id id) { composition.onOpenSource(id); };
    assetLibrary.onCancelImport = [this] {
        for (const auto& task : pendingImports) { task->cancelled.store(true); }
    };
    notesEditor.onEditInstrument = [this](motion::Id id) {
        const motion::Clip* clip = nullptr;
        for (const auto& track : processor.document.project().tracks) {
            for (const auto& item : track.clips) { if (item.id == id) { clip = &item; } }
        }
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
        for (const auto& track : project.tracks) {
            for (const auto& clip : track.clips) {
                if (clip.id != id) { continue; }
                const auto found = std::find_if(project.assets.begin(), project.assets.end(), [&](const auto& asset) { return asset->id == clip.asset; });
                if (found == project.assets.end()) { return; }
                SourceRequest request {{}, processor.position.load(), processor.document.generation(), *found};
                request.uniqueClip = id;
                beginSourceImport(std::move(request));
                return;
            }
        }
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
    composition.onEffectPreview = [this](const std::string& type, std::optional<motion::Id> owner) { previewEffect(type, owner); };
    composition.onEffectDropped = [this](const std::string& type, motion::Id owner) { addEffectTo(type, owner); };
    timeline.onPreview = [this](const motion::Project* project) {
        processor.previewComposition(project != nullptr ? *project : processor.document.project());
        if (project != nullptr) { composition.preview(*project); } else { composition.refresh(); }
    };
    composition.onSelection = timeline.onSelection;
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
    libraryHeader.setBounds(library.removeFromTop(30));
    libraryTabs.setBounds(libraryHeader.getBounds());
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

// Files dropped on the timeline land where they were dropped (time and
// track, as in Premiere); anywhere else they go in at the playhead.
void MotionEditor::filesDropped(const juce::StringArray& files, int x, int y) {
    std::optional<std::pair<double, motion::Id>> placement;
    if (timeline.isShowing()) { placement = timeline.dropTarget(timeline.getLocalPoint(this, juce::Point<int>(x, y))); }
    for (const auto& file : files) {
        // A dropped project opens, as it would from Finder or Explorer.
        if (juce::File(file).hasFileExtension(".osci-motion")) {
            openProject(juce::File(file));
            return;
        }
        importSourceFile(juce::File(file), 0, placement);
    }
}

bool MotionEditor::openSourceFile(const juce::File& file) {
    return importSourceFile(file, 0);
}

void MotionEditor::replaceSourceFile(motion::Id asset) {
    chooser = std::make_unique<juce::FileChooser>("Replace source", processor.getLastOpenedDirectory(), osci::files::sourceWildcard());
    const juce::Component::SafePointer<MotionEditor> owner(this);
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [owner, asset](const juce::FileChooser& chosen) {
        if (owner != nullptr && chosen.getResult().existsAsFile()) { owner->importSourceFile(chosen.getResult(), asset); }
    });
}

bool MotionEditor::importSourceFile(const juce::File& file, motion::Id relink, std::optional<std::pair<double, motion::Id>> placement) {
    const auto extension = file.getFileExtension().toLowerCase();
    if (!motion::isImportableSource(extension)) {
        importError = "This source type is not connected yet.";
        statusBar.show(importError);
        repaint();
        return false;
    }
    SourceRequest request {file, placement.has_value() ? placement->first : processor.position.load(), processor.document.generation(), {}};
    request.relink = relink;
    if (placement.has_value()) { request.track = placement->second; }
    if (relink != 0 && motion::isMidiSource(extension)) {
        statusBar.show("MIDI files are assigned to clips, not swapped in as their media.");
        return false;
    }
    if (extension == ".lua" || extension == ".lsystem" || osci::files::isImage(extension)) {
        preparationRequests.push_back(std::move(request));
        showNextPreparationSettings();
    } else {
        beginSourceImport(std::move(request));
    }
    return true;
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

void MotionEditor::openProject(const juce::File& file) {
    if (file == juce::File()) { return; }
    if (projectLoad != nullptr) { projectLoad->cancelled.store(true); }
    if (projectLoadOverlay != nullptr) { dismissOverlay(projectLoadOverlay.getComponent()); }
    auto task = std::make_shared<ProjectLoad>();
    projectLoad = task;
    const auto generation = processor.document.generation();
    const auto revision = processor.document.revision();
    const juce::Component::SafePointer<MotionEditor> owner(this);
    auto cancel = [owner, task] {
        task->cancelled.store(true);
        if (owner != nullptr && owner->projectLoad == task) {
            const auto editor = owner;
            auto* overlay = editor->projectLoadOverlay.getComponent();
            editor->projectLoadOverlay = nullptr;
            editor->projectLoad.reset();
            // Dismissal destroys this button and its callback: no captures may
            // be accessed after it returns.
            if (overlay != nullptr) { editor->dismissOverlay(overlay); }
        }
    };
    auto overlay = std::make_unique<osci::ComponentOverlay>(std::make_unique<MotionProjectLoading>(cancel), "Opening " + file.getFileName(), juce::Point<int>(420, 130), true);
    overlay->onDismissRequested = [owner, task] {
        task->cancelled.store(true);
        if (owner != nullptr && owner->projectLoad == task) {
            owner->projectLoadOverlay = nullptr;
            owner->projectLoad.reset();
        }
    };
    projectLoadOverlay = overlay.get();
    showOverlay(std::move(overlay));
    imports.addJob([owner, task, file, generation, revision] {
        auto result = juce::Result::fail("Project loading cancelled.");
        if (!task->cancelled.load()) {
            try {
                juce::MemoryBlock bytes;
                if (!file.loadFileAsData(bytes) || bytes.getSize() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
                    result = juce::Result::fail("Cannot read the project file.");
                } else {
                    task->xml = juce::AudioProcessor::getXmlFromBinary(bytes.getData(), static_cast<int>(bytes.getSize()));
                    const auto* composition = task->xml != nullptr ? task->xml->getChildByName("composition") : nullptr;
                    if (task->xml == nullptr || !task->xml->hasTagName("motion-project") || task->xml->getIntAttribute("schema") != 1 || composition == nullptr) {
                        result = juce::Result::fail("This is not a valid osci-motion project.");
                    } else {
                        result = motion::Document::prepareLoad(*composition, task->prepared, &task->cancelled);
                    }
                }
            } catch (const std::exception& error) {
                result = juce::Result::fail("Cannot open project: " + juce::String(error.what()));
            }
        }
        juce::MessageManager::callAsync([owner, task, file, generation, revision, result] {
            if (owner == nullptr || owner->projectLoad != task || task->cancelled.load()) { return; }
            auto* overlay = owner->projectLoadOverlay.getComponent();
            owner->projectLoadOverlay = nullptr;
            owner->projectLoad.reset();
            if (overlay != nullptr) { owner->dismissOverlay(overlay); }
            if (result.failed()) {
                owner->projectLoadFailed = true;
                osci::showOverlayMessage(*owner, "Open Project Failed", result.getErrorMessage());
                return;
            }
            if (owner->processor.document.generation() != generation || owner->processor.document.revision() != revision) {
                osci::showOverlayMessage(*owner, "Project Changed", "The current project changed while loading. Open the file again to replace it.");
                return;
            }
            owner->processor.applyPreparedProject(std::move(task->prepared), *task->xml);
            owner->processor.currentProjectFile = file.getFullPathName();
            owner->processor.setLastOpenedDirectory(file.getParentDirectory());
            owner->processor.addRecentProjectFile(file);
            owner->updateTitle();
        });
    });
}

void MotionEditor::chooseSourceFile() {
        chooser = std::make_unique<juce::FileChooser>("Import media", processor.getLastOpenedDirectory(), motion::importWildcard());
        const juce::Component::SafePointer<MotionEditor> owner(this);
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [owner](const juce::FileChooser& chosen) {
                if (owner != nullptr && chosen.getResult().existsAsFile()) {
                    owner->openSourceFile(chosen.getResult());
                }
            });
}

void MotionEditor::showBlenderSettings(motion::Id id) {
    auto& document = processor.document;
    const auto& assets = document.mainProject().assets;
    const auto found = std::find_if(assets.begin(), assets.end(), [id](const auto& asset) { return asset->id == id; });
    if (id != 0 && (found == assets.end() || (*found)->liveIdentity == nullptr)) { return; }
    const auto original = found != assets.end() ? *found : std::shared_ptr<const motion::Asset>();
    auto panel = std::make_unique<MotionBlenderSourcePanel>(original != nullptr ? original->name : "Blender", original != nullptr ? original->blenderSettings : motion::BlenderSourceSettings{}, id != 0);
    auto* controls = panel.get();
    auto overlay = std::make_unique<osci::ComponentOverlay>(std::move(panel), id == 0 ? "Add Blender source" : "Blender source", juce::Point<int>(460, id != 0 ? 254 : 222), true);
    const juce::Component::SafePointer<MotionEditor> owner(this);
    const juce::Component::SafePointer<osci::OverlayComponent> dialog(overlay.get());
    const auto generation = document.generation();
    auto panelIdentity = std::make_shared<std::shared_ptr<const motion::LiveSourceIdentity>>(original != nullptr ? original->liveIdentity : nullptr);
    controls->onApply = [owner, dialog, id, generation, panelIdentity, expected = original](juce::String name, motion::BlenderSourceSettings settings, bool start) mutable {
        if (owner == nullptr || dialog == nullptr || owner->processor.document.generation() != generation) { return juce::Result::fail("The project changed. Reopen source settings."); }
        auto sourceId = id;
        auto& document = owner->processor.document;
        const bool creating = id == 0;
        if (!creating && std::find(document.mainProject().assets.begin(), document.mainProject().assets.end(), expected) == document.mainProject().assets.end()) {
            return juce::Result::fail("The source changed. Reopen its settings.");
        }
        const auto result = id == 0 ? document.addBlenderSource(name, settings, sourceId) : document.setBlenderSource(id, name, settings);
        if (result.failed()) { return result; }
        owner->assetLibrary.refresh(); owner->assetLibrary.selectAsset(sourceId);
        id = sourceId;
        for (const auto& asset : document.mainProject().assets) { if (asset->id == sourceId) { expected = asset; break; } }
        *panelIdentity = expected->liveIdentity;
        const auto listening = start ? owner->processor.blenderInputs().listen(sourceId, true) : juce::Result::ok();
        if (listening.failed()) { owner->statusBar.show(listening.getErrorMessage()); }
        if (creating) {
            juce::MessageManager::callAsync([owner, dialog, sourceId, generation, identity = expected->liveIdentity] {
                if (owner == nullptr || dialog == nullptr || owner->processor.document.generation() != generation) { return; }
                owner->dismissOverlay(dialog.getComponent(), [owner, sourceId, generation, identity] {
                    if (owner == nullptr || owner->processor.document.generation() != generation) { return; }
                    for (const auto& asset : owner->processor.document.mainProject().assets) {
                        if (asset->id == sourceId && asset->liveIdentity == identity) { owner->showBlenderSettings(sourceId); break; }
                    }
                });
            });
        }
        return listening;
    };
    controls->onStop = [owner, id, generation] { if (owner != nullptr && owner->processor.document.generation() == generation) { owner->processor.blenderInputs().listen(id, false); } };
    controls->isListening = [owner, id, generation] { return owner != nullptr && owner->processor.document.generation() == generation && owner->processor.blenderInputs().listening(id); };
    controls->connectionStatus = [owner, id, generation] { return owner != nullptr && owner->processor.document.generation() == generation ? owner->processor.blenderInputs().statusText(id) : juce::String("Project changed"); };
    controls->isCapturing = [owner, id, generation] { return owner != nullptr && owner->processor.document.generation() == generation && owner->processor.blenderInputs().capturing(id); };
    controls->onCancelCapture = [owner, id, generation] {
        if (owner != nullptr && owner->processor.document.generation() == generation) { owner->processor.blenderInputs().cancelCapture(id); }
    };
    controls->onRecord = [owner, dialog, id, generation, panelIdentity] {
        const auto identity = *panelIdentity;
        if (owner == nullptr || owner->processor.document.generation() != generation) { return juce::Result::fail("The project changed. Reopen source settings."); }
        auto& inputs = owner->processor.blenderInputs();
        const auto& assets = owner->processor.document.mainProject().assets;
        const auto source = std::find_if(assets.begin(), assets.end(), [id, identity](const auto& asset) { return asset->id == id && asset->liveIdentity == identity; });
        if (source == assets.end()) { return juce::Result::fail("The source changed. Reopen its settings."); }
        if (!inputs.capturing(id)) {
            if (std::any_of(owner->pendingImports.begin(), owner->pendingImports.end(), [](const auto& task) { return task->capture; })) {
                return juce::Result::fail("Wait for the previous capture to finish preparing.");
            }
            return inputs.beginCapture(id);
        }
        auto recording = inputs.finishCapture(id);
        if (recording == nullptr) { return juce::Result::fail("There is no capture to save."); }
        if (recording->failure != motion::BlenderCapture::Failure::none) { return juce::Result::fail(recording->error()); }
        auto capture = std::shared_ptr<const motion::BlenderCapture>(std::move(recording));
        auto task = std::make_shared<ImportState>();
        task->capture = true;
        task->name = (*source)->name + " capture";
        task->generation = generation;
        owner->pendingImports.push_back(task);
        owner->imports.addJob([owner, generation, identity, task, capture] {
            auto asset = std::make_shared<motion::Asset>();
            asset->name = task->name;
            asset->extension = ".blender-capture";
            auto result = juce::Result::fail("Capture cancelled.");
            try {
                auto archive = motion::BlenderCaptureArchive::encode(*capture, &task->cancelled);
                if (archive) {
                    asset->data.replaceAll(archive.bytes.data(), archive.bytes.size());
                    result = motion::decodeAsset(*asset, &task->cancelled, &task->progress);
                } else { result = juce::Result::fail(archive.error); }
            } catch (const std::exception& error) { result = juce::Result::fail("Cannot prepare capture: " + juce::String(error.what())); }
            juce::MessageManager::callAsync([owner, generation, identity, task, asset, result] {
                if (owner == nullptr) { return; }
                std::erase(owner->pendingImports, task);
                if (task->cancelled.load() || owner->processor.document.generation() != generation) { return; }
                auto& document = owner->processor.document;
                const auto& sources = document.mainProject().assets;
                if (std::none_of(sources.begin(), sources.end(), [&](const auto& source) { return source->liveIdentity == identity; })) { return; }
                if (result.failed()) { owner->statusBar.show(result.getErrorMessage()); return; }
                asset->id = document.newId();
                document.edit("Capture Blender source", [&](motion::Project& project) { project.assets.push_back(asset); });
                owner->assetLibrary.refresh(); owner->assetLibrary.selectAsset(asset->id);
                owner->libraryTabs.setSelectedIndex(0);
            });
        });
        juce::MessageManager::callAsync([owner, dialog] { if (owner != nullptr && dialog != nullptr) { owner->dismissOverlay(dialog.getComponent(), [] {}); } });
        return juce::Result::ok();
    };
    showOverlay(std::move(overlay));
}

void MotionEditor::showNextPreparationSettings() {
    // Queued sources wait while a dialog or a Scene editor is open.
    if (preparationSettingsOpen || drawingEditor != nullptr) { return; }
    while (!preparationRequests.empty() && preparationRequests.front().generation != processor.document.generation()) { preparationRequests.pop_front(); }
    if (preparationRequests.empty()) { return; }
    auto request = std::move(preparationRequests.front());
    preparationRequests.pop_front();
    const auto name = request.replacement != nullptr ? request.replacement->name : request.file.getFileName();
    const auto extension = request.replacement != nullptr ? request.replacement->extension : request.file.getFileExtension();
    const bool raster = osci::files::isImage(extension);
    const bool video = osci::files::isVideo(extension);
    const bool text = extension.equalsIgnoreCase(".txt");
    const bool fractal = extension.equalsIgnoreCase(".lsystem");
    std::unique_ptr<juce::Component> content;
    MotionRasterSettingsPanel* imagePanel = nullptr;
    MotionFractalSettingsPanel* fractalPanel = nullptr;
    // Text and Lua are written in the Scene; images and fractals keep a
    // short settings dialog.
    if (text) {
        showTextEditor(std::move(request));
        return;
    }
    if (!raster && !fractal) {
        showLuaEditor(std::move(request));
        return;
    }
    if (raster) {
        auto panel = std::make_unique<MotionRasterSettingsPanel>(request.replacement != nullptr ? request.replacement->rasterSettings : motion::RasterSettings(), video);
        imagePanel = panel.get();
        content = std::move(panel);
    } else if (fractal) {
        const auto initialDepth = request.fractalDepth.value_or(request.replacement != nullptr ? request.replacement->fractalDepth : 3);
        auto panel = std::make_unique<MotionFractalSettingsPanel>(initialDepth);
        fractalPanel = panel.get();
        content = std::move(panel);
    }
    auto overlay = std::make_unique<osci::ComponentOverlay>(std::move(content), "Prepare " + name, juce::Point<int>(440, fractal ? 102 : video ? 328 : 270), true);
    const juce::Component::SafePointer<MotionEditor> owner(this);
    const juce::Component::SafePointer<osci::OverlayComponent> overlayPointer(overlay.get());
    preparationSettingsOpen = true;
    overlay->onDismissRequested = [owner] {
        if (owner != nullptr) {
            owner->preparationSettingsOpen = false;
            owner->showNextPreparationSettings();
        }
    };
    auto submit = [owner, overlayPointer, request](motion::BakeSettings settings, motion::RasterSettings rasterSettings, std::optional<juce::String> editedText = {}, std::optional<motion::TextSettings> textSettings = {}, std::optional<int> fractalDepth = {}) mutable {
        request.textSettings = std::move(textSettings);
        request.editedText = std::move(editedText);
        request.fractalDepth = fractalDepth;
        juce::MessageManager::callAsync([owner, overlayPointer, request, settings, rasterSettings] {
            if (owner == nullptr || overlayPointer == nullptr) { return; }
            owner->dismissOverlay(overlayPointer.getComponent(), [owner, request, settings, rasterSettings] {
                if (owner == nullptr) { return; }
                owner->preparationSettingsOpen = false;
                if (owner->processor.document.generation() == request.generation) {
                    owner->beginSourceImport(request, settings, rasterSettings);
                }
                owner->showNextPreparationSettings();
            });
        });
    };
    if (imagePanel != nullptr) { imagePanel->onPrepare = [submit](motion::RasterSettings settings) mutable { submit({}, settings); }; }
    if (fractalPanel != nullptr) { fractalPanel->onPrepare = [submit](int depth) mutable { submit({}, {}, {}, {}, depth); }; }
    showOverlay(std::move(overlay));
}

void MotionEditor::beginSourceImport(SourceRequest request, motion::BakeSettings settings, motion::RasterSettings rasterSettings) {
    if (request.generation != processor.document.generation()) { return; }
    const auto extension = request.replacement != nullptr ? request.replacement->extension : request.file.getFileExtension();
    if (osci::files::isVideo(extension) && request.uniqueClip == 0) {
        if (!processor.getFFmpegFile().existsAsFile()) {
            const juce::Component::SafePointer<MotionEditor> owner(this);
            processor.ensureFFmpegExists({}, [owner, request, settings, rasterSettings] {
                if (owner != nullptr) { owner->beginSourceImport(request, settings, rasterSettings); }
            });
            return;
        }
        if (!processor.ensureFFmpegExists()) { return; }
    }
    importError.clear();
    statusBar.clear();
    auto asset = std::make_shared<motion::Asset>();
    asset->name = request.replacement != nullptr ? request.replacement->name : request.file.getFileName();
    asset->extension = request.replacement != nullptr ? request.replacement->extension : request.file.getFileExtension().toLowerCase();
    asset->bakeSettings = settings;
    asset->rasterSettings = rasterSettings;
    asset->fractalDepth = request.fractalDepth.value_or(request.replacement != nullptr ? request.replacement->fractalDepth : 3);
    asset->textSettings = request.textSettings.value_or(request.replacement != nullptr ? request.replacement->textSettings : motion::TextSettings());
    asset->midiImportBpm = processor.document.project().bpm;
    const auto time = request.time;
    const auto generation = request.generation;
    auto task = std::make_shared<ImportState>();
    task->name = asset->name;
    task->generation = generation;
    pendingImports.push_back(task);
    const juce::Component::SafePointer<MotionEditor> owner(this);
    const auto videoDecoder = processor.getFFmpegFile();
    imports.addJob([owner, request, asset, time, generation, task, videoDecoder] {
        auto result = juce::Result::fail("Import cancelled.");
        if (!task->cancelled.load()) {
            try {
                if (request.replacement != nullptr) {
                    if (request.uniqueClip != 0) {
                        *asset = *request.replacement;
                    } else if (request.editedText.has_value()) {
                        const auto& text = *request.editedText;
                        asset->data.replaceAll(text.toRawUTF8(), text.getNumBytesAsUTF8());
                    } else {
                        asset->data = request.replacement->data;
                    }
                    result = request.uniqueClip != 0 ? juce::Result::ok() : motion::decodeAsset(*asset, &task->cancelled, &task->progress, videoDecoder);
                } else if (request.file.getSize() > static_cast<juce::int64>(motion::maximumSourceBytes)) {
                    result = juce::Result::fail("This source exceeds the 64 MiB preparation limit.");
                } else {
                    result = request.file.loadFileAsData(asset->data) ? motion::decodeAsset(*asset, &task->cancelled, &task->progress, videoDecoder) : juce::Result::fail("Cannot read the source file.");
                }
            } catch (const std::exception& error) {
                result = juce::Result::fail("Cannot import this source: " + juce::String(error.what()));
            }
        }
        juce::MessageManager::callAsync([owner, request, asset, time, result, generation, task] {
            if (owner == nullptr) {
                return;
            }
            std::erase(owner->pendingImports, task);
            juce::MessageManager::callAsync([owner] {
                if (owner == nullptr || !owner->pendingImports.empty() || !owner->queuedTextAnimation.has_value()) { return; }
                const auto queued = *owner->queuedTextAnimation;
                owner->queuedTextAnimation.reset();
                if (queued.generation != owner->processor.document.generation()) { owner->textAnimation.clearPending(); return; }
                if (owner->textAnimation.onApply) { owner->textAnimation.onApply(queued.asset, queued.settings); }
            });
            if (task->cancelled.load() || owner->processor.document.generation() != generation) { return; }
            if (result.failed()) {
                owner->importError = result.getErrorMessage();
                owner->statusBar.show(owner->importError);
                owner->textAnimation.clearPending();
                owner->repaint();
                // A new script that fails reopens with its code, as an edit does.
                if (request.replacement == nullptr && request.relink == 0 && request.file.hasFileExtension("lua")) {
                    auto retry = request;
                    retry.retrySettings = asset->bakeSettings;
                    retry.preparationError = result.getErrorMessage();
                    owner->preparationRequests.push_front(std::move(retry));
                    owner->showNextPreparationSettings();
                    return;
                }
                if (request.uniqueClip == 0 && request.replacement != nullptr && (request.replacement->extension.equalsIgnoreCase(".lua") || request.replacement->extension.equalsIgnoreCase(".txt"))) {
                    const auto& assets = owner->processor.document.project().assets;
                    if (std::find(assets.begin(), assets.end(), request.replacement) != assets.end()) {
                        auto retry = request;
                        retry.retrySettings = asset->bakeSettings;
                        retry.preparationError = result.getErrorMessage();
                        owner->preparationRequests.push_front(std::move(retry));
                        owner->showNextPreparationSettings();
                    }
                }
                return;
            }
            auto& document = owner->processor.document;
            if (request.uniqueClip != 0) {
                const auto separated = document.makeSourceUnique(request.uniqueClip, request.replacement, asset);
                if (separated.failed()) { owner->statusBar.show(separated.getErrorMessage()); return; }
                owner->assetLibrary.refresh();
                owner->assetLibrary.selectAsset(asset->id);
                owner->libraryTabs.setSelectedIndex(0);
                owner->select(request.uniqueClip);
                return;
            }
            if (request.replacement != nullptr) {
                const auto& assets = document.project().assets;
                if (std::find(assets.begin(), assets.end(), request.replacement) == assets.end()) {
                    owner->statusBar.show("The source changed while baking. Its previous result has been kept.");
                    return;
                }
                asset->id = request.replacement->id;
                document.edit(request.editedText.has_value() ? (asset->extension.equalsIgnoreCase(".lua") ? "Edit Lua source" : "Edit text source") : request.textSettings.has_value() ? "Animate text" : "Rebuild source cache", [&](motion::Project& project) {
                    for (auto& item : project.assets) {
                        if (item == request.replacement) { item = asset; }
                    }
                });
                owner->assetLibrary.refresh();
                owner->assetLibrary.selectAsset(asset->id);
                return;
            }
            if (request.relink != 0) {
                asset->id = request.relink;
                const auto replaced = document.replaceAsset(request.relink, asset);
                if (replaced.failed()) { owner->statusBar.show(replaced.getErrorMessage()); return; }
                owner->assetLibrary.refresh();
                owner->assetLibrary.selectAsset(asset->id);
                owner->statusBar.show("Replaced the source; its clips kept their timing, keys and effects.", MotionStatusBar::Kind::notice);
                return;
            }
            asset->id = document.newId();
            asset->name = motion::Document::uniqueAssetName(document.mainProject(), asset->name);
            if (asset->midi != nullptr) {
                document.edit("Import MIDI file", [&](motion::Project& project) { project.assets.push_back(asset); });
                owner->assetLibrary.refresh(); owner->assetLibrary.selectAsset(asset->id);
                // Point at the file's tempo when it differs from the project's.
                const auto& project = document.project();
                if (asset->midiSuggestedBpm != project.bpm || asset->midiTempoChanges != nullptr) {
                    owner->statusBar.show(asset->name + " is written at " + juce::String(asset->midiSuggestedBpm, 1) + " BPM"
                        + (asset->midiTempoChanges != nullptr ? " with tempo changes" : "") + ". Right-click it in Assets to use its tempo.", MotionStatusBar::Kind::notice);
                }
                return;
            }
            auto clip = motion::Document::makeClip(document.newId(), *asset, time);
            motion::Track track;
            track.id = document.newId();
            track.name = asset->name.toStdString();
            track.kind = asset->audio != nullptr ? motion::TrackKind::audio : motion::TrackKind::visual;
            track.insert(clip, document.project().tempo());
            // A drop on a free spot of a matching, unlocked track goes there.
            const auto& tracks = document.project().tracks;
            const auto target = std::find_if(tracks.begin(), tracks.end(), [&](const auto& item) { return item.id == request.track; });
            const auto onTrack = request.track != 0 && target != tracks.end() && !target->locked && target->kind == track.kind && target->canPlace(clip, 0, document.project().tempo());
            const auto trackId = onTrack ? request.track : 0;
            document.edit(asset->audio != nullptr ? "Import soundtrack" : "Import object", [&](motion::Project& project) {
                project.assets.push_back(asset);
                const auto existing = std::find_if(project.tracks.begin(), project.tracks.end(), [&](const auto& item) { return item.id == trackId; });
                if (trackId != 0 && existing != project.tracks.end()) { existing->insert(clip, project.tempo()); } else { project.tracks.push_back(track); }
                project.duration = std::max(project.duration, clip.timing(project.tempo()).end());
            });
            owner->assetLibrary.refresh();
            owner->assetLibrary.selectAsset(asset->id);
            owner->select(clip.id);
        });
    });
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
    // A moving playhead repaints only its own columns of the time views. A
    // slow full refresh (about 3 Hz) still catches anything that changes
    // without an edit or a playhead move, and live inputs (armed MIDI
    // tracks, Blender capture) repaint everything while stopped too.
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
    const auto refresh = live || slowTick;
    const auto moved = position != lastPaintedPosition || playing;
    lastPaintedPosition = position;
    timeline.followPlayhead(position, playing);
    if (curveEditor.isVisible() && timeline.followEnabled) { curveEditor.followPlayhead(position, playing); }
    if (refresh) {
        timeline.repaint();
        if (notesEditor.isVisible()) { notesEditor.repaint(); }
        if (curveEditor.isVisible()) { curveEditor.repaint(); }
    } else if (moved) {
        timeline.repaintPlayhead();
        if (notesEditor.isVisible()) { notesEditor.repaintPlayhead(); }
        if (curveEditor.isVisible()) { curveEditor.repaintPlayhead(); }
    }
    if (refresh || moved) { composition.repaint(); }
    if (moved || slowTick) {
        refreshInspector();
        refreshCameraTools();
        effectStack.updateValues();
    }
}

void MotionEditor::changeListenerCallback(juce::ChangeBroadcaster* source) {
    // The audio device decides whether the 5-channel output can be chosen.
    if (source != &processor.document) {
        refreshOutputChoices();
        return;
    }
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
    changeListenerCallback(nullptr);
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
    changeListenerCallback(nullptr);
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

// Draw a new source, or edit a drawn one (`asset`). The drawing is saved as
// SVG in a temporary folder and imported like any file, relinking an edit.
void MotionEditor::showDrawingEditor(motion::Id asset) {
    if (drawingEditor != nullptr || textEditor != nullptr || luaEditor != nullptr) { return; }
    motion::drawing::Drawing initial;
    juce::String name;
    const auto& assets = processor.document.mainProject().assets;
    if (asset != 0) {
        // Only an existing drawing opens; anything else would relink to nothing.
        const auto found = std::find_if(assets.begin(), assets.end(), [asset](const auto& item) { return item->id == asset; });
        if (found == assets.end()) { return; }
        const auto parsed = motion::drawing::fromSvg(juce::String::fromUTF8(static_cast<const char*>((*found)->data.getData()), static_cast<int>((*found)->data.getSize())));
        if (!parsed.has_value()) { return; }
        initial = *parsed;
        name = (*found)->name.upToLastOccurrenceOf(".", false, false);
    } else {
        const auto drawings = std::count_if(assets.begin(), assets.end(), [](const auto& item) { return item->name.startsWith("Drawing"); });
        name = "Drawing " + juce::String(static_cast<int>(drawings) + 1);
    }
    // The drawing takes over the Scene; the Scope shows it live as a beam.
    drawingAsset = asset;
    drawingGeneration = processor.document.generation();
    drawingEditor = std::make_unique<MotionDrawingEditor>(initial, name, asset != 0);
    addAndMakeVisible(*drawingEditor);
    drawingEditor->onChanged = [this] { previewDrawing(); };
    drawingEditor->onCancel = [this] { juce::MessageManager::callAsync([owner = juce::Component::SafePointer<MotionEditor>(this)] { if (owner != nullptr) { owner->closeDrawingEditor(); } }); };
    drawingEditor->onDone = [this](const motion::drawing::Drawing& drawing, const juce::String& text) {
        const auto folder = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("osci-motion drawings");
        const auto file = folder.getChildFile(juce::File::createLegalFileName(text) + ".svg");
        if (!folder.createDirectory().wasOk() || !file.replaceWithText(motion::drawing::toSvg(drawing))) {
            statusBar.show("Could not save the drawing.");
            return;
        }
        const auto asset = drawingAsset;
        juce::MessageManager::callAsync([owner = juce::Component::SafePointer<MotionEditor>(this), file, asset] {
            if (owner == nullptr) { return; }
            owner->closeDrawingEditor();
            owner->importSourceFile(file, asset);
        });
    };
    resized();
    previewDrawing();
    drawingEditor->grabKeyboardFocus();
}

void MotionEditor::showTextEditor(SourceRequest request) {
    if (request.replacement == nullptr || textEditor != nullptr || drawingEditor != nullptr || luaEditor != nullptr) { return; }
    const auto& asset = *request.replacement;
    const auto draft = request.editedText.value_or(juce::String::fromUTF8(static_cast<const char*>(asset.data.getData()), static_cast<int>(asset.data.getSize())));
    const auto settings = request.textSettings.value_or(asset.textSettings);
    textRequest = request;
    textGeneration = processor.document.generation();
    // Other queued sources wait until the text is saved or cancelled.
    preparationSettingsOpen = true;
    textEditor = std::make_unique<MotionTextSourceEditor>(draft, asset.name.upToLastOccurrenceOf(".", false, false), settings, request.preparationError);
    addAndMakeVisible(*textEditor);
    textEditor->onChanged = [this] { textPreviewDue = juce::Time::getMillisecondCounterHiRes() + 120; };
    textEditor->onCancel = [this] { juce::MessageManager::callAsync([owner = juce::Component::SafePointer<MotionEditor>(this)] { if (owner != nullptr) { owner->closeTextEditor(); } }); };
    textEditor->onDone = [this](const juce::String& text, const motion::TextSettings& chosen) {
        juce::MessageManager::callAsync([owner = juce::Component::SafePointer<MotionEditor>(this), editor = textEditor.get(), text, chosen] {
            // A second Save before this runs finds the editor already gone.
            if (owner == nullptr || owner->textEditor.get() != editor) { return; }
            auto next = owner->textRequest;
            owner->closeTextEditor();
            if (owner->processor.document.generation() != next.generation) { return; }
            // The source may have changed while the editor was open (its
            // animation, an undo): build on what it is now.
            const auto& assets = owner->processor.document.project().assets;
            const auto id = next.replacement->id;
            const auto found = std::find_if(assets.begin(), assets.end(), [id](const auto& asset) { return asset->id == id; });
            if (found == assets.end()) { owner->statusBar.show("The text source was removed while it was being edited."); return; }
            auto settings = (*found)->textSettings;
            settings.family = chosen.family;
            settings.style = chosen.style;
            settings.alignment = chosen.alignment;
            settings.lineSpacing = chosen.lineSpacing;
            settings.tracking = chosen.tracking;
            next.replacement = *found;
            next.editedText = text;
            next.textSettings = settings;
            next.preparationError.clear();
            owner->beginSourceImport(next);
        });
    };
    resized();
    previewText();
    textEditor->focusText();
}

void MotionEditor::showLuaEditor(SourceRequest request) {
    if (luaEditor != nullptr || textEditor != nullptr || drawingEditor != nullptr) { return; }
    const auto editing = request.replacement != nullptr;
    const auto code = editing ? request.editedText.value_or(juce::String::fromUTF8(static_cast<const char*>(request.replacement->data.getData()), static_cast<int>(request.replacement->data.getSize())))
                              : request.file.loadFileAsString();
    motion::BakeSettings initial;
    initial.bpm = processor.document.project().bpm;
    initial.frameRate = processor.document.project().frameRate;
    if (editing) { initial = request.replacement->bakeSettings; }
    initial = request.retrySettings.value_or(initial);
    const auto title = (editing ? request.replacement->name : request.file.getFileName()).upToLastOccurrenceOf(".", false, false);
    luaRequest = request;
    luaGeneration = processor.document.generation();
    luaSubmitted = false;
    preparationSettingsOpen = true;
    luaEditor = std::make_unique<MotionLuaSourceEditor>(code, title, initial, editing, request.preparationError);
    addAndMakeVisible(*luaEditor);
    luaEditor->onCancel = [this] { juce::MessageManager::callAsync([owner = juce::Component::SafePointer<MotionEditor>(this)] { if (owner != nullptr) { owner->closeLuaEditor(); } }); };
    luaEditor->onDone = [this, original = code](motion::BakeSettings settings, const juce::String& written) {
        if (luaSubmitted) { return; }
        auto next = luaRequest;
        next.preparationError.clear();
        if (next.replacement != nullptr) {
            next.editedText = written == original ? std::optional<juce::String>() : std::optional<juce::String>(written);
        } else if (written != original) {
            // A new file edited before it is added imports from a copy.
            const auto folder = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("osci-motion lua").getChildFile(juce::Uuid().toString());
            const auto copy = folder.getChildFile(next.file.getFileName());
            if (!folder.createDirectory().wasOk() || !copy.replaceWithText(written)) {
                statusBar.show("Could not save the edited script.");
                return;
            }
            next.file = copy;
        }
        luaSubmitted = true;
        juce::MessageManager::callAsync([owner = juce::Component::SafePointer<MotionEditor>(this), next, settings] {
            if (owner == nullptr) { return; }
            owner->closeLuaEditor();
            if (owner->processor.document.generation() == next.generation) { owner->beginSourceImport(next, settings); }
        });
    };
    resized();
    luaEditor->focusCode();
}

void MotionEditor::closeLuaEditor() {
    if (luaEditor == nullptr) { return; }
    removeChildComponent(luaEditor.get());
    luaEditor.reset();
    for (auto* component : std::initializer_list<juce::Component*> {&composition, &sceneView, &viewportHeader}) { component->setVisible(true); }
    preparationSettingsOpen = false;
    resized();
    showNextPreparationSettings();
}

void MotionEditor::closeTextEditor() {
    if (textEditor == nullptr) { return; }
    removeChildComponent(textEditor.get());
    textEditor.reset();
    textPreviewDue = 0;
    for (auto* component : std::initializer_list<juce::Component*> {&composition, &sceneView, &viewportHeader}) { component->setVisible(true); }
    processor.previewComposition(processor.document.project());
    preparationSettingsOpen = false;
    resized();
    showNextPreparationSettings();
}

// The words being written, on the output: the source is swapped in place,
// still, so typing stays quick; its animation plays once saved.
void MotionEditor::previewText() {
    textPreviewDue = 0;
    if (textEditor == nullptr || textRequest.replacement == nullptr) { return; }
    auto project = processor.document.project();
    auto asset = std::make_shared<motion::Asset>(*textRequest.replacement);
    const auto text = textEditor->currentText();
    asset->data.reset();
    asset->data.append(text.toRawUTF8(), text.getNumBytesAsUTF8());
    asset->textSettings = textEditor->currentSettings();
    asset->textSettings.animation = motion::TextSettings::Animation::none;
    if (text.trim().isEmpty() || text.length() > 16384 || motion::decodeAsset(*asset).failed()) {
        processor.previewComposition(project);
        return;
    }
    for (auto& item : project.assets) {
        if (item->id == asset->id) { item = asset; }
    }
    processor.previewComposition(project);
}

void MotionEditor::closeDrawingEditor() {
    if (drawingEditor == nullptr) { return; }
    removeChildComponent(drawingEditor.get());
    drawingEditor.reset();
    for (auto* component : std::initializer_list<juce::Component*> {&composition, &sceneView, &viewportHeader}) { component->setVisible(true); }
    processor.previewComposition(processor.document.project());
    resized();
    showNextPreparationSettings();
}

// The drawing in progress on the output: an edited source is swapped in place;
// a new one plays on a track of its own for the whole project.
void MotionEditor::previewDrawing() {
    if (drawingEditor == nullptr) { return; }
    auto project = processor.document.project();
    auto asset = std::make_shared<motion::Asset>();
    asset->id = drawingAsset != 0 ? drawingAsset : std::numeric_limits<motion::Id>::max() - 1;
    asset->name = "Drawing.svg";
    asset->extension = ".svg";
    const auto svg = motion::drawing::toSvg(drawingEditor->current());
    asset->data.append(svg.toRawUTF8(), svg.getNumBytesAsUTF8());
    if (drawingEditor->current().empty() || motion::decodeAsset(*asset).failed()) {
        processor.previewComposition(project);
        return;
    }
    if (drawingAsset != 0) {
        for (auto& item : project.assets) {
            if (item->id == drawingAsset) { item = asset; }
        }
    } else {
        project.assets.push_back(asset);
        motion::Track track;
        track.id = std::numeric_limits<motion::Id>::max() - 2;
        track.name = "Drawing";
        auto clip = motion::Document::makeClip(std::numeric_limits<motion::Id>::max() - 3, *asset, 0);
        clip.duration = project.duration;
        track.insert(clip, project.tempo());
        project.tracks.push_back(track);
    }
    processor.previewComposition(project);
}

// Examples are written to a temporary folder and imported like any file;
// the project keeps its own copy of the data.
void MotionEditor::importExample(const juce::String& resource) {
    int size = 0;
    const auto* data = BinaryData::getNamedResource(resource.toRawUTF8(), size);
    const juce::String name = BinaryData::getNamedResourceOriginalFilename(resource.toRawUTF8());
    if (data == nullptr || name.isEmpty()) { return; }
    const auto folder = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("osci-motion examples");
    const auto file = folder.getChildFile(name);
    if (!folder.createDirectory().wasOk() || !file.replaceWithData(data, static_cast<std::size_t>(size))) {
        statusBar.show("Could not prepare the example " + name + ".");
        return;
    }
    importSourceFile(file, 0);
}

// The project with an effect of `type` added to `owner`, on the output.
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

// Menu 0 (File) is built by buildFileMenu, so its commands only bind keys.
void MotionEditor::addCommand(int menu, juce::String name, juce::KeyPress key, juce::String shortcut, std::function<void()> action) {
    if (menu != 0) { menus.addMenuItem(menu, name, action, motion::style::shortcutText(shortcut)); }
    commands.push_back({menu, std::move(name), std::move(shortcut), key, std::move(action)});
}

void MotionEditor::buildFileMenu(juce::PopupMenu& menu) {
    menu.addItem(motion::style::menuItem("New project", 1001, "Cmd+N"));
    menu.addItem(motion::style::menuItem("Open project...", 1002, "Cmd+O"));
    menus.addRecentProjectsSubmenu(menu, processor, 1100, 1099);
    menu.addSeparator();
    menu.addItem(motion::style::menuItem("Save project", 1003, "Cmd+S"));
    menu.addItem(motion::style::menuItem("Save project as...", 1004, "Cmd+Shift+S"));
    menu.addSeparator();
    menu.addItem(motion::style::menuItem("Import source...", 1005, "Cmd+I"));
    menu.addSeparator();
    menu.addItem(motion::style::menuItem("Export XYRGB signal...", 1006, {}));
#if OSCI_PREMIUM
    menu.addItem(motion::style::menuItem("Export video...", 1007, {}));
#endif
}

bool MotionEditor::fileMenuItemSelected(int id) {
    if (menus.handleRecentProjectMenuItem(id, processor, *this, 1100, 1099)) { return true; }
    switch (id) {
        case 1001: resetToDefault(); return true;
        case 1002: CommonPluginEditor::openProject(); return true;
        case 1003: saveProject(); return true;
        case 1004: saveProjectAs(); return true;
        case 1005: chooseSourceFile(); return true;
        case 1006: exportSignal(); return true;
        case 1007: exportVideo(); return true;
        default: return false;
    }
}

void MotionEditor::registerCommands() {
    const auto command = juce::ModifierKeys::commandModifier;
    const auto shift = juce::ModifierKeys::shiftModifier;
    menus.addMenuSeparator(1);
    addCommand(1, "Cut", juce::KeyPress('x', command, 0), "Cmd+X", [this] { copySelection(true); });
    addCommand(1, "Copy", juce::KeyPress('c', command, 0), "Cmd+C", [this] { copySelection(false); });
    addCommand(1, "Paste", juce::KeyPress('v', command, 0), "Cmd+V", [this] { pasteClipboard(); });
    addCommand(1, "Delete", juce::KeyPress(), "Delete", [this] { timeline.deleteSelection(); });
    menus.addMenuSeparator(1);
    addCommand(1, "Select all", juce::KeyPress('a', command, 0), "Cmd+A", [this] {
        if (timelineTabs.getCurrentTabIndex() == 1) { curveEditor.selectAllKeys(); } else { timeline.selectAll(); }
    });
    // After Effects' Easy Ease, on the selected keys of the visible editor.
    const auto ease = [this](bool in, bool out) {
        const auto graph = timelineTabs.getCurrentTabIndex() == 1;
        const auto selected = graph ? curveEditor.hasSelectedKeys() : timeline.hasSelectedKeys();
        if (!selected) {
            statusBar.show("Select keyframes to ease.");
            return;
        }
        // Smooth keys already ease at their ends, so say what happened.
        const auto eased = graph ? curveEditor.easeSelected(in, out) : timeline.easeSelectedKeys(in, out);
        const juce::String what = in && out ? "Easy ease" : (in ? "Easy ease in" : "Easy ease out");
        statusBar.show(eased ? what + " applied: the keys stop with a third of each segment as influence." : "The selected keys already ease that way.", MotionStatusBar::Kind::notice);
    };
    menus.addMenuSeparator(1);
    addCommand(1, "Easy ease", juce::KeyPress(juce::KeyPress::F9Key), "F9", [ease] { ease(true, true); });
    addCommand(1, "Easy ease in", juce::KeyPress(juce::KeyPress::F9Key, shift, 0), "Shift+F9", [ease] { ease(true, false); });
    addCommand(1, "Easy ease out", juce::KeyPress(juce::KeyPress::F9Key, command | shift, 0), "Cmd+Shift+F9", [ease] { ease(false, true); });
    addCommand(2, "Split at playhead", juce::KeyPress('k', command, 0), "Cmd+K", [this] { splitAtPlayhead(); });
    addCommand(2, "Duplicate", juce::KeyPress('d', command, 0), "Cmd+D", [this] {
        std::vector<motion::Id> duplicates;
        const auto clips = timeline.copiedClips();
        std::vector<motion::Id> ids;
        for (const auto& copied : clips) { ids.push_back(copied.clip.id); }
        if (ids.empty()) { return; }
        const auto result = processor.document.duplicateClips(ids, duplicates);
        if (result.failed()) { osci::showOverlayMessage(*this, "Cannot duplicate", result.getErrorMessage()); return; }
        timeline.selectClips(duplicates);
    });
    menus.addMenuSeparator(2);
    // After Effects conventions: [ and ] move the clip's start or end to the
    // playhead; Alt+[ and Alt+] trim it there.
    addCommand(2, "Move clip start to playhead", juce::KeyPress('[', 0, 0), "[", [this] { placeClipAtPlayhead(true, false); });
    addCommand(2, "Move clip end to playhead", juce::KeyPress(']', 0, 0), "]", [this] { placeClipAtPlayhead(false, false); });
    addCommand(2, "Trim clip start to playhead", juce::KeyPress('[', juce::ModifierKeys::altModifier, 0), "Alt+[", [this] { placeClipAtPlayhead(true, true); });
    addCommand(2, "Trim clip end to playhead", juce::KeyPress(']', juce::ModifierKeys::altModifier, 0), "Alt+]", [this] { placeClipAtPlayhead(false, true); });
    addCommand(2, "Show keyframe lanes", juce::KeyPress('u', 0, 0), "U", [this] { timeline.toggleLanesForSelection(); });
    menus.addMenuSeparator(2);
    const auto alt = juce::ModifierKeys::altModifier;
    for (const auto& [letter, group, name] : std::initializer_list<std::tuple<char, const char*, const char*>> {
             {'p', "Position", "Key position"}, {'r', "Rotation", "Key rotation"}, {'s', "Scale", "Key scale"}, {'c', "Colour", "Key colour"}, {'t', "Drawing", "Key drawing weight"}}) {
        const juce::String groupName(group);
        addCommand(2, name, juce::KeyPress(letter, alt | shift, 0), "Alt+Shift+" + juce::String::charToString(letter).toUpperCase(), [this, groupName] {
            if (!propertyInspector.toggleGroupKeys(groupName)) { statusBar.show("Select a clip or camera with " + groupName.toLowerCase() + " to key it."); }
        });
    }

    addCommand(3, "Play / pause", juce::KeyPress(juce::KeyPress::spaceKey), "Space", [this] { processor.playing.store(!processor.playing.load()); });
    addCommand(3, "Go to start", juce::KeyPress(juce::KeyPress::homeKey), "Home", [this] { processor.seek(0); timeline.revealTime(0); });
    addCommand(3, "Go to end", juce::KeyPress(juce::KeyPress::endKey), "End", [this] {
        const auto end = processor.document.project().duration;
        processor.seek(end); timeline.revealTime(end);
    });
    addCommand(3, "Record armed track", juce::KeyPress('r', shift, 0), "Shift+R", [this] { recordArmedTrack(); });
    addCommand(3, "Previous frame", juce::KeyPress(juce::KeyPress::leftKey), "Left", [this] { stepFrames(-1); });
    addCommand(3, "Next frame", juce::KeyPress(juce::KeyPress::rightKey), "Right", [this] { stepFrames(1); });
    addCommand(3, "Back ten frames", juce::KeyPress(juce::KeyPress::leftKey, shift, 0), "Shift+Left", [this] { stepFrames(-10); });
    addCommand(3, "Forward ten frames", juce::KeyPress(juce::KeyPress::rightKey, shift, 0), "Shift+Right", [this] { stepFrames(10); });
    addCommand(3, "Previous key or marker", juce::KeyPress('j', 0, 0), "J", [this] { jumpToKey(false); });
    addCommand(3, "Next key or marker", juce::KeyPress('k', 0, 0), "K", [this] { jumpToKey(true); });
    addCommand(3, "Previous edit point", juce::KeyPress(juce::KeyPress::upKey), "Up", [this] { jumpToEdit(false); });
    addCommand(3, "Next edit point", juce::KeyPress(juce::KeyPress::downKey), "Down", [this] { jumpToEdit(true); });
    menus.addMenuSeparator(3);
    addCommand(3, "Loop playback", juce::KeyPress('l', 0, 0), "L", [this] { toggleLoop(); });
    addCommand(3, "Set loop start at playhead", juce::KeyPress('i', 0, 0), "I", [this] { setLoopEdge(true); });
    addCommand(3, "Set loop end at playhead", juce::KeyPress('o', 0, 0), "O", [this] { setLoopEdge(false); });
    addCommand(3, "Loop selected clips", juce::KeyPress('l', shift, 0), "Shift+L", [this] { loopSelection(); });
    // View: what the lower panel shows, zoom, follow and track height.
    menus.addMenuSeparator(5);
    addCommand(5, "Show timeline", juce::KeyPress('1', juce::ModifierKeys::altModifier, 0), "Alt+1", [this] { timelineTabs.setSelectedIndex(0); });
    addCommand(5, "Show graph", juce::KeyPress('2', juce::ModifierKeys::altModifier, 0), "Alt+2", [this] { timelineTabs.setSelectedIndex(1); });
    addCommand(5, "Show notes", juce::KeyPress('3', juce::ModifierKeys::altModifier, 0), "Alt+3", [this] { timelineTabs.setSelectedIndex(2); });
    menus.addMenuSeparator(5);
    // Zoom acts on whichever lower panel is showing.
    const auto zoom = [this](double factor) {
        if (curveEditor.isVisible()) { curveEditor.zoomTime(curveEditor.getWidth() * .5f, factor); curveEditor.repaint(); return; }
        if (notesEditor.isVisible()) { notesEditor.zoomAround(notesEditor.getWidth() / 2, factor); notesEditor.repaint(); return; }
        timeline.zoomBy(factor);
    };
    addCommand(5, "Zoom in", juce::KeyPress('=', command, 0), "Cmd+=", [zoom] { zoom(1.5); });
    addCommand(5, "Zoom out", juce::KeyPress('-', command, 0), "Cmd+-", [zoom] { zoom(1 / 1.5); });
    addCommand(5, "Fit timeline to project", juce::KeyPress(), "F", [this] { timeline.fitToProject(); });
    addCommand(5, "Taller tracks", juce::KeyPress('=', command | shift, 0), "Cmd+Shift+=", [this] { timeline.setDefaultTrackHeight(timeline.defaultTrackHeight + 8); });
    addCommand(5, "Shorter tracks", juce::KeyPress('-', command | shift, 0), "Cmd+Shift+-", [this] { timeline.setDefaultTrackHeight(timeline.defaultTrackHeight - 8); });
    menus.addToggleMenuItem(5, "Follow playhead", [this] { timeline.followEnabled = !timeline.followEnabled; saveLayout(); }, [this] { return timeline.followEnabled; });
    menus.addMenuSeparator(5);
   #if JUCE_MAC
    const juce::String fullScreenKeys = "Ctrl+Cmd+F";
    commands.push_back({5, "Full screen", fullScreenKeys, juce::KeyPress('f', command | juce::ModifierKeys::ctrlModifier, 0), [this] { toggleFullScreen(); }});
   #else
    const juce::String fullScreenKeys = "F11";
   #endif
    menus.addToggleMenuItem(5, "Full screen", [this] { toggleFullScreen(); }, [this] { return isFullScreen(); }, motion::style::shortcutText(fullScreenKeys));
    menus.addMenuItem(5, "Reset window size and position", [this] { resetWindowSizeAndPosition(); });
    menus.addMenuSeparator(5);
    addCommand(5, "Keyboard shortcuts...", juce::KeyPress('/', command, 0), "Cmd+/", [this] { showShortcuts(); });
    // File items live in the File menu built by buildFileMenu; these bind keys.
    addCommand(0, "Import source...", juce::KeyPress('i', command, 0), "Cmd+I", [this] { chooseSourceFile(); });
}

void MotionEditor::copySelection(bool cut) {
    if (timeline.hasSelectedKeys()) {
        clipboard = timeline.copiedKeys();
    } else if (timeline.hasSelectedClips()) {
        clipboard = timeline.copiedClips();
    } else {
        return;
    }
    if (cut) { timeline.deleteSelection(); }
}

void MotionEditor::pasteClipboard() {
    const auto time = processor.position.load();
    if (const auto* keys = std::get_if<std::vector<motion::Document::CopiedKey>>(&clipboard)) {
        const auto result = processor.document.pasteKeys(selection, *keys, time);
        if (result.failed()) { osci::showOverlayMessage(*this, "Cannot paste keyframes", result.getErrorMessage()); }
        return;
    }
    if (const auto* clips = std::get_if<std::vector<motion::Document::CopiedClip>>(&clipboard)) {
        std::vector<motion::Id> pasted;
        const auto result = processor.document.pasteClips(*clips, time, pasted);
        if (result.failed()) { osci::showOverlayMessage(*this, "Cannot paste clips", result.getErrorMessage()); return; }
        timeline.refreshTracks();
        timeline.selectClips(pasted);
    }
}

void MotionEditor::stepFrames(int frames) {
    const auto& project = processor.document.project();
    const auto rate = project.frameRate > 0 ? project.frameRate : 30.0;
    const auto current = std::round(processor.position.load() * rate);
    const auto next = std::clamp((current + frames) / rate, 0.0, project.duration);
    processor.playing.store(false);
    processor.seek(next);
    timeline.revealTime(next);
}

// The Views button's menu; right-clicking the Scene adds keying for the
// selection.
void MotionEditor::showSceneViewMenu(bool atMouse) {
    juce::PopupMenu menu;
    menu.setLookAndFeel(&getLookAndFeel());
    const std::array<std::pair<const char*, const char*>, 8> items {{{"Front", "1"}, {"Right", "3"}, {"Top", "7"}, {"Back", "Ctrl+1"}, {"Left", "Ctrl+3"},
        {"Bottom", "Ctrl+7"}, {"Frame selection", "F"}, {"Reset view", "0"}}};
    juce::PopupMenu views;
    for (int index = 0; index < 6; ++index) { views.addItem(motion::style::menuItem(items[static_cast<std::size_t>(index)].first, index + 1, items[static_cast<std::size_t>(index)].second)); }
    if (atMouse) {
        menu.addItem(motion::style::menuItem("Frame selection", 7, "F"));
        menu.addItem(motion::style::menuItem("Reset view", 8, "0"));
        menu.addSubMenu("View from", views);
        if (selection != 0) {
            menu.addSeparator();
            const std::array<std::pair<const char*, const char*>, 5> keys {{{"Key position", "Alt+Shift+P"}, {"Key rotation", "Alt+Shift+R"}, {"Key scale", "Alt+Shift+S"}, {"Key colour", "Alt+Shift+C"}, {"Key drawing weight", "Alt+Shift+T"}}};
            for (int index = 0; index < 5; ++index) { menu.addItem(motion::style::menuItem(keys[static_cast<std::size_t>(index)].first, 20 + index, keys[static_cast<std::size_t>(index)].second)); }
            menu.addSeparator();
            menu.addItem(motion::style::menuItem("Show in timeline", 30, {}));
        }
    } else {
        for (int index = 0; index < 6; ++index) { menu.addItem(motion::style::menuItem(items[static_cast<std::size_t>(index)].first, index + 1, items[static_cast<std::size_t>(index)].second)); }
        menu.addSeparator();
        menu.addItem(motion::style::menuItem("Frame selection", 7, "F"));
        menu.addItem(motion::style::menuItem("Reset view", 8, "0"));
    }
    const juce::Component::SafePointer<MotionEditor> owner(this);
    const auto options = atMouse ? juce::PopupMenu::Options().withTargetComponent(composition).withMousePosition() : juce::PopupMenu::Options().withTargetComponent(sceneView);
    menu.showMenuAsync(options, [owner](int result) {
        if (owner == nullptr || result == 0) { return; }
        using Preset = MotionCompositionView::ViewPreset;
        const std::array<Preset, 6> presets {Preset::front, Preset::right, Preset::top, Preset::back, Preset::left, Preset::bottom};
        if (result >= 1 && result <= 6) { owner->composition.setViewPreset(presets[static_cast<std::size_t>(result - 1)]); }
        if (result == 7) { owner->composition.frameSelection(); }
        if (result == 8) { owner->composition.resetView(); }
        const std::array<const char*, 5> groups {"Position", "Rotation", "Scale", "Colour", "Drawing"};
        if (result >= 20 && result < 25 && !owner->propertyInspector.toggleGroupKeys(groups[static_cast<std::size_t>(result - 20)])) {
            owner->statusBar.show("The selection has no such property to key.");
        }
        if (result == 30) { owner->timelineTabs.setSelectedIndex(0); owner->timeline.revealSelection(); }
    });
}

// L toggles looping. Without a range yet, it loops the selection, or the
// four bars from the playhead's bar.
void MotionEditor::toggleLoop() {
    const auto& project = processor.document.project();
    if (!project.hasLoop()) {
        if (timeline.hasSelectedClips()) { loopSelection(); return; }
        const auto tempo = project.tempo();
        const auto bar = std::max(1, project.beatsPerBar);
        // Near the end, the four bars end at the end instead.
        const auto last = std::max(0.0, tempo.beats(project.duration) - 4 * bar);
        const auto first = std::min(std::floor(tempo.beats(processor.position.load()) / bar) * bar, last);
        if (!setLoop(tempo.seconds(first), std::min(project.duration, tempo.seconds(first + 4 * bar)), true, "Set loop")) {
            statusBar.show("The composition is too short to loop.");
        }
        return;
    }
    setLoop(project.loopStart, project.loopEnd, !project.looping, project.looping ? "Stop looping" : "Loop playback");
}

void MotionEditor::setLoopEdge(bool start) {
    const auto& project = processor.document.project();
    const auto time = std::clamp(processor.position.load(), 0.0, project.duration);
    const auto first = start ? time : (project.hasLoop() ? project.loopStart : 0.0);
    const auto last = start ? (project.hasLoop() && project.loopEnd > time ? project.loopEnd : project.duration) : time;
    if (last - first < 1 / std::max(1.0, project.frameRate)) { statusBar.show(start ? "The loop start must be at least a frame before its end." : "The loop end must be at least a frame after its start."); return; }
    // Like a DAW's loop markers, I and O move the range; L switches looping.
    setLoop(first, last, project.looping, start ? "Set loop start" : "Set loop end");
}

void MotionEditor::loopSelection() {
    const auto& project = processor.document.project();
    double first = std::numeric_limits<double>::max(), last = 0;
    for (const auto& track : project.tracks) {
        for (const auto& clip : track.clips) {
            if (!timeline.selectedClipIds().contains(clip.id)) { continue; }
            const auto timing = clip.timing(project.tempo());
            first = std::min(first, timing.start);
            last = std::max(last, timing.end());
        }
    }
    if (last <= first) { statusBar.show("Select clips to loop."); return; }
    setLoop(first, last, true, "Loop selection");
}

// False when the range is shorter than a frame; an unchanged loop is fine.
bool MotionEditor::setLoop(double start, double end, bool enabled, juce::String label) {
    const auto& current = processor.document.project();
    const auto frame = current.frameRate > 0 ? 1 / current.frameRate : 1.0 / 30;
    if (!(end - start >= frame) || start < 0) { return false; }
    if (current.loopStart == start && current.loopEnd == end && current.looping == enabled) { return true; }
    // Switching looping on or off is transport state, not an edit.
    if (current.loopStart == start && current.loopEnd == end) {
        processor.document.changeView([enabled](motion::Composition& state) { state.looping = enabled; });
        return true;
    }
    const motion::Document::ViewChange view(processor.document);
    processor.document.edit(label, [start, end, enabled](motion::Project& state) {
        state.loopStart = start; state.loopEnd = end; state.looping = enabled;
    });
    return true;
}

// Up/Down, like Premiere: the previous or next clip start or end.
void MotionEditor::jumpToEdit(bool forward) {
    const auto now = processor.position.load();
    std::optional<double> best;
    for (const auto time : timeline.editPoints()) {
        const bool candidate = forward ? time > now + 1.0e-6 : time < now - 1.0e-6;
        if (candidate && (!best.has_value() || (forward ? time < *best : time > *best))) { best = time; }
    }
    if (!best.has_value()) { return; }
    const auto time = std::clamp(*best, 0.0, processor.document.project().duration);
    processor.seek(time);
    timeline.revealTime(time);
}

// J/K, like After Effects: the previous or next visible item - a key of the
// selection, a marker or a tempo change.
void MotionEditor::jumpToKey(bool forward) {
    const auto& project = processor.document.project();
    const auto now = processor.position.load();
    std::optional<double> best;
    const auto consider = [&](double time) {
        const bool candidate = forward ? time > now + 1.0e-6 : time < now - 1.0e-6;
        if (candidate && (!best.has_value() || (forward ? time < *best : time > *best))) { best = time; }
    };
    const auto target = motion::findPropertyTarget(project, selection);
    if (target.has_value() && target->properties != nullptr && target->rate != 0) {
        for (const auto& [name, curve] : *target->properties) {
            for (const auto& key : curve.keyframes()) { consider(target->projectTime(key.time)); }
        }
    }
    for (const auto& marker : project.markers) { consider(marker.time); }
    if (project.tempoChanges != nullptr) {
        const auto tempo = project.tempo();
        for (const auto& change : *project.tempoChanges) { consider(tempo.seconds(change.beat)); }
    }
    if (!best.has_value()) { return; }
    const auto time = std::clamp(*best, 0.0, processor.document.project().duration);
    processor.seek(time);
    timeline.revealTime(time);
}

// Records into the first armed track's clip under the playhead (or the next
// clip after it), through the same take pipeline as the Notes editor.
void MotionEditor::recordArmedTrack() {
    auto& session = processor.midiRecordingSession();
    if (session.busy()) {
        session.stop();
        return;
    }
    const auto& project = processor.document.project();
    const auto tempo = project.tempo();
    const auto time = processor.position.load();
    for (const auto& track : project.tracks) {
        if (track.midiInput == 0 || track.kind != motion::TrackKind::visual) { continue; }
        const motion::Clip* target = nullptr;
        for (const auto& clip : track.clips) {
            const auto timing = clip.timing(tempo);
            if (clip.composition != 0 || timing.end() <= time) { continue; }
            if (target == nullptr || timing.start < target->timing(tempo).start) { target = &clip; }
        }
        if (target == nullptr) { continue; }
        const auto started = session.start(target->id);
        if (started.failed()) { statusBar.show(started.getErrorMessage()); } else { statusBar.show("Recording into " + juce::String(target->name) + ". Shift+R stops.", MotionStatusBar::Kind::notice); }
        return;
    }
    const auto armed = std::any_of(project.tracks.begin(), project.tracks.end(), [](const auto& track) { return track.midiInput != 0; });
    statusBar.show(armed ? "No armed track has a clip at or after the playhead to record into." : "Arm a track for MIDI input first (the red dot in its header).");
}

void MotionEditor::placeClipAtPlayhead(bool start, bool trim) {
    // Like After Effects, [ ] and Alt+[ ] act on every selected clip at once.
    const auto& project = processor.document.project();
    const auto tempo = project.tempo();
    const auto time = processor.position.load();
    auto ids = timeline.selectedClipIds();
    if (ids.empty() && selection != 0) { ids.insert(selection); }
    std::vector<std::pair<motion::Id, motion::ClipTiming>> timings;
    int skipped = 0;
    for (const auto& track : project.tracks) {
        for (const auto& clip : track.clips) {
            if (!ids.contains(clip.id)) { continue; }
            auto timing = clip.timing(tempo);
            if (trim) {
                if (start) {
                    if (time >= timing.end()) { ++skipped; continue; }
                    timing.offset = timing.localTime(time);
                    timing.setStart(time);
                } else {
                    if (time <= timing.start) { ++skipped; continue; }
                    timing.setEnd(time);
                }
            } else {
                const auto target = start ? time : time - timing.duration();
                if (target < 0) { ++skipped; continue; }
                timing.moveTo(target);
            }
            timings.emplace_back(clip.id, timing);
        }
    }
    if (timings.empty()) {
        if (skipped > 0) { statusBar.show(trim ? "The playhead is outside the selected clips." : "The selected clips cannot start before 0."); }
        return;
    }
    const auto result = processor.document.setClipTimings(timings);
    if (result.failed()) {
        statusBar.show(result.getErrorMessage());
    } else if (skipped > 0) {
        statusBar.show(juce::String(skipped) + (skipped == 1 ? " clip was" : " clips were") + " left unchanged.", MotionStatusBar::Kind::notice);
    }
}

void MotionEditor::splitAtPlayhead() {
    const auto time = processor.position.load();
    const auto& tracks = processor.document.project().tracks;
    const bool canSplit = std::any_of(tracks.begin(), tracks.end(), [&](const motion::Track& track) {
        return std::any_of(track.clips.begin(), track.clips.end(), [&](const motion::Clip& clip) {
            return clip.id == selection && canSplitClip(&clip, time, processor.document.project().tempo());
        });
    });
    if (!canSplit) {
        return;
    }
    const auto id = processor.document.newId();
    processor.document.tryEdit("Split clip", [&](motion::Project& project) {
        for (auto& track : project.tracks) {
            for (std::size_t index = 0; index < track.clips.size(); ++index) {
                if (track.clips[index].id != selection) { continue; }
                if (track.locked) { return false; }
                auto parts = track.clips[index].split(time, id, project.tempo());
                if (!parts.has_value()) { return false; }
                // The right half keeps the left's routes and internal links.
                std::map<motion::Id, motion::Id> owners {{parts->first.id, id}};
                for (auto& effect : parts->second.effects) {
                    const auto clone = processor.document.newId();
                    owners.emplace(effect.id, clone);
                    effect.id = clone;
                }
                track.clips[index] = std::move(parts->first);
                if (!track.insert(std::move(parts->second), project.tempo())) { return false; }
                motion::cloneDrivers(project, owners, [this] { return processor.document.newId(); });
                return true;
            }
        }
        return false;
    });
}

void MotionEditor::showShortcuts() {
    using Overlay = MotionShortcutsOverlay;
    std::vector<Overlay::Section> sections;
    const std::array<std::pair<int, const char*>, 5> menuOrder {{{3, "Transport"}, {1, "Edit"}, {2, "Clip and keys"}, {5, "View"}, {0, "File"}}};
    for (const auto& [menu, title] : menuOrder) {
        Overlay::Section section {title, {}};
        if (menu == 0) {
            for (const auto& [keys, action] : std::initializer_list<std::pair<const char*, const char*>> {{"Cmd+N", "New project"}, {"Cmd+O", "Open project"}, {"Cmd+S", "Save project"}, {"Cmd+Shift+S", "Save project as"}}) {
                section.entries.push_back({keys, action});
            }
        }
        if (menu == 1) {
            section.entries.push_back({"Cmd+Z", "Undo"});
            section.entries.push_back({"Cmd+Shift+Z", "Redo"});
        }
        for (const auto& command : commands) {
            if (command.menu == menu && command.shortcut.isNotEmpty()) { section.entries.push_back({command.shortcut, command.name.trimCharactersAtEnd(".")}); }
        }
        sections.push_back(std::move(section));
    }
    sections.push_back({"Timeline", {{"V / B / S / R", "Move, ripple trim, slip and stretch tools"}, {"M", "Add a marker at the playhead"},
        {"Drag empty space", "Select clips (Shift adds)"}, {"Alt while dragging", "Bypass snapping"}, {"Drag a row's bottom edge", "Resize the track (double-click resets)"},
        {"Double-click a lane", "Add a key"}}});
    sections.push_back({"Scrolling and zoom (timeline, graph, notes)", {{"Wheel / two fingers", "Pan (Shift: horizontal)"}, {"Cmd+wheel / pinch", "Zoom time around the pointer"},
        {"Alt+wheel", "Track height, value range or key height"}}});
    sections.push_back({"Graph", {{"Double-click", "Add a key"}, {"Drag a box", "Select keys (Shift adds)"}, {"Drag box edges", "Scale key times"},
        {"F / Shift+F", "Frame all curves / this curve"}, {"Right-click a key", "Interpolation and easing"}, {"Drag the time axis", "Scrub"}}});
    sections.push_back({"Scene", {{"G / R / S", "Move, rotate and scale tools"}, {"F", "Frame the selection"}, {"0", "Reset the view"},
        {"1 / 3 / 7", "Front, right and top views (Ctrl: opposite side)"}, {"N", "Fly through the scene"}, {"P", "Show the motion path"},
        {"Two fingers / wheel", "Orbit / zoom"}, {"Shift + two fingers", "Pan"}, {"Alt+drag", "Orbit with the mouse"}}});
    showOverlay(std::make_unique<Overlay>(std::move(sections)));
}

void MotionEditor::exportVideo() {
#if OSCI_PREMIUM
    if (exportState != nullptr) { return; }
    if (!processor.ensureFFmpegExists()) { return; }
    std::shared_ptr<OfflineVisualiserParameters> beamSnapshot;
    try {
        beamSnapshot = std::make_shared<OfflineVisualiserParameters>(processor.visualiserParameters, OfflineVisualiserParameters::ExternalModulation::replace);
    } catch (const std::exception& error) {
        statusBar.show("Cannot capture the beam settings: " + juce::String(error.what()));
        return;
    }
    auto config = recordingSettings.createVideoEncodingConfiguration();
    const auto project = processor.document.mainProject();
    config.frameRate = project.frameRate;
    const auto renderMode = visualiser.getRenderMode();
    auto state = std::make_shared<ExportState>();
    state->sampleRate = processor.exportSampleRate();
    exportState = state;
    const juce::Component::SafePointer<MotionEditor> owner(this);
    auto settings = std::make_unique<MotionVideoExportSettings>(config);
    auto* settingsPointer = settings.get();
    auto overlay = std::make_unique<osci::ComponentOverlay>(std::move(settings), "Export video", juce::Point<int>(440, 330), true);
    const juce::Component::SafePointer<osci::ComponentOverlay> settingsOverlay(overlay.get());
    auto accepted = std::make_shared<bool>(false);
    overlay->onDismissRequested = [owner, state, accepted] {
        if (!*accepted && owner != nullptr && owner->exportState == state) { owner->exportState.reset(); }
    };
    settingsPointer->onExport = [owner, state, project, beamSnapshot, renderMode, settingsOverlay, accepted](VideoEncodingConfiguration config) {
        if (owner == nullptr || *accepted) { return; }
        *accepted = true;
        juce::MessageManager::callAsync([owner, state, project, beamSnapshot, renderMode, config, settingsOverlay] {
            // Dismiss only after the settings button callback has returned: the
            // overlay owns that callback and its captured project snapshot.
            if (settingsOverlay != nullptr) { settingsOverlay->requestDismiss(); }
            if (owner == nullptr) { return; }
            owner->chooser = std::make_unique<juce::FileChooser>("Export composition video",
                owner->processor.getLastOpenedDirectory().getChildFile("composition." + config.fileExtension), "*." + config.fileExtension);
            owner->chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                | juce::FileBrowserComponent::warnAboutOverwriting, [owner, state, project, beamSnapshot, renderMode, config](const juce::FileChooser& selected) {
                if (owner == nullptr) { return; }
                if (selected.getResult() == juce::File()) { owner->exportState.reset(); return; }
                const auto destination = selected.getResult();
                if (!destination.getFileExtension().equalsIgnoreCase("." + config.fileExtension)) {
                    owner->exportState.reset();
                    owner->statusBar.show("Video export requires a ." + config.fileExtension + " filename. Choose Export video again and use that extension.");
                    return;
                }
                owner->processor.setLastOpenedDirectory(destination.getParentDirectory());
                owner->startVideoExport(state, project, beamSnapshot, renderMode, config, destination, {});
            });
        });
    };
    showOverlay(std::move(overlay));
#endif
}

void MotionEditor::startVideoExport(std::shared_ptr<ExportState> state, motion::Project project, std::shared_ptr<OfflineVisualiserParameters> beamSnapshot,
    VisualiserRenderer::RenderMode renderMode, VideoEncodingConfiguration config, juce::File destination, std::function<void(bool)> finished) {
#if OSCI_PREMIUM
    auto& recording = processor.recordingParameters;
    recording.setCanvasSize(config.renderSize);
    recording.frameRate.setUnnormalisedValueNotifyingHost(static_cast<float>(config.frameRate));
    recording.qualityParameter.setUnnormalisedValueNotifyingHost(RecordingParameters::qualityForCRF(config.crf));
    recording.losslessVideo.setBoolValue(config.crf == 0);
    recording.recordAudio.setBoolValue(config.includeAudio);
    recording.videoCodec = config.codec;
    recording.compressionPreset = config.compressionPreset;
    const juce::Component::SafePointer<MotionEditor> owner(this);
    juce::MessageManager::callAsync([owner, state, project, beamSnapshot, renderMode, config, destination, finished] {
        if (owner == nullptr) { return; }
        state->videoWithAudio = config.includeAudio;
        auto content = std::make_unique<MotionVideoPreparation>([state] {
            return state->videoWithAudio ? (state->progress.load() + state->soundtrackProgress.load()) * 0.5 : state->progress.load();
        }, [state] { state->cancelled.store(true); }, config.includeAudio);
        auto overlay = std::make_unique<osci::ComponentOverlay>(std::move(content), "Preparing video", juce::Point<int>(440, 130), true);
        overlay->setDismissible(false);
        const juce::Component::SafePointer<osci::ComponentOverlay> preparationOverlay(overlay.get());
        owner->showOverlay(std::move(overlay));
        // The worker owns one immutable prepared snapshot for both WAVs.
        owner->exports.addJob([owner, state, project, beamSnapshot, renderMode, config, destination, preparationOverlay, finished] {
            std::shared_ptr<const motion::PreparedBeam> picture;
            std::shared_ptr<MotionVideoTemporaryFiles> temporary;
            auto result = juce::Result::ok();
            try {
                temporary = std::make_shared<MotionVideoTemporaryFiles>();
                result = temporary->directory.createDirectory();
                if (result.wasOk() && !state->cancelled.load()) {
                    const auto rate = state->sampleRate;
                    const motion::PreparedComposition prepared(project, rate, &state->cancelled);
                    picture = std::make_shared<const motion::PreparedBeam>(prepared.beam);
                    result = motion::SignalExporter::write(prepared, temporary->signal(), rate, state->cancelled, &state->progress);
                    if (result.wasOk() && config.includeAudio) {
                        result = motion::SoundtrackExporter::write(prepared, temporary->soundtrack(), rate, state->cancelled, &state->soundtrackProgress);
                    }
                }
            } catch (...) {
                result = juce::Result::fail("Could not prepare the composition for video export.");
            }
            // Native save-dialog callbacks have returned before this starts
            // the shared GL renderer and its own cancellable progress overlay.
            juce::MessageManager::callAsync([owner, state, temporary, result, beamSnapshot, picture, renderMode, config, destination, preparationOverlay, finished] {
                if (owner == nullptr) { return; }
                if (state->cancelled.load() || result.failed()) {
                    if (preparationOverlay != nullptr) { owner->dismissOverlay(preparationOverlay.getComponent()); }
                    owner->exportState.reset();
                    if (!state->cancelled.load()) { owner->statusBar.show(result.getErrorMessage()); }
                    if (finished) { finished(false); }
                    return;
                }
                auto startRender = [owner, state, temporary, config, destination, renderMode, beamSnapshot, picture, finished] {
                    if (owner == nullptr) { return; }
                    // Frames render in order from the start, so the beam
                    // follows the document sample by sample.
                    if (picture != nullptr) {
                        auto slots = std::make_shared<motion::ScopeBeamSlots>(beamSnapshot->params);
                        beamSnapshot->params.applyExternalModulation = [picture, slots, rate = state->sampleRate, cursor = std::make_shared<juce::int64>(0)](int samples) {
                            for (std::size_t index = 0; index < motion::beamPropertySpecs.size(); ++index) {
                                slots->write(index, samples, [&](int sample) { return picture->value(index, static_cast<double>(*cursor + sample) / rate); });
                            }
                            *cursor += samples;
                        };
                    }
                    const auto started = owner->startOfflineVideoRender(temporary->signal(), config.includeAudio ? temporary->soundtrack() : juce::File(),
                        destination, config, renderMode, [owner, state, temporary, finished] {
                            // Completion can run from base-editor destruction;
                            // touch derived UI only in a later safe callback.
                            juce::MessageManager::callAsync([owner, state, finished] {
                                if (owner == nullptr) { return; }
                                if (owner->exportState == state) { owner->exportState.reset(); }
                                if (finished) { finished(owner->lastOfflineRenderSucceeded()); }
                            });
                        }, beamSnapshot);
                    if (!started && finished) { finished(false); }
                };
                if (preparationOverlay != nullptr) {
                    owner->dismissOverlay(preparationOverlay.getComponent(), std::move(startRender));
                } else {
                    startRender();
                }
            });
        });
    });
#else
    juce::ignoreUnused(state, project, beamSnapshot, renderMode, config, destination);
    if (finished) { finished(false); }
#endif
}

// Standalone batch render: osci-motion --render-video <out.mp4> <project.osci-motion>
// opens the project, renders it with the canvas and project frame rate, then quits.
void MotionEditor::handleCommandLine(const juce::String& commandLine) {
    auto tokens = juce::StringArray::fromTokens(commandLine, " ", "\"");
    tokens.removeEmptyStrings();
    juce::StringArray remaining;
    for (int index = 0; index < tokens.size(); ++index) {
        const auto token = tokens[index].trim().unquoted();
        if (token == "--render-video" && index + 1 < tokens.size()) {
            commandLineRender = juce::File::createFileWithoutCheckingPath(tokens[++index].trim().unquoted());
            continue;
        }
        if (token.startsWith("-psn_")) { continue; }
        remaining.add(tokens[index]);
    }
    if (commandLineRender != juce::File()) {
        const auto project = remaining.isEmpty() ? juce::File() : juce::File::createFileWithoutCheckingPath(remaining[0].trim().unquoted());
        if (!project.existsAsFile() || !project.hasFileExtension(".osci-motion")) {
            failCommandLineRender("pass an existing .osci-motion project after --render-video <output>.");
            return;
        }
        projectLoadFailed = false;
        commandLineProjectRequested = true;
    }
    if (remaining.size() > 0) { CommonPluginEditor::handleCommandLine(remaining.joinIntoString(" ")); }
}

void MotionEditor::failCommandLineRender(const juce::String& message) {
    std::cerr << "osci-motion render failed: " << message << std::endl;
    commandLineRender = juce::File();
    juce::JUCEApplicationBase::getInstance()->setApplicationReturnValue(1);
    juce::JUCEApplicationBase::quit();
}

void MotionEditor::continueCommandLineRender() {
#if OSCI_PREMIUM
    if (commandLineRender == juce::File() || commandLineRenderStarted || projectLoad != nullptr || processor.isPreparingComposition() || exportState != nullptr) { return; }
    if (projectLoadFailed) { failCommandLineRender("the project could not be opened."); return; }
    if (!commandLineProjectRequested) { return; }
    if (processor.getPreparationError().isNotEmpty()) { failCommandLineRender(processor.getPreparationError()); return; }
    commandLineRenderStarted = true;
    if (!processor.ensureFFmpegExists()) { failCommandLineRender("FFmpeg is unavailable."); return; }
    std::shared_ptr<OfflineVisualiserParameters> beamSnapshot;
    try {
        beamSnapshot = std::make_shared<OfflineVisualiserParameters>(processor.visualiserParameters, OfflineVisualiserParameters::ExternalModulation::replace);
    } catch (const std::exception& error) {
        failCommandLineRender(error.what());
        return;
    }
    auto config = recordingSettings.createVideoEncodingConfiguration();
    const auto project = processor.document.mainProject();
    config.frameRate = project.frameRate;
    config.includeAudio = true;
    auto state = std::make_shared<ExportState>();
    state->sampleRate = processor.exportSampleRate();
    exportState = state;
    const auto destination = commandLineRender;
    std::cout << "osci-motion rendering " << project.name << " to " << destination.getFullPathName() << std::endl;
    const juce::Component::SafePointer<MotionEditor> owner(this);
    startVideoExport(state, project, beamSnapshot, visualiser.getRenderMode(), config, destination, [owner, destination](bool succeeded) {
        if (owner == nullptr) { return; }
        if (!succeeded) { owner->failCommandLineRender("the export did not complete."); return; }
        std::cout << "osci-motion rendered " << destination.getFullPathName() << std::endl;
        juce::JUCEApplicationBase::quit();
    });
#endif
}

void MotionEditor::exportSignal() {
    if (exportState != nullptr) {
        return;
    }
    auto state = std::make_shared<ExportState>();
    exportState = state;
    state->sampleRate = processor.exportSampleRate();
    chooser = std::make_unique<juce::FileChooser>("Export XYRGB signal - " + juce::String(state->sampleRate / 1000.0, 1) + " kHz float WAV",
        processor.getLastOpenedDirectory().getChildFile("composition.wav"), "*.wav");
    const juce::Component::SafePointer<MotionEditor> owner(this);
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
        | juce::FileBrowserComponent::warnAboutOverwriting, [owner, state](const juce::FileChooser& selected) {
        if (owner == nullptr) { return; }
        if (selected.getResult() == juce::File()) { owner->exportState.reset(); return; }
        const auto destination = selected.getResult();
        if (!destination.getFileExtension().equalsIgnoreCase(".wav")) {
            owner->exportState.reset();
            owner->statusBar.show("Signal export requires a .wav filename. Choose Export XYRGB signal again and use that extension.");
            return;
        }
        const auto project = owner->processor.document.mainProject();
        owner->exportBar.setName("Signal export progress");
        owner->exportProgress = 0;
        owner->exportBar.setVisible(true);
        owner->cancelExport.setVisible(true);
        owner->exports.addJob([owner, state, project, destination] {
            const auto result = motion::SignalExporter::write(project, destination, state->sampleRate, state->cancelled, &state->progress);
            juce::MessageManager::callAsync([owner, state, result] {
                if (owner == nullptr) {
                    return;
                }
                owner->exportState.reset();
                owner->exportBar.setVisible(false);
                owner->cancelExport.setVisible(false);
                if (result.failed() && !state->cancelled.load()) {
                    owner->statusBar.show(result.getErrorMessage());
                }
            });
        });
    });
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


void MotionEditor::refreshTiming() {
    timeline.repaint();
    curveEditor.repaint();
}

// The menu bar's Timing menu.
static const std::array<double, 8> timingRates { 24000.0 / 1001, 24, 25, 30000.0 / 1001, 30, 50, 60, 120 };

juce::PopupMenu MotionEditor::timingMenu() {
    const auto& project = processor.document.project();
    juce::PopupMenu menu;
    menu.setLookAndFeel(&getLookAndFeel());
    // What the ruler's right-click offers, at the playhead.
    menu.addItem(motion::style::menuItem("Add marker...", 700, "M"));
    menu.addItem(701, "Add tempo change...");
    menu.addSeparator();
    menu.addItem(600, detectingTempo ? "Detecting tempo..." : "Detect tempo from soundtrack", !detectingTempo && soundtrackClip() != 0);
    menu.addSeparator();
    menu.addItem(101, "Seconds", true, project.timeDisplay == motion::TimeDisplay::seconds);
    menu.addItem(102, "Frames", true, project.timeDisplay == motion::TimeDisplay::frames);
    menu.addItem(103, "Bars / beats", true, project.timeDisplay == motion::TimeDisplay::beats);
    menu.addSeparator();
    menu.addItem(200, "Snap to grid", true, project.gridSnap);
    const std::array<const char*, 7> names { "1 bar", "1 beat", "1/8 note", "1/16 note", "1/32 note", "1/8 triplet", "1/16 triplet" };
    juce::PopupMenu beatGrid;
    for (std::size_t i = 0; i < names.size(); ++i) { beatGrid.addItem(300 + static_cast<int>(i), names[i], true, std::abs(project.snapBeats - snapDivision(static_cast<int>(i))) < 1.0e-9); }
    menu.addSubMenu("Beat grid", beatGrid, project.timeDisplay == motion::TimeDisplay::beats);
    juce::PopupMenu meter;
    for (const auto beats : { 2, 3, 4, 5, 6, 7 }) { meter.addItem(400 + beats, juce::String(beats) + "/4", true, project.beatsPerBar == beats); }
    menu.addSubMenu("Meter", meter);
    const std::array<const char*, 8> rateNames { "23.976 fps", "24 fps", "25 fps", "29.97 fps", "30 fps", "50 fps", "60 fps", "120 fps" };
    juce::PopupMenu frames;
    for (std::size_t i = 0; i < timingRates.size(); ++i) { frames.addItem(500 + static_cast<int>(i), rateNames[i], true, std::abs(project.frameRate - timingRates[i]) < 1.0e-9); }
    menu.addSubMenu("Frame rate", frames);
    return menu;
}

double MotionEditor::snapDivision(int index) const {
    const std::array<double, 7> divisions { static_cast<double>(processor.document.project().beatsPerBar), 1, 0.5, 0.25, 0.125, 1.0 / 3, 1.0 / 6 };
    return divisions[static_cast<std::size_t>(std::clamp(index, 0, 6))];
}

// Display and snapping are view options (no undo step); meter and frame
// rate are edits.
bool MotionEditor::applyTiming(int result) {
    if (result == 600) { detectTempo(); return true; }
    if (result == 700 && timeline.onEditMarker) {
        timeline.onEditMarker(0, std::clamp(processor.position.load(), 0.0, processor.document.project().duration));
        return true;
    }
    if (result == 701) {
        timeline.editTempoAt(processor.position.load());
        return true;
    }
    const auto& current = processor.document.project();
    if (result >= 101 && result <= 103) {
        const auto display = static_cast<motion::TimeDisplay>(result - 101);
        if (current.timeDisplay != display) { processor.document.changeView([display](motion::Composition& state) { state.timeDisplay = display; }); }
        return true;
    }
    if (result == 200) {
        processor.document.changeView([](motion::Composition& state) { state.gridSnap = !state.gridSnap; });
        return true;
    }
    if (result >= 300 && result < 307) {
        const auto subdivision = snapDivision(result - 300);
        processor.document.changeView([subdivision](motion::Composition& state) { state.snapBeats = subdivision; state.gridSnap = true; });
        return true;
    }
    if (result >= 402 && result <= 407) {
        if (current.beatsPerBar == result - 400) { return true; }
        processor.document.edit("Change meter", [result](motion::Project& state) {
            if (std::abs(state.snapBeats - state.beatsPerBar) < 1.0e-9) { state.snapBeats = result - 400; }
            state.beatsPerBar = result - 400;
        });
        return true;
    }
    if (result >= 500 && result < 508) {
        const auto rate = timingRates[static_cast<std::size_t>(result - 500)];
        if (std::abs(current.frameRate - rate) >= 1.0e-9) { processor.document.edit("Change frame rate", [rate](motion::Project& state) { state.frameRate = rate; }); }
        return true;
    }
    return false;
}

void MotionEditor::tap() {
    const auto bpm = tapTempo.tap(juce::Time::getMillisecondCounterHiRes() / 1000);
    tapButton.setButtonText(tapTempo.count() > 1 ? juce::String(static_cast<int>(tapTempo.count())) : "Tap");
    if (bpm.has_value()) {
        tappedBpm = bpm;
        tempoValue.setText(juce::String(*bpm, 1), juce::dontSendNotification);
    }
    if (tapCommit == nullptr) {
        tapCommit = std::make_unique<juce::TimedCallback>([this] {
            tapCommit->stopTimer();
            tapButton.setButtonText("Tap");
            tapTempo.reset();
            if (!tappedBpm.has_value()) { return; }
            const auto bpm = *tappedBpm;
            tappedBpm.reset();
            const auto result = processor.document.changeTempo(bpm);
            if (result.failed()) { statusBar.show(result.getErrorMessage()); } else { statusBar.show("Tempo " + juce::String(bpm, 1) + " BPM from tapping.", MotionStatusBar::Kind::notice); }
            refreshTiming();
        });
    }
    tapCommit->startTimer(1600);
}

// The first clip on an audio track: the piece the picture is cut to.
motion::Id MotionEditor::soundtrackClip() const {
    for (const auto& track : processor.document.mainProject().tracks) {
        if (track.kind != motion::TrackKind::audio) { continue; }
        for (const auto& clip : track.clips) { return clip.id; }
    }
    return 0;
}

// Analyses the soundtrack off the message thread, then sets a steady tempo
// and slides the soundtrack onto the bar grid in one undo step.
void MotionEditor::detectTempo() {
    const auto& project = processor.document.mainProject();
    const auto id = soundtrackClip();
    std::shared_ptr<const motion::PreparedAudio> audio;
    for (const auto& track : project.tracks) {
        for (const auto& clip : track.clips) {
            if (clip.id != id) { continue; }
            for (const auto& asset : project.assets) { if (asset != nullptr && asset->id == clip.asset) { audio = asset->audio; } }
        }
    }
    if (audio == nullptr || processor.document.editingComposition() != 0) {
        statusBar.show(audio == nullptr ? "Import a soundtrack first." : "Tempo is set on the main composition. Go back to it first.");
        return;
    }
    detectingTempo = true;
    statusBar.show("Detecting tempo...", MotionStatusBar::Kind::notice);
    const juce::Component::SafePointer<MotionEditor> owner(this);
    const auto generation = processor.document.generation();
    const auto beatsPerBar = project.beatsPerBar;
    juce::Thread::launch([owner, audio, id, generation, beatsPerBar] {
        std::vector<float> mono(audio->frameCount());
        const auto channels = audio->channelCount();
        for (std::size_t channel = 0; channel < channels; ++channel) {
            const auto samples = audio->channel(channel);
            for (std::size_t i = 0; i < mono.size(); ++i) { mono[i] += samples[i] / static_cast<float>(channels); }
        }
        const auto estimate = motion::TempoDetection::estimate(mono, audio->sampleRate(), beatsPerBar);
        juce::MessageManager::callAsync([owner, estimate, id, generation] {
            if (owner == nullptr) { return; }
            owner->detectingTempo = false;
            if (owner->processor.document.generation() != generation) { return; }
            if (!estimate.has_value()) {
                owner->statusBar.show("Could not find a steady tempo in the soundtrack. Set it by hand or tap it.");
                return;
            }
            double moved = 0;
            const auto bars = owner->processor.document.project().timeDisplay == motion::TimeDisplay::beats;
            const auto result = owner->processor.document.setTempoFromAudio(id, estimate->bpm, estimate->firstDownbeat, moved);
            if (result.failed()) { owner->statusBar.show(result.getErrorMessage()); return; }
            const auto bpm = juce::String(estimate->bpm, estimate->bpm == std::round(estimate->bpm) ? 0 : 2);
            const auto other = estimate->bpm * 2 <= 200 ? estimate->bpm * 2 : estimate->bpm / 2;
            owner->statusBar.show("Tempo " + bpm + " BPM from the soundtrack" + (estimate->confidence < 0.4 ? " (low confidence - check it)" : "")
                + ". If it feels " + (other > estimate->bpm ? "twice as fast" : "half as fast") + ", type " + juce::String(other, other == std::round(other) ? 0 : 2) + " in BPM"
                + (moved > 0 ? ". Moved it " + juce::String(moved, 3) + " s so its first downbeat is on a bar." : "; its downbeats are already on the bars.")
                + (bars ? "" : " The ruler now shows bars and beats."), MotionStatusBar::Kind::notice);
        });
    });
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

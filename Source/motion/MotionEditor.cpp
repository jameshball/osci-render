#include "live/BlenderCaptureArchive.h"
#include "ui/BlenderSourcePanel.h"
#include "MotionEditor.h"
#include "export/SignalExporter.h"
#include "export/SoundtrackExporter.h"
#include "ui/VideoExportSettings.h"
#include "ui/BakeSettingsPanel.h"
#include "ui/FractalSettingsPanel.h"
#include "ui/RasterSettingsPanel.h"
#include "ui/TextSourcePanel.h"
#include "ui/LuaSourcePanel.h"
#include "ui/MarkerPanel.h"
#include "ui/MidiEnvelopePanel.h"
#include "../components/OverlayDialogHelpers.h"
#include <cstdlib>

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
bool canSplitClip(const motion::Clip* clip, double time, double bpm) {
    return clip != nullptr && clip->valid() && std::isfinite(time) && time > clip->timing(bpm).start && time < clip->timing(bpm).end();
}
}

MotionEditor::MotionEditor(MotionProcessor& ownerProcessor)
    : CommonPluginEditor(ownerProcessor, "osci-motion", "osci-motion", 1440, 900), processor(ownerProcessor), timeline(ownerProcessor), composition(ownerProcessor), assetLibrary(ownerProcessor.document), curveEditor(ownerProcessor), notesEditor(ownerProcessor), cameraPanel(ownerProcessor), clipTimingPanel(ownerProcessor), effectsPanel(ownerProcessor), modulationPanel(ownerProcessor), propertyInspector(ownerProcessor) {
    lookAndFeel.setControlCornerRadius(3.0f);
    visualiserSettings.setSurfaceColours(osci::Colours::veryDark(), osci::Colours::surface());
    beamSettingsWindow.setLookAndFeel(&lookAndFeel);
    beamSettingsWindow.setBackgroundColour(osci::Colours::veryDark());
    visualiser.openSettings = [this] {
        beamSettingsWindow.setVisible(true);
        beamSettingsWindow.toFront(true);
    };
    visualiser.closeSettings = [this] { beamSettingsWindow.setVisible(false); };
    beamSettingsWindow.addKeyListener(this);
#if JUCE_MAC || JUCE_WINDOWS
    beamSettingsWindow.setUsingNativeTitleBar(true);
#endif
    menus.addTopLevelMenu("File");
    menus.addProjectMenuItems(0, processor, *this);
    menus.addMenuSeparator(0);
    menus.addMenuItem(0, "Export XYRGB signal...", [this] { exportSignal(); });
#if OSCI_PREMIUM
    menus.addMenuItem(0, "Export video...", [this] { exportVideo(); });
#endif
    menus.addTopLevelMenu("Edit");
    menus.addEditMenuItems(1, processor);
    menus.addTopLevelMenu("Clip");
    menus.addTopLevelMenu("Play");
    menus.addTopLevelMenu("Audio");
    menus.addStandaloneAudioSettingsMenuItem(4, processor, *this);
    menus.addMenuItem(4, "Playback health...", [this] { osci::showOverlayMessage(*this, "Playback health", playbackHealth.summary(), osci::ErrorOverlay::Icon::None, {520, 380}, juce::Justification::centredLeft); });
    addAndMakeVisible(playbackHealth);
    playbackHealth.isPreparing = [this] { return processor.isPreparingComposition(); };
    playbackHealth.onClick = [this] { osci::showOverlayMessage(*this, "Playback health", playbackHealth.summary(), osci::ErrorOverlay::Icon::None, {520, 380}, juce::Justification::centredLeft); };
    menus.addTopLevelMenu("Interface");
    menus.addCommonInterfaceMenuItems(5, processor, *this);
    registerCommands();
    initialiseMenuBar(menus);
    for (auto* header : { &libraryHeader, &viewportHeader, &outputHeader, &inspectorHeader, &timelineHeader }) {
        addAndMakeVisible(header);
    }
    for (auto* component : std::initializer_list<juce::Component*> { &timeline, &composition, &assetLibrary, &importButton, &playButton, &startButton, &endButton, &splitButton, &timeLabel, &propertyInspector, &curveEditor, &notesEditor, &timelineTabs, &curveProperty, &timelineDivider, &previewDivider, &cameraPanel, &inspectorTabs }) {
        addAndMakeVisible(component);
    }
    addChildComponent(scopeBack);
    addChildComponent(scopeLabel);
    addChildComponent(scopeShared);
    scopeShared.setText("Shared composition", juce::dontSendNotification);
    scopeShared.setFont(juce::FontOptions(12.0f));
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
    addAndMakeVisible(compositionTitle);
    compositionTitle.setText("Composition", juce::dontSendNotification);
    compositionTitle.setFont(juce::FontOptions(15.0f));
    compositionTitle.setBorderSize(juce::BorderSize<int>(0));
    addAndMakeVisible(transformTool);
    transformTool.setName("Composition transform tool");
    transformTool.setColour(juce::ComboBox::backgroundColourId, osci::Colours::surfaceRaised());
    transformTool.addItem("Move", 1);
    transformTool.addItem("Rotate", 2);
    transformTool.addItem("Scale", 3);
    transformTool.setSelectedId(1, juce::dontSendNotification);
    transformTool.setTooltip("Transform the selected object. Preview shortcuts: G move, R rotate, S scale. Scale's centre handle changes all axes.");
    transformTool.onChange = [this] { composition.setTool(static_cast<MotionTransformTool>(transformTool.getSelectedId() - 1)); };
    composition.onToolChanged = [this](MotionTransformTool tool) { transformTool.setSelectedId(static_cast<int>(tool) + 1, juce::dontSendNotification); };
    addAndMakeVisible(navigateView);
    addAndMakeVisible(frameView);
    addAndMakeVisible(pathView);
    pathView.setName("Show motion path");
    pathView.setClickingTogglesState(true);
    pathView.setColour(juce::TextButton::buttonOnColourId, osci::Colours::accentColor().withAlpha(.22f));
    pathView.setColour(juce::TextButton::textColourOnId, osci::Colours::text());
    pathView.setTooltip("Show the selected object's position path before effects. Click a path key to seek, then edit with the gizmo. Shortcut: P.");
    pathView.onClick = [this] { composition.setMotionPathVisible(pathView.getToggleState()); };
    composition.onMotionPathChanged = [this](bool visible) { pathView.setToggleState(visible, juce::dontSendNotification); };
    navigateView.setName("Navigate composition view");
    navigateView.setWantsKeyboardFocus(false);
    navigateView.setMouseClickGrabsKeyboardFocus(false);
    navigateView.setTooltip("Explore the scene with the mouse and WASD or arrow keys. Esc finishes. This does not change the output camera. Shortcut: N.");
    navigateView.setColour(juce::TextButton::buttonColourId, osci::Colours::surfaceRaised());
    navigateView.onClick = [this] { composition.setNavigating(!composition.isNavigating()); };
    composition.onNavigationChanged = [this](bool active) {
        navigateView.setToggleState(active, juce::dontSendNotification);
        navigateView.setButtonText(active ? "Done" : viewportHeader.getWidth() >= 320 ? "Navigate" : "3D");
    };
    frameView.setName("Frame composition selection");
    frameView.setTooltip("Frame the selected object, or all visible objects. Shortcut: F. Press 0 in the preview to reset the view.");
    frameView.onClick = [this] { composition.frameSelection(); };
    addChildComponent(exportBar);
    addAndMakeVisible(libraryTabs);
    addChildComponent(modulationPanel);
    addAndMakeVisible(tempoValue);
    addAndMakeVisible(tempoLabel);
    addAndMakeVisible(timingButton);
    addAndMakeVisible(canvasButton);
    canvasButton.setName("Output canvas");
    canvasButton.setTooltip("Set the output framing and default video dimensions.");
    canvasButton.onClick = [this] {
        auto panel = std::make_unique<MotionCanvasSettings>(processor.recordingParameters.getCanvasSize(), processor.document.mainProject().frameRate);
        auto* controls = panel.get();
        auto overlay = std::make_unique<osci::ComponentOverlay>(std::move(panel), "Output canvas", juce::Point<int>(420, 216), true);
        const juce::Component::SafePointer<MotionEditor> owner(this);
        const juce::Component::SafePointer<osci::OverlayComponent> dialog(overlay.get());
        controls->onApply = [owner, dialog](VisualiserRenderSize size) {
            juce::MessageManager::callAsync([owner, dialog, size] {
                if (owner == nullptr || dialog == nullptr) { return; }
                owner->processor.recordingParameters.setCanvasSize(size);
                owner->dismissOverlay(dialog.getComponent());
            });
        };
        showOverlay(std::move(overlay));
    };
    addAndMakeVisible(monitorOutput);
    monitorOutput.setName("Audio output mode");
    monitorOutput.setColour(juce::ComboBox::backgroundColourId, osci::Colours::surfaceRaised());
    monitorOutput.setColour(juce::ComboBox::arrowColourId, osci::Colours::textMuted());
    monitorOutput.addItem("Music monitor", 1);
    monitorOutput.addItem("XY signal", 2);
    monitorOutput.addItem("XYRGB signal (5 ch)", 3);
    refreshOutputChoices();
    monitorOutput.setSelectedId(static_cast<int>(processor.getOutputMode()) + 1, juce::dontSendNotification);
    monitorOutput.setTooltip("Physical audio output. The visualiser always receives the beam signal. XYRGB requires five enabled output channels.");
    monitorOutput.onChange = [this] {
        const auto mode = static_cast<MotionProcessor::OutputMode>(monitorOutput.getSelectedId() - 1);
        if (mode == MotionProcessor::OutputMode::xyrgb) {
            auto* holder = juce::StandalonePluginHolder::getInstance();
            const auto result = holder != nullptr ? holder->configureOutputChannels(5) : juce::Result::fail("Five-channel output requires the standalone audio device.");
            if (result.failed()) {
                assetLibrary.setError(result.getErrorMessage());
                monitorOutput.setSelectedId(static_cast<int>(processor.getOutputMode()) + 1, juce::dontSendNotification);
                return;
            }
        }
        processor.setOutputMode(mode);
    };
    timingButton.setColour(juce::TextButton::buttonColourId, osci::Colours::surfaceRaised());
    playButton.setTooltip("Play / pause (Space)");
    startButton.setTooltip("Go to start (Home)");
    endButton.setTooltip("Go to end (End)");
    startButton.onClick = [this] { processor.seek(0); timeline.revealTime(0); };
    endButton.onClick = [this] {
        const auto end = processor.document.project().duration;
        processor.seek(end); timeline.revealTime(end);
    };
    timeLabel.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 14.0f, juce::Font::plain));
    timeLabel.setJustificationType(juce::Justification::centred);
    tempoValue.setFont(motion::style::body());
    tempoLabel.setFont(motion::style::small());
    tempoLabel.setColour(juce::Label::textColourId, motion::style::muted());
    curveProperty.setColour(juce::ComboBox::backgroundColourId, osci::Colours::surfaceRaised());
    timingButton.setName("Time and grid");
    timingButton.setTitle("Time and grid");
    timingButton.setTooltip("Time display, snapping, meter and frame rate. Alt temporarily bypasses snapping.");
    timingButton.onClick = [this] { showTimingMenu(); };
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
    addChildComponent(effectsPanel);
    libraryHeader.setVisible(false);
    libraryTabs.setName("Library tabs");
    inspectorTabs.setName("Inspector tabs");
    inspectorTabs.setTabSpacing(58, 8);
    libraryTabs.addTab("Assets");
    libraryTabs.addTab("Effects");
    libraryTabs.onSelectionChanged = [this](int index) {
        assetLibrary.setVisible(index == 0);
        importButton.setVisible(index == 0);
        effectLibrary.setVisible(index == 1);
        resized();
    };
    effectLibrary.onInsert = [this](const std::string& type) {
        inspectorTabs.setSelectedIndex(1);
        effectsPanel.addEffect(type);
    };
    effectsPanel.onPropertySelected = [this](motion::Id id, std::string property) { selectCurveTarget(id, property, false); };
    timeline.onEffectAdded = [this](motion::Id owner, motion::Id effect) {
        effectsPanel.showOwner(owner, effect);
        inspectorTabs.setSelectedIndex(1);
    };
    addChildComponent(cancelExport);
    exportBar.setName("Signal export progress");
    cancelExport.onClick = [this] { if (exportState != nullptr) { exportState->cancelled.store(true); } };
    inspectorTabs.addTab("Object");
    inspectorTabs.addTab("Effects");
    inspectorTabs.addTab("Camera");
    inspectorTabs.addTab("Clip");
    addChildComponent(clipTimingPanel);
    inspectorTabs.onSelectionChanged = [this](int index) {
        cameraPanel.setVisible(index == 2);
        clipTimingPanel.setVisible(index == 3);
        effectsPanel.setVisible(index == 1);
        propertyInspector.setVisible(index == 0);
        refreshInspector();
        if (index == 1) { effectsPanel.activate(); } else { selectCurveTarget(index == 2 ? cameraPanel.selectedCameraId() : selection, curvePropertyName, index == 2); }
        repaint();
    };
    cameraPanel.setVisible(false);
    timelineTabs.addTab("Timeline");
    timelineTabs.addTab("Graph");
    timelineTabs.addTab("Notes");
    timelineTabs.onSelectionChanged = [this](int index) {
        if (index == 2) { timelineFraction = std::max(timelineFraction, .42); }
        timeline.setVisible(index == 0);
        curveEditor.setVisible(index == 1);
        notesEditor.setVisible(index == 2);
        modulationPanel.setVisible(index == 1);
        curveProperty.setVisible(index == 1);
        resized();
        if (index == 2) { notesEditor.fitContents(); }
    };
    curveEditor.setVisible(false);
    notesEditor.setVisible(false);
    curveProperty.setVisible(false);
    curveProperty.setName("Animated property");
    for (std::size_t index = 0; index < motion::propertyNames.size(); ++index) {
        curveProperties.emplace_back(motion::propertyNames[index]);
        curveProperty.addItem(juce::String(motion::propertyNames[index]).replace(".", " "), static_cast<int>(index) + 1);
    }
    curveProperty.setSelectedId(1, juce::dontSendNotification);
    curveProperty.onChange = [this] {
        const auto index = static_cast<std::size_t>(std::max(0, curveProperty.getSelectedId() - 1));
        if (index >= curveProperties.size()) { return; }
        curvePropertyName = curveProperties[index];
        curveEditor.setSelection(curveTarget, curvePropertyName);
        modulationPanel.setTarget(curveTarget, curvePropertyName);
    };
    cameraPanel.onPropertySelected = [this](motion::Id id, std::string property) {
        selectCurveTarget(id, property, true);
    };
    curveEditor.onPreview = [this](const motion::Curve* curve) {
        auto preview = processor.document.project();
        if (curve != nullptr) {
            auto* target = motion::findPropertyCurve(preview, curveTarget, curvePropertyName);
            if (target != nullptr) { *target = *curve; }
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
        menu.addItem(1, "Import file...");
        menu.addItem(2, "Blender live source...");
        const juce::Component::SafePointer<MotionEditor> owner(this);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&importButton), [owner](int choice) {
            if (owner == nullptr) { return; }
            if (choice == 1) { owner->chooseSourceFile(); }
            if (choice == 2) { owner->showBlenderSettings(); }
        });
    };
    playButton.onClick = [this] { processor.playing.store(!processor.playing.load()); };
    splitButton.onClick = [this] {
        const auto time = processor.position.load();
        const auto& tracks = processor.document.project().tracks;
        const bool canSplit = std::any_of(tracks.begin(), tracks.end(), [&](const motion::Track& track) {
            return std::any_of(track.clips.begin(), track.clips.end(), [&](const motion::Clip& clip) {
                return clip.id == selection && canSplitClip(&clip, time, processor.document.project().bpm);
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
                    auto parts = track.clips[index].split(time, id, project.bpm);
                    if (!parts.has_value()) { return false; }
                    for (auto& effect : parts->second.effects) { effect.id = processor.document.newId(); }
                    track.clips[index] = std::move(parts->first);
                    return track.insert(std::move(parts->second), project.bpm);
                }
            }
            return false;
        });
    };
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
                if (result.failed()) { owner->assetLibrary.setError(result.getErrorMessage()); }
            });
    };
    assetLibrary.onOpenComposition = [this](motion::Id id) { enterComposition(id, true); };
    assetLibrary.onInsert = [this](motion::Id id) { timeline.insertAsset(id, -1, -1); };
    assetLibrary.liveStatus = [this](motion::Id id) { return processor.blenderInputs().statusText(id); };
    assetLibrary.onBake = [this](motion::Id id) {
        for (const auto& asset : processor.document.mainProject().assets) {
            if (asset->id == id && asset->liveIdentity != nullptr) { showBlenderSettings(id); return; }
        }
        const auto& assets = processor.document.project().assets;
        const auto found = std::find_if(assets.begin(), assets.end(), [id](const auto& asset) { return asset->id == id; });
        if (found == assets.end() || (!(*found)->extension.equalsIgnoreCase(".lua") && !(*found)->extension.equalsIgnoreCase(".txt")
                && !(*found)->extension.equalsIgnoreCase(".lsystem") && !motion::Document::isRasterSource((*found)->extension))) { return; }
        preparationRequests.push_back({{}, processor.position.load(), processor.document.generation(), *found});
        showNextPreparationSettings();
    };
    assetLibrary.onCancelImport = [this] {
        for (const auto& task : pendingImports) { task->cancelled.store(true); }
    };
    notesEditor.onEditInstrument = [this](motion::Id id) {
        const motion::Clip* clip = nullptr;
        for (const auto& track : processor.document.project().tracks) {
            for (const auto& item : track.clips) { if (item.id == id) { clip = &item; } }
        }
        if (clip == nullptr) { return; }
        const auto generation = processor.document.generation();
        const auto revision = processor.document.revision();
        auto panel = std::make_unique<MotionMidiEnvelopePanel>(clip->instrument);
        auto* controls = panel.get();
        auto overlay = std::make_unique<osci::ComponentOverlay>(std::move(panel), "MIDI envelope", juce::Point<int>(420, 330), true);
        const juce::Component::SafePointer<MotionEditor> owner(this);
        const juce::Component::SafePointer<osci::OverlayComponent> dialog(overlay.get());
        controls->onApply = [owner, dialog, id, generation, revision](motion::MidiInstrument settings) {
            juce::MessageManager::callAsync([owner, dialog, id, generation, revision, settings] {
                if (owner == nullptr || dialog == nullptr) { return; }
                if (owner->processor.document.generation() == generation && owner->processor.document.revision() == revision) {
                    const auto result = owner->processor.document.setMidiInstrument(id, settings);
                    if (result.failed()) { owner->assetLibrary.setError(result.getErrorMessage()); }
                }
                owner->dismissOverlay(dialog.getComponent());
            });
        };
        showOverlay(std::move(overlay));
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
        const auto generation = processor.document.generation();
        const auto revision = processor.document.revision();
        auto panel = std::make_unique<MotionMarkerPanel>(name, time, project.timeGrid(), project.duration);
        auto* controls = panel.get();
        auto overlay = std::make_unique<osci::ComponentOverlay>(std::move(panel), id == 0 ? "Add marker" : "Edit marker", juce::Point<int>(420, 170), true);
        const juce::Component::SafePointer<MotionEditor> owner(this);
        const juce::Component::SafePointer<osci::OverlayComponent> overlayPointer(overlay.get());
        const juce::Component::SafePointer<MotionMarkerPanel> markerPanel(controls);
        controls->onApply = [owner, overlayPointer, markerPanel, id, generation, revision](juce::String name, double time) {
            juce::MessageManager::callAsync([owner, overlayPointer, markerPanel, id, generation, revision, name, time] {
                if (owner == nullptr || overlayPointer == nullptr) { return; }
                if (owner->processor.document.generation() == generation && owner->processor.document.revision() == revision) {
                    const auto result = owner->processor.document.setMarker(id, time, name);
                    if (result.failed()) { if (markerPanel != nullptr) { markerPanel->setError(result.getErrorMessage()); } return; }
                }
                owner->dismissOverlay(overlayPointer.getComponent());
            });
        };
        showOverlay(std::move(overlay));
    };
    timeline.onEnterComposition = [this](motion::Id id) { enterComposition(id); };
    timeline.onSelection = [this](motion::Id id) { select(id); };
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
    timeline.onTimingRequested = [this](motion::Id id) { select(id); inspectorTabs.setSelectedIndex(3); };
    timeline.onError = [this](const juce::String& message) { osci::showOverlayMessage(*this, "Cannot edit timeline", message); };
    composition.onSelection = timeline.onSelection;
    propertyInspector.onPropertySelected = [this](motion::Id id, const std::string& property) { selectCurveTarget(id, property, false); };
    propertyInspector.selectedKeyTime = [this](motion::Id id) { return composition.selectedKeyContentTime(id); };
    propertyInspector.onKeyTimeEdited = [this] { composition.retainSelectedKeyAfterEdit(); };
    processor.document.addChangeListener(this);
    composition.refresh();
    timeline.refreshTracks();
    refreshInspector();
    startTimerHz(30);
    setResizeLimits(1100, 700, 4096, 2160);
    resized();
}

MotionEditor::~MotionEditor() {
    processor.blenderInputs().cancelAllCaptures();
    stopTimer();
    visualiser.openSettings = {};
    visualiser.closeSettings = {};
    beamSettingsWindow.removeKeyListener(this);
    beamSettingsWindow.setLookAndFeel(nullptr);
    curveEditor.onPreview = {};
    processor.previewComposition(processor.document.project());
    processor.document.removeChangeListener(this);
    for (const auto& task : pendingImports) { task->cancelled.store(true); }
    if (projectLoad != nullptr) { projectLoad->cancelled.store(true); }
    imports.removeAllJobs(true, -1);
    if (exportState != nullptr) { exportState->cancelled.store(true); }
    exports.removeAllJobs(true, -1);
    menuBar.setModel(nullptr);
}

void MotionEditor::resized() {
    CommonPluginEditor::resized();
    auto area = getLocalBounds().reduced(3);
    auto top = area.removeFromTop(30);
    undoRedoControls.setBounds(top.removeFromRight(undoRedoControls.getPreferredWidth()));
    playbackHealth.setBounds(top.removeFromRight(96).reduced(3));
    // Transport sits centred in the menu row, leaving the full height below
    // for the workspace.
    auto transport = top.withSizeKeepingCentre(std::min(top.getWidth() - 340, 470), 30).withX(std::max(top.getX() + 340, top.getCentreX() - 235));
    menuBar.setBounds(top.withRight(transport.getX()));
    startButton.setBounds(transport.removeFromLeft(28).reduced(1, 3));
    playButton.setBounds(transport.removeFromLeft(32).reduced(1, 3));
    endButton.setBounds(transport.removeFromLeft(28).reduced(1, 3));
    transport.removeFromLeft(8);
    timeLabel.setBounds(transport.removeFromLeft(118).reduced(0, 3));
    transport.removeFromLeft(8);
    tempoValue.setBounds(transport.removeFromLeft(52).reduced(0, 4));
    tempoLabel.setBounds(transport.removeFromLeft(34));
    timingButton.setBounds(transport.removeFromLeft(140).reduced(0, 3));
    area.removeFromTop(3);
    workspaceHeight = area.getHeight();
    timelineBounds = area.removeFromBottom(std::clamp(juce::roundToInt(workspaceHeight * timelineFraction), 240, workspaceHeight - 370));
    auto timeline = timelineBounds;
    auto header = timeline.removeFromTop(30);
    timelineHeader.setBounds(header);
    timelineTabs.setBounds(header.removeFromLeft(270));
    splitButton.setBounds(header.removeFromLeft(65).reduced(2, 3));
    curveProperty.setBounds(header.removeFromLeft(165).reduced(2));
    cancelExport.setBounds(header.removeFromRight(62).reduced(2));
    exportBar.setBounds(header.removeFromRight(180).reduced(2));
    const bool nested = processor.document.editingComposition() != 0;
    scopeBack.setVisible(nested); scopeLabel.setVisible(nested); scopeShared.setVisible(nested);
    if (nested) {
        auto breadcrumb = timeline.removeFromTop(28);
        scopeBack.setBounds(breadcrumb.removeFromLeft(160).reduced(2));
        scopeShared.setBounds(breadcrumb.removeFromRight(170).reduced(6, 1));
        scopeLabel.setBounds(breadcrumb.reduced(5, 1));
    }
    this->timeline.setBounds(timeline.withTrimmedTop(3));
    notesEditor.setBounds(timeline.withTrimmedTop(3));
    auto graph = timeline.withTrimmedTop(3);
    modulationPanel.setBounds(graph.removeFromRight(285));
    graph.removeFromRight(3);
    curveEditor.setBounds(graph);
    timelineDivider.setBounds(area.removeFromBottom(7));
    libraryBounds = area.removeFromLeft(190);
    auto library = libraryBounds;
    libraryHeader.setBounds(library.removeFromTop(30));
    libraryTabs.setBounds(libraryHeader.getBounds());
    effectLibrary.setBounds(library.reduced(4, 0));
    importButton.setBounds(library.removeFromTop(42).reduced(8, 6));
    assetLibrary.setBounds(library.reduced(4, 0));
    area.removeFromLeft(3);
    inspectorBounds = area.removeFromRight(300);
    auto inspector = inspectorBounds;
    inspectorHeader.setBounds(inspector.removeFromTop(30));
    inspectorTabs.setBounds(inspectorHeader.getBounds());
    cameraPanel.setBounds(inspector);
    clipTimingPanel.setBounds(inspector);
    effectsPanel.setBounds(inspector);
    propertyInspector.setBounds(inspector);
    area.removeFromRight(3);
    previewWidth = area.getWidth();
    auto editing = area.removeFromLeft(juce::roundToInt((previewWidth - 7) * previewFraction));
    previewDivider.setBounds(area.removeFromLeft(7));
    auto output = area;
    outputHeader.setBounds(output.removeFromTop(30));
    auto monitorBounds = outputHeader.getBounds().withTrimmedLeft(68).reduced(4, 3);
    canvasButton.setBounds(monitorBounds.removeFromRight(64));
    monitorBounds.removeFromRight(6);
    monitorOutput.setBounds(monitorBounds.withWidth(std::min(190, monitorBounds.getWidth())));
    output.removeFromTop(3);
    visualiser.setBounds(output);
    viewportBounds = editing;
    viewportHeader.setBounds(editing.removeFromTop(30));
    auto viewControls = viewportHeader.getBounds().reduced(5, 3);
    const auto fullViewControls = viewportHeader.getWidth() >= 400;
    compositionTitle.setVisible(fullViewControls);
    if (fullViewControls) { compositionTitle.setBounds(viewControls.removeFromLeft(100)); }
    frameView.setVisible(fullViewControls);
    if (fullViewControls) { frameView.setBounds(viewControls.removeFromRight(36)); viewControls.removeFromRight(3); }
    const auto namedNavigation = viewportHeader.getWidth() >= 320;
    navigateView.setBounds(viewControls.removeFromRight(namedNavigation ? 76 : 40));
    viewControls.removeFromRight(3);
    pathView.setVisible(viewportHeader.getWidth() >= 200);
    if (pathView.isVisible()) { pathView.setBounds(viewControls.removeFromRight(44)); viewControls.removeFromRight(3); }
    transformTool.setBounds(viewControls.removeFromLeft(std::min(86, viewControls.getWidth())));
    navigateView.setButtonText(composition.isNavigating() ? "Done" : namedNavigation ? "Navigate" : "3D");
    composition.setBounds(editing.withTrimmedTop(3));
}

void MotionEditor::paintOverChildren(juce::Graphics& graphics) {
    if (findActiveOverlay<osci::OverlayComponent>() != nullptr) { return; }
    graphics.setColour(osci::Colours::outlineSubtle());
    graphics.drawVerticalLine(timelineTabs.getRight() + 1, static_cast<float>(timelineHeader.getY() + 8), static_cast<float>(timelineHeader.getBottom() - 8));
}

void MotionEditor::paint(juce::Graphics& graphics) {
    graphics.fillAll(osci::Colours::veryDark());
    graphics.setColour(osci::Colours::surface());
    for (const auto& panel : { libraryBounds, viewportBounds, inspectorBounds, timelineBounds }) {
        graphics.fillRoundedRectangle(panel.toFloat(), 5.0f);
    }
}

void MotionEditor::filesDropped(const juce::StringArray& files, int, int) {
    for (const auto& file : files) {
        openSourceFile(juce::File(file));
    }
}

bool MotionEditor::openSourceFile(const juce::File& file) {
    const auto extension = file.getFileExtension().toLowerCase();
    if (extension != ".obj" && extension != ".svg" && extension != ".txt" && extension != ".lua" && extension != ".lsystem" && !motion::Document::isRasterSource(extension)
        && !motion::Document::isMidiSource(extension) && extension != ".gpla" && extension != ".json" && extension != ".lottie"
        && extension != ".wav" && extension != ".wave" && extension != ".aif" && extension != ".aiff" && extension != ".flac" && extension != ".ogg") {
        importError = "This source type is not connected yet.";
        assetLibrary.setError(importError);
        repaint();
        return false;
    }
    SourceRequest request {file, processor.position.load(), processor.document.generation(), {}};
    if (extension == ".lua" || extension == ".lsystem" || motion::Document::isRasterSource(extension)) {
        preparationRequests.push_back(std::move(request));
        showNextPreparationSettings();
    } else {
        beginSourceImport(std::move(request));
    }
    return true;
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
        chooser = std::make_unique<juce::FileChooser>("Import media", processor.getLastOpenedDirectory(), "*.obj;*.svg;*.txt;*.lua;*.lsystem;*.png;*.jpg;*.jpeg;*.gif;*.mp4;*.mov;*.gpla;*.json;*.lottie;*.mid;*.midi;*.wav;*.wave;*.aif;*.aiff;*.flac;*.ogg");
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
    auto overlay = std::make_unique<osci::ComponentOverlay>(std::move(panel), id == 0 ? "Add Blender source" : "Blender source", juce::Point<int>(460, id != 0 ? 358 : 310), true);
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
        if (listening.failed()) { owner->assetLibrary.setError(listening.getErrorMessage()); }
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
                    result = motion::Document::decodeAsset(*asset, &task->cancelled, &task->progress);
                } else { result = juce::Result::fail(archive.error); }
            } catch (const std::exception& error) { result = juce::Result::fail("Cannot prepare capture: " + juce::String(error.what())); }
            juce::MessageManager::callAsync([owner, generation, identity, task, asset, result] {
                if (owner == nullptr) { return; }
                std::erase(owner->pendingImports, task);
                if (task->cancelled.load() || owner->processor.document.generation() != generation) { return; }
                auto& document = owner->processor.document;
                const auto& sources = document.mainProject().assets;
                if (std::none_of(sources.begin(), sources.end(), [&](const auto& source) { return source->liveIdentity == identity; })) { return; }
                if (result.failed()) { owner->assetLibrary.setError(result.getErrorMessage()); return; }
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
    if (preparationSettingsOpen) { return; }
    while (!preparationRequests.empty() && preparationRequests.front().generation != processor.document.generation()) { preparationRequests.pop_front(); }
    if (preparationRequests.empty()) { return; }
    auto request = std::move(preparationRequests.front());
    preparationRequests.pop_front();
    const auto name = request.replacement != nullptr ? request.replacement->name : request.file.getFileName();
    const auto extension = request.replacement != nullptr ? request.replacement->extension : request.file.getFileExtension();
    const bool raster = motion::Document::isRasterSource(extension);
    const bool video = motion::Document::isVideoSource(extension);
    const bool text = extension.equalsIgnoreCase(".txt");
    const bool fractal = extension.equalsIgnoreCase(".lsystem");
    const bool editLua = extension.equalsIgnoreCase(".lua") && request.replacement != nullptr;
    std::unique_ptr<juce::Component> content;
    MotionBakeSettingsPanel* luaPanel = nullptr;
    MotionRasterSettingsPanel* imagePanel = nullptr;
    MotionFractalSettingsPanel* fractalPanel = nullptr;
    MotionTextSourcePanel* textPanel = nullptr;
    MotionLuaSourcePanel* sourcePanel = nullptr;
    if (text) {
        const auto instances = motion::sourceReferenceCount(processor.document.mainProject(), request.replacement->id);
        const auto draft = request.editedText.value_or(juce::String::fromUTF8(static_cast<const char*>(request.replacement->data.getData()), static_cast<int>(request.replacement->data.getSize())));
        auto panel = std::make_unique<MotionTextSourcePanel>(draft, instances, request.textSettings.value_or(request.replacement->textSettings), request.preparationError);
        textPanel = panel.get();
        content = std::move(panel);
    } else if (editLua) {
        const auto instances = motion::sourceReferenceCount(processor.document.mainProject(), request.replacement->id);
        const auto code = request.editedText.value_or(juce::String::fromUTF8(static_cast<const char*>(request.replacement->data.getData()), static_cast<int>(request.replacement->data.getSize())));
        auto panel = std::make_unique<MotionLuaSourcePanel>(code, request.retrySettings.value_or(request.replacement->bakeSettings), instances, request.preparationError);
        sourcePanel = panel.get();
        content = std::move(panel);
    } else if (raster) {
        auto panel = std::make_unique<MotionRasterSettingsPanel>(request.replacement != nullptr ? request.replacement->rasterSettings : motion::RasterSettings(), video);
        imagePanel = panel.get();
        content = std::move(panel);
    } else if (fractal) {
        const auto initialDepth = request.fractalDepth.value_or(request.replacement != nullptr ? request.replacement->fractalDepth : 3);
        auto panel = std::make_unique<MotionFractalSettingsPanel>(initialDepth);
        fractalPanel = panel.get();
        content = std::move(panel);
    } else {
        motion::BakeSettings initial;
        initial.bpm = processor.document.project().bpm;
        initial.frameRate = processor.document.project().frameRate;
        if (request.replacement != nullptr) { initial = request.replacement->bakeSettings; }
        auto panel = std::make_unique<MotionBakeSettingsPanel>(initial);
        luaPanel = panel.get();
        content = std::move(panel);
    }
    auto overlay = std::make_unique<osci::ComponentOverlay>(std::move(content), (text || editLua ? "Edit " : (raster || fractal ? "Prepare " : "Bake ")) + name, juce::Point<int>(editLua ? 920 : text ? 560 : 440, editLua ? 550 : fractal ? 170 : video ? 444 : 400), true);
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
    if (luaPanel != nullptr) { luaPanel->onBake = [submit](motion::BakeSettings settings) mutable { submit(settings, {}); }; }
    if (sourcePanel != nullptr) {
        sourcePanel->onBake = [submit, original = request.replacement](motion::BakeSettings settings, juce::String code) mutable {
            const auto unchanged = code == juce::String::fromUTF8(static_cast<const char*>(original->data.getData()), static_cast<int>(original->data.getSize()));
            submit(settings, {}, unchanged ? std::optional<juce::String>() : std::optional<juce::String>(std::move(code)));
        };
    }
    if (imagePanel != nullptr) { imagePanel->onPrepare = [submit](motion::RasterSettings settings) mutable { submit({}, settings); }; }
    if (fractalPanel != nullptr) { fractalPanel->onPrepare = [submit](int depth) mutable { submit({}, {}, {}, {}, depth); }; }
    if (textPanel != nullptr) { textPanel->onApply = [submit](juce::String text, motion::TextSettings settings) mutable { submit({}, {}, std::move(text), std::move(settings)); }; }
    showOverlay(std::move(overlay));
}

void MotionEditor::beginSourceImport(SourceRequest request, motion::BakeSettings settings, motion::RasterSettings rasterSettings) {
    if (request.generation != processor.document.generation()) { return; }
    const auto extension = request.replacement != nullptr ? request.replacement->extension : request.file.getFileExtension();
    if (motion::Document::isVideoSource(extension) && request.uniqueClip == 0) {
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
    assetLibrary.setError({});
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
                    result = request.uniqueClip != 0 ? juce::Result::ok() : motion::Document::decodeAsset(*asset, &task->cancelled, &task->progress, videoDecoder);
                } else if (request.file.getSize() > static_cast<juce::int64>(motion::Document::maximumSourceBytes)) {
                    result = juce::Result::fail("This source exceeds the 64 MiB preparation limit.");
                } else {
                    result = request.file.loadFileAsData(asset->data) ? motion::Document::decodeAsset(*asset, &task->cancelled, &task->progress, videoDecoder) : juce::Result::fail("Cannot read the source file.");
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
            if (task->cancelled.load() || owner->processor.document.generation() != generation) { return; }
            if (result.failed()) {
                owner->importError = result.getErrorMessage();
                owner->assetLibrary.setError(owner->importError);
                owner->repaint();
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
                if (separated.failed()) { owner->assetLibrary.setError(separated.getErrorMessage()); return; }
                owner->assetLibrary.refresh();
                owner->assetLibrary.selectAsset(asset->id);
                owner->libraryTabs.setSelectedIndex(0);
                owner->select(request.uniqueClip);
                return;
            }
            if (request.replacement != nullptr) {
                const auto& assets = document.project().assets;
                if (std::find(assets.begin(), assets.end(), request.replacement) == assets.end()) {
                    owner->assetLibrary.setError("The source changed while baking. Its previous result has been kept.");
                    return;
                }
                asset->id = request.replacement->id;
                document.edit(request.editedText.has_value() ? (asset->extension.equalsIgnoreCase(".lua") ? "Edit Lua source" : "Edit text source") : "Rebuild source cache", [&](motion::Project& project) {
                    for (auto& item : project.assets) {
                        if (item == request.replacement) { item = asset; }
                    }
                });
                owner->assetLibrary.refresh();
                owner->assetLibrary.selectAsset(asset->id);
                return;
            }
            asset->id = document.newId();
            if (asset->midi != nullptr) {
                document.edit("Import MIDI file", [&](motion::Project& project) { project.assets.push_back(asset); });
                owner->assetLibrary.refresh(); owner->assetLibrary.selectAsset(asset->id);
                return;
            }
            auto clip = motion::Document::makeClip(document.newId(), *asset, time);
            motion::Track track;
            track.id = document.newId();
            track.name = asset->name.toStdString();
            track.kind = asset->audio != nullptr ? motion::TrackKind::audio : motion::TrackKind::visual;
            track.insert(clip);
            document.edit(asset->audio != nullptr ? "Import soundtrack" : "Import object", [&](motion::Project& project) {
                project.assets.push_back(asset);
                project.tracks.push_back(track);
                project.duration = std::max(project.duration, clip.timing(project.bpm).end());
            });
            owner->assetLibrary.refresh();
            owner->assetLibrary.selectAsset(asset->id);
            owner->select(clip.id);
        });
    });
}

void MotionEditor::timerCallback() {
    refreshOutputChoices();
    auto& previewRate = processor.recordingParameters.frameRate;
    // Keep playback and export cadence aligned within the live renderer's supported range.
    const auto projectFrameRate = static_cast<float>(std::clamp<double>(processor.document.mainProject().frameRate, previewRate.min, previewRate.max));
    if (!visualiser.isRecording() && std::abs(previewRate.getValueUnnormalised() - projectFrameRate) > 0.005f) {
        previewRate.setUnnormalisedValueNotifyingHost(projectFrameRate);
    }
    canvasButton.setEnabled(!visualiser.isRecording() && exportState == nullptr);
    processor.collectPreparedState();
    assetLibrary.updateLiveStatus();
    const auto preparationError = processor.getPreparationError();
    if (preparationError != lastPreparationError) {
        lastPreparationError = preparationError;
        if (preparationError.isNotEmpty()) { osci::showOverlayMessage(*this, "Cannot play composition", preparationError); }
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
    playButton.setIcon(playing ? motion::style::IconButton::Icon::pause : motion::style::IconButton::Icon::play);
    if (playButton.getName() != (playing ? "Pause" : "Play")) {
        playButton.setName(playing ? "Pause" : "Play");
        playButton.setTitle(playButton.getName());
    }
    if (timeLabel.isBeingEdited() && (positionEditGeneration != processor.document.generation() || positionEditRevision != processor.document.revision())) { timeLabel.hideEditor(true); }
    if (!timeLabel.isBeingEdited()) { timeLabel.setText(juce::String(processor.document.project().timeGrid().positionLabel(processor.position.load())), juce::dontSendNotification); }
    if (!tempoValue.isBeingEdited()) { tempoValue.setText(juce::String(processor.document.project().bpm, 1), juce::dontSendNotification); }
    timeline.repaint();
    if (notesEditor.isVisible()) { notesEditor.repaint(); }
    curveEditor.repaint();
    composition.repaint();
    refreshInspector();
    if (effectsPanel.isVisible()) { effectsPanel.updateValues(); }
}

void MotionEditor::changeListenerCallback(juce::ChangeBroadcaster*) {
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
    effectsPanel.refresh();
    modulationPanel.refresh();
    curveEditor.refresh();
    notesEditor.refresh();
    composition.refresh();
    refreshInspector();
    resized();
    repaint();
}

void MotionEditor::enterComposition(motion::Id id, bool fromLibrary) {
    const auto& project = processor.document.project();
    motion::Id definition = 0;
    double time = 0;
    for (const auto& track : project.tracks) {
        for (const auto& clip : track.clips) {
            if (clip.id != id || clip.composition == 0) { continue; }
            definition = clip.composition;
            const auto clock = clip.timing(project.bpm);
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
    previous.timelineTab = timelineTabs.getCurrentTabIndex(); previous.inspectorTab = inspectorTabs.getCurrentTabIndex();
    previous.curveTarget = curveTarget; previous.property = curvePropertyName; previous.cameraCurve = cameraCurve;
    previous.timeline = timeline.viewState(); previous.preview = composition.viewState();
    previous.graph = curveEditor.viewState(); previous.notes = notesEditor.viewState();
    previous.effects = effectsPanel.viewState(); previous.cameraSelection = cameraPanel.selectedCameraId();
    composition.setNavigating(false);
    const auto entered = processor.document.enterComposition(definition);
    if (entered.failed()) { assetLibrary.setError(entered.getErrorMessage()); return; }
    scopeHistory.push_back(previous);
    assetLibrary.setError({});
    processor.playing.store(false);
    processor.seek(std::clamp(time, 0.0, processor.document.project().duration));
    composition.restoreView({});
    select(0); timeline.scrollRows = 0; timeline.scrollTime = 0; timeline.revealTime(time);
    timelineTabs.setSelectedIndex(0);
    changeListenerCallback(nullptr);
}

void MotionEditor::leaveComposition() {
    ScopeView previous;
    if (!scopeHistory.empty()) { previous = scopeHistory.back(); scopeHistory.pop_back(); }
    auto entered = processor.document.enterComposition(previous.scope);
    if (entered.failed()) { previous = {}; processor.document.enterComposition(0); scopeHistory.clear(); }
    assetLibrary.setError({});
    processor.playing.store(false); processor.seek(previous.position);
    select(previous.selection);
    cameraPanel.restoreSelection(previous.cameraSelection);
    timelineTabs.setSelectedIndex(previous.timelineTab);
    inspectorTabs.setSelectedIndex(previous.inspectorTab);
    effectsPanel.restoreView(previous.effects);
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
    effectsPanel.setSelectedClip(id);
    if (inspectorTabs.getCurrentTabIndex() != 3) { inspectorTabs.setSelectedIndex(0); }
    timeline.setSelection(id);
    timeline.revealSelection();
    composition.selected = id;
    selectCurveTarget(id, curvePropertyName, false);
    refreshInspector();
    resized();
    repaint();
}

bool MotionEditor::audioSelected() const {
    const auto target = motion::findPropertyTarget(processor.document.project(), selection);
    return target.has_value() && target->isAudio;
}

void MotionEditor::refreshInspector() {
    cameraPanel.refresh();
    clipTimingPanel.refresh();
    const motion::Clip* selected = nullptr;
    for (const auto& track : processor.document.project().tracks) {
        for (const auto& clip : track.clips) {
            if (clip.id == selection) {
                selected = &clip;
            }
        }
    }
    splitButton.setEnabled(canSplitClip(selected, processor.position.load(), processor.document.project().bpm));
    const auto target = motion::findPropertyTarget(processor.document.project(), selection);
    const bool editable = target.has_value() && !target->camera && !target->isEffect;
    const auto tabName = editable && target->isGroup ? "Group" : (audioSelected() ? "Audio" : "Object");
    if (inspectorTabs.getTabNames()[0] != tabName) { inspectorTabs.setTabName(0, tabName); }
    propertyInspector.setTarget(editable ? selection : 0);
}

bool MotionEditor::keyPressed(const juce::KeyPress& key) {
    for (const auto& command : commands) {
        if (command.key.isValid() && command.key == key) {
            command.action();
            return true;
        }
    }
    return CommonPluginEditor::keyPressed(key);
}

void MotionEditor::addCommand(int menu, juce::String name, juce::KeyPress key, juce::String shortcut, std::function<void()> action) {
    menus.addMenuItem(menu, name, action, shortcut);
    commands.push_back({std::move(name), std::move(shortcut), key, std::move(action)});
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
    addCommand(1, "Select all", juce::KeyPress('a', command, 0), "Cmd+A", [this] { timeline.selectAll(); });
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
    addCommand(2, "Show keyframe lanes", juce::KeyPress('u', 0, 0), "U", [this] { timeline.toggleLanesForSelection(); });
    addCommand(2, "Edit notes", juce::KeyPress(), {}, [this] { timelineTabs.setSelectedIndex(2); });
    addCommand(2, "Edit curves", juce::KeyPress(), {}, [this] { timelineTabs.setSelectedIndex(1); });
    addCommand(3, "Play / pause", juce::KeyPress(juce::KeyPress::spaceKey), "Space", [this] { processor.playing.store(!processor.playing.load()); });
    addCommand(3, "Go to start", juce::KeyPress(juce::KeyPress::homeKey), "Home", [this] { processor.seek(0); timeline.revealTime(0); });
    addCommand(3, "Go to end", juce::KeyPress(juce::KeyPress::endKey), "End", [this] {
        const auto end = processor.document.project().duration;
        processor.seek(end); timeline.revealTime(end);
    });
    addCommand(3, "Previous frame", juce::KeyPress(juce::KeyPress::leftKey), "Left", [this] { stepFrames(-1); });
    addCommand(3, "Next frame", juce::KeyPress(juce::KeyPress::rightKey), "Right", [this] { stepFrames(1); });
    addCommand(3, "Back ten frames", juce::KeyPress(juce::KeyPress::leftKey, shift, 0), "Shift+Left", [this] { stepFrames(-10); });
    addCommand(3, "Forward ten frames", juce::KeyPress(juce::KeyPress::rightKey, shift, 0), "Shift+Right", [this] { stepFrames(10); });
    addCommand(3, "Previous keyframe", juce::KeyPress('j', 0, 0), "J", [this] { jumpToKey(false); });
    addCommand(3, "Next keyframe", juce::KeyPress('k', 0, 0), "K", [this] { jumpToKey(true); });
    menus.addMenuSeparator(3);
    addCommand(3, "Zoom timeline in", juce::KeyPress('=', command, 0), "Cmd+=", [this] { timeline.zoomBy(1.5); });
    addCommand(3, "Zoom timeline out", juce::KeyPress('-', command, 0), "Cmd+-", [this] { timeline.zoomBy(1 / 1.5); });
    addCommand(3, "Fit timeline to project", juce::KeyPress(), "F", [this] { timeline.fitToProject(); });
    menus.addMenuSeparator(5);
    addCommand(5, "Keyboard shortcuts...", juce::KeyPress('/', command, 0), "Cmd+/", [this] { showShortcuts(); });
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

void MotionEditor::jumpToKey(bool forward) {
    const auto target = motion::findPropertyTarget(processor.document.project(), selection);
    if (!target.has_value() || target->properties == nullptr || target->rate == 0) { return; }
    const auto now = processor.position.load();
    std::optional<double> best;
    for (const auto& [name, curve] : *target->properties) {
        for (const auto& key : curve.keyframes()) {
            const auto time = target->start + (key.time - target->offset) / target->rate;
            const bool candidate = forward ? time > now + 1.0e-6 : time < now - 1.0e-6;
            if (candidate && (!best.has_value() || (forward ? time < *best : time > *best))) { best = time; }
        }
    }
    if (!best.has_value()) { return; }
    const auto time = std::clamp(*best, 0.0, processor.document.project().duration);
    processor.seek(time);
    timeline.revealTime(time);
}

void MotionEditor::splitAtPlayhead() {
    splitButton.triggerClick();
}

void MotionEditor::showShortcuts() {
    juce::String text;
    for (const auto& command : commands) {
        if (command.shortcut.isNotEmpty()) { text << command.shortcut.paddedRight(' ', 14) << command.name << "\n"; }
    }
    text << "\nTimeline\n"
         << "V / B / S / R   Move, ripple trim, slip, stretch tools\n"
         << "M               Add marker     [ ]  Previous / next marker\n"
         << "Alt             Bypass snapping while dragging\n"
         << "Cmd+wheel       Zoom          Double-click lane: add key\n"
         << "\nComposition view\n"
         << "G / R / S       Move, rotate, scale    F  Frame selection    N  Navigate    P  Motion path\n";
    osci::showOverlayMessage(*this, "Keyboard shortcuts", text, osci::ErrorOverlay::Icon::None, {620, 560}, juce::Justification::centredLeft);
}

void MotionEditor::exportVideo() {
#if OSCI_PREMIUM
    if (exportState != nullptr) { return; }
    if (!processor.ensureFFmpegExists()) { return; }
    std::shared_ptr<OfflineVisualiserParameters> beamSnapshot;
    try {
        beamSnapshot = captureOfflineVisualiserParameters();
    } catch (const std::exception& error) {
        assetLibrary.setError("Cannot capture the beam settings: " + juce::String(error.what()));
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
    auto overlay = std::make_unique<osci::ComponentOverlay>(std::move(settings), "Export video", juce::Point<int>(440, 388), true);
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
                    owner->assetLibrary.setError("Video export requires a ." + config.fileExtension + " filename. Choose Export video again and use that extension.");
                    return;
                }
                owner->processor.setLastOpenedDirectory(destination.getParentDirectory());
                auto& recording = owner->processor.recordingParameters;
                recording.setCanvasSize(config.renderSize);
                recording.frameRate.setUnnormalisedValueNotifyingHost(static_cast<float>(config.frameRate));
                recording.qualityParameter.setUnnormalisedValueNotifyingHost(static_cast<float>((51.0 - config.crf) / 50.0));
                recording.losslessVideo.setBoolValue(config.crf == 0);
                recording.recordAudio.setBoolValue(config.includeAudio);
                recording.videoCodec = config.codec;
                recording.compressionPreset = config.compressionPreset;
                juce::MessageManager::callAsync([owner, state, project, beamSnapshot, renderMode, config, destination] {
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
                    owner->exports.addJob([owner, state, project, beamSnapshot, renderMode, config, destination, preparationOverlay] {
                        std::shared_ptr<MotionVideoTemporaryFiles> temporary;
                        auto result = juce::Result::ok();
                        try {
                            temporary = std::make_shared<MotionVideoTemporaryFiles>();
                            result = temporary->directory.createDirectory();
                            if (result.wasOk() && !state->cancelled.load()) {
                                const auto rate = state->sampleRate;
                                const motion::PreparedComposition prepared(project, rate, &state->cancelled);
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
                        juce::MessageManager::callAsync([owner, state, temporary, result, beamSnapshot, renderMode, config, destination, preparationOverlay] {
                            if (owner == nullptr) { return; }
                            if (state->cancelled.load() || result.failed()) {
                                if (preparationOverlay != nullptr) { owner->dismissOverlay(preparationOverlay.getComponent()); }
                                owner->exportState.reset();
                                if (!state->cancelled.load()) { owner->assetLibrary.setError(result.getErrorMessage()); }
                                return;
                            }
                            auto startRender = [owner, state, temporary, config, destination, renderMode, beamSnapshot] {
                                if (owner == nullptr) { return; }
                                owner->startOfflineVideoRender(temporary->signal(), config.includeAudio ? temporary->soundtrack() : juce::File(),
                                destination, config, renderMode, [owner, state, temporary] {
                                    // Completion can run from base-editor destruction;
                                    // touch derived UI only in a later safe callback.
                                    juce::MessageManager::callAsync([owner, state] {
                                        if (owner != nullptr && owner->exportState == state) { owner->exportState.reset(); }
                                    });
                                }, beamSnapshot);
                            };
                            if (preparationOverlay != nullptr) {
                                owner->dismissOverlay(preparationOverlay.getComponent(), std::move(startRender));
                            } else {
                                startRender();
                            }
                        });
                    });
                });
            });
        });
    };
    showOverlay(std::move(overlay));
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
            owner->assetLibrary.setError("Signal export requires a .wav filename. Choose Export XYRGB signal again and use that extension.");
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
                    owner->assetLibrary.setError(result.getErrorMessage());
                }
            });
        });
    });
}

void MotionEditor::selectCurveTarget(motion::Id id, const std::string& property, bool camera) {
    curveTarget = id;
    cameraCurve = camera;
    curveProperty.clear(juce::dontSendNotification);
    curveProperties.clear();
    const auto* effect = motion::findEffect(processor.document.project(), id);
    const auto* definition = effect == nullptr ? nullptr : motion::effectDefinition(effect->type);
    const auto target = motion::findPropertyTarget(processor.document.project(), id);
    const bool audio = target.has_value() && target->isAudio;
    const auto count = audio ? 2 : definition != nullptr ? definition->parameters.size() : (camera ? motion::cameraPropertyNames.size() : motion::propertyNames.size());
    int selectedIndex = 0;
    for (std::size_t index = 0; index < count; ++index) {
        const std::string name = audio ? (index == 0 ? "gain" : "pan") : definition != nullptr ? definition->parameters[index].id : (camera ? motion::cameraPropertyNames[index] : motion::propertyNames[index]);
        curveProperties.push_back(name);
        curveProperty.addItem(definition != nullptr ? juce::String(definition->parameters[index].name) : juce::String(name).replace(".", " "), static_cast<int>(index) + 1);
        if (property == name) {
            selectedIndex = static_cast<int>(index);
        }
    }
    curveProperty.setSelectedId(selectedIndex + 1, juce::dontSendNotification);
    curvePropertyName = curveProperties[static_cast<std::size_t>(selectedIndex)];
    curveEditor.setSelection(id, curvePropertyName);
    modulationPanel.setTarget(id, curvePropertyName);
}


void MotionEditor::refreshTiming() {
    const auto& project = processor.document.project();
    const auto display = project.timeDisplay == motion::TimeDisplay::beats ? "Beats" : (project.timeDisplay == motion::TimeDisplay::frames ? "Frames" : "Seconds");
    juce::String grid = "Free";
    if (project.gridSnap) {
        if (project.timeDisplay != motion::TimeDisplay::beats) { grid = "Frame"; }
        else if (std::abs(project.snapBeats - project.beatsPerBar) < 1.0e-9) { grid = "Bar"; }
        else if (std::abs(project.snapBeats - 1) < 1.0e-9) { grid = "Beat"; }
        else if (std::abs(project.snapBeats - 1.0 / 3) < 1.0e-9) { grid = "1/8 T"; }
        else if (std::abs(project.snapBeats - 1.0 / 6) < 1.0e-9) { grid = "1/16 T"; }
        else { grid = "1/" + juce::String(4.0 / project.snapBeats, 0); }
    }
    timingButton.setButtonText(juce::String(display) + " / " + grid + " " + juce::String::charToString(0x25be));
    timeline.repaint();
    curveEditor.repaint();
}

void MotionEditor::showTimingMenu() {
    const auto& project = processor.document.project();
    juce::PopupMenu menu;
    menu.setLookAndFeel(&getLookAndFeel());
    menu.addSectionHeader("Time display");
    menu.addItem(101, "Seconds", true, project.timeDisplay == motion::TimeDisplay::seconds);
    menu.addItem(102, "Frames", true, project.timeDisplay == motion::TimeDisplay::frames);
    menu.addItem(103, "Bars / beats", true, project.timeDisplay == motion::TimeDisplay::beats);
    menu.addSeparator();
    menu.addItem(200, "Snap to grid", true, project.gridSnap);
    const std::array<double, 7> divisions { static_cast<double>(project.beatsPerBar), 1, 0.5, 0.25, 0.125, 1.0 / 3, 1.0 / 6 };
    const std::array<const char*, 7> names { "1 bar", "1 beat", "1/8 note", "1/16 note", "1/32 note", "1/8 triplet", "1/16 triplet" };
    juce::PopupMenu beatGrid;
    for (std::size_t i = 0; i < divisions.size(); ++i) { beatGrid.addItem(300 + static_cast<int>(i), names[i], true, std::abs(project.snapBeats - divisions[i]) < 1.0e-9); }
    menu.addSubMenu("Beat grid", beatGrid, project.timeDisplay == motion::TimeDisplay::beats);
    juce::PopupMenu meter;
    for (const auto beats : { 2, 3, 4, 5, 6, 7 }) { meter.addItem(400 + beats, juce::String(beats) + "/4", true, project.beatsPerBar == beats); }
    menu.addSubMenu("Meter", meter);
    const std::array<double, 8> rates { 24000.0 / 1001, 24, 25, 30000.0 / 1001, 30, 50, 60, 120 };
    const std::array<const char*, 8> rateNames { "23.976 fps", "24 fps", "25 fps", "29.97 fps", "30 fps", "50 fps", "60 fps", "120 fps" };
    juce::PopupMenu frames;
    for (std::size_t i = 0; i < rates.size(); ++i) { frames.addItem(500 + static_cast<int>(i), rateNames[i], true, std::abs(project.frameRate - rates[i]) < 1.0e-9); }
    menu.addSubMenu("Frame rate", frames);
    const juce::Component::SafePointer<MotionEditor> owner(this);
    const auto generation = processor.document.generation();
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(timingButton), [owner, generation, divisions, rates](int result) {
        if (owner == nullptr || result == 0 || owner->processor.document.generation() != generation) { return; }
        const auto& current = owner->processor.document.project();
        const auto subdivision = result == 300 ? static_cast<double>(current.beatsPerBar)
            : (result > 300 && result < 307 ? divisions[static_cast<std::size_t>(result - 300)] : current.snapBeats);
        if (result >= 101 && result <= 103 && static_cast<int>(current.timeDisplay) == result - 101) { return; }
        if (result >= 300 && result < 307 && current.gridSnap && std::abs(current.snapBeats - subdivision) < 1.0e-9) { return; }
        if (result >= 402 && result <= 407 && current.beatsPerBar == result - 400) { return; }
        if (result >= 500 && result < 508 && std::abs(current.frameRate - rates[static_cast<std::size_t>(result - 500)]) < 1.0e-9) { return; }
        owner->processor.document.edit("Change timeline grid", [result, subdivision, rates](motion::Project& state) {
            if (result >= 101 && result <= 103) {
                state.timeDisplay = static_cast<motion::TimeDisplay>(result - 101);
            } else if (result == 200) {
                state.gridSnap = !state.gridSnap;
            } else if (result >= 300 && result < 307) {
                state.snapBeats = subdivision;
                state.gridSnap = true;
            } else if (result >= 402 && result <= 407) {
                if (std::abs(state.snapBeats - state.beatsPerBar) < 1.0e-9) { state.snapBeats = result - 400; }
                state.beatsPerBar = result - 400;
            } else if (result >= 500 && result < 508) { state.frameRate = rates[static_cast<std::size_t>(result - 500)]; }
        });
    });
}

void MotionEditor::refreshOutputChoices() {
    auto* holder = juce::StandalonePluginHolder::getInstance();
    auto* device = holder != nullptr ? holder->deviceManager.getCurrentAudioDevice() : nullptr;
    monitorOutput.setItemEnabled(3, device != nullptr && device->getOutputChannelNames().size() >= 5);
}

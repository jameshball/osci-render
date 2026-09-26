#include "MotionEditor.h"
#include "export/SignalExporter.h"
#include "export/SoundtrackExporter.h"
#include "ui/VideoExportSettings.h"
#include <cstdlib>

namespace {
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
bool canSplitClip(const motion::Clip* clip, double time) {
    return clip != nullptr && clip->valid() && std::isfinite(time) && time > clip->start && time < clip->end();
}
}

MotionEditor::MotionEditor(MotionProcessor& ownerProcessor)
    : CommonPluginEditor(ownerProcessor, "osci-motion", "osci-motion", 1440, 900), processor(ownerProcessor), timeline(ownerProcessor), composition(ownerProcessor), assetLibrary(ownerProcessor.document), curveEditor(ownerProcessor), cameraPanel(ownerProcessor), effectsPanel(ownerProcessor), modulationPanel(ownerProcessor) {
    lookAndFeel.setControlCornerRadius(3.0f);
    menus.addTopLevelMenu("File");
    menus.addProjectMenuItems(0, processor, *this);
    menus.addMenuSeparator(0);
    menus.addMenuItem(0, "Export XYRGB signal...", [this] { exportSignal(); });
#if OSCI_PREMIUM
    menus.addMenuItem(0, "Export video...", [this] { exportVideo(); });
#endif
    menus.addTopLevelMenu("Edit");
    menus.addEditMenuItems(1, processor);
    menus.addTopLevelMenu("Audio");
    menus.addStandaloneAudioSettingsMenuItem(2, processor, *this);
    menus.addTopLevelMenu("Interface");
    menus.addCommonInterfaceMenuItems(3, processor, *this);
    initialiseMenuBar(menus);
    for (auto* header : { &libraryHeader, &viewportHeader, &outputHeader, &inspectorHeader, &timelineHeader }) {
        addAndMakeVisible(header);
    }
    for (auto* component : std::initializer_list<juce::Component*> { &timeline, &composition, &assetLibrary, &importButton, &playButton, &splitButton, &timeLabel, &selectionLabel, &curveEditor, &timelineTabs, &curveProperty, &timelineDivider, &previewDivider, &cameraPanel, &inspectorTabs }) {
        addAndMakeVisible(component);
    }
    addAndMakeVisible(navigateView);
    addAndMakeVisible(frameView);
    navigateView.setName("Navigate composition view");
    navigateView.setWantsKeyboardFocus(false);
    navigateView.setMouseClickGrabsKeyboardFocus(false);
    navigateView.setTooltip("Explore the scene with the mouse and WASD or arrow keys. Esc finishes. This does not change the output camera. Shortcut: N.");
    navigateView.setColour(juce::TextButton::buttonColourId, osci::Colours::surfaceRaised());
    navigateView.onClick = [this] { composition.setNavigating(!composition.isNavigating()); };
    composition.onNavigationChanged = [this](bool active) {
        navigateView.setToggleState(active, juce::dontSendNotification);
        navigateView.setButtonText(active ? "Done" : viewportHeader.getWidth() >= 230 ? "Navigate" : "3D");
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
    playButton.setColour(juce::TextButton::buttonColourId, osci::Colours::surfaceRaised());
    curveProperty.setColour(juce::ComboBox::backgroundColourId, osci::Colours::surfaceRaised());
    timingButton.setName("Time and grid");
    timingButton.setTitle("Time and grid");
    timingButton.setTooltip("Time display, snapping, meter and frame rate. Alt temporarily bypasses snapping.");
    timingButton.onClick = [this] { showTimingMenu(); };
    refreshTiming();
    tempoLabel.setText("BPM", juce::dontSendNotification);
    timeLabel.setName("Timeline position");
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
            processor.document.edit("Change tempo", [value](motion::Project& project) { project.bpm = value; });
        }
    };
    addChildComponent(effectLibrary);
    addChildComponent(effectsPanel);
    libraryHeader.setVisible(false);
    libraryTabs.setName("Library tabs");
    inspectorTabs.setName("Inspector tabs");
    inspectorTabs.setMinimumTabScaleFactor(0.7);
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
    inspectorTabs.onSelectionChanged = [this](int index) {
        cameraPanel.setVisible(index == 2);
        effectsPanel.setVisible(index == 1);
        selectionLabel.setVisible(index == 0);
        for (auto& value : values) { value.setVisible(index == 0); }
        for (auto& button : keyButtons) { button.setVisible(index == 0); }
        refreshInspector();
        if (index == 1) { effectsPanel.activate(); } else { selectCurveTarget(index == 2 ? cameraPanel.selectedCameraId() : selection, curvePropertyName, index == 2); }
        repaint();
    };
    cameraPanel.setVisible(false);
    timelineTabs.addTab("Timeline");
    timelineTabs.addTab("Graph");
    timelineTabs.onSelectionChanged = [this](int index) {
        timeline.setVisible(index == 0);
        curveEditor.setVisible(index == 1);
        modulationPanel.setVisible(index == 1);
        curveProperty.setVisible(index == 1);
        resized();
    };
    curveEditor.setVisible(false);
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
        chooser = std::make_unique<juce::FileChooser>("Import media", processor.getLastOpenedDirectory(), "*.obj;*.svg;*.txt;*.gpla;*.json;*.lottie;*.wav;*.wave;*.aif;*.aiff;*.flac;*.ogg");
        const juce::Component::SafePointer<MotionEditor> owner(this);
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [owner](const juce::FileChooser& chosen) {
                if (owner != nullptr && chosen.getResult().existsAsFile()) {
                    owner->openSourceFile(chosen.getResult());
                }
            });
    };
    playButton.onClick = [this] { processor.playing.store(!processor.playing.load()); };
    splitButton.onClick = [this] {
        const auto time = processor.position.load();
        const auto& tracks = processor.document.project().tracks;
        const bool canSplit = std::any_of(tracks.begin(), tracks.end(), [&](const motion::Track& track) {
            return std::any_of(track.clips.begin(), track.clips.end(), [&](const motion::Clip& clip) {
                return clip.id == selection && canSplitClip(&clip, time);
            });
        });
        if (!canSplit) {
            return;
        }
        const auto id = processor.document.newId();
        processor.document.edit("Split clip", [&](motion::Project& project) {
            for (auto& track : project.tracks) {
                for (std::size_t index = 0; index < track.clips.size(); ++index) {
                    if (track.clips[index].id == selection) {
                        auto parts = track.clips[index].split(time, id);
                        if (parts.has_value()) {
                            for (auto& effect : parts->second.effects) { effect.id = processor.document.newId(); }
                            track.clips[index] = std::move(parts->first);
                            track.insert(std::move(parts->second));
                        }
                        return;
                    }
                }
            }
        });
    };
    assetLibrary.onInsert = [this](motion::Id id) { timeline.insertAsset(id, -1, -1); };
    assetLibrary.onCancelImport = [this] {
        for (const auto& task : pendingImports) { task->cancelled.store(true); }
    };
    timeline.onSelection = [this](motion::Id id) { select(id); };
    composition.onSelection = timeline.onSelection;
    selectionLabel.setColour(juce::Label::textColourId, osci::Colours::text());
    selectionLabel.setFont(juce::FontOptions(14.0f, juce::Font::bold));
    for (std::size_t index = 0; index < values.size(); ++index) {
        auto& value = values[index];
        value.setEditable(false, true);
        value.setName(motion::propertyNames[index]);
        value.setTitle(motion::propertyNames[index]);
        value.setComponentID("motion." + juce::String(motion::propertyNames[index]));
        value.setJustificationType(juce::Justification::centredRight);
        value.setFont(juce::FontOptions(13.0f));
        value.setColour(juce::Label::backgroundColourId, osci::Colours::veryDark());
        value.onTextChange = [this, index] { if (!updatingInspector) { setProperty(static_cast<int>(index), false); } };
        addAndMakeVisible(value);
        auto& button = keyButtons[index];
        button.setName("Key " + juce::String(motion::propertyNames[index]));
        button.setButtonText(button.getName());
        button.setTooltip("Add or update a keyframe at the playhead");
        button.onClick = [this, index] { setProperty(static_cast<int>(index), true); };
        addAndMakeVisible(button);
    }
    processor.document.addChangeListener(this);
    composition.refresh();
    timeline.refreshTracks();
    refreshInspector();
    startTimerHz(30);
    setResizeLimits(1100, 700, 4096, 2160);
    resized();
}

MotionEditor::~MotionEditor() {
    stopTimer();
    curveEditor.onPreview = {};
    processor.previewComposition(processor.document.project());
    processor.document.removeChangeListener(this);
    for (const auto& task : pendingImports) { task->cancelled.store(true); }
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
    menuBar.setBounds(top);
    area.removeFromTop(3);
    workspaceHeight = area.getHeight();
    timelineBounds = area.removeFromBottom(std::clamp(juce::roundToInt(workspaceHeight * timelineFraction), 240, workspaceHeight - 370));
    auto timeline = timelineBounds;
    auto transport = timeline.removeFromTop(30);
    timelineHeader.setBounds(transport);
    timelineTabs.setBounds(transport.removeFromLeft(205));
    playButton.setBounds(transport.removeFromLeft(65).reduced(2));
    splitButton.setBounds(transport.removeFromLeft(65).reduced(2));
    timeLabel.setBounds(transport.removeFromLeft(110));
    tempoValue.setBounds(transport.removeFromLeft(56).reduced(1, 3));
    tempoLabel.setBounds(transport.removeFromLeft(34));
    timingButton.setBounds(transport.removeFromLeft(150).reduced(2));
    curveProperty.setBounds(transport.removeFromLeft(165).reduced(2));
    cancelExport.setBounds(transport.removeFromRight(62).reduced(2));
    exportBar.setBounds(transport.removeFromRight(180).reduced(2));
    this->timeline.setBounds(timeline.withTrimmedTop(3));
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
    inspectorBounds = area.removeFromRight(260);
    auto inspector = inspectorBounds;
    inspectorHeader.setBounds(inspector.removeFromTop(30));
    inspectorTabs.setBounds(inspectorHeader.getBounds());
    cameraPanel.setBounds(inspector);
    effectsPanel.setBounds(inspector);
    selectionLabel.setBounds(inspector.removeFromTop(36).reduced(10, 0));
    inspector.reduce(10, 0);
    for (int group = 0; group < (audioSelected() ? 2 : 5); ++group) {
        inspector.removeFromTop(22);
        auto row = inspector.removeFromTop(26);
        for (int axis = 0; axis < (audioSelected() || group == 4 ? 1 : 3); ++axis) {
            const auto index = audioSelected() ? group : group * 3 + axis;
            auto field = row.removeFromLeft(audioSelected() || group == 4 ? row.getWidth() : inspector.getWidth() / 3);
            keyButtons[index].setBounds(field.removeFromRight(18).reduced(1));
            values[index].setBounds(field.reduced(2, 1));
        }
        inspector.removeFromTop(8);
    }
    area.removeFromRight(3);
    previewWidth = area.getWidth();
    auto editing = area.removeFromLeft(juce::roundToInt((previewWidth - 7) * previewFraction));
    previewDivider.setBounds(area.removeFromLeft(7));
    auto output = area;
    outputHeader.setBounds(output.removeFromTop(30));
    auto monitorBounds = outputHeader.getBounds().withTrimmedLeft(68).reduced(4, 3);
    monitorOutput.setBounds(monitorBounds.withWidth(std::min(190, monitorBounds.getWidth())));
    output.removeFromTop(3);
    visualiser.setBounds(output);
    viewportBounds = editing;
    viewportHeader.setBounds(editing.removeFromTop(30));
    auto viewControls = viewportHeader.getBounds().reduced(5, 3);
    const auto fullViewControls = viewportHeader.getWidth() >= 230;
    frameView.setVisible(fullViewControls);
    if (fullViewControls) { frameView.setBounds(viewControls.removeFromRight(36)); viewControls.removeFromRight(3); }
    navigateView.setBounds(viewControls.removeFromRight(fullViewControls ? 76 : 40));
    navigateView.setButtonText(composition.isNavigating() ? "Done" : fullViewControls ? "Navigate" : "3D");
    composition.setBounds(editing.withTrimmedTop(3));
}

void MotionEditor::paintOverChildren(juce::Graphics& graphics) {
    if (findActiveOverlay<osci::OverlayComponent>() != nullptr) { return; }
    graphics.setColour(osci::Colours::outlineSubtle());
    for (const auto x : { splitButton.getRight() + 1, timingButton.getRight() + 1 }) {
        graphics.drawVerticalLine(x, static_cast<float>(timelineHeader.getY() + 8), static_cast<float>(timelineHeader.getBottom() - 8));
    }
}

void MotionEditor::paint(juce::Graphics& graphics) {
    graphics.fillAll(osci::Colours::veryDark());
    graphics.setColour(osci::Colours::surface());
    for (const auto& panel : { libraryBounds, viewportBounds, inspectorBounds, timelineBounds }) {
        graphics.fillRoundedRectangle(panel.toFloat(), 5.0f);
    }
    graphics.setColour(osci::Colours::textMuted());
    graphics.setFont(12.0f);
    if (inspectorTabs.getCurrentTabIndex() != 0) {
        return;
    }
    auto labelArea = inspectorBounds.withTrimmedTop(66).reduced(12, 0);
    const auto target = motion::findPropertyTarget(processor.document.project(), selection);
    if (!target.has_value() || target->camera || target->isEffect) {
        graphics.drawFittedText("Select an object in the composition or a clip on the timeline to edit its properties.", labelArea.removeFromTop(70), juce::Justification::topLeft, 4);
        return;
    }
    if (audioSelected()) {
        for (const auto* label : { "Gain", "Pan   Left / Right" }) {
            graphics.drawText(label, labelArea.removeFromTop(22), juce::Justification::centredLeft);
            labelArea.removeFromTop(34);
        }
        return;
    }
    for (const auto* label : { "Position   X / Y / Z", "Rotation   X / Y / Z", "Scale   X / Y / Z", "Color   R / G / B", "Drawing weight" }) {
        graphics.drawText(label, labelArea.removeFromTop(22), juce::Justification::centredLeft);
        labelArea.removeFromTop(34);
    }

}

void MotionEditor::filesDropped(const juce::StringArray& files, int, int) {
    for (const auto& file : files) {
        openSourceFile(juce::File(file));
    }
}

bool MotionEditor::openSourceFile(const juce::File& file) {
    const auto extension = file.getFileExtension().toLowerCase();
    if (extension != ".obj" && extension != ".svg" && extension != ".txt"
        && extension != ".gpla" && extension != ".json" && extension != ".lottie"
        && extension != ".wav" && extension != ".wave" && extension != ".aif" && extension != ".aiff" && extension != ".flac" && extension != ".ogg") {
        importError = "This source type is not connected yet.";
        assetLibrary.setError(importError);
        repaint();
        return false;
    }
    importError.clear();
    assetLibrary.setError({});
    auto asset = std::make_shared<motion::Asset>();
    asset->name = file.getFileName();
    asset->extension = extension;
    const auto time = processor.position.load();
    const auto generation = processor.document.generation();
    auto task = std::make_shared<ImportState>();
    task->name = asset->name;
    task->generation = generation;
    pendingImports.push_back(task);
    const juce::Component::SafePointer<MotionEditor> owner(this);
    imports.addJob([owner, file, asset, time, generation, task] {
        auto result = juce::Result::fail("Import cancelled.");
        if (!task->cancelled.load()) {
            try {
                if (file.getSize() > static_cast<juce::int64>(motion::Document::maximumSourceBytes)) {
                    result = juce::Result::fail("This source exceeds the 64 MiB preparation limit.");
                } else {
                    result = file.loadFileAsData(asset->data) ? motion::Document::decodeAsset(*asset, &task->cancelled, &task->progress) : juce::Result::fail("Cannot read the source file.");
                }
            } catch (const std::exception& error) {
                result = juce::Result::fail("Cannot import this source: " + juce::String(error.what()));
            }
        }
        juce::MessageManager::callAsync([owner, asset, time, result, generation, task] {
            if (owner == nullptr) {
                return;
            }
            std::erase(owner->pendingImports, task);
            if (task->cancelled.load() || owner->processor.document.generation() != generation) { return; }
            if (result.failed()) {
                owner->importError = result.getErrorMessage();
                owner->assetLibrary.setError(owner->importError);
                owner->repaint();
                return;
            }
            auto& document = owner->processor.document;
            asset->id = document.newId();
            auto clip = motion::Document::makeClip(document.newId(), *asset, time);
            motion::Track track;
            track.id = document.newId();
            track.name = asset->name.toStdString();
            track.kind = asset->audio != nullptr ? motion::TrackKind::audio : motion::TrackKind::visual;
            track.insert(clip);
            document.edit(asset->audio != nullptr ? "Import soundtrack" : "Import object", [&](motion::Project& project) {
                project.assets.push_back(asset);
                project.tracks.push_back(track);
                project.duration = std::max(project.duration, clip.end());
            });
            owner->assetLibrary.refresh();
            owner->assetLibrary.selectAsset(asset->id);
            owner->select(clip.id);
        });
    });
    return true;
}

void MotionEditor::timerCallback() {
    refreshOutputChoices();
    processor.collectPreparedState();
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
    playButton.setButtonText(processor.playing.load() ? "Pause" : "Play");
    timeLabel.setText(juce::String(processor.document.project().timeGrid().positionLabel(processor.position.load())), juce::dontSendNotification);
    if (!tempoValue.isBeingEdited()) { tempoValue.setText(juce::String(processor.document.project().bpm, 1), juce::dontSendNotification); }
    timeline.repaint();
    curveEditor.repaint();
    composition.repaint();
    refreshInspector();
    if (effectsPanel.isVisible()) { effectsPanel.updateValues(); }
}

void MotionEditor::changeListenerCallback(juce::ChangeBroadcaster*) {
    for (const auto& task : pendingImports) {
        if (task->generation != processor.document.generation()) { task->cancelled.store(true); }
    }
    assetLibrary.refresh();
    refreshTiming();
    timeline.refreshTracks();
    effectsPanel.refresh();
    modulationPanel.refresh();
    curveEditor.refresh();
    composition.refresh();
    refreshInspector();
    resized();
    repaint();
}

void MotionEditor::select(motion::Id id) {
    selection = id;
    effectsPanel.setSelectedClip(id);
    inspectorTabs.setSelectedIndex(0);
    timeline.selected = id;
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

const char* MotionEditor::inspectorProperty(std::size_t index) const {
    return audioSelected() && index < 2 ? (index == 0 ? "gain" : "pan") : motion::propertyNames[index];
}

void MotionEditor::refreshInspector() {
    cameraPanel.refresh();
    const motion::Clip* selected = nullptr;
    for (const auto& track : processor.document.project().tracks) {
        for (const auto& clip : track.clips) {
            if (clip.id == selection) {
                selected = &clip;
            }
        }
    }
    splitButton.setEnabled(canSplitClip(selected, processor.position.load()));
    const auto target = motion::findPropertyTarget(processor.document.project(), selection);
    const bool editable = target.has_value() && !target->camera && !target->isEffect;
    selectionLabel.setText(editable ? juce::String(target->name.data(), target->name.size()) : "No object selected", juce::dontSendNotification);
    const auto tabName = editable && target->isGroup ? "Group" : (audioSelected() ? "Audio" : "Object");
    if (inspectorTabs.getTabNames()[0] != tabName) { inspectorTabs.setTabName(0, tabName); }
    updatingInspector = true;
    for (std::size_t index = 0; index < values.size(); ++index) {
        const bool visible = editable && inspectorTabs.getCurrentTabIndex() == 0 && (!audioSelected() || index < 2);
        values[index].setVisible(visible);
        keyButtons[index].setVisible(visible);
        const auto property = inspectorProperty(index);
        values[index].setName(property);
        values[index].setTitle(property);
        values[index].setComponentID("motion." + juce::String(property));
        keyButtons[index].setName("Key " + juce::String(property));
        keyButtons[index].setButtonText(keyButtons[index].getName());
        values[index].setEnabled(editable);
        keyButtons[index].setEnabled(editable);
        if (editable && !values[index].isBeingEdited()) {
            const auto found = target->properties->find(property);
            if (found != target->properties->end()) {
                values[index].setText(juce::String(found->second.evaluateBase(target->localTime(processor.position.load())), 2), juce::dontSendNotification);
                const auto time = target->localTime(processor.position.load());
                const auto& keys = found->second.keyframes();
                const auto keyed = std::any_of(keys.begin(), keys.end(), [time](const auto& key) { return std::abs(key.time - time) < 1.0e-6; });
                keyButtons[index].setState(keyed ? osci::KeyframeButton::State::keyed
                    : (found->second.animated() ? osci::KeyframeButton::State::animated : osci::KeyframeButton::State::unanimated));
            }
        }
    }
    updatingInspector = false;
}

void MotionEditor::setProperty(int index, bool keyframe) {
    if (audioSelected() && index >= 2) { return; }
    const std::string property = inspectorProperty(static_cast<std::size_t>(index));
    selectCurveTarget(selection, property, false);
    const auto time = processor.position.load();
    const auto target = motion::findPropertyTarget(processor.document.project(), selection);
    const auto* existing = target.has_value() ? target->curve(property) : nullptr;
    if (existing == nullptr) {
        return;
    }
    auto value = existing->evaluateBase(target->localTime(time));
    if (!keyframe) {
        const auto text = values[index].getText().trim();
        char* end = nullptr;
        value = std::strtod(text.toRawUTF8(), &end);
        if (text.isEmpty() || end == nullptr || *end != '\0' || !std::isfinite(value)) {
            refreshInspector();
            return;
        }
    }
    if (!std::isfinite(value)) {
        return;
    }
    if (audioSelected()) { value = index == 0 ? std::clamp(value, 0.0, 4.0) : std::clamp(value, -1.0, 1.0); }
    if (!audioSelected() && index >= 9) { value = std::clamp(value, 0.0, index == 12 ? 1000000.0 : 1.0); }
    processor.document.edit(keyframe ? "Set keyframe" : "Change property", [&](motion::Project& project) {
        const auto updated = motion::findPropertyTarget(project, selection);
        auto* curve = updated.has_value() ? updated->curve(property) : nullptr;
        if (curve == nullptr) { return; }
        if (keyframe || curve->animated()) {
            curve->setKeyValue(updated->localTime(time), value);
        } else {
            curve->base = value;
        }
    });
}

bool MotionEditor::keyPressed(const juce::KeyPress& key) {
    if (key == juce::KeyPress::spaceKey) {
        processor.playing.store(!processor.playing.load());
        return true;
    }
    return CommonPluginEditor::keyPressed(key);
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
    const auto project = processor.document.project();
    config.frameRate = project.frameRate;
    const auto renderMode = visualiser.getRenderMode();
    auto state = std::make_shared<ExportState>();
    exportState = state;
    const juce::Component::SafePointer<MotionEditor> owner(this);
    auto settings = std::make_unique<MotionVideoExportSettings>(config);
    auto* settingsPointer = settings.get();
    auto overlay = std::make_unique<osci::ComponentOverlay>(std::move(settings), "Export video", juce::Point<int>(440, 352), true);
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
                                const motion::PreparedComposition prepared(project);
                                result = motion::SignalExporter::write(prepared, temporary->signal(), 48000.0, state->cancelled, &state->progress);
                                if (result.wasOk() && config.includeAudio) {
                                    result = motion::SoundtrackExporter::write(prepared, temporary->soundtrack(), 48000.0, state->cancelled, &state->soundtrackProgress);
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
    chooser = std::make_unique<juce::FileChooser>("Export XYRGB signal - 48 kHz float WAV",
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
        const auto project = owner->processor.document.project();
        owner->exportBar.setName("Signal export progress");
        owner->exportProgress = 0;
        owner->exportBar.setVisible(true);
        owner->cancelExport.setVisible(true);
        owner->exports.addJob([owner, state, project, destination] {
            const auto result = motion::SignalExporter::write(project, destination, 48000.0, state->cancelled, &state->progress);
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

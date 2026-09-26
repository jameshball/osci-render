#include "MotionEditor.h"
#include "export/SignalExporter.h"
#include <cstdlib>

namespace {
bool canSplitClip(const motion::Clip* clip, double time) {
    return clip != nullptr && clip->valid() && std::isfinite(time) && time > clip->start && time < clip->end();
}
}

MotionEditor::MotionEditor(MotionProcessor& ownerProcessor)
    : CommonPluginEditor(ownerProcessor, "osci-motion", "osci-motion", 1440, 900), processor(ownerProcessor), timeline(ownerProcessor), composition(ownerProcessor), assetLibrary(ownerProcessor.document), curveEditor(ownerProcessor), cameraPanel(ownerProcessor), effectsPanel(ownerProcessor) {
    menus.addTopLevelMenu("File");
    menus.addProjectMenuItems(0, processor, *this);
    menus.addMenuSeparator(0);
    menus.addMenuItem(0, "Export XYRGB signal...", [this] { exportSignal(); });
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
    addChildComponent(exportBar);
    addAndMakeVisible(libraryTabs);
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
        cameraPanel.refresh();
        if (index == 1) { effectsPanel.activate(); } else { selectCurveTarget(index == 2 ? cameraPanel.selectedCameraId() : selection, curvePropertyName, index == 2); }
        repaint();
    };
    cameraPanel.setVisible(false);
    timelineTabs.addTab("Timeline");
    timelineTabs.addTab("Graph");
    timelineTabs.onSelectionChanged = [this](int index) {
        timeline.setVisible(index == 0);
        curveEditor.setVisible(index == 1);
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
        chooser = std::make_unique<juce::FileChooser>("Import object", processor.getLastOpenedDirectory(), "*.obj;*.svg;*.txt;*.gpla;*.json;*.lottie");
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
    for (std::size_t index = 0; index < values.size(); ++index) {
        auto& value = values[index];
        value.setEditable(false, true);
        value.setName(motion::propertyNames[index]);
        value.setTitle(motion::propertyNames[index]);
        value.setComponentID("motion." + juce::String(motion::propertyNames[index]));
        value.setJustificationType(juce::Justification::centred);
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
    timelineBounds = area.removeFromBottom(std::clamp(juce::roundToInt(workspaceHeight * timelineFraction), 200, workspaceHeight - 370));
    auto timeline = timelineBounds;
    auto transport = timeline.removeFromTop(30);
    timelineHeader.setBounds(transport);
    timelineTabs.setBounds(transport.removeFromLeft(205));
    playButton.setBounds(transport.removeFromLeft(65).reduced(2));
    splitButton.setBounds(transport.removeFromLeft(65).reduced(2));
    timeLabel.setBounds(transport.removeFromLeft(110));
    curveProperty.setBounds(transport.removeFromLeft(195).reduced(2));
    cancelExport.setBounds(transport.removeFromRight(62).reduced(2));
    exportBar.setBounds(transport.removeFromRight(180).reduced(2));
    this->timeline.setBounds(timeline.withTrimmedTop(3));
    curveEditor.setBounds(timeline.withTrimmedTop(3));
    timelineDivider.setBounds(area.removeFromBottom(7));
    libraryBounds = area.removeFromLeft(190);
    auto library = libraryBounds;
    libraryHeader.setBounds(library.removeFromTop(30));
    libraryTabs.setBounds(libraryHeader.getBounds());
    effectLibrary.setBounds(library.reduced(4, 0));
    importButton.setBounds(library.removeFromTop(42).reduced(8, 6));
    assetLibrary.setBounds(library.reduced(4, 0));
    area.removeFromLeft(3);
    inspectorBounds = area.removeFromRight(230);
    auto inspector = inspectorBounds;
    inspectorHeader.setBounds(inspector.removeFromTop(30));
    inspectorTabs.setBounds(inspectorHeader.getBounds());
    cameraPanel.setBounds(inspector);
    effectsPanel.setBounds(inspector);
    selectionLabel.setBounds(inspector.removeFromTop(36).reduced(10, 0));
    inspector.reduce(10, 0);
    for (int group = 0; group < 5; ++group) {
        inspector.removeFromTop(22);
        auto row = inspector.removeFromTop(28);
        for (int axis = 0; axis < (group == 4 ? 1 : 3); ++axis) {
            const auto index = group * 3 + axis;
            auto field = row.removeFromLeft(group == 4 ? row.getWidth() : inspector.getWidth() / 3);
            keyButtons[index].setBounds(field.removeFromRight(18).reduced(1));
            values[index].setBounds(field.reduced(2, 0));
        }
        inspector.removeFromTop(8);
    }
    area.removeFromRight(3);
    previewWidth = area.getWidth();
    auto editing = area.removeFromLeft(juce::roundToInt((previewWidth - 7) * previewFraction));
    previewDivider.setBounds(area.removeFromLeft(7));
    auto output = area;
    outputHeader.setBounds(output.removeFromTop(30));
    output.removeFromTop(3);
    visualiser.setBounds(output);
    viewportBounds = editing;
    viewportHeader.setBounds(editing.removeFromTop(30));
    composition.setBounds(editing.withTrimmedTop(3));
}

void MotionEditor::paint(juce::Graphics& graphics) {
    graphics.fillAll(osci::Colours::veryDark());
    graphics.setColour(osci::Colours::dark());
    for (const auto& panel : { libraryBounds, viewportBounds, inspectorBounds, timelineBounds }) {
        graphics.fillRoundedRectangle(panel.toFloat(), 5.0f);
    }
    graphics.setColour(juce::Colours::white.withAlpha(0.5f));
    graphics.setFont(14.0f);
    if (inspectorTabs.getCurrentTabIndex() != 0) {
        return;
    }
    auto labelArea = inspectorBounds.withTrimmedTop(66).reduced(12, 0);
    for (const auto* label : { "Position   X / Y / Z", "Rotation   X / Y / Z", "Scale   X / Y / Z", "Color   R / G / B", "Drawing weight" }) {
        graphics.drawText(label, labelArea.removeFromTop(22), juce::Justification::centredLeft);
        labelArea.removeFromTop(36);
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
        && extension != ".gpla" && extension != ".json" && extension != ".lottie") {
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
            track.insert(clip);
            document.edit("Import object", [&](motion::Project& project) {
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
    processor.collectPreparedState();
    if (pendingImports.empty()) {
        assetLibrary.setImportStatus({});
    } else {
        const auto& task = pendingImports.front();
        assetLibrary.setImportStatus(task->cancelled.load() ? "Cancelling import..." : "Preparing " + task->name + "\n" + juce::String(juce::roundToInt(task->progress.load() * 100)) + "%"
            + (pendingImports.size() > 1 ? "  (" + juce::String(static_cast<int>(pendingImports.size() - 1)) + " queued)" : ""));
    }
    if (exportState != nullptr) { exportProgress = exportState->progress.load(); }
    playButton.setButtonText(processor.playing.load() ? "Pause" : "Play");
    timeLabel.setText(juce::String(processor.position.load(), 2) + " s", juce::dontSendNotification);
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
    effectsPanel.refresh();
    curveEditor.refresh();
    composition.refresh();
    refreshInspector();
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
    repaint();
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
    selectionLabel.setText(selected == nullptr ? "No object selected" : juce::String(selected->name), juce::dontSendNotification);
    updatingInspector = true;
    for (std::size_t index = 0; index < values.size(); ++index) {
        values[index].setEnabled(selected != nullptr);
        keyButtons[index].setEnabled(selected != nullptr);
        if (selected != nullptr && !values[index].isBeingEdited()) {
            const auto found = selected->properties.find(motion::propertyNames[index]);
            if (found != selected->properties.end()) {
                values[index].setText(juce::String(found->second.evaluate(selected->localTime(processor.position.load())), 2), juce::dontSendNotification);
                const auto time = selected->localTime(processor.position.load());
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
    selectCurveTarget(selection, motion::propertyNames[static_cast<std::size_t>(index)], false);
    const auto time = processor.position.load();
    const auto target = motion::findPropertyTarget(processor.document.project(), selection);
    const auto* existing = target.has_value() ? target->curve(motion::propertyNames[index]) : nullptr;
    if (existing == nullptr) {
        return;
    }
    auto value = existing->evaluate(target->localTime(time));
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
    processor.document.edit(keyframe ? "Set keyframe" : "Change property", [&](motion::Project& project) {
        for (auto& track : project.tracks) {
            for (auto& clip : track.clips) {
                if (clip.id == selection) {
                    auto& curve = clip.properties[motion::propertyNames[index]];
                    if (keyframe || curve.animated()) {
                        curve.setKeyValue(clip.localTime(time), value);
                    } else {
                        curve.base = value;
                    }
                }
            }
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

void MotionEditor::exportSignal() {
    if (exportState != nullptr) {
        return;
    }
    chooser = std::make_unique<juce::FileChooser>("Export XYRGB signal - 48 kHz float WAV",
        processor.getLastOpenedDirectory().getChildFile("composition.wav"), "*.wav");
    const juce::Component::SafePointer<MotionEditor> owner(this);
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
        | juce::FileBrowserComponent::warnAboutOverwriting, [owner](const juce::FileChooser& selected) {
        if (owner == nullptr || selected.getResult() == juce::File()) {
            return;
        }
        const auto destination = selected.getResult().withFileExtension("wav");
        const auto project = owner->processor.document.project();
        auto state = std::make_shared<ExportState>();
        owner->exportState = state;
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
    const auto count = definition != nullptr ? definition->parameters.size() : (camera ? motion::cameraPropertyNames.size() : motion::propertyNames.size());
    int selectedIndex = 0;
    for (std::size_t index = 0; index < count; ++index) {
        const std::string name = definition != nullptr ? definition->parameters[index].id : (camera ? motion::cameraPropertyNames[index] : motion::propertyNames[index]);
        curveProperties.push_back(name);
        curveProperty.addItem(definition != nullptr ? juce::String(definition->parameters[index].name) : juce::String(name).replace(".", " "), static_cast<int>(index) + 1);
        if (property == name) {
            selectedIndex = static_cast<int>(index);
        }
    }
    curveProperty.setSelectedId(selectedIndex + 1, juce::dontSendNotification);
    curvePropertyName = curveProperties[static_cast<std::size_t>(selectedIndex)];
    curveEditor.setSelection(id, curvePropertyName);
}

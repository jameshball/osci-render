#include "MotionEditor.h"
#include "model/SourceParts.h"
#include "import/SourceDecoding.h"
#include "live/BlenderCaptureArchive.h"
#include "ui/BlenderSourcePanel.h"
#include "ui/FractalSettingsPanel.h"
#include "ui/RasterSettingsPanel.h"

namespace {
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
    if (!osci::files::isSupportedSource(extension)) {
        importError = "This source type is not connected yet.";
        statusBar.show(importError);
        repaint();
        return false;
    }
    SourceRequest request {file, placement.has_value() ? placement->first : processor.position.load(), processor.document.generation(), {}};
    request.relink = relink;
    if (placement.has_value()) { request.track = placement->second; }
    if (extension == ".lua" || extension == ".lsystem" || osci::files::isImage(extension)) {
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
    auto sheet = std::make_unique<motion::ui::ProgressSheet>("Opening project", file.getFileName(), "Preparing sources. Your current project stays open until loading succeeds.", nullptr, cancel, "Cancel loading");
    const auto size = juce::Point<int>(sheet->getWidth(), sheet->getHeight());
    auto overlay = std::make_unique<osci::ComponentOverlay>(std::move(sheet), juce::String(), size, false);
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
                motion::ui::MessageSheet::show(*owner, "Couldn't open the project", result.getErrorMessage(), "OK", {}, file.getFileName());
                return;
            }
            if (owner->processor.document.generation() != generation || owner->processor.document.revision() != revision) {
                motion::ui::MessageSheet::show(*owner, "Project changed", "The current project changed while loading. Open the file again to replace it.");
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
    chooser = std::make_unique<juce::FileChooser>("Import media", processor.getLastOpenedDirectory(), osci::files::sourceWildcard());
    const juce::Component::SafePointer<MotionEditor> owner(this);
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [owner](const juce::FileChooser& chosen) {
        if (owner != nullptr && chosen.getResult().existsAsFile()) {
            owner->openSourceFile(chosen.getResult());
        }
    });
}

void MotionEditor::showBlenderSettings(motion::Id id) {
    auto& document = processor.document;
    const auto& assets = document.mainProject().assets;
    const auto found = motion::findAsset(assets, id);
    if (id != 0 && (found == nullptr || found->liveIdentity == nullptr)) { return; }
    const auto original = found != nullptr ? found : std::shared_ptr<const motion::Asset>();
    auto panel = std::make_unique<MotionBlenderSourcePanel>(original != nullptr ? original->name : "Blender", original != nullptr ? original->blenderSettings : motion::BlenderSourceSettings{}, id != 0);
    auto* controls = panel.get();
    const auto size = juce::Point<int>(panel->getWidth(), panel->getHeight());
    auto overlay = std::make_unique<osci::ComponentOverlay>(std::move(panel), juce::String(), size, false);
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
        expected = motion::findAsset(document.mainProject().assets, sourceId);
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
    if (preparationSettingsOpen || sceneEditor != nullptr) { return; }
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
    // The source's bytes, for the preview beside the settings.
    juce::MemoryBlock data;
    if (request.replacement != nullptr) {
        data = request.replacement->data;
    } else if (!video) {
        request.file.loadFileAsData(data);
    }
    if (raster) {
        auto panel = std::make_unique<MotionRasterSettingsPanel>(request.replacement != nullptr ? request.replacement->rasterSettings : motion::RasterSettings(), video, name, video ? juce::MemoryBlock() : std::move(data));
        imagePanel = panel.get();
        content = std::move(panel);
    } else if (fractal) {
        const auto initialDepth = request.fractalDepth.value_or(request.replacement != nullptr ? request.replacement->fractalDepth : 3);
        auto panel = std::make_unique<MotionFractalSettingsPanel>(initialDepth, name, data.toString());
        fractalPanel = panel.get();
        content = std::move(panel);
    }
    const auto size = juce::Point<int>(content->getWidth(), content->getHeight());
    auto overlay = std::make_unique<osci::ComponentOverlay>(std::move(content), juce::String(), size, false);
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
                owner->textAnimation.clearPending();
                owner->repaint();
                // A new script that fails reopens with its code, as an edit
                // does; the editor shows the error where it is.
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
                        return;
                    }
                }
                owner->statusBar.show(owner->importError);
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
                auto* existing = trackId != 0 ? project.tracks.changeById(trackId) : nullptr;
                if (existing != nullptr) { existing->insert(clip, project.tempo()); } else { project.tracks.push_back(track); }
                project.duration = std::max(project.duration, clip.timing(project.tempo()).end());
            });
            owner->assetLibrary.refresh();
            owner->assetLibrary.selectAsset(asset->id);
            owner->select(clip.id);
        });
    });
}

template <typename Editor>
Editor& MotionEditor::openSceneEditor(std::unique_ptr<Editor> editor) {
    auto& opened = *editor;
    sceneEditor = std::move(editor);
    sceneEditorGeneration = processor.document.generation();
    assetLibrary.setEditingSource(true);
    addAndMakeVisible(opened);
    opened.onCancel = [this] { juce::MessageManager::callAsync([owner = juce::Component::SafePointer<MotionEditor>(this)] { if (owner != nullptr) { owner->closeSceneEditor(); } }); };
    return opened;
}

void MotionEditor::closeSceneEditor() {
    if (sceneEditor == nullptr) { return; }
    // Text and Lua editors are the import queue's settings for their source.
    if (sceneEditorAs<MotionDrawingEditor>() == nullptr) { preparationSettingsOpen = false; }
    removeChildComponent(sceneEditor.get());
    sceneEditor.reset();
    assetLibrary.setEditingSource(false);
    textPreviewDue = 0;
    for (auto* component : std::initializer_list<juce::Component*> {&composition, &viewportHeader}) { component->setVisible(true); }
    // Drop the preview of what was being edited.
    processor.prepareComposition(processor.document.project());
    resized();
    showNextPreparationSettings();
}

// Draw a new source, or edit a drawn one (`asset`). The drawing is saved as
// SVG in a temporary folder and imported like any file, relinking an edit.
void MotionEditor::showDrawingEditor(motion::Id asset) {
    if (sceneEditor != nullptr) { return; }
    motion::drawing::Drawing initial;
    juce::String name;
    const auto& assets = processor.document.mainProject().assets;
    if (asset != 0) {
        // Only an existing drawing opens; anything else would relink to nothing.
        const auto found = std::find_if(assets.begin(), assets.end(), [asset](const auto& item) { return item->id == asset; });
        if (found == assets.end()) { return; }
        const auto parsed = motion::drawing::fromSvg((*found)->data.toString());
        if (!parsed.has_value()) { return; }
        initial = *parsed;
        name = (*found)->name.upToLastOccurrenceOf(".", false, false);
    } else {
        const auto drawings = std::count_if(assets.begin(), assets.end(), [](const auto& item) { return item->name.startsWith("Drawing"); });
        name = "Drawing " + juce::String(static_cast<int>(drawings) + 1);
    }
    // The drawing takes over the Scene; the Scope shows it live as a beam.
    drawingAsset = asset;
    auto& editor = openSceneEditor(std::make_unique<MotionDrawingEditor>(initial, name, asset != 0));
    editor.onChanged = [this] { previewDrawing(); };
    editor.onDone = [this](const motion::drawing::Drawing& drawing, const juce::String& text) {
        const auto folder = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("osci-motion drawings");
        const auto file = folder.getChildFile(juce::File::createLegalFileName(text) + ".svg");
        if (!folder.createDirectory().wasOk() || !file.replaceWithText(motion::drawing::toSvg(drawing))) {
            statusBar.show("Could not save the drawing.");
            return;
        }
        const auto asset = drawingAsset;
        juce::MessageManager::callAsync([owner = juce::Component::SafePointer<MotionEditor>(this), file, asset] {
            if (owner == nullptr) { return; }
            owner->closeSceneEditor();
            owner->importSourceFile(file, asset);
        });
    };
    resized();
    previewDrawing();
    editor.grabKeyboardFocus();
}

void MotionEditor::showTextEditor(SourceRequest request) {
    if (request.replacement == nullptr || sceneEditor != nullptr) { return; }
    const auto& asset = *request.replacement;
    const auto draft = request.editedText.value_or(asset.data.toString());
    const auto settings = request.textSettings.value_or(asset.textSettings);
    textRequest = request;
    // Other queued sources wait until the text is saved or cancelled.
    preparationSettingsOpen = true;
    auto& editor = openSceneEditor(std::make_unique<MotionTextSourceEditor>(draft, asset.name.upToLastOccurrenceOf(".", false, false), settings, request.preparationError));
    editor.onChanged = [this] { textPreviewDue = juce::Time::getMillisecondCounterHiRes() + 120; };
    editor.onDone = [this, opened = &editor](const juce::String& text, const motion::TextSettings& chosen) {
        juce::MessageManager::callAsync([owner = juce::Component::SafePointer<MotionEditor>(this), opened, text, chosen] {
            // A second Save before this runs finds the editor already gone.
            if (owner == nullptr || owner->sceneEditor.get() != opened) { return; }
            auto next = owner->textRequest;
            owner->closeSceneEditor();
            if (owner->processor.document.generation() != next.generation) { return; }
            // The source may have changed while the editor was open (its
            // animation, an undo): build on what it is now.
            const auto& assets = owner->processor.document.project().assets;
            const auto id = next.replacement->id;
            const auto found = motion::findAsset(assets, id);
            if (found == nullptr) { owner->statusBar.show("The text source was removed while it was being edited."); return; }
            auto settings = found->textSettings;
            settings.family = chosen.family;
            settings.style = chosen.style;
            settings.alignment = chosen.alignment;
            settings.lineSpacing = chosen.lineSpacing;
            settings.tracking = chosen.tracking;
            next.replacement = found;
            next.editedText = text;
            next.textSettings = settings;
            next.preparationError.clear();
            owner->beginSourceImport(next);
        });
    };
    resized();
    previewText();
    editor.focusText();
}

void MotionEditor::showLuaEditor(SourceRequest request) {
    if (sceneEditor != nullptr) { return; }
    const auto editing = request.replacement != nullptr;
    const auto code = editing ? request.editedText.value_or(request.replacement->data.toString())
                              : request.file.loadFileAsString();
    motion::BakeSettings initial;
    initial.bpm = processor.document.project().bpm;
    initial.frameRate = processor.document.project().frameRate;
    if (editing) { initial = request.replacement->bakeSettings; }
    initial = request.retrySettings.value_or(initial);
    const auto title = (editing ? request.replacement->name : request.file.getFileName()).upToLastOccurrenceOf(".", false, false);
    luaRequest = request;
    luaSubmitted = false;
    preparationSettingsOpen = true;
    auto& editor = openSceneEditor(std::make_unique<MotionLuaSourceEditor>(code, title, initial, editing, request.preparationError));
    editor.onDone = [this, original = code](motion::BakeSettings settings, const juce::String& written) {
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
            owner->closeSceneEditor();
            if (owner->processor.document.generation() == next.generation) { owner->beginSourceImport(next, settings); }
        });
    };
    resized();
    editor.focusCode();
}

// The words being written, on the output: the source is swapped in place,
// still, so typing stays quick; its animation plays once saved.
void MotionEditor::previewText() {
    textPreviewDue = 0;
    const auto* editor = sceneEditorAs<MotionTextSourceEditor>();
    if (editor == nullptr || textRequest.replacement == nullptr) { return; }
    auto project = processor.document.project();
    auto asset = std::make_shared<motion::Asset>(*textRequest.replacement);
    const auto text = editor->currentText();
    asset->data.reset();
    asset->data.append(text.toRawUTF8(), text.getNumBytesAsUTF8());
    asset->textSettings = editor->currentSettings();
    asset->textSettings.animation = motion::TextSettings::Animation::none;
    if (text.trim().isEmpty() || text.length() > 16384 || motion::decodeAsset(*asset).failed()) {
        processor.prepareComposition(project);
        return;
    }
    for (auto& item : project.assets) {
        if (item->id == asset->id) { item = asset; }
    }
    processor.prepareComposition(project);
}

// The drawing in progress on the output: an edited source is swapped in place;
// a new one plays on a track of its own for the whole project.
void MotionEditor::previewDrawing() {
    const auto* editor = sceneEditorAs<MotionDrawingEditor>();
    if (editor == nullptr) { return; }
    auto project = processor.document.project();
    auto asset = std::make_shared<motion::Asset>();
    asset->id = drawingAsset != 0 ? drawingAsset : std::numeric_limits<motion::Id>::max() - 1;
    asset->name = "Drawing.svg";
    asset->extension = ".svg";
    const auto svg = motion::drawing::toSvg(editor->current());
    asset->data.append(svg.toRawUTF8(), svg.getNumBytesAsUTF8());
    if (editor->current().empty() || motion::decodeAsset(*asset).failed()) {
        processor.prepareComposition(project);
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
    processor.prepareComposition(project);
}

void MotionEditor::extractParts(const std::map<motion::Id, MotionCompositionView::Picked>& picks, const juce::String& name) {
    const auto& project = processor.document.project();
    std::vector<motion::Document::PartSplit> splits;
    for (const auto& [clipId, chosen] : picks) {
        const auto* clip = motion::findClip(project, clipId);
        const auto asset = clip != nullptr ? motion::findAsset(project.assets, clip->asset) : nullptr;
        const auto* drawing = asset != nullptr ? motion::parts::drawingOf(*asset) : nullptr;
        // The picks index the shapes the Scene showed; a source prepared
        // since then may hold others.
        if (drawing == nullptr || asset->source != chosen.source) {
            statusBar.show("The object changed. Pick its parts again.");
            return;
        }
        const auto split = motion::parts::split(*drawing, chosen.shapes);
        if (!split.has_value()) {
            statusBar.show("Pick some of the object's parts, not all of them.");
            return;
        }
        // Both halves are small vector sources, prepared here so the split
        // is one step.
        const auto prepare = [&split](const juce::String& content) {
            auto made = std::make_shared<motion::Asset>();
            made->extension = split->extension;
            made->data.append(content.toRawUTF8(), content.getNumBytesAsUTF8());
            return motion::decodeAsset(*made).wasOk() ? made : nullptr;
        };
        auto part = prepare(split->part), rest = prepare(split->rest);
        if (part == nullptr || rest == nullptr) {
            statusBar.show("The parts could not be prepared.");
            return;
        }
        splits.push_back({clipId, std::move(part), std::move(rest), name});
    }
    std::vector<motion::Id> created;
    const auto result = processor.document.extractParts(splits, created);
    if (result.failed()) {
        statusBar.show(result.getErrorMessage());
        return;
    }
    composition.setPartMode(false);
    selectionBeforeExtract.clear();
    for (const auto& split : splits) { selectionBeforeExtract.push_back(split.clip); }
    timeline.selectClips(created);
    // The library shows the new part's source, as importing does.
    const auto* made = created.empty() ? nullptr : motion::findClip(processor.document.project(), created.front());
    assetLibrary.refresh();
    if (made != nullptr) { assetLibrary.selectAsset(made->asset); }
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

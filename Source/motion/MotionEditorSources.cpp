#include "MotionEditor.h"
#include "model/SourceParts.h"
#include "import/SourceDecoding.h"
#include "live/BlenderCaptureArchive.h"
#include "import/BakedSourceArchive.h"
#include "ui/BlenderSourcePanel.h"
#include "ui/DrawingEditor.h"
#include "ui/LuaSourceEditor.h"
#include "ui/TextSourceEditor.h"
#include "ui/FractalSettingsPanel.h"
#include "ui/RasterSettingsPanel.h"
#include "ui/SvgSequencePanel.h"

namespace {
// A file written for importing or opening, in a folder of its own under the
// temporary folder ("osci-motion drawings"...), so another of the same name,
// written before this one is read, can't replace it. Text is written as it
// is held, as UTF-8.
// Those a day old are read long since, and go when the next is written.
std::optional<juce::File> writeTemporary(const juce::String& purpose, const juce::String& fileName, const void* data, std::size_t size) {
    const auto parent = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("osci-motion " + purpose);
    const auto dayAgo = juce::Time::getCurrentTime() - juce::RelativeTime::days(1);
    for (const auto& old : parent.findChildFiles(juce::File::findDirectories, false)) {
        if (old.getLastModificationTime() < dayAgo) { old.deleteRecursively(); }
    }
    const auto folder = parent.getChildFile(juce::Uuid().toString());
    const auto file = folder.getChildFile(fileName);
    if (!folder.createDirectory().wasOk() || !file.replaceWithData(data, size)) { return std::nullopt; }
    return file;
}
std::optional<juce::File> writeTemporary(const juce::String& purpose, const juce::String& fileName, const juce::String& text) {
    return writeTemporary(purpose, fileName, text.toRawUTF8(), text.getNumBytesAsUTF8());
}
// A shipped example, copied out to be read as any file is.
std::optional<juce::File> writeExample(const juce::String& resource) {
    int size = 0;
    const auto* data = BinaryData::getNamedResource(resource.toRawUTF8(), size);
    if (data == nullptr || size < 0) { return std::nullopt; }
    return writeTemporary("examples", motion::ui::examples::fileName(resource), data, static_cast<std::size_t>(size));
}
}

// Files dropped on the timeline land where they were dropped (time and
// track, as in Premiere); anywhere else they go in at the playhead.
void MotionEditor::filesDropped(const juce::StringArray& files, int x, int y) {
    std::optional<std::pair<double, motion::Id>> placement;
    if (timeline.isShowing()) { placement = timeline.dropTarget(timeline.getLocalPoint(this, juce::Point<int>(x, y))); }
    juce::Array<juce::File> chosen;
    for (const auto& file : files) {
        // A dropped project opens, as it would from Finder or Explorer.
        if (juce::File(file).hasFileExtension(".osci-motion")) {
            openProject(juce::File(file));
            return;
        }
        chosen.add(juce::File(file));
    }
    importSourceFiles(chosen, placement);
}

// Several SVGs together may be an animation's frames: asked which.
void MotionEditor::importSourceFiles(juce::Array<juce::File> files, std::optional<std::pair<double, motion::Id>> placement) {
    juce::Array<juce::File> svgs;
    for (const auto& file : files) {
        if (file.hasFileExtension(".svg")) { svgs.add(file); }
    }
    for (const auto& file : files) {
        if (svgs.size() < 2 || !svgs.contains(file)) { importSourceFile(file, 0, placement); }
    }
    if (svgs.size() < 2) { return; }
    motion::svgsequence::sortByName(svgs);
    auto sheet = std::make_unique<MotionSvgSequencePanel>(svgs);
    const juce::Component::SafePointer<MotionEditor> owner(this);
    sheet->onImport = [owner, svgs, placement, sheet = sheet.get()](bool animation, double frameRate) {
        sheet->close();
        if (owner == nullptr) { return; }
        if (!animation) {
            for (const auto& file : svgs) { owner->importSourceFile(file, 0, placement); }
            return;
        }
        motion::svgsequence::Frames frames;
        frames.frameRate = frameRate;
        for (const auto& file : svgs) { frames.svgs.push_back(file.loadFileAsString()); }
        // Named for what the files share ("walk_001.svg"...: walk).
        const auto shared = motion::svgsequence::sharedName(svgs);
        const auto name = shared.isNotEmpty() ? shared : svgs.getFirst().getFileNameWithoutExtension();
        const auto data = motion::svgsequence::encode(frames);
        const auto file = writeTemporary("sequences", juce::File::createLegalFileName(name) + motion::svgsequence::extension, data.getData(), data.getSize());
        if (!file.has_value()) {
            owner->statusBar.show("Couldn't prepare the SVG sequence.");
            return;
        }
        SourceRequest request {*file, placement.has_value() ? placement->first : owner->processor.position.load(), owner->processor.document.generation(), {}};
        if (placement.has_value()) { request.track = placement->second; }
        owner->beginSourceImport(std::move(request));
    };
    motion::ui::Sheet::show(*this, std::move(sheet));
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
        statusBar.show("This kind of file can't be used as a source.");
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

void MotionEditor::afterDiscarding(const juce::String& doing, std::function<void()> then) {
    if (!processor.document.unsaved()) {
        then();
        return;
    }
    const juce::Component::SafePointer<MotionEditor> owner(this);
    const auto name = processor.currentProjectFile.isEmpty() ? juce::String("This project") : juce::File(processor.currentProjectFile).getFileNameWithoutExtension();
    motion::ui::MessageSheet::show(*this, "Discard unsaved changes?", name + " has changes that are not saved. " + doing + " and lose them?", "Discard",
        [owner, then = std::move(then)] {
            if (owner != nullptr) { then(); }
        }, {}, true);
}

void MotionEditor::openProject(const juce::File& file) {
    if (file == juce::File()) { return; }
    const juce::Component::SafePointer<MotionEditor> owner(this);
    afterDiscarding("Open " + file.getFileName(), [owner, file] {
        if (owner != nullptr) { owner->loadProject(file); }
    });
}

void MotionEditor::resetToDefault() {
    const juce::Component::SafePointer<MotionEditor> owner(this);
    afterDiscarding("Start a new project", [owner] {
        if (owner != nullptr) { owner->CommonPluginEditor::resetToDefault(); }
    });
}

void MotionEditor::projectNotLoaded(const juce::File& file) {
    if (commandLineProject == CommandLineProject::waiting && file == commandLineProjectFile) { commandLineProject = CommandLineProject::failed; }
}

void MotionEditor::openExample(const juce::String& resource) {
    const auto name = motion::ui::examples::fileName(resource);
    if (name.isEmpty()) { return; }
    const juce::Component::SafePointer<MotionEditor> owner(this);
    afterDiscarding("Open the " + motion::ui::examples::displayName(resource) + " example", [owner, resource, name] {
        if (owner == nullptr) { return; }
        // Read from a copy, as any project is.
        const auto file = writeExample(resource);
        if (!file.has_value()) {
            owner->statusBar.show("Could not prepare the example " + name + ".");
            return;
        }
        owner->loadProject(*file, true);
    });
}

void MotionEditor::loadProject(const juce::File& file, bool example) {
    // Opened another way (dropped, or from the Finder): the start screen goes.
    if (startScreenOverlay != nullptr) { startScreenOverlay->requestDismiss(); }
    if (projectLoad != nullptr) { projectLoad->cancelled.store(true); }
    if (projectLoadOverlay != nullptr) { dismissOverlay(projectLoadOverlay.getComponent()); }
    auto task = std::make_shared<ProjectLoad>();
    projectLoad = task;
    const auto generation = processor.document.generation();
    const auto revision = processor.document.revision();
    const juce::Component::SafePointer<MotionEditor> owner(this);
    auto cancel = [owner, task, file] {
        task->cancelled.store(true);
        if (owner != nullptr && owner->projectLoad == task) {
            owner->projectNotLoaded(file);
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
    projectLoadOverlay = motion::ui::Sheet::show(*this, std::move(sheet), [owner, task, file] {
        task->cancelled.store(true);
        if (owner != nullptr && owner->projectLoad == task) {
            owner->projectNotLoaded(file);
            owner->projectLoadOverlay = nullptr;
            owner->projectLoad.reset();
        }
    });
    // Its own thread: a load never waits behind imports into the current project.
    projectLoads.addJob([owner, task, file, generation, revision, example] {
        auto result = juce::Result::fail("Project loading cancelled.");
        if (!task->cancelled.load()) {
            try {
                result = motion::ProjectFile::read(file, task->file, &task->cancelled);
            } catch (const std::exception& error) {
                result = juce::Result::fail("Couldn't open the project: " + juce::String(error.what()));
            }
        }
        juce::MessageManager::callAsync([owner, task, file, generation, revision, result, example] {
            if (owner == nullptr || owner->projectLoad != task || task->cancelled.load()) { return; }
            auto* overlay = owner->projectLoadOverlay.getComponent();
            owner->projectLoadOverlay = nullptr;
            owner->projectLoad.reset();
            if (overlay != nullptr) { owner->dismissOverlay(overlay); }
            if (result.failed()) {
                owner->projectNotLoaded(file);
                motion::ui::MessageSheet::show(*owner, "Couldn't open the project", result.getErrorMessage(), "OK", {}, file.getFileName());
                return;
            }
            if (owner->processor.document.generation() != generation || owner->processor.document.revision() != revision) {
                owner->projectNotLoaded(file);
                motion::ui::MessageSheet::show(*owner, "Project changed", "The current project changed while loading. Open the file again to replace it.");
                return;
            }
            const auto notes = task->file.notes;
            owner->processor.applyPreparedProject(std::move(task->file.project), *task->file.xml);
            if (owner->commandLineProject == CommandLineProject::waiting && file == owner->commandLineProjectFile) { owner->commandLineProject = CommandLineProject::loaded; }
            // An example is a new project: saving it asks where.
            if (example) {
                owner->processor.currentProjectFile = {};
            } else {
                owner->processor.currentProjectFile = file.getFullPathName();
                owner->processor.setLastOpenedDirectory(file.getParentDirectory());
                owner->processor.addRecentProjectFile(file);
            }
            owner->updateTitle();
            // What couldn't be used as it was (from another version, or a linked file not found): left out, kept or reset.
            if (!notes.empty()) {
                juce::String message = "Some of it couldn't be used as it was:\n";
                for (const auto& line : notes.lines()) { message << juce::String(juce::CharPointer_UTF8("\xe2\x80\xa2 ")) << line << "\n"; }
                message << "\nSaving keeps the project as it opened now.";
                motion::ui::MessageSheet::show(*owner, "Opened, with some changes", message, "OK", {}, file.getFileName());
            }
        });
    });
}

void MotionEditor::chooseSourceFile() {
    chooser = std::make_unique<juce::FileChooser>("Import source", processor.getLastOpenedDirectory(), osci::files::sourceWildcard());
    const juce::Component::SafePointer<MotionEditor> owner(this);
    // Several at once: SVGs among them may be an animation's frames.
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems, [owner](const juce::FileChooser& chosen) {
        if (owner == nullptr) { return; }
        juce::Array<juce::File> files;
        for (const auto& file : chosen.getResults()) {
            if (file.existsAsFile()) { files.add(file); }
        }
        owner->importSourceFiles(files, std::nullopt);
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
    const juce::Component::SafePointer<MotionEditor> owner(this);
    const juce::Component::SafePointer<osci::OverlayComponent> dialog(motion::ui::Sheet::show(*this, std::move(panel)));
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
        owner->assetLibrary.refresh();
        owner->assetLibrary.selectAsset(sourceId);
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
    controls->connectionStatus = [owner, id, generation] { return owner != nullptr && owner->processor.document.generation() == generation ? owner->processor.blenderInputs().connectionText(id) : juce::String("Project changed"); };
    controls->captureError = [owner, id, generation] { return owner != nullptr && owner->processor.document.generation() == generation ? owner->processor.blenderInputs().captureError(id) : juce::String(); };
    controls->isCapturing = [owner, id, generation] { return owner != nullptr && owner->processor.document.generation() == generation && owner->liveCapturing(id); };
    controls->onCancelCapture = [owner, id, generation] {
        if (owner != nullptr && owner->processor.document.generation() == generation) { owner->cancelLiveCapture(id); }
    };
    controls->onRecord = [owner, id, generation, panelIdentity] {
        const auto identity = *panelIdentity;
        if (owner == nullptr || owner->processor.document.generation() != generation) { return juce::Result::fail("The project changed. Reopen source settings."); }
        const auto& assets = owner->processor.document.mainProject().assets;
        const auto source = std::find_if(assets.begin(), assets.end(), [id, identity](const auto& asset) { return asset->id == id && asset->liveIdentity == identity; });
        if (source == assets.end()) { return juce::Result::fail("The source changed. Reopen its settings."); }
        return owner->recordLiveCapture(id);
    };
    liveSettings = controls;
    liveSettingsId = id;
}

void MotionEditor::showLiveSettings(motion::Id id) {
    const auto asset = motion::findAsset(processor.document.mainProject().assets, id);
    if (asset != nullptr && motion::texture::isSource(asset->extension)) {
        showTextureSettings(id);
    } else {
        showBlenderSettings(id);
    }
}

void MotionEditor::addTextureSource(const motion::texture::Sender& sender) {
    motion::RasterSettings settings;
    // As an image starts: a Hilbert curve shades as the scope can show.
    settings.shading = processor.document.project().scope.xyOnly ? motion::RasterSettings::Shading::density : motion::RasterSettings::Shading::colour;
    motion::Id id = 0;
    const auto added = processor.document.addTextureSource(motion::texture::encode(sender), sender.name.isNotEmpty() ? sender.name : sender.application, settings, id);
    if (added.failed()) {
        statusBar.show(added.getErrorMessage());
        return;
    }
    assetLibrary.refresh();
    assetLibrary.selectAsset(id);
    libraryTabs.setSelectedIndex(0);
    timeline.insertAsset(id, -1, -1);
}

void MotionEditor::showTextureSettings(motion::Id id) {
    const auto& document = processor.document;
    const auto found = motion::findAsset(document.mainProject().assets, id);
    if (found == nullptr || found->liveIdentity == nullptr || !motion::texture::isSource(found->extension)) { return; }
    const juce::Component::SafePointer<MotionEditor> owner(this);
    const auto generation = document.generation();
    const auto current = [owner, generation] { return owner != nullptr && owner->processor.document.generation() == generation; };
    MotionRasterSettingsPanel::Live live;
    live.frame = [owner, id, current] { return current() ? owner->processor.textureInputs().frame(id) : nullptr; };
    live.status = [owner, id, current] { return current() ? owner->processor.textureInputs().statusText(id) : juce::String("The project changed"); };
    live.onChange = [owner, id, current](motion::RasterSettings settings) {
        if (!current()) { return; }
        const auto changed = owner->processor.document.setTextureSettings(id, settings);
        if (changed.failed()) { owner->statusBar.show(changed.getErrorMessage()); }
    };
    live.recording = [owner, id, current] { return current() && owner->liveCapturing(id); };
    live.onRecord = [owner, id, current] { return current() ? owner->recordLiveCapture(id) : juce::Result::fail("The project changed. Reopen its settings."); };
    auto panel = std::make_unique<MotionRasterSettingsPanel>(found->rasterSettings, false, found->name, juce::MemoryBlock(), document.project().scope.xyOnly, juce::File(), juce::File(), std::move(live));
    liveSettings = panel.get();
    liveSettingsId = id;
    motion::ui::Sheet::show(*this, std::move(panel));
}

bool MotionEditor::liveCapturing(motion::Id id) {
    return processor.blenderInputs().capturing(id) || processor.textureInputs().capturing(id);
}

juce::Result MotionEditor::recordLiveCapture(motion::Id id) {
    if (liveCapturing(id)) { return keepLiveCapture(id); }
    if (std::any_of(pendingImports.begin(), pendingImports.end(), [](const auto& task) { return task->capture; })) {
        return juce::Result::fail("Wait for the previous capture to finish preparing.");
    }
    const auto source = motion::findAsset(processor.document.mainProject().assets, id);
    if (source == nullptr || source->liveIdentity == nullptr) { return juce::Result::fail("The source changed. Reopen its settings."); }
    return motion::texture::isSource(source->extension) ? processor.textureInputs().beginCapture(id) : processor.blenderInputs().beginCapture(id);
}

void MotionEditor::cancelLiveCapture(motion::Id id) {
    processor.blenderInputs().cancelCapture(id);
    processor.textureInputs().cancelCapture(id);
}

juce::Result MotionEditor::keepLiveCapture(motion::Id id) {
    const auto source = motion::findAsset(processor.document.mainProject().assets, id);
    if (source == nullptr || source->liveIdentity == nullptr) { return juce::Result::fail("There is no capture to keep."); }
    // Each kind of source makes its capture's file its own way, on a worker;
    // the rest is the same for all.
    std::function<juce::Result(motion::Asset&, const std::atomic<bool>&)> make;
    juce::String extension;
    auto ending = motion::CaptureEnding::none;
    if (motion::texture::isSource(source->extension)) {
        auto recording = processor.textureInputs().finishCapture(id);
        if (recording == nullptr) { return juce::Result::fail("There is no capture to keep."); }
        if (recording->frames.empty()) { return juce::Result::fail("Nothing arrived while recording."); }
        ending = recording->ending;
        extension = motion::texture::captureExtension;
        make = [capture = std::shared_ptr<const motion::TextureCapture>(std::move(recording))](motion::Asset& asset, const std::atomic<bool>&) {
            const auto frames = capture->steady();
            if (!frames) { return juce::Result::fail(juce::String(frames.error)); }
            auto archive = motion::BakedSourceArchive::encode(*frames.source);
            if (!archive) { return juce::Result::fail(archive.error); }
            asset.data = std::move(archive.data);
            return juce::Result::ok();
        };
    } else {
        auto recording = processor.blenderInputs().finishCapture(id);
        if (recording == nullptr) { return juce::Result::fail("There is no capture to keep."); }
        if (recording->failure != motion::BlenderCapture::Failure::none) { return juce::Result::fail(recording->error()); }
        ending = recording->ending;
        extension = ".blender-capture";
        make = [capture = std::shared_ptr<const motion::BlenderCapture>(std::move(recording))](motion::Asset& asset, const std::atomic<bool>& cancelled) {
            auto archive = motion::BlenderCaptureArchive::encode(*capture, &cancelled);
            if (!archive) { return juce::Result::fail(archive.error); }
            asset.data.replaceAll(archive.bytes.data(), archive.bytes.size());
            return juce::Result::ok();
        };
    }
    const juce::String why = motion::captureEndedBecause(ending, source->name.toStdString());
    const auto identity = source->liveIdentity;
    const auto generation = processor.document.generation();
    auto task = std::make_shared<ImportState>();
    task->capture = true;
    task->name = source->name + " capture";
    task->generation = generation;
    pendingImports.push_back(task);
    const juce::Component::SafePointer<MotionEditor> owner(this);
    imports.addJob([owner, id, identity, generation, task, extension, make, why] {
        auto asset = std::make_shared<motion::Asset>();
        asset->name = task->name;
        asset->extension = extension;
        auto result = juce::Result::fail("Capture cancelled.");
        try {
            result = make(*asset, task->cancelled);
            if (result.wasOk()) { result = motion::decodeAsset(*asset, &task->cancelled, &task->progress); }
        } catch (const std::exception& error) { result = juce::Result::fail("Couldn't keep the capture: " + juce::String(error.what())); }
        juce::MessageManager::callAsync([owner, id, identity, generation, task, asset, result, why] {
            if (owner == nullptr) { return; }
            owner->importEnded(task);
            if (task->cancelled.load() || owner->processor.document.generation() != generation) { return; }
            auto& document = owner->processor.document;
            const auto& sources = document.mainProject().assets;
            if (std::none_of(sources.begin(), sources.end(), [&](const auto& source) { return source->liveIdentity == identity; })) { return; }
            if (result.failed()) { owner->statusBar.show(result.getErrorMessage()); return; }
            asset->id = document.newId();
            document.edit("Capture live source", [&](motion::Project& project) { project.assets.push_back(asset); });
            owner->assetLibrary.refresh();
            owner->assetLibrary.selectAsset(asset->id);
            owner->libraryTabs.setSelectedIndex(0);
            owner->captured(id, asset->id, why);
        });
    });
    // Its settings, if open, close, so what was kept shows.
    if (liveSettingsId == id) {
        juce::MessageManager::callAsync([sheet = liveSettings] { if (sheet != nullptr) { sheet->close(); } });
    }
    return juce::Result::ok();
}

// A capture that ended by itself (its sender gone, or full) is kept, open
// settings or not.
void MotionEditor::keepEndedCaptures() {
    std::vector<motion::Id> ended;
    for (const auto& asset : processor.document.mainProject().assets) {
        if (asset->liveIdentity != nullptr && (processor.blenderInputs().captureEnded(asset->id) || processor.textureInputs().captureEnded(asset->id))) { ended.push_back(asset->id); }
    }
    for (const auto id : ended) {
        const auto kept = keepLiveCapture(id);
        if (kept.failed()) { statusBar.show(kept.getErrorMessage()); }
    }
}

void MotionEditor::captured(motion::Id live, motion::Id capture, const juce::String& why) {
    auto& document = processor.document;
    const auto source = motion::findAsset(document.mainProject().assets, live);
    if (source == nullptr || document.assetUses(live) == 0) {
        captureForExport = 0;
        if (why.isNotEmpty()) { statusBar.show(why + " It's in the library.", MotionStatusBar::Kind::notice); }
        return;
    }
    const juce::Component::SafePointer<MotionEditor> owner(this);
    const auto name = source->name;
    const auto first = why.isNotEmpty() ? why + " " : juce::String();
    // Recorded to export with: its clips draw the capture straight away.
    if (captureForExport == live && captureForExportGeneration == document.generation()) {
        captureForExport = 0;
        const auto used = document.drawSourceInstead(live, capture);
        if (used.failed()) { statusBar.show(used.getErrorMessage()); return; }
        const auto video = captureForVideo;
        const auto mode = captureForSignal;
        statusBar.show(first + "The capture now draws in place of " + name, MotionStatusBar::Kind::notice, video ? "Export video" : "Export audio", [owner, video, mode] {
            if (owner == nullptr) { return; }
            if (video) { owner->exportVideo(); } else { owner->exportSignal(mode); }
        });
        return;
    }
    statusBar.show(first + "Captured " + name, MotionStatusBar::Kind::notice, "Use in its clips", [owner, live, capture] {
        if (owner == nullptr) { return; }
        const auto used = owner->processor.document.drawSourceInstead(live, capture);
        if (used.failed()) { owner->statusBar.show(used.getErrorMessage()); }
    });
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
    std::unique_ptr<motion::ui::Sheet> content;
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
    // A video's settings show one of its frames, which needs FFmpeg first.
    // Until it's here, the queued videos wait aside (asked about once), so
    // the sources behind them go on; FFmpeg arriving brings them back.
    if (video) {
        const juce::Component::SafePointer<MotionEditor> owner(this);
        if (!ffmpegReady([owner] { if (owner != nullptr) { owner->showNextPreparationSettings(); } })) {
            videosAwaitingFFmpeg.push_back(std::move(request));
            const auto isVideo = [](const SourceRequest& queued) { return osci::files::isVideo(queued.replacement != nullptr ? queued.replacement->extension : queued.file.getFileExtension()); };
            std::copy_if(preparationRequests.begin(), preparationRequests.end(), std::back_inserter(videosAwaitingFFmpeg), isVideo);
            std::erase_if(preparationRequests, isVideo);
            showNextPreparationSettings();
            return;
        }
    }
    // The source's bytes, for the preview beside the settings.
    juce::MemoryBlock data;
    if (request.replacement != nullptr) {
        data = request.replacement->data;
    } else if (!video) {
        request.file.loadFileAsData(data);
    }
    // A source edited, or relinked to this file, starts from its own settings.
    const auto previous = request.replacement != nullptr ? request.replacement : request.relink != 0 ? motion::findAsset(processor.document.mainProject().assets, request.relink) : nullptr;
    if (raster) {
        // A video's frame is traced from the file, or the bytes of the source
        // it replaces, when FFmpeg is already here.
        auto panel = std::make_unique<MotionRasterSettingsPanel>(previous != nullptr ? previous->rasterSettings : motion::RasterSettings(), video, name,
            std::move(data), processor.document.project().scope.xyOnly, request.replacement != nullptr ? request.replacement->linked : request.file,
            processor.getFFmpegFile().existsAsFile() ? processor.getFFmpegFile() : juce::File());
        imagePanel = panel.get();
        content = std::move(panel);
    } else if (fractal) {
        const auto initialDepth = request.fractalDepth.value_or(previous != nullptr ? previous->fractalDepth : 3);
        auto panel = std::make_unique<MotionFractalSettingsPanel>(initialDepth, name, data.toString());
        fractalPanel = panel.get();
        content = std::move(panel);
    }
    const juce::Component::SafePointer<MotionEditor> owner(this);
    preparationSettingsOpen = true;
    const juce::Component::SafePointer<osci::OverlayComponent> overlayPointer(motion::ui::Sheet::show(*this, std::move(content), [owner] {
        if (owner != nullptr) {
            owner->preparationSettingsOpen = false;
            owner->showNextPreparationSettings();
        }
    }));
    auto submit = [owner, overlayPointer, request](motion::BakeSettings settings, motion::RasterSettings rasterSettings, std::optional<juce::String> editedText = {}, std::optional<motion::TextSettings> textSettings = {}, std::optional<int> fractalDepth = {}, std::shared_ptr<const juce::MemoryBlock> preparedVideo = {}) mutable {
        request.preparedVideo = std::move(preparedVideo);
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
    if (imagePanel != nullptr) {
        imagePanel->onPrepare = [submit, imagePanel](motion::RasterSettings settings) mutable { submit({}, settings, {}, {}, {}, imagePanel->preparedFor(settings)); };
    }
    if (fractalPanel != nullptr) { fractalPanel->onPrepare = [submit](int depth) mutable { submit({}, {}, {}, {}, depth); }; }
}

void MotionEditor::beginSourceImport(SourceRequest request, motion::BakeSettings settings, motion::RasterSettings rasterSettings) {
    if (request.generation != processor.document.generation()) { return; }
    const auto extension = request.replacement != nullptr ? request.replacement->extension : request.file.getFileExtension();
    if (osci::files::isVideo(extension) && request.uniqueClip == 0) {
        const juce::Component::SafePointer<MotionEditor> owner(this);
        if (!ffmpegReady([owner, request, settings, rasterSettings] { if (owner != nullptr) { owner->beginSourceImport(request, settings, rasterSettings); } })) { return; }
    }
    statusBar.clear();
    auto asset = std::make_shared<motion::Asset>();
    asset->name = request.replacement != nullptr ? request.replacement->name : request.file.getFileName();
    // A sequence goes by the name its SVGs share, not a file of its own.
    if (request.replacement == nullptr && request.file.hasFileExtension(motion::svgsequence::extension)) { asset->name = request.file.getFileNameWithoutExtension(); }
    asset->extension = request.replacement != nullptr ? request.replacement->extension : request.file.getFileExtension().toLowerCase();
    asset->bakeSettings = settings;
    asset->rasterSettings = rasterSettings;
    // A source relinked to another file keeps its settings, as an edited one does.
    const auto previous = request.replacement != nullptr ? request.replacement : request.relink != 0 ? motion::findAsset(processor.document.mainProject().assets, request.relink) : nullptr;
    asset->fractalDepth = request.fractalDepth.value_or(previous != nullptr ? previous->fractalDepth : 3);
    asset->originalSize = request.originalSize.value_or(previous != nullptr && previous->originalSize);
    asset->textSettings = request.textSettings.value_or(previous != nullptr ? previous->textSettings : motion::TextSettings());
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
        // A video its settings sheet already prepared keeps those frames as
        // its cache, so decoding it doesn't prepare it again.
        const auto keepPrepared = [&] {
            if (request.preparedVideo == nullptr || !osci::files::isVideo(asset->extension)) { return; }
            asset->bakedData = *request.preparedVideo;
            asset->bakeKey = motion::videoCacheKey(*asset);
        };
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
                        asset->linked = request.replacement->linked;
                        keepPrepared();
                    }
                    result = request.uniqueClip != 0 ? juce::Result::ok() : motion::decodeAsset(*asset, &task->cancelled, &task->progress, videoDecoder);
                } else if (motion::linksFromDisk(request.file)) {
                    // Large sound or video: linked where it is, not read in.
                    asset->linked = request.file;
                    keepPrepared();
                    result = motion::decodeAsset(*asset, &task->cancelled, &task->progress, videoDecoder);
                } else if (request.file.getSize() > static_cast<juce::int64>(motion::maximumSourceBytes)) {
                    result = juce::Result::fail("This source exceeds the 64 MiB preparation limit.");
                } else {
                    if (request.file.loadFileAsData(asset->data)) {
                        keepPrepared();
                        result = motion::decodeAsset(*asset, &task->cancelled, &task->progress, videoDecoder);
                    } else {
                        result = juce::Result::fail("Couldn't read the source file.");
                    }
                }
            } catch (const std::exception& error) {
                result = juce::Result::fail("Couldn't import this source: " + juce::String(error.what()));
            }
        }
        juce::MessageManager::callAsync([owner, request, asset, time, result, generation, task] {
            if (owner == nullptr) {
                return;
            }
            owner->importEnded(task);
            // A text animation change that won't be applied stops showing.
            if (task->cancelled.load() || owner->processor.document.generation() != generation) {
                owner->textAnimation.clearPending();
                return;
            }
            if (result.failed()) {
                owner->textAnimation.clearPending();
                // A new script that fails reopens with its code, as an edit
                // does; the editor shows the error where it is.
                const auto& assets = owner->processor.document.project().assets;
                const auto newScript = request.replacement == nullptr && request.relink == 0 && request.file.hasFileExtension("lua");
                const auto editedScript = request.uniqueClip == 0 && request.replacement != nullptr && (request.replacement->extension.equalsIgnoreCase(".lua") || request.replacement->extension.equalsIgnoreCase(".txt"))
                    && std::find(assets.begin(), assets.end(), request.replacement) != assets.end();
                if (newScript || editedScript) {
                    auto retry = request;
                    retry.retrySettings = asset->bakeSettings;
                    retry.preparationError = result.getErrorMessage();
                    owner->preparationRequests.push_front(std::move(retry));
                    owner->showNextPreparationSettings();
                    return;
                }
                owner->statusBar.show(result.getErrorMessage());
                return;
            }
            auto& document = owner->processor.document;
            if (request.uniqueClip != 0) {
                motion::Id made = 0;
                const auto separated = document.makeSourceUnique(request.uniqueClip, request.replacement, *asset, made);
                if (separated.failed()) { owner->statusBar.show(separated.getErrorMessage()); return; }
                owner->assetLibrary.refresh();
                owner->assetLibrary.selectAsset(made);
                owner->libraryTabs.setSelectedIndex(0);
                owner->select(request.uniqueClip);
                return;
            }
            if (request.replacement != nullptr) {
                const auto& assets = document.project().assets;
                if (std::find(assets.begin(), assets.end(), request.replacement) == assets.end()) {
                    // Gone (removed or undone) or changed by another edit meanwhile.
                    const auto gone = std::none_of(assets.begin(), assets.end(), [&](const auto& item) { return item->id == request.replacement->id; });
                    owner->statusBar.show(gone ? "The source was removed while it was being edited, so the edit wasn't applied."
                                               : "The source changed while baking. Its previous result has been kept.", MotionStatusBar::Kind::warning);
                    owner->textAnimation.clearPending();
                    return;
                }
                asset->id = request.replacement->id;
                const auto label = request.editedText.has_value() ? juce::String(asset->extension.equalsIgnoreCase(".lua") ? "Edit Lua source" : "Edit text source")
                    : request.textSettings.has_value() ? juce::String("Animate text")
                    : request.originalSize.has_value() ? juce::String(*request.originalSize ? "Keep original size" : "Fit to the screen") : juce::String("Change source settings");
                document.edit(label, [&](motion::Project& project) {
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
            document.edit(asset->audio != nullptr ? "Import audio" : "Import source", [&](motion::Project& project) {
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

void MotionEditor::importEnded(const std::shared_ptr<ImportState>& task) {
    std::erase(pendingImports, task);
    const juce::Component::SafePointer<MotionEditor> owner(this);
    juce::MessageManager::callAsync([owner] {
        if (owner == nullptr || !owner->pendingImports.empty() || !owner->queuedTextAnimation.has_value()) { return; }
        const auto queued = *owner->queuedTextAnimation;
        owner->queuedTextAnimation.reset();
        if (queued.generation != owner->processor.document.generation()) { owner->textAnimation.clearPending(); return; }
        if (owner->textAnimation.onApply) { owner->textAnimation.onApply(queued.asset, queued.settings); }
    });
}

void MotionEditor::cancelImports() {
    for (const auto& task : pendingImports) { task->cancelled.store(true); }
    if (queuedTextAnimation.has_value()) {
        queuedTextAnimation.reset();
        textAnimation.clearPending();
    }
}

void MotionEditor::addGenerator(const motion::GeneratorDefinition& definition, std::optional<motion::LightPreset> preset, std::string_view named, const motion::PropertyMap& settings) {
    auto& document = processor.document;
    auto asset = std::make_shared<motion::Asset>();
    asset->id = document.newId();
    if (named.empty()) { named = preset.has_value() ? motion::lightPresetName(*preset) : definition.name; }
    asset->name = motion::Document::uniqueAssetName(document.mainProject(), juce::String(named.data(), named.size()));
    asset->extension = juce::String(motion::generatorExtension.data(), motion::generatorExtension.size());
    asset->data.append(definition.id.data(), definition.id.size());
    // Drawn at its defaults at once: there is nothing to read or wait for.
    const auto decoded = motion::decodeAsset(*asset);
    if (decoded.failed()) {
        statusBar.show(decoded.getErrorMessage(), MotionStatusBar::Kind::error);
        return;
    }
    auto clip = motion::Document::makeClip(document.newId(), *asset, document.project().frameTime(processor.position.load()));
    for (const auto& [property, curve] : settings) { clip.properties[property] = curve; }
    // A light shines to the end, rather than going out partway through.
    if (definition.category == motion::GeneratorCategory::light && document.project().duration - clip.start > 1.0e-3) { clip.duration = document.project().duration - clip.start; }
    motion::Track track;
    track.id = document.newId();
    track.name = asset->name.toStdString();
    track.insert(clip, document.project().tempo());
    // A preset's modulators and routes in the same edit, their ids made first.
    std::vector<motion::Id> ids;
    for (std::size_t index = 0; preset.has_value() && index < motion::lightPresetIds; ++index) { ids.push_back(document.newId()); }
    document.edit("Add " + juce::String(named.data(), named.size()).toLowerCase(), [&](motion::Project& project) {
        project.assets.push_back(asset);
        project.tracks.push_back(track);
        project.duration = std::max(project.duration, clip.timing(project.tempo()).end());
        std::size_t used = 0;
        if (preset.has_value()) { motion::applyLightPreset(project, clip.id, *preset, [&] { return ids[used++]; }); }
    });
    assetLibrary.refresh();
    assetLibrary.selectAsset(asset->id);
    select(clip.id);
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
    luaPreviewDue = 0;
    ++luaPreviewGeneration;
    if (luaStartedPlayback) { processor.playing.store(false); }
    luaStartedPlayback = false;
    if (luaPreviewCancel != nullptr) { luaPreviewCancel->store(true); }
    for (auto* component : std::initializer_list<juce::Component*> {&composition, &viewportHeader}) { component->setVisible(true); }
    // Drop the preview of what was being edited.
    processor.prepareComposition(processor.document.project());
    resized();
    showNextPreparationSettings();
}

bool MotionEditor::sceneEditorOpen() {
    if (sceneEditor == nullptr) { return false; }
    statusBar.show("An editor is open in the Scene: finish it or cancel it first.", MotionStatusBar::Kind::warning);
    sceneEditor->grabKeyboardFocus();
    return true;
}

// Draw a new source, or edit a drawn one (`asset`). The drawing is saved as
// SVG in a temporary folder and imported like any file, relinking an edit.
void MotionEditor::showDrawingEditor(motion::Id asset) {
    if (sceneEditorOpen()) { return; }
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
        const auto file = writeTemporary("drawings", juce::File::createLegalFileName(text) + ".svg", motion::drawing::toSvg(drawing));
        if (!file.has_value()) {
            statusBar.show("Could not save the drawing.");
            return;
        }
        const auto asset = drawingAsset;
        juce::MessageManager::callAsync([owner = juce::Component::SafePointer<MotionEditor>(this), file = *file, asset] {
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
    if (request.replacement == nullptr || sceneEditorOpen()) { return; }
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
            if (found == nullptr) { owner->statusBar.show("The text source was removed while it was being edited.", MotionStatusBar::Kind::warning); return; }
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
    if (sceneEditorOpen()) { return; }
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
            const auto copy = writeTemporary("lua", next.file.getFileName(), written);
            if (!copy.has_value()) {
                statusBar.show("Could not save the edited script.");
                return;
            }
            next.file = *copy;
        }
        luaSubmitted = true;
        juce::MessageManager::callAsync([owner = juce::Component::SafePointer<MotionEditor>(this), next, settings] {
            if (owner == nullptr) { return; }
            owner->closeSceneEditor();
            if (owner->processor.document.generation() == next.generation) { owner->beginSourceImport(next, settings); }
        });
    };
    editor.onChanged = [this] { luaPreviewDue = juce::Time::getMillisecondCounterHiRes() + 300; };
    resized();
    editor.focusCode();
    previewLua();
    // A script shows running, as osci-render plays it, while it is written.
    luaStartedPlayback = !processor.playing.load();
    processor.playing.store(true);
}

void MotionEditor::previewLua() {
    luaPreviewDue = 0;
    const auto* editor = sceneEditorAs<MotionLuaSourceEditor>();
    if (editor == nullptr) { return; }
    if (luaPreviewCancel != nullptr) { luaPreviewCancel->store(true); }
    auto cancel = std::make_shared<std::atomic<bool>>(false);
    luaPreviewCancel = cancel;
    auto asset = std::make_shared<motion::Asset>();
    asset->name = luaRequest.replacement != nullptr ? luaRequest.replacement->name : luaRequest.file.getFileName();
    asset->extension = ".lua";
    const auto code = editor->code();
    asset->data.append(code.toRawUTF8(), code.getNumBytesAsUTF8());
    asset->bakeSettings = editor->settings();
    const auto generation = ++luaPreviewGeneration;
    luaPreviews.addJob([owner = juce::Component::SafePointer<MotionEditor>(this), asset, cancel, generation] {
        const auto result = motion::decodeAsset(*asset, cancel.get());
        if (cancel->load()) { return; }
        juce::MessageManager::callAsync([owner, asset, result, generation] {
            if (owner == nullptr || generation != owner->luaPreviewGeneration) { return; }
            auto* editor = owner->sceneEditorAs<MotionLuaSourceEditor>();
            if (editor == nullptr) { return; }
            // A problem shows as it is found; the Scope keeps the last that ran.
            editor->showLiveProblem(result.failed() ? result.getErrorMessage() : juce::String());
            if (result.wasOk()) { owner->showLuaPreview(asset); }
        });
    });
}

// In place of the source being edited, or a new one on a track of its own
// for the whole project, as a drawing previews.
void MotionEditor::showLuaPreview(std::shared_ptr<motion::Asset> asset) {
    previewSource(asset, luaRequest.replacement != nullptr ? luaRequest.replacement->id : 0, "Script");
}

void MotionEditor::previewSource(std::shared_ptr<motion::Asset> asset, motion::Id replacing, const juce::String& trackName) {
    auto project = processor.document.project();
    if (replacing != 0) {
        asset->id = replacing;
        for (auto& item : project.assets) {
            if (item->id == replacing) { item = asset; }
        }
    } else {
        asset->id = PreviewId::asset;
        project.assets.push_back(asset);
        motion::Track track;
        track.id = PreviewId::track;
        track.name = trackName.toStdString();
        auto clip = motion::Document::makeClip(PreviewId::clip, *asset, 0);
        clip.duration = project.duration;
        track.insert(clip, project.tempo());
        project.tracks.push_back(track);
    }
    processor.prepareComposition(project);
}

// The words being written, on the output: the source is swapped in place,
// still, so typing stays quick; its animation plays once saved.
void MotionEditor::previewText() {
    textPreviewDue = 0;
    const auto* editor = sceneEditorAs<MotionTextSourceEditor>();
    if (editor == nullptr || textRequest.replacement == nullptr) { return; }
    auto asset = std::make_shared<motion::Asset>(*textRequest.replacement);
    const auto text = editor->currentText();
    asset->data.reset();
    asset->data.append(text.toRawUTF8(), text.getNumBytesAsUTF8());
    asset->textSettings = editor->currentSettings();
    asset->textSettings.animation = motion::TextSettings::Animation::none;
    if (text.trim().isEmpty() || text.length() > 16384 || motion::decodeAsset(*asset).failed()) {
        processor.prepareComposition(processor.document.project());
        return;
    }
    previewSource(asset, asset->id, {});
}

// The drawing in progress on the output: an edited source is swapped in place;
// a new one plays on a track of its own for the whole project.
void MotionEditor::previewDrawing() {
    const auto* editor = sceneEditorAs<MotionDrawingEditor>();
    if (editor == nullptr) { return; }
    auto asset = std::make_shared<motion::Asset>();
    asset->name = "Drawing.svg";
    asset->extension = ".svg";
    const auto svg = motion::drawing::toSvg(editor->current());
    asset->data.append(svg.toRawUTF8(), svg.getNumBytesAsUTF8());
    if (editor->current().empty() || motion::decodeAsset(*asset).failed()) {
        processor.prepareComposition(processor.document.project());
        return;
    }
    previewSource(asset, drawingAsset, "Drawing");
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
            statusBar.show("The object changed. Pick its parts again.", MotionStatusBar::Kind::notice);
            return;
        }
        const auto split = motion::parts::split(*drawing, chosen.shapes);
        if (!split.has_value()) {
            statusBar.show("Pick some of the object's parts, not all of them.", MotionStatusBar::Kind::notice);
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

void MotionEditor::newScript() {
    static constexpr const char* script =
        "-- Runs for every point the beam draws.\n"
        "-- Return {x, y} or {x, y, z}, each -1 to 1.\n"
        "--\n"
        "-- phase      0 to 2 pi across each frame\n"
        "-- play_time  seconds since the clip began\n"
        "-- slider_a   0 to 1, animated in Properties\n"
        "--            (slider_b to slider_z too)\n"
        "\n"
        "local petals = 3 + math.floor(slider_a * 6)\n"
        "local turn = play_time * 0.5\n"
        "local r = 0.45 + 0.4 * math.cos(petals * phase)\n"
        "\n"
        "local x = r * math.cos(phase + turn)\n"
        "local y = r * math.sin(phase + turn)\n"
        "return {x, y}\n";
    // Its editor opens now or not at all: never queued behind one open.
    if (sceneEditorOpen()) { return; }
    // Named Script, or Script 2 and on beside others.
    juce::String name = "Script";
    for (int number = 2; std::any_of(processor.document.project().assets.begin(), processor.document.project().assets.end(), [&](const auto& asset) { return asset->name.upToLastOccurrenceOf(".", false, false) == name; }); ++number) {
        name = "Script " + juce::String(number);
    }
    const auto file = writeTemporary("scripts", name + ".lua", juce::String(script));
    if (!file.has_value()) {
        statusBar.show("Could not start a new script.");
        return;
    }
    importSourceFile(*file, 0);
}

// Examples are written to a temporary folder and imported like any file;
// the project keeps its own copy of the data.
void MotionEditor::importExample(const juce::String& resource) {
    const auto name = motion::ui::examples::fileName(resource);
    if (name.isEmpty()) { return; }
    const auto file = writeExample(resource);
    if (!file.has_value()) {
        statusBar.show("Could not prepare the example " + name + ".");
        return;
    }
    importSourceFile(*file, 0);
}

// The project with an effect of `type` added to `owner`, on the output.

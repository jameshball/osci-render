#include "MotionEditor.h"
#include "../JucewrightAutomation.h"
#include "../visualiser/OfflineVisualiserParameters.h"
#include "export/SignalExporter.h"
#include "export/SoundtrackExporter.h"
#include "ui/VideoExportSettings.h"
#include <iostream>

namespace {
struct MotionVideoTemporaryFiles {
    const juce::File directory = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("osci-motion-video-" + juce::Uuid().toString());
    ~MotionVideoTemporaryFiles() { directory.deleteRecursively(); }
    juce::File signal() const { return directory.getChildFile("beam.wav"); }
    juce::File soundtrack() const { return directory.getChildFile("soundtrack.wav"); }
};

// The beam as an Output choice names it.
juce::String exportedAs(motion::OutputMode mode) {
    switch (mode) {
        case motion::OutputMode::xy: return "Beam XY";
        case motion::OutputMode::xyz: return "Beam XYZ";
        case motion::OutputMode::xyzInverted: return "Beam XYZ, inverted Z";
        case motion::OutputMode::xyrgb: return "Beam XYRGB";
        case motion::OutputMode::soundtrack: break;
    }
    return "Audio";
}
}

juce::File MotionEditor::defaultExportFile(const juce::String& extension) const {
    const auto named = processor.currentProjectFile.isNotEmpty() ? juce::File(processor.currentProjectFile).getFileNameWithoutExtension() : juce::String(processor.document.mainProject().name);
    const auto stem = juce::File::createLegalFileName(named.trim());
    return processor.getLastOpenedDirectory().getChildFile((stem.isEmpty() ? juce::String("composition") : stem) + "." + extension);
}

juce::File MotionEditor::withExtension(juce::File chosen, const juce::String& extension) {
    return chosen.getFileExtension().isEmpty() ? chosen.withFileExtension(extension) : chosen;
}

void MotionEditor::exported(const juce::File& file) {
#if JUCE_MAC
    const juce::String reveal = "Show in Finder";
#else
    const juce::String reveal = "Show in folder";
#endif
    statusBar.show("Exported " + file.getFileName(), MotionStatusBar::Kind::notice, reveal, [file] { file.revealToUser(); });
}

bool MotionEditor::waitsForCapture(bool video, std::function<void()> exportWithout) {
    const auto live = processor.document.liveSourcesInUse();
    if (live.empty()) { return false; }
    juce::StringArray names;
    for (const auto& source : live) { names.add(source->name); }
    const auto many = live.size() > 1;
    // Blender's sources play in Blender; a shared picture plays in its app.
    const auto blender = std::all_of(live.begin(), live.end(), [](const auto& source) { return !motion::texture::isSource(source->extension); });
    const auto message = blender
        ? "Export draws every frame offline, so it can't wait for Blender to send them. Record a capture of " + (many ? "each of them" : names[0]) + " while it plays in Blender: its clips then draw the capture, and you can export."
        : "Export draws every frame offline, so it can't wait for live sources. Record a capture of " + (many ? "each of them" : names[0]) + " while it plays: its clips then draw the capture, and you can export.";
    const juce::Component::SafePointer<MotionEditor> owner(this);
    const auto first = live.front()->id;
    auto sheet = std::make_unique<motion::ui::MessageSheet>("Capture before exporting", message, "Record capture", [owner, first, video] {
        if (owner == nullptr) { return; }
        owner->captureForExport = first;
        owner->captureForExportGeneration = owner->processor.document.generation();
        owner->captureForVideo = video;
        owner->showLiveSettings(first);
    }, names.joinIntoString(", "));
    sheet->addAlternative(many ? "Export without them" : "Export without it", std::move(exportWithout));
    motion::ui::Sheet::show(*this, std::move(sheet));
    return true;
}

VideoEncodingConfiguration MotionEditor::exportConfiguration(const motion::Project& project) {
    auto config = recordingSettings.createVideoEncodingConfiguration();
    const auto& settings = project.exportSettings;
    config.renderSize = {settings.width, settings.height};
    config.codec = config.preserveAlpha ? VideoCodec::ProRes4444 : static_cast<VideoCodec>(settings.codec);
    config.crf = settings.crf;
    config.includeAudio = settings.soundtrack;
    config.frameRate = project.frameRate;
    const auto& info = VideoEncodingConstants::getVideoCodecInfo(config.codec);
    config.fileExtension = config.preserveAlpha ? "mov" : info.defaultFileExtension;
    config.audioCodecArgs = info.proRes ? juce::StringArray {"-c:a", "pcm_s16le"} : juce::StringArray {"-c:a", "aac", "-b:a", "384k"};
    return config;
}

bool MotionEditor::ffmpegReady(std::function<void()> resume) {
#if OSCI_PREMIUM
    // In place already: checked, and nothing to resume (its success
    // callback would run at once and call back in here).
    if (processor.getFFmpegFile().existsAsFile() && processor.validateFFmpegBinary()) { return true; }
    if (ffmpegDownloader.isThreadRunning()) {
        statusBar.showWorking("Downloading FFmpeg...");
        return false;
    }
    const juce::Component::SafePointer<MotionEditor> self(this);
    ffmpegDownloader.onProgress = [self](double fraction) {
        if (self == nullptr) { return; }
        self->statusBar.showWorking(fraction < 0 ? juce::String("Downloading FFmpeg...") : "Downloading FFmpeg... " + juce::String(juce::roundToInt(fraction * 100)) + "%");
    };
    ffmpegDownloader.onFailedDownload = [self, resume] {
        if (self == nullptr) { return; }
        self->statusBar.show("Couldn't download FFmpeg, which video needs. Check the internet connection.", MotionStatusBar::Kind::error, "Try again", [self, resume] {
            if (self != nullptr && self->ffmpegReady(resume)) { resume(); }
        });
    };
    return processor.ensureFFmpegExists([self] { if (self != nullptr) { self->statusBar.showWorking("Downloading FFmpeg..."); } },
                                        [self, resume] {
                                            if (self == nullptr) { return; }
                                            self->statusBar.show("FFmpeg is ready.", MotionStatusBar::Kind::notice);
                                            resume();
                                            // Videos dropped while it came wait for it too.
                                            self->showNextPreparationSettings();
                                        });
#else
    juce::ignoreUnused(resume);
    return false;
#endif
}

void MotionEditor::exportVideo(bool leaveOutLive) {
#if OSCI_PREMIUM
    const juce::Component::SafePointer<MotionEditor> self(this);
    if (exportState != nullptr || waitsForSliderBakes([self, leaveOutLive] { if (self != nullptr) { self->exportVideo(leaveOutLive); } })) { return; }
    if (!leaveOutLive && waitsForCapture(true, [self] { if (self != nullptr) { self->exportVideo(true); } })) { return; }
    if (!ffmpegReady([self, leaveOutLive] { if (self != nullptr) { self->exportVideo(leaveOutLive); } })) { return; }
    std::shared_ptr<OfflineVisualiserParameters> beamSnapshot;
    try {
        beamSnapshot = std::make_shared<OfflineVisualiserParameters>(processor.visualiserParameters, OfflineVisualiserParameters::ExternalModulation::replace);
    } catch (const std::exception& error) {
        statusBar.show("Couldn't capture the beam settings: " + juce::String(error.what()));
        return;
    }
    const auto project = leaveOutLive ? motion::Document::withoutLiveSources(processor.document.mainProject()) : processor.document.mainProject();
    auto config = exportConfiguration(project);
    const auto renderMode = visualiser.getRenderMode();
    auto state = std::make_shared<ExportState>();
    state->sampleRate = processor.exportSampleRate();
    exportState = state;
    const juce::Component::SafePointer<MotionEditor> owner(this);
    auto settings = std::make_unique<MotionVideoExportSettings>(config, juce::String(project.name));
    auto* settingsPointer = settings.get();
    const auto size = juce::Point<int>(settings->getWidth(), settings->getHeight());
    auto overlay = std::make_unique<osci::ComponentOverlay>(std::move(settings), juce::String(), size, false);
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
            // What was chosen is the project's, for next time and to save.
            if (owner != nullptr) {
                auto chosen = owner->processor.document.mainProject().exportSettings;
                if (!config.preserveAlpha) { chosen.codec = static_cast<int>(config.codec); }
                chosen.crf = config.crf;
                chosen.soundtrack = config.includeAudio;
                if (!(chosen == owner->processor.document.mainProject().exportSettings)) {
                    owner->processor.document.edit("Change export settings", [chosen](motion::Project& updated) { updated.exportSettings = chosen; });
                }
            }
            if (owner == nullptr) { return; }
            // Automation names the file itself: a native save panel can't be driven.
            const auto automated = osci::isJucewrightAutomationLaunch() ? juce::SystemStats::getEnvironmentVariable("OSCI_MOTION_EXPORT_FILE", {}) : juce::String();
            if (automated.isNotEmpty()) {
                owner->startVideoExport(state, project, beamSnapshot, renderMode, config, juce::File(automated), {});
                return;
            }
            owner->chooser = std::make_unique<juce::FileChooser>("Export composition video", owner->defaultExportFile(config.fileExtension), "*." + config.fileExtension);
            owner->chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                | juce::FileBrowserComponent::warnAboutOverwriting, [owner, state, project, beamSnapshot, renderMode, config](const juce::FileChooser& selected) {
                if (owner == nullptr) { return; }
                if (selected.getResult() == juce::File()) { owner->exportState.reset(); return; }
                const auto destination = withExtension(selected.getResult(), config.fileExtension);
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
        auto content = std::make_unique<motion::ui::ProgressSheet>("Exporting video", destination.getFileName(), config.includeAudio ? "Preparing the beam signal and audio..." : "Preparing the beam signal...", [state] {
            return state->videoWithAudio ? (state->progress.load() + state->soundtrackProgress.load()) * 0.5 : state->progress.load();
        }, [state] { state->cancelled.store(true); }, "Cancel video preparation");
        const auto size = juce::Point<int>(content->getWidth(), content->getHeight());
        auto overlay = std::make_unique<osci::ComponentOverlay>(std::move(content), juce::String(), size, false);
        const juce::Component::SafePointer<osci::ComponentOverlay> preparationOverlay(overlay.get());
        owner->showOverlay(std::move(overlay));
        // The worker owns one immutable prepared snapshot for both WAVs.
        // What the editor has baked already isn't baked again.
        const auto baked = owner->processor.hiddenLineBake();
        owner->exports.addJob([owner, state, project, beamSnapshot, renderMode, config, destination, preparationOverlay, finished, baked] {
            std::shared_ptr<const motion::PreparedBeam> picture;
            std::shared_ptr<MotionVideoTemporaryFiles> temporary;
            auto result = juce::Result::ok();
            try {
                temporary = std::make_shared<MotionVideoTemporaryFiles>();
                result = temporary->directory.createDirectory();
                if (result.wasOk() && !state->cancelled.load()) {
                    const auto rate = state->sampleRate;
                    const motion::PreparedComposition prepared(project, rate, &state->cancelled, motion::CompositionPurpose::signal, motion::hidden::Reach::ofCanvas(config.renderSize.width, config.renderSize.height, VisualiserGeometry::signalGain));
                    picture = std::make_shared<const motion::PreparedBeam>(prepared.beam);
                    result = motion::SignalExporter::write(prepared, temporary->signal(), rate, state->cancelled, &state->progress, baked.get(), state->stretches.has_value() ? &*state->stretches : nullptr);
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
                        beamSnapshot->params.applyExternalModulation = [picture, slots, state, rate = state->sampleRate, cursor = std::make_shared<juce::int64>(0)](int samples) {
                            for (std::size_t index = 0; index < motion::beamPropertySpecs.size(); ++index) {
                                slots->fill(index, samples, [&](int sample) {
                                    const auto seconds = static_cast<double>(*cursor + sample) / rate;
                                    return picture->value(index, state->stretches.has_value() ? state->stretches->timeAt(seconds) : seconds);
                                });
                            }
                            *cursor += samples;
                        };
                    }
                    // A render that never starts still runs this completion (unsuccessful).
                    owner->startOfflineVideoRender(temporary->signal(), config.includeAudio ? temporary->soundtrack() : juce::File(),
                        destination, config, renderMode, [owner, state, temporary, destination, finished] {
                            // Completion can run from base-editor destruction;
                            // touch derived UI only in a later safe callback.
                            juce::MessageManager::callAsync([owner, state, destination, finished] {
                                if (owner == nullptr) { return; }
                                if (owner->exportState == state) { owner->exportState.reset(); }
                                if (owner->lastOfflineRenderSucceeded() && !state->cancelled.load()) { owner->exported(destination); }
                                if (finished) { finished(owner->lastOfflineRenderSucceeded()); }
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
            processor.keepLaunchSession();
            continue;
        }
        if (token.startsWith("--stills=")) {
            for (const auto& time : juce::StringArray::fromTokens(token.fromFirstOccurrenceOf("=", false, false), ",", {})) { commandLineStills.push_back(time.getDoubleValue()); }
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
        // The render is of that project alone: unsaved work open here isn't
        // discarded or rendered in its place.
        if (processor.document.unsaved()) {
            failCommandLineRender("the open project has unsaved changes; save it first.");
            return;
        }
        commandLineProject = CommandLineProject::waiting;
        commandLineProjectFile = project;
    }
    if (remaining.isEmpty()) { return; }
    if (!launched) {
        launchFiles = (launchFiles + " " + remaining.joinIntoString(" ")).trim();
        return;
    }
    CommonPluginEditor::handleCommandLine(remaining.joinIntoString(" "));
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
    if (commandLineProject == CommandLineProject::failed) { failCommandLineRender("the project could not be opened."); return; }
    if (commandLineProject != CommandLineProject::loaded) { return; }
    if (processor.getPreparationError().isNotEmpty()) { failCommandLineRender(processor.getPreparationError()); return; }
    // Lua sliders bake in the background: frames drawn before they land
    // would be drawn without them.
    sliderBakes.update();
    if (sliderBakes.busy()) { return; }
    commandLineRenderStarted = true;
    if (!processor.ensureFFmpegExists()) { failCommandLineRender("FFmpeg is unavailable."); return; }
    std::shared_ptr<OfflineVisualiserParameters> beamSnapshot;
    try {
        beamSnapshot = std::make_shared<OfflineVisualiserParameters>(processor.visualiserParameters, OfflineVisualiserParameters::ExternalModulation::replace);
    } catch (const std::exception& error) {
        failCommandLineRender(error.what());
        return;
    }
    // Nothing waits for Blender here: live sources are left out, and said so.
    for (const auto& source : processor.document.liveSourcesInUse()) { std::cerr << "osci-motion: leaving out " << source->name << ", a live source; capture it to render it." << std::endl; }
    const auto project = motion::Document::withoutLiveSources(processor.document.mainProject());
    auto config = exportConfiguration(project);
    config.includeAudio = config.includeAudio && commandLineStills.empty();
    auto state = std::make_shared<ExportState>();
    state->sampleRate = processor.exportSampleRate();
    if (!commandLineStills.empty()) { state->stretches = motion::Stretches::upTo(commandLineStills, project.frameRate); }
    exportState = state;
    const auto destination = commandLineRender;
    std::cout << "osci-motion rendering " << project.name << " at " << state->sampleRate << " Hz to " << destination.getFullPathName() << std::endl;
    const juce::Component::SafePointer<MotionEditor> owner(this);
    startVideoExport(state, project, beamSnapshot, visualiser.getRenderMode(), config, destination, [owner, destination](bool succeeded) {
        if (owner == nullptr) { return; }
        if (!succeeded) { owner->failCommandLineRender("the export did not complete."); return; }
        std::cout << "osci-motion rendered " << destination.getFullPathName() << std::endl;
        juce::JUCEApplicationBase::quit();
    });
#endif
}

// An export drawn before the Lua sliders finish baking would leave them out,
// so it waits for them.
bool MotionEditor::waitsForSliderBakes(std::function<void()> resume) {
    sliderBakes.update();
    if (!sliderBakes.busy()) {
        return false;
    }
    afterSliderBakes = std::move(resume);
    afterSliderBakesGeneration = processor.document.generation();
    statusBar.showWorking(waitingForSliderBakes);
    return true;
}

void MotionEditor::exportSignal(MotionProcessor::OutputMode mode, bool leaveOutLive) {
    // The project's audio draws nothing: Lua sliders and live sources don't
    // matter to it.
    const auto beam = motion::beamOutputChannels(mode) > 0;
    const juce::Component::SafePointer<MotionEditor> self(this);
    if (exportState != nullptr || (beam && waitsForSliderBakes([self, mode, leaveOutLive] { if (self != nullptr) { self->exportSignal(mode, leaveOutLive); } }))) {
        return;
    }
    if (beam && !leaveOutLive && waitsForCapture(false, [self, mode] { if (self != nullptr) { self->exportSignal(mode, true); } })) {
        captureForSignal = mode;
        return;
    }
    auto state = std::make_shared<ExportState>();
    exportState = state;
    state->sampleRate = processor.exportSampleRate();
    const juce::Component::SafePointer<MotionEditor> owner(this);
    const auto exportTo = [owner, state, leaveOutLive, mode, beam](const juce::File& chosen) {
        if (owner == nullptr) { return; }
        if (chosen == juce::File()) { owner->exportState.reset(); return; }
        const auto destination = withExtension(chosen, "wav");
        if (!destination.getFileExtension().equalsIgnoreCase(".wav")) {
            owner->exportState.reset();
            owner->statusBar.show("Audio exports as a .wav file. Choose Export audio again and use that extension.");
            return;
        }
        const auto project = leaveOutLive ? motion::Document::withoutLiveSources(owner->processor.document.mainProject()) : owner->processor.document.mainProject();
        const auto reach = owner->processor.canvasReach();
        const auto baked = owner->processor.hiddenLineBake();
        owner->exportBar.setName("Audio export progress");
        owner->exportProgress = 0;
        owner->exportBar.setVisible(true);
        owner->cancelExport.setVisible(true);
        owner->exports.addJob([owner, state, project, destination, reach, baked, mode, beam] {
            const auto result = beam ? motion::SignalExporter::write(project, destination, state->sampleRate, state->cancelled, &state->progress, reach, baked.get(), mode)
                                     : motion::SoundtrackExporter::write(project, destination, state->sampleRate, state->cancelled, &state->progress);
            juce::MessageManager::callAsync([owner, state, result, destination] {
                if (owner == nullptr) {
                    return;
                }
                owner->exportState.reset();
                owner->exportBar.setVisible(false);
                owner->cancelExport.setVisible(false);
                if (result.failed() && !state->cancelled.load()) {
                    owner->statusBar.show(result.getErrorMessage());
                } else if (result.wasOk() && !state->cancelled.load()) {
                    owner->exported(destination);
                }
            });
        });
    };
    // Automation names the file itself: a native save panel can't be driven.
    const auto automated = osci::isJucewrightAutomationLaunch() ? juce::SystemStats::getEnvironmentVariable("OSCI_MOTION_EXPORT_FILE", {}) : juce::String();
    if (automated.isNotEmpty()) {
        exportTo(juce::File(automated));
        return;
    }
    chooser = std::make_unique<juce::FileChooser>("Export " + exportedAs(mode) + motion::style::dot() + juce::String(state->sampleRate / 1000.0, 1) + " kHz float WAV",
        defaultExportFile("wav"), "*.wav");
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
        | juce::FileBrowserComponent::warnAboutOverwriting, [exportTo](const juce::FileChooser& selected) { exportTo(selected.getResult()); });
}

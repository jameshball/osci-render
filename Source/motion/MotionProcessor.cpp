#include "MotionProcessor.h"
#include "MotionEditor.h"
#include "VisualiserState.h"

MotionProcessor::MotionProcessor()
    : CommonAudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)) {
    addAllParameters();
    rgbEnabled = true;
    // The document drives the Scope's beam and display properties: the audio
    // thread publishes their values each block and the visualiser applies
    // them to its animated values every frame.
    for (std::size_t index = 0; index < scopeBeam.size(); ++index) { scopeBeam[index].store(static_cast<float>(motion::beamPropertySpecs[index].defaultValue)); }
    scopeBeamSlots = std::make_unique<motion::ScopeBeamSlots>(visualiserParameters);
    visualiserParameters.applyExternalModulation = [this](int samples) {
        for (std::size_t index = 0; index < scopeBeam.size(); ++index) {
            const auto value = scopeBeam[index].load(std::memory_order_relaxed);
            scopeBeamSlots->fill(index, samples, [value](int) { return value; });
        }
    };
    // Undo snapshots share media and every track a step did not change. Bound
    // the history to about 256 MiB, always keeping the last 5 steps.
    getUndoManager().setMaxNumberOfStoredUnits(256 * 1024, 5);
    preparationWorker = std::make_unique<motion::CompositionPreparationWorker>([this] { triggerAsyncUpdate(); });
    recordingParameters.canvasWidth.addListener(this);
    recordingParameters.canvasHeight.addListener(this);
    blender = std::make_unique<motion::LiveBlenderController>(document, [this](auto entries) {
        blenderEntries = std::move(entries);
        publishLiveSources();
    });
    textures = std::make_unique<motion::LiveTextureController>(document, [this](auto entries) {
        textureEntries = std::move(entries);
        publishLiveSources();
    });
    // A parameter dragged after a document edit is its own undo step, never
    // joined to the edit's.
    document.onUndoStep = [this] { lastUndoParamId = {}; };
    document.onChanged = [this] {
        // The loop range reaches the audio thread with every edit, editor or not.
        const auto& project = document.project();
        loopStart.store(project.loopStart);
        loopEnd.store(project.loopEnd);
        looping.store(project.looping && project.hasLoop());
        if (document.viewOnlyChange()) { return; }
        // The project's rate, resampled to the device as it plays, and its
        // export frame as the Scope's canvas.
        setProcessingSampleRate(document.mainProject().sampleRate);
        // The project's rate, clamped, is the rate the device path prepares,
        // and stands in for it when there's no device (none, or one that
        // failed to open).
        const auto projectRate = document.mainProject().sampleRate;
        if (supportsInternalSampleRateOverride() && projectRate > 0) {
            requestedSampleRate.store(juce::jlimit(ProjectRateAdapter::lowestRate, ProjectRateAdapter::highestRate, projectRate));
        }
        const auto& exporting = document.mainProject().exportSettings;
        const VisualiserRenderSize frame {exporting.width, exporting.height};
        if (!(recordingParameters.getCanvasSize() == frame)) { recordingParameters.setCanvasSize(frame); }
        prepareComposition(project);
    };
    document.onChanged();
}

MotionProcessor::~MotionProcessor() {
    // Host teardown can occur off the message thread; exclude the controller's
    // timer before destroying its document access and publication callback.
    const juce::MessageManagerLock messageLock;
    visualiserParameters.applyExternalModulation = nullptr;
    recordingParameters.canvasWidth.removeListener(this);
    recordingParameters.canvasHeight.removeListener(this);
    blender.reset();
    textures.reset();
    preparationWorker.reset();
    cancelPendingUpdate();
    document.onChanged = nullptr;
    getUndoManager().clearUndoHistory();
}

void MotionProcessor::prepareComposition(const motion::Project& project) {
    preparationSampleRate = requestedSampleRate.load();
    preparationReach = canvasReach();
    preparationRevision = preparationWorker->request(project, preparationSampleRate, preparationReach);
}

void MotionProcessor::handleAsyncUpdate() {
    // The canvas changed elsewhere (the Scope's settings): the project's frame
    // follows, as an edit of its own.
    const auto canvas = recordingParameters.getCanvasSize();
    const auto& exporting = document.mainProject().exportSettings;
    if (canvas.width != exporting.width || canvas.height != exporting.height) {
        document.edit("Change the canvas", [canvas](motion::Project& project) {
            project.exportSettings.width = canvas.width;
            project.exportSettings.height = canvas.height;
        });
    }
    if (preparationSampleRate != requestedSampleRate.load() || preparationReach != canvasReach()) {
        prepareComposition(document.project());
        return;
    }
    // A result older than the latest request still plays until the newest
    // one is ready, so a drag updates playback as it goes.
    auto result = preparationWorker->take();
    if (result == nullptr || result->revision <= acceptedPreparationRevision) { return; }
    if (result->composition != nullptr && result->composition->sampleRate != preparationSampleRate) { return; }
    acceptedPreparationRevision = result->revision;
    preparationError = result->error;
    // A failed edit keeps playing the last good snapshot instead of blanking;
    // the editor reports the error. The audio thread only ever sees snapshots
    // that prepared completely.
    if (result->composition != nullptr && preparationError.isEmpty()) {
        result->composition->publicationRevision = result->revision;
        // What the edit left as it was is already baked.
        const auto previous = std::exchange(publishedBake, result->composition->hiddenLines);
        if (publishedBake != nullptr && previous != nullptr) { publishedBake->inherit(*previous); }
        composition.publish(std::move(result->composition));
        if (publishedBake != nullptr) {
            hiddenLineBaker.start(publishedBake, preparationSampleRate, publishedBake->frameOf(position.load()));
        } else {
            hiddenLineBaker.stop();
        }
    }
}

bool MotionProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto channels = layouts.getMainOutputChannelSet().size();
    // Two for the sound alone, more for the beam beside it.
    return layouts.getMainInputChannelSet().isDisabled() && channels >= 2 && channels <= mostOutputs;
}

void MotionProcessor::prepareToPlayInternal(double sampleRate, int samplesPerBlock) {
    signal.setSize(6, samplesPerBlock);
    audioSample = motion::sampleIndex(audioTime, sampleRate).value_or(0);
    oscillatorSample = audioSample;
    requestedSampleRate.store(sampleRate);
    triggerAsyncUpdate();
    transitionGuard.begin();
}

void MotionProcessor::processBlockInternal(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;
    lastCallbackMs.store(juce::Time::getMillisecondCounterHiRes(), std::memory_order_relaxed);
    buffer.clear();
    midi.clear();
    // The audio device may send more than it said it would: drawn in pieces
    // no longer than was prepared for (each referring to the buffer,
    // allocating nothing).
    const auto capacity = signal.getNumSamples();
    if (capacity <= 0) { return; }
    // Usually one piece: the buffer itself (a buffer referring to it would
    // allocate its channel list beyond 32 channels).
    if (buffer.getNumSamples() <= capacity) {
        processChunk(buffer);
        return;
    }
    for (int start = 0; start < buffer.getNumSamples(); start += capacity) {
        juce::AudioBuffer<float> part(buffer.getArrayOfWritePointers(), buffer.getNumChannels(), start, std::min(capacity, buffer.getNumSamples() - start));
        processChunk(part);
    }
}

void MotionProcessor::processChunk(juce::AudioBuffer<float>& buffer) {
    const auto* prepared = composition.acquire();
    const auto* liveBlock = liveSources.acquire();
    const auto* liveFrames = liveBlock != nullptr ? liveBlock->frames.get() : nullptr;
    if (liveBlock != nullptr && liveBlock->revision != previousLiveRevision) {
        previousLiveRevision = liveBlock->revision;
        transitionGuard.begin();
    }
    const auto sampleRate = getEffectiveSampleRate();
    const auto count = buffer.getNumSamples();
    // Shared output gain/clip buffers are allocated during prepareToPlay, but
    // must be populated each block before music monitoring or physical XY output.
    volumeEffect->animateValues(count, nullptr);
    thresholdEffect->animateValues(count, nullptr);
    const auto durationIndex = prepared != nullptr && std::isfinite(sampleRate) && sampleRate > 0 ? motion::sampleIndex(prepared->duration, sampleRate) : std::nullopt;
    // Nothing to play: the beam rests dark, and the Scope still hears so.
    if (prepared == nullptr || prepared->preparationError.isNotEmpty() || prepared->sampleRate != sampleRate || !durationIndex.has_value() || *durationIndex < 1) {
        transitionGuard.begin();
        for (int i = 0; i < count; ++i) {
            const auto point = transitionGuard.apply({0, 0, 0, 0, 0, 0});
            writeSignal(i, point.withColour(0, 0, 0));
        }
        juce::AudioBuffer<float> silent(signal.getArrayOfWritePointers(), 6, count);
        threadManager.write(silent, "VisualiserRenderer");
        routeSignalOutput(buffer);
        return;
    }
    if (prepared->publicationRevision != previousRevision) {
        previousRevision = prepared->publicationRevision;
        transitionGuard.begin();
    }
    const auto durationSamples = *durationIndex;
    // Plans hold pointers into the composition and live frames; either change replans.
    const auto liveGeneration = liveBlock != nullptr ? liveBlock->revision : std::numeric_limits<std::uint64_t>::max();
    const auto beamGeneration = prepared->publicationRevision * 0x9E3779B97F4A7C15ull + liveGeneration;
    const auto requested = requestedPosition.exchange(-1.0);
    if (requested >= 0.0 && std::isfinite(requested)) {
        audioSample = motion::sampleIndex(std::min(requested, prepared->duration), sampleRate).value_or(0);
        oscillatorSample = audioSample;
        speedCarry = 0;
        transitionGuard.begin();
    }
    // At 1x the beam's cycles keep step with the timeline; at another speed
    // they run on by themselves, so the beam keeps its frequency.
    const auto wanted = playbackSpeed.load(std::memory_order_relaxed);
    const auto speed = std::isfinite(wanted) ? std::clamp(wanted, playbackSpeeds.front(), playbackSpeeds.back()) : 1.0;
    const auto atSpeed = speed == 1.0;
    if (speed != previousSpeed) {
        previousSpeed = speed;
        speedCarry = 0;
        transitionGuard.begin();
    }
    // The beam keeps drawing while stopped (the picture at the playhead);
    // starting or stopping guards the jump.
    const auto running = playing.load();
    if (running && audioSample >= durationSamples) { audioSample = 0; }
    if (running != wasPlaying) { transitionGuard.begin(); }
    wasPlaying = running;
    juce::int64 loopStartSample = 0, loopEndSample = 0;
    if (looping.load(std::memory_order_relaxed)) {
        const auto first = motion::sampleIndex(std::clamp(loopStart.load(std::memory_order_relaxed), 0.0, prepared->duration), sampleRate);
        const auto last = motion::sampleIndex(std::clamp(loopEnd.load(std::memory_order_relaxed), 0.0, prepared->duration), sampleRate);
        if (first.has_value() && last.has_value() && *last > *first) { loopStartSample = static_cast<juce::int64>(*first); loopEndSample = static_cast<juce::int64>(*last); }
    }
    const auto route = getRouting();
    const auto audible = !muteParameter->getBoolValue();
    const auto* volumes = volumeEffect->getAnimatedValuesReadPointer(0, count);
    const auto fallbackVolume = volumeEffect->getValue();
    for (int i = 0; i < count; ++i) {
        audioTime = static_cast<double>(audioSample) / sampleRate;
        if (running && atSpeed) { oscillatorSample = audioSample; }
        const auto point = transitionGuard.apply(beam.sample(*prepared, audioTime, static_cast<std::int64_t>(oscillatorSample), sampleRate, running, beamGeneration, liveFrames, speed));
        if (route.soundOn() && running && audible && buffer.getNumChannels() >= route.sound + 2) {
            // Between samples off normal speed, so slow playback glides rather than steps.
            const auto audio = prepared->soundtrack.sample(atSpeed ? audioTime : (static_cast<double>(audioSample) + speedCarry) / sampleRate);
            const auto volume = volumes != nullptr ? volumes[i] : fallbackVolume;
            const auto gain = std::isfinite(volume) ? static_cast<double>(volume) : 0.0;
            constexpr auto limit = static_cast<double>(std::numeric_limits<float>::max());
            buffer.setSample(route.sound, i, static_cast<float>(std::clamp(audio.left * gain, -limit, limit)));
            buffer.setSample(route.sound + 1, i, static_cast<float>(std::clamp(audio.right * gain, -limit, limit)));
        }
        writeSignal(i, point);
        ++oscillatorSample;
        if (running) {
            const auto step = motion::advancePlayhead(audioSample, speed, speedCarry, loopStartSample, loopEndSample, durationSamples);
            audioSample = step.sample;
            if (step.jumped) { transitionGuard.begin(); }
        }
    }
    beamLayers.store(static_cast<int>(beam.plannedLayers()), std::memory_order_relaxed);
    beamSimplified.store(beam.plannedSimplified(), std::memory_order_relaxed);
    audioTime = static_cast<double>(audioSample) / sampleRate;
    position.store(audioTime);
    const auto picture = prepared->beam.at(audioTime);
    for (std::size_t index = 0; index < picture.size(); ++index) { scopeBeam[index].store(picture[index], std::memory_order_relaxed); }
    juce::AudioBuffer<float> block(signal.getArrayOfWritePointers(), 6, count);
    threadManager.write(block, "VisualiserRenderer");
    routeSignalOutput(buffer);
}

void MotionProcessor::routeSignalOutput(juce::AudioBuffer<float>& buffer) {
    const auto route = getRouting();
    if (!route.beamOn()) { return; }
    const auto audible = !muteParameter->getBoolValue();
    const auto count = buffer.getNumSamples();
    if (!audible) {
        motion::blankBeam(route.beam, buffer, count, route.beamFirst);
    } else if (motion::routeBeam(route.beam, signal, buffer, count, route.beamFirst)) {
        applyVolumeAndThreshold(buffer.getArrayOfWritePointers() + route.beamFirst, count);
    }
}

void MotionProcessor::afterDeviceRate(juce::AudioBuffer<float>& buffer) {
    const auto route = getRouting();
    if (route.beamOn()) { motion::clampBrightness(route.beam, buffer, buffer.getNumSamples(), route.beamFirst); }
}

juce::AudioProcessorEditor* MotionProcessor::createEditor() {
    return new MotionEditor(*this);
}

void MotionProcessor::getStateInformation(juce::MemoryBlock& destination) {
    // The document belongs to the message thread: it is read there, or
    // under its lock if the session is saved from elsewhere.
    std::optional<juce::MessageManagerLock> lock;
    if (!juce::MessageManager::existsAndIsCurrentThread()) {
        lock.emplace();
        if (!lock->lockWasGained()) { return; }
    }
    // A launch that only renders leaves the last session as it found it.
    if (keepsLaunchSession) {
        destination = launchSession;
        return;
    }
    if (unrestoredState.getSize() > 0) {
        if (document.generation() == unrestoredGeneration && document.revision() == unrestoredRevision) {
            destination = unrestoredState;
            return;
        }
        unrestoredState.reset();
    }
    juce::XmlElement project("motion-project");
    project.setAttribute("schema", 1);
    // A session with changes its file lacks reopens with them still unsaved.
    if (document.unsaved() && !savingProjectFile) { project.setAttribute("edited", true); }
    saveStandaloneProjectFilePathToXml(project);
    saveProperties(project);
    VisualiserState::save(project, visualiserParameters, recordingParameters);
    project.addChildElement(new juce::XmlElement(document.save()));
    motion::ProjectSummary::write(project, picture, document.mainProject().description);
    copyXmlToBinary(project, destination);
}

void MotionProcessor::setStateInformation(const void* data, int size) {
    if (!keepsLaunchSession || launchSession.isEmpty()) { launchSession = juce::MemoryBlock(data, static_cast<std::size_t>(std::max(0, size))); }
    motion::ProjectFile file;
    const auto result = motion::ProjectFile::read(data, static_cast<std::size_t>(std::max(0, size)), file);
    if (result.failed()) {
        // Kept as it was, so a newer or mended osci-motion can still reopen it.
        const juce::MessageManagerLock messageLock;
        unrestoredState = juce::MemoryBlock(data, static_cast<std::size_t>(std::max(0, size)));
        unrestoredGeneration = document.generation();
        unrestoredRevision = document.revision();
        const auto why = file.problem == motion::ProjectFile::Problem::newer ? juce::String("a newer osci-motion saved it; update osci-motion to reopen it") : juce::String("what was saved of it is damaged");
        restoreError = "Your last session couldn't be reopened: " + why + ". It is kept as it was until you change this project; open a saved project to carry on meanwhile.";
        return;
    }
    {
        // Restored: an earlier failure's state and warning no longer apply;
        // what this version couldn't use of it is said instead.
        const juce::MessageManagerLock messageLock;
        unrestoredState.reset();
        restoreError = file.notes.empty() ? juce::String() : "Reopened your last session without " + juce::String(file.notes.count()) + (file.notes.count() == 1 ? " thing" : " things") + " this version can't use: " + file.notes.lines().joinIntoString("; ") + ".";
    }
    const auto edited = file.xml->getBoolAttribute("edited");
    applyPreparedProject(std::move(file.project), *file.xml);
    if (edited) {
        const juce::MessageManagerLock messageLock;
        document.markUnsaved();
    }
}

void MotionProcessor::applyPreparedProject(motion::Project prepared, juce::XmlElement& state) {
    // Restoring a session is not a realtime operation. Serialize its commit
    // with the editor and the live-source owner, after worker preparation.
    const juce::MessageManagerLock messageLock;
    VisualiserState::load(state, visualiserParameters, recordingParameters);
    picture = motion::ProjectSummary::pictureIn(state);
    document.reset(std::move(prepared));
    restoreStandaloneProjectFilePathFromXml(state);
    loadProperties(state);
    getUndoManager().clearUndoHistory();
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new MotionProcessor();
}

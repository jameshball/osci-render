#include "MotionProcessor.h"
#include "MotionEditor.h"
#include "render/SampleClock.h"
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
            scopeBeamSlots->write(index, samples, [value](int) { return value; });
        }
    };
    // Undo keeps whole-project snapshots (shared media is not copied); bound
    // the history to about 256 MiB while always keeping the last 20 steps.
    getUndoManager().setMaxNumberOfStoredUnits(256 * 1024, 20);
    preparationWorker = std::make_unique<motion::CompositionPreparationWorker>([this] { triggerAsyncUpdate(); });
    blender = std::make_unique<motion::LiveBlenderController>(document, [this](auto frames) { publishLiveSources(std::move(frames)); });
    midiSession = std::make_unique<motion::MidiRecordingSession>(document, midiRecording, motion::MidiRecordingSession::Transport{
        [this] { return getEffectiveSampleRate(); }, [this] { return position.load(); },
        [this] { return midiDeviceReady.load() && !isSuspended() && !legalNoticePending.load() && !isPreparingComposition() && !preparationFailed.load(); },
        [this](motion::Id id) { setMidiAudition(id); }, [this](const auto& config) { return armMidiRecording(config); }
    });
    document.onChanged = [this] {
        // The loop range reaches the audio thread with every edit, editor or not.
        const auto& project = document.project();
        loopStart.store(project.loopStart);
        loopEnd.store(project.loopEnd);
        looping.store(project.looping && project.hasLoop());
        if (document.viewOnlyChange()) { return; }
        requestComposition(project);
    };
    document.onChanged();
}

MotionProcessor::~MotionProcessor() {
    // Host teardown can occur off the message thread; exclude the controller's
    // timer before destroying its document access and publication callback.
    const juce::MessageManagerLock messageLock;
    visualiserParameters.applyExternalModulation = nullptr;
    midiSession.reset();
    blender.reset();
    preparationWorker.reset();
    cancelPendingUpdate();
    document.onChanged = nullptr;
    getUndoManager().clearUndoHistory();
}

void MotionProcessor::requestComposition(const motion::Project& project) {
    preparationSampleRate = requestedSampleRate.load();
    preparationRevision = preparationWorker->request(project, preparationSampleRate);
}

void MotionProcessor::handleAsyncUpdate() {
    if (preparationSampleRate != requestedSampleRate.load()) {
        requestComposition(document.project());
        return;
    }
    // A result older than the latest request still plays until the newest
    // one is ready, so a drag updates playback as it goes.
    auto result = preparationWorker->take();
    if (result == nullptr || result->revision <= acceptedPreparationRevision) { return; }
    if (result->composition != nullptr && result->composition->sampleRate != preparationSampleRate) { return; }
    acceptedPreparationRevision = result->revision;
    preparationError = result->error;
    preparationFailed.store(preparationError.isNotEmpty());
    // A failed edit keeps playing the last good snapshot instead of blanking;
    // the editor reports the error. The audio thread only ever sees snapshots
    // that prepared completely.
    if (result->composition != nullptr && preparationError.isEmpty() && result->composition->preparationError.isEmpty()) {
        result->composition->publicationRevision = result->revision;
        composition.publish(std::move(result->composition));
    }
}

bool MotionProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto channels = layouts.getMainOutputChannelSet().size();
    return layouts.getMainInputChannelSet().isDisabled() && (channels == 2 || channels == 5);
}

bool MotionProcessor::armMidiRecording(const motion::MidiRecording::Config& config) {
    const juce::SpinLock::ScopedLockType lock(midiLifecycleLock);
    return midiDeviceReady.load() && midiRecording.arm(config);
}

void MotionProcessor::stopMidiDevice() {
    // Callbacks are excluded here. Holding the arm lock means a take is either
    // armed before this stop (and finished by it) or refused afterwards.
    const juce::SpinLock::ScopedLockType lock(midiLifecycleLock);
    midiDeviceReady.store(false);
    midiRecording.deviceStopped();
    recordingOwnsTransport = false;
}

void MotionProcessor::releaseResources() {
    stopMidiDevice();
    CommonAudioProcessor::releaseResources();
}

void MotionProcessor::prepareToPlayInternal(double sampleRate, int samplesPerBlock) {
    stopMidiDevice();
    signal.setSize(6, samplesPerBlock);
    audioSample = motion::sampleIndex(audioTime, sampleRate).value_or(0);
    oscillatorSample = audioSample;
    liveMidi.prepare(sampleRate);
    liveMidiSample = 0;
    {
        const juce::SpinLock::ScopedLockType lock(midiLifecycleLock);
        midiDeviceReady.store(true);
    }
    requestedSampleRate.store(sampleRate);
    triggerAsyncUpdate();
    transitionGuard.begin();
}

void MotionProcessor::releaseRecordingTransport() {
    const auto state = midiRecording.state();
    if (recordingOwnsTransport && state != motion::MidiRecording::State::armed && state != motion::MidiRecording::State::recording) {
        recordingOwnsTransport = false;
        playing.store(false);
    }
}

void MotionProcessor::processBlockInternal(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    lastCallbackMs.store(juce::Time::getMillisecondCounterHiRes(), std::memory_order_relaxed);
    buffer.clear();
    releaseRecordingTransport();
    const auto* prepared = composition.acquire();
    const auto* liveBlock = liveSources.acquire();
    const auto* liveFrames = liveBlock != nullptr ? liveBlock->frames.get() : nullptr;
    if (liveBlock != nullptr && liveBlock->revision != previousLiveRevision) {
        previousLiveRevision = liveBlock->revision;
        transitionGuard.begin();
    }
    const auto sampleRate = getEffectiveSampleRate();
    const auto count = buffer.getNumSamples();
    const auto unavailableRecording = [&] {
        midiRecording.beginBlock(sampleRate, static_cast<std::uint64_t>(std::max<juce::int64>(0, audioSample)), static_cast<std::uint32_t>(std::max(0, count)), playing.load(), false);
    };
    if (count > signal.getNumSamples()) { unavailableRecording(); midi.clear(); liveMidi.reset(); liveInputs.reset(); return; }
    // Shared output gain/clip buffers are allocated during prepareToPlay, but
    // must be populated each block before music monitoring or physical XY output.
    volumeEffect->animateValues(count, nullptr);
    thresholdEffect->animateValues(count, nullptr);
    if (prepared == nullptr || prepared->preparationError.isNotEmpty() || prepared->sampleRate != sampleRate || !std::isfinite(sampleRate) || sampleRate <= 0) {
        unavailableRecording();
        liveMidi.reset(); liveInputs.reset();
        midi.clear();
        transitionGuard.begin();
        for (int i = 0; i < count; ++i) {
            const auto point = transitionGuard.apply({0, 0, 0, 0, 0, 0});
            signal.setSample(0, i, point.x); signal.setSample(1, i, point.y); signal.setSample(2, i, point.z);
            signal.setSample(3, i, 0); signal.setSample(4, i, 0); signal.setSample(5, i, 0);
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
    const auto durationIndex = motion::sampleIndex(prepared->duration, sampleRate);
    if (!durationIndex.has_value() || *durationIndex < 1) {
        unavailableRecording();
        midi.clear();
        liveMidi.reset(); liveInputs.reset();
        return;
    }
    const auto durationSamples = *durationIndex;
    // Plans hold pointers into the composition and live frames; either change replans.
    const auto liveGeneration = liveBlock != nullptr ? liveBlock->revision : std::numeric_limits<std::uint64_t>::max();
    const auto beamGeneration = prepared->publicationRevision * 0x9E3779B97F4A7C15ull + liveGeneration;
    const auto auditionId = midiAuditionTarget.load();
    const motion::PreparedClip* audition = nullptr;
    if (auditionId != 0) {
        const auto found = std::find_if(prepared->clips.begin(), prepared->clips.end(), [auditionId](const auto& clip) { return clip.id == auditionId; });
        if (found != prepared->clips.end()) { audition = &*found; }
    }
    const auto resolvedAudition = audition != nullptr ? auditionId : 0;
    if (resolvedAudition != previousAuditionTarget) {
        liveMidi.reset(); liveInputs.reset();
        previousAuditionTarget = resolvedAudition;
        transitionGuard.begin();
    }
    if (audition != nullptr && audition->liveInstrument != nullptr && !liveMidi.usesInstrument(*audition->liveInstrument)) {
        liveMidi.useInstrument(*audition->liveInstrument);
        transitionGuard.begin();
    }
    // Armed tracks, in project order, each with its clip's instrument.
    const auto armed = std::min(prepared->liveTracks.size(), motion::LiveMidiInputs::maximumRoutes);
    for (std::size_t index = 0; index < armed; ++index) {
        const auto& track = prepared->liveTracks[index];
        auto& route = liveInputs.routes[index];
        if (route.track != track.track) {
            route.performance.reset();
            route.track = track.track;
        }
        route.channel = track.channel;
        if (track.instrument != nullptr && !route.performance.usesInstrument(*track.instrument)) { route.performance.useInstrument(*track.instrument); }
    }
    for (auto index = armed; index < liveInputs.count; ++index) {
        liveInputs.routes[index].performance.reset();
        liveInputs.routes[index].track = 0;
    }
    liveInputs.count = armed;
    const auto requested = requestedPosition.exchange(-1.0);
    if (requested >= 0.0 && std::isfinite(requested)) {
        audioSample = motion::sampleIndex(std::min(requested, prepared->duration), sampleRate).value_or(0);
        oscillatorSample = audioSample;
        liveMidi.reset(); liveInputs.reset();
        transitionGuard.begin();
    }
    const auto recordingStart = midiRecording.transportStart();
    if (recordingStart.has_value()) {
        audioSample = static_cast<juce::int64>(std::min<std::uint64_t>(*recordingStart, static_cast<std::uint64_t>(durationSamples - 1)));
        oscillatorSample = audioSample;
        liveMidi.reset(); liveInputs.reset();
        playing.store(true);
        recordingOwnsTransport = true;
        transitionGuard.begin();
    }
    const auto requestedPlay = playing.load();
    if (requestedPlay && audioSample >= durationSamples) {
        audioSample = 0;
    }
    // Capture unowned byte views before the input buffer is consumed. Only the
    // continuous span before a project wrap belongs to this pass.
    const auto recordSamples = static_cast<std::uint32_t>(std::min<juce::int64>(count, std::max<juce::int64>(0, durationSamples - audioSample)));
    if (midiRecording.beginBlock(sampleRate, static_cast<std::uint64_t>(audioSample), recordSamples, requestedPlay, true, requested >= 0)) {
        for (const auto metadata : midi) { midiRecording.event(metadata.samplePosition, metadata.data, metadata.numBytes); }
        midiRecording.endBlock();
    } else {
        // A Stop or Cancel observed by beginBlock ends recording-started playback
        // before this block produces a sample, even if it raced the start above.
        releaseRecordingTransport();
    }
    const auto running = playing.load() && requestedPlay;
    // A held note on an armed track draws even while the transport is stopped.
    const auto drawing = running || freezeWhenStopped.load() || liveInputs.sounding(liveMidiSample);
    if (running != wasPlaying || drawing != wasDrawing) { transitionGuard.begin(); }
    wasPlaying = running;
    wasDrawing = drawing;
    juce::int64 loopStartSample = 0, loopEndSample = 0;
    // A take records straight through: no loop while recording MIDI.
    const auto takeOpen = midiRecording.state() == motion::MidiRecording::State::armed || midiRecording.state() == motion::MidiRecording::State::recording;
    if (looping.load(std::memory_order_relaxed) && !takeOpen) {
        const auto first = motion::sampleIndex(std::clamp(loopStart.load(std::memory_order_relaxed), 0.0, prepared->duration), sampleRate);
        const auto last = motion::sampleIndex(std::clamp(loopEnd.load(std::memory_order_relaxed), 0.0, prepared->duration), sampleRate);
        if (first.has_value() && last.has_value() && *last > *first) { loopStartSample = static_cast<juce::int64>(*first); loopEndSample = static_cast<juce::int64>(*last); }
    }
    const auto mode = outputMode.load();
    const auto audible = !muteParameter->getBoolValue();
    const auto* volumes = volumeEffect->getAnimatedValuesReadPointer(0, count);
    const auto fallbackVolume = volumeEffect->getValue();
    motion::LiveMidiInputCursor events(midi);
    for (int i = 0; i < count; ++i) {
        if (events.dispatch(audition != nullptr ? &liveMidi : nullptr, i, liveMidiSample, &liveInputs)) { transitionGuard.begin(); }
        audioTime = static_cast<double>(audioSample) / sampleRate;
        if (running) { oscillatorSample = audioSample; }
        liveInputs.clockOffset = static_cast<std::int64_t>(liveMidiSample) - static_cast<std::int64_t>(oscillatorSample);
        auto point = audition != nullptr ? motion::sampleLiveMidiAudition(*prepared, *audition, liveMidi, audioTime, liveMidiSample, sampleRate, running, liveFrames)
            : drawing ? beam.sample(*prepared, audioTime, static_cast<std::int64_t>(oscillatorSample), sampleRate, running, beamGeneration, liveFrames, liveInputs.count > 0 ? &liveInputs : nullptr) : osci::Point(0, 0, 0, 0, 0, 0);
        point = transitionGuard.apply(point);
        if (mode == OutputMode::soundtrack && running && audible && buffer.getNumChannels() >= 2) {
            const auto audio = prepared->soundtrack.sample(audioTime);
            const auto volume = volumes != nullptr ? volumes[i] : fallbackVolume;
            const auto gain = std::isfinite(volume) ? static_cast<double>(volume) : 0.0;
            constexpr auto limit = static_cast<double>(std::numeric_limits<float>::max());
            buffer.setSample(0, i, static_cast<float>(std::clamp(audio.left * gain, -limit, limit)));
            buffer.setSample(1, i, static_cast<float>(std::clamp(audio.right * gain, -limit, limit)));
        }
        signal.setSample(0, i, point.x);
        signal.setSample(1, i, point.y);
        signal.setSample(2, i, point.z);
        signal.setSample(3, i, point.r);
        signal.setSample(4, i, point.g);
        signal.setSample(5, i, point.b);
        ++oscillatorSample;
        if (liveMidiSample == std::numeric_limits<std::uint64_t>::max()) {
            liveMidi.reset(); liveInputs.reset();
            liveMidiSample = 0;
            transitionGuard.begin();
        } else { ++liveMidiSample; }
        if (running) {
            ++audioSample;
            // Crossing the loop end jumps back to its start; starting after
            // the loop plays on to the end.
            if (loopEndSample > loopStartSample && audioSample == loopEndSample) {
                audioSample = loopStartSample;
                transitionGuard.begin();
            } else if (audioSample >= durationSamples) {
                audioSample = 0;
                transitionGuard.begin();
            }
        }
    }
    midi.clear();
    beamLayers.store(static_cast<int>(beam.plannedLayers()), std::memory_order_relaxed);
    beamInterleave.store(static_cast<int>(beam.plannedInterleave()), std::memory_order_relaxed);
    audioTime = static_cast<double>(audioSample) / sampleRate;
    position.store(audioTime);
    const auto picture = prepared->beam.at(audioTime);
    for (std::size_t index = 0; index < picture.size(); ++index) { scopeBeam[index].store(picture[index], std::memory_order_relaxed); }
    juce::AudioBuffer<float> block(signal.getArrayOfWritePointers(), 6, count);
    threadManager.write(block, "VisualiserRenderer");
    routeSignalOutput(buffer);
}

void MotionProcessor::routeSignalOutput(juce::AudioBuffer<float>& buffer) {
    const auto mode = outputMode.load();
    const auto audible = !muteParameter->getBoolValue();
    const auto count = buffer.getNumSamples();
    const auto requiredChannels = mode == OutputMode::xyrgb ? 5 : 2;
    if (audible && (mode == OutputMode::xy || mode == OutputMode::xyrgb) && buffer.getNumChannels() >= requiredChannels) {
        for (int channel = 0; channel < requiredChannels; ++channel) {
            const auto source = channel < 2 ? channel : channel + 1;
            buffer.copyFrom(channel, 0, signal, source, 0, count);
        }
        applyVolumeAndThreshold(buffer.getArrayOfWritePointers(), count);
    }
}

juce::AudioProcessorEditor* MotionProcessor::createEditor() {
    return new MotionEditor(*this);
}

void MotionProcessor::getStateInformation(juce::MemoryBlock& destination) {
    juce::XmlElement project("motion-project");
    project.setAttribute("schema", 1);
    saveStandaloneProjectFilePathToXml(project);
    saveProperties(project);
    VisualiserState::save(project, visualiserParameters, recordingParameters);
    project.addChildElement(new juce::XmlElement(document.save()));
    copyXmlToBinary(project, destination);
}

void MotionProcessor::setStateInformation(const void* data, int size) {
    auto project = getXmlFromBinary(data, size);
    if (project == nullptr || !project->hasTagName("motion-project") || project->getIntAttribute("schema") != 1) {
        return;
    }
    const auto* compositionXml = project->getChildByName("composition");
    if (compositionXml == nullptr) { return; }
    motion::Project prepared;
    const auto result = motion::Document::prepareLoad(*compositionXml, prepared);
    if (result.failed()) { return; }
    applyPreparedProject(std::move(prepared), *project);
}

void MotionProcessor::applyPreparedProject(motion::Project prepared, juce::XmlElement& state) {
    // Host state restore is not a realtime operation. Serialize its commit
    // with the editor and the live-source owner, after worker preparation.
    const juce::MessageManagerLock messageLock;
    document.reset(std::move(prepared));
    setMidiAudition(0);
    VisualiserState::load(state, visualiserParameters, recordingParameters);
    restoreStandaloneProjectFilePathFromXml(state);
    loadProperties(state);
    getUndoManager().clearUndoHistory();
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new MotionProcessor();
}

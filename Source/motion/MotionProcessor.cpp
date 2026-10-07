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
    blender = std::make_unique<motion::LiveBlenderController>(document, [this](auto frames) { publishLiveSources(std::move(frames)); });
    document.onChanged = [this] {
        // The loop range reaches the audio thread with every edit, editor or not.
        const auto& project = document.project();
        loopStart.store(project.loopStart);
        loopEnd.store(project.loopEnd);
        looping.store(project.looping && project.hasLoop());
        if (document.viewOnlyChange()) { return; }
        prepareComposition(project);
    };
    document.onChanged();
}

MotionProcessor::~MotionProcessor() {
    // Host teardown can occur off the message thread; exclude the controller's
    // timer before destroying its document access and publication callback.
    const juce::MessageManagerLock messageLock;
    visualiserParameters.applyExternalModulation = nullptr;
    blender.reset();
    preparationWorker.reset();
    cancelPendingUpdate();
    document.onChanged = nullptr;
    getUndoManager().clearUndoHistory();
}

void MotionProcessor::prepareComposition(const motion::Project& project) {
    preparationSampleRate = requestedSampleRate.load();
    preparationRevision = preparationWorker->request(project, preparationSampleRate);
}

void MotionProcessor::handleAsyncUpdate() {
    if (preparationSampleRate != requestedSampleRate.load()) {
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
        composition.publish(std::move(result->composition));
    }
}

bool MotionProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto channels = layouts.getMainOutputChannelSet().size();
    return layouts.getMainInputChannelSet().isDisabled() && (channels == 2 || channels == 5);
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
    const auto* prepared = composition.acquire();
    const auto* liveBlock = liveSources.acquire();
    const auto* liveFrames = liveBlock != nullptr ? liveBlock->frames.get() : nullptr;
    if (liveBlock != nullptr && liveBlock->revision != previousLiveRevision) {
        previousLiveRevision = liveBlock->revision;
        transitionGuard.begin();
    }
    const auto sampleRate = getEffectiveSampleRate();
    const auto count = buffer.getNumSamples();
    if (count > signal.getNumSamples()) { midi.clear(); return; }
    // Shared output gain/clip buffers are allocated during prepareToPlay, but
    // must be populated each block before music monitoring or physical XY output.
    volumeEffect->animateValues(count, nullptr);
    thresholdEffect->animateValues(count, nullptr);
    if (prepared == nullptr || prepared->preparationError.isNotEmpty() || prepared->sampleRate != sampleRate || !std::isfinite(sampleRate) || sampleRate <= 0) {
        midi.clear();
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
    const auto durationIndex = motion::sampleIndex(prepared->duration, sampleRate);
    if (!durationIndex.has_value() || *durationIndex < 1) {
        midi.clear();
        return;
    }
    const auto durationSamples = *durationIndex;
    // Plans hold pointers into the composition and live frames; either change replans.
    const auto liveGeneration = liveBlock != nullptr ? liveBlock->revision : std::numeric_limits<std::uint64_t>::max();
    const auto beamGeneration = prepared->publicationRevision * 0x9E3779B97F4A7C15ull + liveGeneration;
    const auto requested = requestedPosition.exchange(-1.0);
    if (requested >= 0.0 && std::isfinite(requested)) {
        audioSample = motion::sampleIndex(std::min(requested, prepared->duration), sampleRate).value_or(0);
        oscillatorSample = audioSample;
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
    const auto mode = outputMode.load();
    const auto audible = !muteParameter->getBoolValue();
    const auto* volumes = volumeEffect->getAnimatedValuesReadPointer(0, count);
    const auto fallbackVolume = volumeEffect->getValue();
    for (int i = 0; i < count; ++i) {
        audioTime = static_cast<double>(audioSample) / sampleRate;
        if (running) { oscillatorSample = audioSample; }
        const auto point = transitionGuard.apply(beam.sample(*prepared, audioTime, static_cast<std::int64_t>(oscillatorSample), sampleRate, running, beamGeneration, liveFrames));
        if (mode == OutputMode::soundtrack && running && audible && buffer.getNumChannels() >= 2) {
            const auto audio = prepared->soundtrack.sample(audioTime);
            const auto volume = volumes != nullptr ? volumes[i] : fallbackVolume;
            const auto gain = std::isfinite(volume) ? static_cast<double>(volume) : 0.0;
            constexpr auto limit = static_cast<double>(std::numeric_limits<float>::max());
            buffer.setSample(0, i, static_cast<float>(std::clamp(audio.left * gain, -limit, limit)));
            buffer.setSample(1, i, static_cast<float>(std::clamp(audio.right * gain, -limit, limit)));
        }
        writeSignal(i, point);
        ++oscillatorSample;
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
    VisualiserState::load(state, visualiserParameters, recordingParameters);
    restoreStandaloneProjectFilePathFromXml(state);
    loadProperties(state);
    getUndoManager().clearUndoHistory();
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new MotionProcessor();
}

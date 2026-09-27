#include "MotionProcessor.h"
#include "MotionEditor.h"
#include "render/SampleClock.h"
#include "../visualiser/VisualiserState.h"

MotionProcessor::MotionProcessor()
    : CommonAudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)) {
    addAllParameters();
    rgbEnabled = true;
    preparationWorker = std::make_unique<motion::CompositionPreparationWorker>([this] { triggerAsyncUpdate(); });
    document.onChanged = [this] { requestComposition(document.project()); };
    document.onChanged();
}

MotionProcessor::~MotionProcessor() {
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
    auto result = preparationWorker->take();
    if (result == nullptr || result->revision != preparationRevision) { return; }
    preparationError = result->error;
    preparationFailed.store(preparationError.isNotEmpty());
    if (result->composition != nullptr) {
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
    liveMidi.prepare(sampleRate);
    liveMidiSample = 0;
    requestedSampleRate.store(sampleRate);
    triggerAsyncUpdate();
    transitionGuard.begin();
}

void MotionProcessor::processBlockInternal(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    buffer.clear();
    const auto* prepared = composition.acquire();
    const auto sampleRate = getEffectiveSampleRate();
    const auto count = buffer.getNumSamples();
    if (count > signal.getNumSamples()) { midi.clear(); liveMidi.reset(); return; }
    if (prepared == nullptr || preparationFailed.load() || prepared->sampleRate != sampleRate || !std::isfinite(sampleRate) || sampleRate <= 0) {
        liveMidi.reset();
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
        midi.clear();
        liveMidi.reset();
        return;
    }
    const auto durationSamples = *durationIndex;
    const auto auditionId = midiAuditionTarget.load();
    const motion::PreparedClip* audition = nullptr;
    if (auditionId != 0) {
        const auto found = std::find_if(prepared->clips.begin(), prepared->clips.end(), [auditionId](const auto& clip) { return clip.id == auditionId; });
        if (found != prepared->clips.end()) { audition = &*found; }
    }
    const auto resolvedAudition = audition != nullptr ? auditionId : 0;
    if (resolvedAudition != previousAuditionTarget) {
        liveMidi.reset();
        previousAuditionTarget = resolvedAudition;
        transitionGuard.begin();
    }
    const auto requested = requestedPosition.exchange(-1.0);
    if (requested >= 0.0 && std::isfinite(requested)) {
        audioSample = motion::sampleIndex(std::min(requested, prepared->duration), sampleRate).value_or(0);
        oscillatorSample = audioSample;
        liveMidi.reset();
        transitionGuard.begin();
    }
    const auto running = playing.load();
    const auto drawing = running || freezeWhenStopped.load();
    if (running != wasPlaying || drawing != wasDrawing) { transitionGuard.begin(); }
    wasPlaying = running;
    wasDrawing = drawing;
    if (running && audioSample >= durationSamples) {
        audioSample = 0;
    }
    const auto mode = outputMode.load();
    const auto audible = !muteParameter->getBoolValue();
    const auto* volumes = volumeEffect->getAnimatedValuesReadPointer(0, count);
    const auto fallbackVolume = volumeEffect->getValue();
    motion::LiveMidiInputCursor events(midi);
    for (int i = 0; i < count; ++i) {
        if (events.dispatch(audition != nullptr ? &liveMidi : nullptr, i, liveMidiSample)) { transitionGuard.begin(); }
        audioTime = static_cast<double>(audioSample) / sampleRate;
        if (running) { oscillatorSample = audioSample; }
        auto point = audition != nullptr ? motion::sampleLiveMidiAudition(*prepared, *audition, liveMidi, audioTime, liveMidiSample, sampleRate, running)
            : drawing ? prepared->sampleAtClock(audioTime, oscillatorSample, sampleRate, running) : osci::Point(0, 0, 0, 0, 0, 0);
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
            liveMidi.reset();
            liveMidiSample = 0;
            transitionGuard.begin();
        } else { ++liveMidiSample; }
        if (running) {
            if (++audioSample >= durationSamples) {
                audioSample = 0;
                transitionGuard.begin();
            }
        }
    }
    midi.clear();
    audioTime = static_cast<double>(audioSample) / sampleRate;
    position.store(audioTime);
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

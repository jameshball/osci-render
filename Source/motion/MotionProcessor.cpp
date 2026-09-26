#include "MotionProcessor.h"
#include "MotionEditor.h"
#include "render/SampleClock.h"

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
    requestedSampleRate.store(sampleRate);
    triggerAsyncUpdate();
    transitionGuard.begin();
}

void MotionProcessor::processBlockInternal(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    buffer.clear();
    midi.clear();
    const auto* prepared = composition.acquire();
    const auto sampleRate = getEffectiveSampleRate();
    const auto count = buffer.getNumSamples();
    if (count > signal.getNumSamples()) { return; }
    if (prepared == nullptr || preparationFailed.load() || prepared->sampleRate != sampleRate || !std::isfinite(sampleRate) || sampleRate <= 0) {
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
        return;
    }
    const auto durationSamples = *durationIndex;
    const auto requested = requestedPosition.exchange(-1.0);
    if (requested >= 0.0 && std::isfinite(requested)) {
        audioSample = motion::sampleIndex(std::min(requested, prepared->duration), sampleRate).value_or(0);
        oscillatorSample = audioSample;
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
    for (int i = 0; i < count; ++i) {
        audioTime = static_cast<double>(audioSample) / sampleRate;
        if (running) { oscillatorSample = audioSample; }
        const auto oscillatorTime = static_cast<double>(oscillatorSample) / sampleRate;
        const auto phase = std::fmod(static_cast<double>(oscillatorSample) * 60.0 / sampleRate, 1.0);
        auto point = drawing ? prepared->sample(audioTime, phase, 60.0 / sampleRate, running ? 1.0 / sampleRate : 0.0, oscillatorTime) : osci::Point(0, 0, 0, 0, 0, 0);
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
        if (running) {
            if (++audioSample >= durationSamples) {
                audioSample = 0;
                transitionGuard.begin();
            }
        }
    }
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
    project.addChildElement(new juce::XmlElement(document.save()));
    copyXmlToBinary(project, destination);
}

void MotionProcessor::setStateInformation(const void* data, int size) {
    auto project = getXmlFromBinary(data, size);
    if (project == nullptr || !project->hasTagName("motion-project") || project->getIntAttribute("schema") != 1) {
        return;
    }
    const auto* compositionXml = project->getChildByName("composition");
    if (compositionXml != nullptr) {
        const auto result = document.load(*compositionXml);
        if (result.failed()) {
            return;
        }
    }
    restoreStandaloneProjectFilePathFromXml(*project);
    loadProperties(*project);
    getUndoManager().clearUndoHistory();
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new MotionProcessor();
}

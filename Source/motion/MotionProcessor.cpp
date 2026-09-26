#include "MotionProcessor.h"
#include "MotionEditor.h"
#include "render/SampleClock.h"

MotionProcessor::MotionProcessor()
    : CommonAudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)) {
    addAllParameters();
    rgbEnabled = true;
    document.onChanged = [this] {
        composition.publish(std::make_unique<motion::PreparedComposition>(document.project()));
    };
    document.onChanged();
}

MotionProcessor::~MotionProcessor() {
    document.onChanged = nullptr;
    getUndoManager().clearUndoHistory();
}

void MotionProcessor::prepareToPlayInternal(double sampleRate, int samplesPerBlock) {
    signal.setSize(6, samplesPerBlock);
    audioSample = motion::sampleIndex(audioTime, sampleRate).value_or(0);
}

void MotionProcessor::processBlockInternal(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    buffer.clear();
    midi.clear();
    const auto* prepared = composition.acquire();
    const auto requested = requestedPosition.exchange(-1.0);
    const auto sampleRate = getEffectiveSampleRate();
    const auto count = buffer.getNumSamples();
    if (prepared == nullptr || !std::isfinite(sampleRate) || sampleRate <= 0 || count > signal.getNumSamples()) {
        return;
    }
    const auto durationIndex = motion::sampleIndex(prepared->duration, sampleRate);
    if (!durationIndex.has_value() || *durationIndex < 1) {
        return;
    }
    const auto durationSamples = *durationIndex;
    if (requested >= 0.0 && std::isfinite(requested)) {
        audioSample = motion::sampleIndex(std::min(requested, prepared->duration), sampleRate).value_or(0);
    }
    const auto running = playing.load();
    const auto drawing = running || freezeWhenStopped.load();
    if (running && audioSample >= durationSamples) {
        audioSample = 0;
    }
    for (int i = 0; i < count; ++i) {
        audioTime = static_cast<double>(audioSample) / sampleRate;
        if (running) {
            phase = std::fmod(static_cast<double>(audioSample) * 60.0 / sampleRate, 1.0);
        }
        const auto point = drawing ? prepared->sample(audioTime, phase) : osci::Point(0, 0, 0, 0, 0, 0);
        signal.setSample(0, i, point.x);
        signal.setSample(1, i, point.y);
        signal.setSample(2, i, point.z);
        signal.setSample(3, i, point.r);
        signal.setSample(4, i, point.g);
        signal.setSample(5, i, point.b);
        phase += 60.0 / sampleRate;
        phase -= std::floor(phase);
        if (running) {
            if (++audioSample >= durationSamples) {
                audioSample = 0;
            }
        }
    }
    audioTime = static_cast<double>(audioSample) / sampleRate;
    position.store(audioTime);
    juce::AudioBuffer<float> block(signal.getArrayOfWritePointers(), 6, count);
    threadManager.write(block, "VisualiserRenderer");
    if (!muteParameter->getBoolValue()) {
        for (int channel = 0; channel < std::min(5, buffer.getNumChannels()); ++channel) {
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

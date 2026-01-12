#include "WavParser.h"
#include "../CommonPluginProcessor.h"


WavParser::WavParser(CommonAudioProcessor& p) : audioProcessor(p) {}

WavParser::~WavParser() {}

bool WavParser::parse(std::unique_ptr<juce::InputStream> stream) {
    initialised = false;
    if (stream == nullptr) {
        return false;
    }
    currentSample = 0;
    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();
    juce::AudioFormatReader* reader = formatManager.createReaderFor(std::move(stream));
    if (reader == nullptr) {
        return false;
    }
    afSource = std::make_unique<juce::AudioFormatReaderSource>(reader, true);
    totalSamples = afSource->getTotalLength();
    afSource->setLooping(looping);
    // afSource is owned by this class (unique_ptr), so ResamplingAudioSource must NOT delete it.
    source = std::make_unique<juce::ResamplingAudioSource>(afSource.get(), false, reader->numChannels);
    if (source == nullptr) {
        return false;
    }
    fileSampleRate = reader->sampleRate;
    numChannels = (int) reader->numChannels;
    audioBuffer.setSize(reader->numChannels, 1);
    // Default behaviour: follow the processor sample rate for realtime playback.
    // If the processor sample rate isn't known yet, fall back to the file sample rate.
    // For offline rendering, callers can disable following before parse() and optionally
    // set a fixed target sample rate.
    const double processorRate = (double) audioProcessor.currentSampleRate.load();
    const double defaultTarget = processorRate > 0.0 ? processorRate : fileSampleRate;

    const bool shouldFollow = followProcessorSampleRate.load();
    const double existingTarget = targetSampleRate.load();
    const double initialTarget = shouldFollow
        ? defaultTarget
        : (existingTarget > 0.0 ? existingTarget : fileSampleRate);

    targetSampleRate.store(initialTarget);
    setSampleRate(initialTarget);
    initialised = true;

    return true;
}

void WavParser::setFollowProcessorSampleRate(bool shouldFollow) {
    followProcessorSampleRate.store(shouldFollow);
}

void WavParser::setTargetSampleRate(double sampleRate) {
    if (sampleRate <= 0.0) {
        return;
    }

    targetSampleRate.store(sampleRate);
    if (initialised && source != nullptr && currentSampleRate != sampleRate) {
        setSampleRate(sampleRate);
    }
}

double WavParser::getFileSampleRate() const {
    return fileSampleRate;
}

int WavParser::getNumChannels() const {
    return numChannels;
}

void WavParser::close() {
    if (initialised) {
        initialised = false;
        source.reset();
        afSource.reset();
    }
}

bool WavParser::isInitialised() {
    return initialised;
}

void WavParser::setSampleRate(double sampleRate) {
    double ratio = fileSampleRate / sampleRate;
    source->setResamplingRatio(ratio);
    source->prepareToPlay(1, sampleRate);
    currentSampleRate = sampleRate;
}

void WavParser::processBlock(juce::AudioBuffer<float> &buffer) {
    if (!initialised || paused) {
        buffer.clear();
        return;
    }

    if (followProcessorSampleRate.load()) {
        const int desired = (int) audioProcessor.currentSampleRate.load();
        if (desired > 0 && currentSampleRate != desired) {
            setSampleRate(desired);
        }
    } else {
        const int desired = (int) targetSampleRate.load();
        if (desired > 0 && currentSampleRate != desired) {
            setSampleRate(desired);
        }
    }

    if (looping != afSource->isLooping()) {
        afSource->setLooping(looping);
    }

    currentSample += source->getResamplingRatio() * buffer.getNumSamples();
    if (currentSample >= totalSamples && afSource->isLooping()) {
        currentSample = 0;
        afSource->setNextReadPosition(0);
    }

    source->getNextAudioBlock(juce::AudioSourceChannelInfo(buffer));
}

void WavParser::setProgress(double progress) {
    if (initialised) {
        afSource->setNextReadPosition(progress * totalSamples);
        currentSample = progress * totalSamples;
    }
}

void WavParser::setLooping(bool looping) {
    this->looping = looping;
}

bool WavParser::isLooping() {
    return looping;
}

void WavParser::setPaused(bool paused) {
    this->paused = paused;
}

bool WavParser::isPaused() {
    return paused;
}

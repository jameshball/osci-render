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
    afSource = new juce::AudioFormatReaderSource(reader, true);
    if (afSource == nullptr) {
        return false;
    }
    totalSamples = afSource->getTotalLength();
    afSource->setLooping(looping);
    source = std::make_unique<juce::ResamplingAudioSource>(afSource, true, reader->numChannels);
    if (source == nullptr) {
        return false;
    }
    fileSampleRate = reader->sampleRate;
    audioBuffer.setSize(reader->numChannels, 1);
    setSampleRate(audioProcessor.currentSampleRate);
    initialised = true;

    return true;
}

void WavParser::close() {
    if (initialised) {
        initialised = false;
        source.reset();
        afSource = nullptr;
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

    if (currentSampleRate != audioProcessor.currentSampleRate) {
        setSampleRate(audioProcessor.currentSampleRate);
    }

    if (looping != afSource->isLooping()) {
        afSource->setLooping(looping);
    }

    source->getNextAudioBlock(juce::AudioSourceChannelInfo(buffer));
    currentSample += source->getResamplingRatio() * buffer.getNumSamples();
    if (currentSample >= totalSamples && afSource->isLooping()) {
        currentSample = 0;
        afSource->setNextReadPosition(0);
    }
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

#include "WavParser.h"
#include "../CommonPluginProcessor.h"


WavParser::WavParser(CommonAudioProcessor& p, std::unique_ptr<juce::InputStream> stream) : audioProcessor(p) {
    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();
    juce::AudioFormatReader* reader = formatManager.createReaderFor(std::move(stream));
    if (reader == nullptr) {
        return;
    }
    afSource = new juce::AudioFormatReaderSource(reader, true);
    totalSamples = afSource->getTotalLength();
    afSource->setLooping(true);
    source = std::make_unique<juce::ResamplingAudioSource>(afSource, true);
    fileSampleRate = reader->sampleRate;
    audioBuffer.setSize(reader->numChannels, 1);
    setSampleRate(audioProcessor.currentSampleRate);
    source->prepareToPlay(1, audioProcessor.currentSampleRate);
}

WavParser::~WavParser() {
}

void WavParser::setSampleRate(double sampleRate) {
    double ratio = fileSampleRate / sampleRate;
    source->setResamplingRatio(ratio);
    source->prepareToPlay(1, sampleRate);
    currentSampleRate = sampleRate;
}

OsciPoint WavParser::getSample() {
    if (currentSampleRate != audioProcessor.currentSampleRate) {
        setSampleRate(audioProcessor.currentSampleRate);
    }
    if (source == nullptr || paused) {
        return OsciPoint();
    }

    source->getNextAudioBlock(juce::AudioSourceChannelInfo(audioBuffer));
    currentSample += source->getResamplingRatio();
    counter++;
    if (currentSample >= totalSamples && afSource->isLooping()) {
        currentSample = 0;
        counter = 0;
        afSource->setNextReadPosition(0);
    }
    if (counter % currentSampleRate == 0) {
        if (onProgress != nullptr) {
            onProgress((double)currentSample / totalSamples);
        }
    }
    
    if (audioBuffer.getNumChannels() == 1) {
        return OsciPoint(audioBuffer.getSample(0, 0), audioBuffer.getSample(0, 0));
    } else {
        return OsciPoint(audioBuffer.getSample(0, 0), audioBuffer.getSample(1, 0));
    }
}

void WavParser::setProgress(double progress) {
    if (source == nullptr) {
        return;
    }
    afSource->setNextReadPosition(progress * totalSamples);
    currentSample = progress * totalSamples;
}

void WavParser::setLooping(bool looping) {
    afSource->setLooping(looping);
    this->looping = looping;
}

bool WavParser::isLooping() {
    return looping;
}

void WavParser::setPaused(bool paused) {
    this->paused = paused;
    counter = 0;
}

bool WavParser::isPaused() {
    return paused;
}

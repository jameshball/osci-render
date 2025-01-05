#include "WavParser.h"
#include "../CommonPluginProcessor.h"


WavParser::WavParser(CommonAudioProcessor& p, std::unique_ptr<juce::InputStream> stream) : audioProcessor(p) {
    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();
    juce::AudioFormatReader* reader = formatManager.createReaderFor(std::move(stream));
    auto* afSource = new juce::AudioFormatReaderSource(reader, true);
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
    if (source == nullptr) {
        return OsciPoint();
    }

    source->getNextAudioBlock(juce::AudioSourceChannelInfo(audioBuffer));
    
    if (audioBuffer.getNumChannels() == 1) {
        return OsciPoint(audioBuffer.getSample(0, 0), audioBuffer.getSample(0, 0));
    } else {
        return OsciPoint(audioBuffer.getSample(0, 0), audioBuffer.getSample(1, 0));
    }
}

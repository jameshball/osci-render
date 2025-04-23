#include "StereoEffect.h"

StereoEffect::StereoEffect() {}

StereoEffect::~StereoEffect() {}

osci::Point StereoEffect::apply(int index, osci::Point input, const std::vector<std::atomic<double>>& values, double sampleRate) {
    if (this->sampleRate != sampleRate) {
        this->sampleRate = sampleRate;
        initialiseBuffer(sampleRate);
    }
    double sampleOffset = values[0].load() / 10;
    sampleOffset = juce::jlimit(0.0, 1.0, sampleOffset);
    sampleOffset *= buffer.size();

    head++;
    if (head >= buffer.size()) {
        head = 0;
    }
    
    buffer[head] = input;

    int readHead = head - sampleOffset;
    if (readHead < 0) {
        readHead += buffer.size();
    }
    
    return osci::Point(input.x, buffer[readHead].y, input.z);
}

void StereoEffect::initialiseBuffer(double sampleRate) {
    buffer.clear();
    buffer.resize(bufferLength * sampleRate);
    head = 0;
}

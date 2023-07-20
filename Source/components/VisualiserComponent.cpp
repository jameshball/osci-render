#include "VisualiserComponent.h"

VisualiserComponent::VisualiserComponent(int numChannels, OscirenderAudioProcessor& p) : numChannels(numChannels), backgroundColour(juce::Colours::black), waveformColour(juce::Colour(0xff00ff00)), audioProcessor(p), juce::Thread("VisualiserComponent") {
    setOpaque(true);
    startTimerHz(60);
    startThread();
}

VisualiserComponent::~VisualiserComponent() {
    audioProcessor.audioProducer.unregisterConsumer(consumer);
    stopThread(1000);
}

void VisualiserComponent::setBuffer(std::vector<float>& newBuffer) {
    juce::SpinLock::ScopedLockType scope(lock);
    buffer.clear();
    for (int i = 0; i < newBuffer.size(); i += precision * numChannels) {
        buffer.push_back(newBuffer[i]);
        buffer.push_back(newBuffer[i + 1]);
    }
}

void VisualiserComponent::setColours(juce::Colour bk, juce::Colour fg) {
    backgroundColour = bk;
    waveformColour = fg;
}

void VisualiserComponent::paint(juce::Graphics& g) {
    g.fillAll(backgroundColour);

    auto r = getLocalBounds().toFloat();
    auto minDim = juce::jmin(r.getWidth(), r.getHeight());

    juce::SpinLock::ScopedLockType scope(lock);
    if (buffer.size() > 0) {
        g.setColour(waveformColour);
        paintXY(g, r.withSizeKeepingCentre(minDim, minDim));
    }
}

void VisualiserComponent::timerCallback() {
    repaint();
}

void VisualiserComponent::run() {
    audioProcessor.audioProducer.registerConsumer(consumer);

    while (!threadShouldExit()) {
        auto buffer = consumer->startProcessing();
        setBuffer(*buffer);
        consumer->finishedProcessing();
    }
}

void VisualiserComponent::paintChannel(juce::Graphics& g, juce::Rectangle<float> area, int channel) {
    juce::Path path;

    for (int i = 0; i < buffer.size(); i += numChannels) {
        auto sample = buffer[i + channel];

        if (i == 0) {
            path.startNewSubPath(0.0f, sample);
        } else {
            path.lineTo((float)i, sample);
        }
    }

    // apply affine transform to path to fit in area
    auto transform = juce::AffineTransform::fromTargetPoints(0.0f, -1.0f, area.getX(), area.getY(), 0.0f, 1.0f, area.getX(), area.getBottom(), buffer.size(), -1.0f, area.getRight(), area.getY());
    path.applyTransform(transform);

    g.strokePath(path, juce::PathStrokeType(1.0f));
}

void VisualiserComponent::paintXY(juce::Graphics& g, juce::Rectangle<float> area) {
    auto transform = juce::AffineTransform::fromTargetPoints(-1.0f, -1.0f, area.getX(), area.getBottom(), 1.0f, 1.0f, area.getRight(), area.getY(), 1.0f, -1.0f, area.getRight(), area.getBottom());
    std::vector<juce::Line<float>> lines;

    for (int i = 2; i < buffer.size(); i += 2) {
        lines.emplace_back(buffer[i - 2], buffer[i - 1], buffer[i], buffer[i + 1]);
    }

    for (auto& line : lines) {
        line.applyTransform(transform);
        float lengthScale = 1.0f / (line.getLength() + 1.0f);
        double strength = 10;
        lengthScale = std::log(strength * lengthScale + 1) / std::log(strength + 1);
        g.setColour(waveformColour.withAlpha(lengthScale));
        g.drawLine(line, 2.0f);
    }
}

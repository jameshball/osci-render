#pragma once

#include <JuceHeader.h>
#include "AudioBackgroundThread.h"
#include "AudioBackgroundThreadManager.h"

AudioBackgroundThread::AudioBackgroundThread(const juce::String& name, AudioBackgroundThreadManager& manager) : juce::Thread(name), manager(manager) {
    manager.registerThread(this);
}

AudioBackgroundThread::~AudioBackgroundThread() {
    setShouldBeRunning(false);
    manager.unregisterThread(this);
}

void AudioBackgroundThread::prepare(double sampleRate, int samplesPerBlock) {
    if (isThreadRunning()) {
        stop();
    }
    
    isPrepared = false;
    int requestedDataSize = prepareTask(sampleRate, samplesPerBlock);
    consumer = std::make_unique<BufferConsumer>(requestedDataSize);
    isPrepared = true;
    
    if (shouldBeRunning) {
        start();
    }
}

void AudioBackgroundThread::setShouldBeRunning(bool shouldBeRunning) {
    if (!isPrepared && shouldBeRunning) {
        prepare(manager.sampleRate, manager.samplesPerBlock);
    }
    
    this->shouldBeRunning = shouldBeRunning;
    
    if (!shouldBeRunning && isThreadRunning()) {
        stop();
    } else if (isPrepared && shouldBeRunning && !isThreadRunning()) {
        start();
    }
}

void AudioBackgroundThread::write(const OsciPoint& point) {
    if (isPrepared && isThreadRunning()) {
        consumer->write(point);
    }
}

void AudioBackgroundThread::run() {
    while (!threadShouldExit() && shouldBeRunning) {
        consumer->waitUntilFull();
        if (shouldBeRunning) {
            runTask(consumer->getBuffer());
        }
    }
}

void AudioBackgroundThread::start() {
    startThread();
}

void AudioBackgroundThread::stop() {
    consumer->forceNotify();
    stopThread(1000);
}

#pragma once

#include <JuceHeader.h>

class AudioBackgroundThread;
class AudioBackgroundThreadManager {
public:
    AudioBackgroundThreadManager() {}
    ~AudioBackgroundThreadManager() {}
    
    void registerThread(AudioBackgroundThread* thread);
    void unregisterThread(AudioBackgroundThread* thread);
    void write(const osci::Point& point);
    void write(const osci::Point& point, juce::String name);
    void prepare(double sampleRate, int samplesPerBlock);
    
    double sampleRate = 44100.0;
    int samplesPerBlock = 128;

private:
    juce::SpinLock lock;
    std::vector<AudioBackgroundThread*> threads;
};

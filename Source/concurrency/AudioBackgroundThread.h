#pragma once

#include <JuceHeader.h>
#include "../shape/OsciPoint.h"
#include "BufferConsumer.h"

class AudioBackgroundThreadManager;
class AudioBackgroundThread : private juce::Thread {
public:
    AudioBackgroundThread(const juce::String& name, AudioBackgroundThreadManager& manager);
    ~AudioBackgroundThread() override;
    
    void prepare(double sampleRate, int samplesPerBlock);
    void setShouldBeRunning(bool shouldBeRunning, std::function<void()> stopCallback = nullptr);
    void write(const OsciPoint& point);
    void setBlockOnAudioThread(bool block);
    
private:
    
    void run() override;
    void start();
    void stop();
    
    AudioBackgroundThreadManager& manager;
    std::unique_ptr<BufferConsumer> consumer = nullptr;
    bool shouldBeRunning = false;
    std::atomic<bool> isPrepared = false;

protected:
    
    virtual int prepareTask(double sampleRate, int samplesPerBlock) = 0;
    virtual void runTask(const std::vector<OsciPoint>& points) = 0;
    virtual void stopTask() = 0;
};

#include "AudioTimelineController.h"

AudioTimelineController::AudioTimelineController(CommonAudioProcessor& processor)
    : audioProcessor(processor)
{
}

void AudioTimelineController::onValueChange(double value)
{
    juce::SpinLock::ScopedLockType sl(audioProcessor.wavParserLock);
    audioProcessor.wavParser.setProgress(value);
}

void AudioTimelineController::onPlay()
{
    audioProcessor.wavParser.setPaused(false);
}

void AudioTimelineController::onPause()
{
    audioProcessor.wavParser.setPaused(true);
}

void AudioTimelineController::onStop()
{
    audioProcessor.stopAudioFile();
    if (onStopCallback) {
        onStopCallback();
    }
}

void AudioTimelineController::onRepeatChanged(bool shouldRepeat)
{
    audioProcessor.wavParser.setLooping(shouldRepeat);
}

bool AudioTimelineController::isActive()
{
    return audioProcessor.wavParser.isInitialised();
}

double AudioTimelineController::getCurrentPosition()
{
    if (!isActive()) {
        return 0.0;
    }
    juce::SpinLock::ScopedLockType sl(audioProcessor.wavParserLock);
    return (double)audioProcessor.wavParser.currentSample / audioProcessor.wavParser.totalSamples;
}

void AudioTimelineController::setup(
    std::function<void(double)> setValueCallback,
    std::function<void(bool)> setPlayingCallback,
    std::function<void(bool)> setRepeatCallback)
{
    // Store the stop callback to reset timeline UI when audio is stopped
    onStopCallback = [setValueCallback]() {
        setValueCallback(0.0);
    };
    
    setRepeatCallback(audioProcessor.wavParser.isLooping());
    
    if (audioProcessor.wavParser.isInitialised()) {
        setPlayingCallback(!audioProcessor.wavParser.isPaused());
    } else {
        setPlayingCallback(false);
    }
}

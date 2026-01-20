#pragma once

#include <JuceHeader.h>
#include "TimelineController.h"
#include "../CommonPluginProcessor.h"

/**
 * Timeline controller for audio file playback.
 * Controls the WAV parser for playing, pausing, and scrubbing through audio files.
 */
class AudioTimelineController : public TimelineController {
public:
    AudioTimelineController(CommonAudioProcessor& processor);
    ~AudioTimelineController() override = default;

    void onValueChange(double value) override;
    void onPlay() override;
    void onPause() override;
    void onStop() override;
    void onRepeatChanged(bool shouldRepeat) override;
    bool isActive() override;
    double getCurrentPosition() override;
    void setup(
        std::function<void(double)> setValueCallback,
        std::function<void(bool)> setPlayingCallback,
        std::function<void(bool)> setRepeatCallback) override;

private:
    CommonAudioProcessor& audioProcessor;
    std::function<void()> onStopCallback;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioTimelineController)
};

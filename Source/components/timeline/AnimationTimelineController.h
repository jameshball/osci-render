#pragma once

#include <JuceHeader.h>
#include "TimelineController.h"

// Forward declaration to avoid circular dependency
class OscirenderAudioProcessor;

/**
 * Timeline controller for animation frame control.
 * Controls playback of GPLA files, GIFs, and video files by managing frame position.
 */
class AnimationTimelineController : public TimelineController {
public:
    AnimationTimelineController(OscirenderAudioProcessor& processor);
    ~AnimationTimelineController() override = default;

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
    
    void update();

private:
    OscirenderAudioProcessor& audioProcessor;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnimationTimelineController)
};

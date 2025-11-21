#pragma once

#include <JuceHeader.h>
#include "TimelineController.h"
#include "../PluginProcessor.h"

/**
 * Timeline controller for audio file playback in osci-render.
 * Controls the WavParser inside a FileParser (not the processor's wavParser).
 */
class OscirenderAudioTimelineController : public TimelineController {
public:
    OscirenderAudioTimelineController(OscirenderAudioProcessor& processor);
    ~OscirenderAudioTimelineController() override = default;

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
    OscirenderAudioProcessor& audioProcessor;
    
    std::shared_ptr<WavParser> getWavParser();
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OscirenderAudioTimelineController)
};

#pragma once

#include <JuceHeader.h>

/**
 * Interface for controlling timeline behavior.
 * Different implementations can provide specific behavior for audio playback,
 * animation frame control, video playback, etc.
 */
class TimelineController {
public:
    virtual ~TimelineController() = default;
    
    /**
     * Called when the timeline slider value changes.
     * @param value Normalized value between 0.0 and 1.0
     */
    virtual void onValueChange(double value) = 0;
    
    /**
     * Called when the play button is pressed.
     */
    virtual void onPlay() = 0;
    
    /**
     * Called when the pause button is pressed.
     */
    virtual void onPause() = 0;
    
    /**
     * Called when the stop button is pressed.
     */
    virtual void onStop() = 0;
    
    /**
     * Called when the repeat/loop button state changes.
     * @param shouldRepeat True if looping should be enabled
     */
    virtual void onRepeatChanged(bool shouldRepeat) = 0;
    
    /**
     * Returns whether the controller is currently active.
     * Used to determine if play/pause buttons should be shown.
     * @return True if the controller is active and can control playback
     */
    virtual bool isActive() = 0;
    
    /**
     * Called periodically by a timer to update the timeline position.
     * Should return the current normalized position (0.0 to 1.0).
     * @return Current position as a value between 0.0 and 1.0
     */
    virtual double getCurrentPosition() = 0;
    
    /**
     * Called when the controller is first set up.
     * Should initialize the timeline state (position, playing state, repeat state).
     * @param setValueCallback Callback to set the timeline slider value
     * @param setPlayingCallback Callback to set the playing state
     * @param setRepeatCallback Callback to set the repeat state
     */
    virtual void setup(
        std::function<void(double)> setValueCallback,
        std::function<void(bool)> setPlayingCallback,
        std::function<void(bool)> setRepeatCallback) = 0;
};

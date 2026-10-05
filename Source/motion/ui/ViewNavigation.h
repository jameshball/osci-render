#pragma once

#include <JuceHeader.h>
#include <cmath>

namespace motion::ui {
// One reading of a wheel or trackpad gesture for every time-based view:
// Cmd or Ctrl zooms time around the pointer, Alt scales the other axis
// (rows or values), and otherwise it pans, Shift turning vertical into
// horizontal.
struct WheelGesture {
    enum class Kind { zoomTime, scaleOther, pan };
    Kind kind = Kind::pan;
    double factor = 1; // zoomTime and scaleOther: greater than 1 grows
    float dx = 0, dy = 0; // pan

    WheelGesture(const juce::ModifierKeys& mods, const juce::MouseWheelDetails& wheel) {
        if (mods.isCommandDown() || mods.isCtrlDown()) {
            kind = Kind::zoomTime;
            factor = std::exp((std::abs(wheel.deltaY) > std::abs(wheel.deltaX) ? wheel.deltaY : wheel.deltaX) * 2.5);
        } else if (mods.isAltDown()) {
            kind = Kind::scaleOther;
            factor = std::exp(wheel.deltaY * 2);
        } else {
            dx = mods.isShiftDown() ? wheel.deltaY + wheel.deltaX : wheel.deltaX;
            dy = mods.isShiftDown() ? 0.0f : wheel.deltaY;
        }
    }
    // Screen pixels one unit of wheel movement pans.
    static constexpr double pixelsPerUnit = 256;
};

// Keeps a playing playhead in view without fighting the user: scrolling or
// zooming pauses following until the playhead is back in view.
struct PlayheadFollow {
    // True when the view should move to show the playhead.
    bool update(bool playing, bool visible) {
        if (!playing) {
            following = paused = false;
            return false;
        }
        if (!following) {
            following = true;
            paused = false;
        }
        if (paused) {
            paused = !visible;
            return false;
        }
        return !visible;
    }
    void userMoved() {
        if (following) { paused = true; }
    }
    bool following = false, paused = false;
};
}

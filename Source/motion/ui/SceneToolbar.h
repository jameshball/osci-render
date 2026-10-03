#pragma once

#include <JuceHeader.h>
#include "MotionIcons.h"

// Blender-style tools inside the Scene: a vertical strip with the transform
// tools, the motion path, fly navigation and framing.
class MotionSceneToolbar final : public motion::icons::ToolStrip {
public:
    using Tool = motion::icons::Button;

    MotionSceneToolbar() {
        setName("Scene tools");
        for (auto* tool : {&move, &rotate, &scale}) { tool->setRadioGroupId(91); tool->setClickingTogglesState(true); }
        path.setClickingTogglesState(true);
        move.setTooltip("Move (G)");
        rotate.setTooltip("Rotate (R)");
        scale.setTooltip("Scale (S)");
        path.setTooltip("Motion path (P)");
        fly.setTooltip("Fly (N): mouse to look, WASD to move, Esc to finish");
        frame.setTooltip("Frame selection (F)");
        lookThrough.setClickingTogglesState(true);
        lookThrough.setTooltip("Look through this camera: moving the view moves the camera");
        lookThrough.setVisible(false);
        setGroups({{&move, &rotate, &scale}, {&path, &fly, &frame}, {&lookThrough}});
    }
    Tool move {"Move tool", motion::icons::Icon::move}, rotate {"Rotate tool", motion::icons::Icon::rotate}, scale {"Scale tool", motion::icons::Icon::scale};
    Tool lookThrough {"Look through camera", motion::icons::Icon::videocam};
    Tool path {"Show motion path", motion::icons::Icon::path}, fly {"Navigate composition view", motion::icons::Icon::fly}, frame {"Frame composition selection", motion::icons::Icon::frame};
    // A short Scene keeps only the transform tools.
    void setCompact(bool value) {
        if (compact == value) { return; }
        compact = value;
        for (auto* tool : {&path, &fly, &frame}) { tool->setVisible(!compact); }
        resized();
    }
    int fullHeight() const { return cell * (lookThrough.isVisible() ? 7 : 6) + groupGap * (lookThrough.isVisible() ? 2 : 1) + inset * 2; }
    bool compact = false;
};

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
        setGroups({{&move, &rotate, &scale}, {&path, &fly, &frame}});
    }
    Tool move {"Move tool", motion::icons::Icon::move}, rotate {"Rotate tool", motion::icons::Icon::rotate}, scale {"Scale tool", motion::icons::Icon::scale};
    Tool path {"Show motion path", motion::icons::Icon::path}, fly {"Navigate composition view", motion::icons::Icon::fly}, frame {"Frame composition selection", motion::icons::Icon::frame};
    // A short Scene keeps only the transform tools.
    void setCompact(bool value) {
        if (compact == value) { return; }
        compact = value;
        for (auto* tool : {&path, &fly, &frame}) { tool->setVisible(!compact); }
        resized();
    }
    int fullHeight() const { return cell * 6 + groupGap + inset * 2; }
    bool compact = false;
};

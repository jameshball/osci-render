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
        for (auto* tool : {&move, &rotate, &scale, &anchor, &parts}) { tool->setRadioGroupId(91); tool->setClickingTogglesState(true); }
        path.setClickingTogglesState(true);
        move.setTooltip("Move (G)");
        rotate.setTooltip("Rotate (R)");
        scale.setTooltip("Scale (S)");
        anchor.setTooltip("Anchor (Y): drag the point the object rotates and scales about");
        parts.setTooltip("Pick parts (Tab)");
        path.setTooltip("Motion path (P)");
        fly.setTooltip("Fly (N): mouse to look, WASD to move, Esc to finish");
        frame.setTooltip("Frame selection (F)");
        lookThrough.setClickingTogglesState(true);
        lookThrough.setTooltip("Look through the selected camera: moving the view moves the camera");
        keyCamera.setTooltip("Key the camera at the playhead: position, rotation and lens");
        setGroups({{&move, &rotate, &scale, &anchor, &parts}, {&path, &fly, &frame}, {&lookThrough, &keyCamera}});
        lookThrough.setEnabled(false);
        keyCamera.setEnabled(false);
    }
    Tool move {"Move tool", motion::icons::Icon::move}, rotate {"Rotate tool", motion::icons::Icon::rotate}, scale {"Scale tool", motion::icons::Icon::scale}, anchor {"Anchor tool", motion::icons::Icon::anchor}, parts {"Pick parts", motion::icons::Icon::parts};
    Tool lookThrough {"Look through camera", motion::icons::Icon::videocam}, keyCamera {"Key camera", motion::icons::Icon::keyframe};
    Tool path {"Show motion path", motion::icons::Icon::path}, fly {"Navigate composition view", motion::icons::Icon::fly}, frame {"Frame composition selection", motion::icons::Icon::frame};
    // A short Scene keeps only the transform tools.
    void setCompact(bool value) {
        if (compact == value) { return; }
        compact = value;
        for (auto* tool : {&path, &fly, &frame}) { tool->setVisible(!compact); }
        resized();
    }
    int fullHeight() const { return cell * 10 + groupGap * 2 + inset * 2; }
    bool compact = false;
};

// How parts are picked, beside the parts tool while it is on: a box, a
// lasso, or whole separate pieces.
class MotionPartPickToolbar final : public motion::icons::ToolStrip {
public:
    using Tool = motion::icons::Button;

    MotionPartPickToolbar() {
        setName("Part picking");
        horizontal = true;
        for (auto* tool : {&box, &lasso, &connected}) { tool->setRadioGroupId(92); tool->setClickingTogglesState(true); }
        box.setTooltip("Box");
        lasso.setTooltip("Lasso");
        connected.setTooltip("Whole pieces, such as a letter");
        box.setToggleState(true, juce::dontSendNotification);
        setGroups({{&box, &lasso, &connected}});
    }
    Tool box {"Box pick", motion::icons::Icon::box}, lasso {"Lasso pick", motion::icons::Icon::lasso}, connected {"Pick pieces", motion::icons::Icon::connected};
};

// The Scope's controls in the same floating strip, at its top right:
// recording and texture output, framing and beam settings, then the
// popout and full screen.
class MotionScopeToolbar final : public motion::icons::ToolStrip {
public:
    using Tool = motion::icons::Button;

    MotionScopeToolbar() {
        setName("Scope tools");
        record.setTooltip("Record the Scope's picture and sound");
        record.tint = motion::style::record();
        record.onColour = motion::style::record().withAlpha(.25f);
        textureOutput.setTooltip("Share the Scope's picture with other apps (Syphon/Spout)");
        canvas.setTooltip("Output canvas size");
        settings.setTooltip("Scope properties: beam, display and timing");
        popout.setTooltip("Open the Scope in its own window");
        fullScreen.setTooltip("Full screen");
        setGroups({{&record, &textureOutput}, {&canvas, &settings}, {&popout, &fullScreen}});
    }
    Tool record {"Record scope", motion::icons::Icon::record}, textureOutput {"Share picture", motion::icons::Icon::cast};
    Tool canvas {"Output canvas", motion::icons::Icon::aspectRatio}, settings {"Scope settings", motion::icons::Icon::settings};
    Tool popout {"Pop out scope", motion::icons::Icon::openInNew}, fullScreen {"Full screen scope", motion::icons::Icon::fullscreen};
};

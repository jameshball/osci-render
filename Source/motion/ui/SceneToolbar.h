#pragma once

#include <JuceHeader.h>
#include "MotionIcons.h"
#include "KeyPositions.h"

// Blender-style tools inside the Scene: a vertical strip with the transform
// tools, the motion path, free look and framing, and Look through.
class MotionSceneToolbar final : public motion::icons::ToolStrip {
public:
    using Tool = motion::icons::Button;

    MotionSceneToolbar() {
        setName("Scene tools");
        for (auto* tool : {&move, &rotate, &scale, &anchor, &parts}) { tool->setRadioGroupId(91); tool->setClickingTogglesState(true); }
        path.setClickingTogglesState(true);
        faces.setClickingTogglesState(true);
        faces.setToggleState(true, juce::dontSendNotification);
        move.setTooltip("Move (G)");
        rotate.setTooltip("Rotate (R)");
        scale.setTooltip("Scale (S)");
        anchor.setTooltip("Anchor (Y): drag the point the object rotates and scales about");
        parts.setTooltip("Pick parts (Tab)");
        path.setTooltip("Motion path (P)");
        faces.setTooltip("Solid view: faces hide the lines behind them");
        fly.setTooltip("Free look (N): move the mouse to look around, " + motion::ui::keysAtPositions({motion::ui::KeyPosition::w, motion::ui::KeyPosition::a, motion::ui::KeyPosition::s, motion::ui::KeyPosition::d}) + " or the arrows to move, Esc to finish");
        frame.setTooltip("Frame selection (F)");
        lookThrough.setClickingTogglesState(true);
        setGroups({{&move, &rotate, &scale, &anchor, &parts}, {&path, &faces, &fly, &frame}, {&lookThrough}});
        resized();
    }
    Tool move {"Move tool", motion::icons::Icon::move}, rotate {"Rotate tool", motion::icons::Icon::rotate}, scale {"Scale tool", motion::icons::Icon::scale}, anchor {"Anchor tool", motion::icons::Icon::anchor}, parts {"Pick parts", motion::icons::Icon::parts};
    Tool lookThrough {"Look through camera", motion::icons::Icon::videocam};
    Tool path {"Show motion path", motion::icons::Icon::path}, faces {"Solid view", motion::icons::Icon::solidView}, fly {"Free look", motion::icons::Icon::fly}, frame {"Frame composition selection", motion::icons::Icon::frame};
    // A short Scene keeps only the transform tools.
    void setCompact(bool value) {
        if (compact == value) { return; }
        compact = value;
        for (auto* tool : {&path, &faces, &fly, &frame}) { tool->setVisible(!compact); }
        resized();
    }
    int fullHeight() const {
        const auto camera = lookThrough.isVisible() ? 1 : 0;
        return cell * (9 + camera) + groupGap * (1 + camera) + inset * 2;
    }
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
        box.setTooltip("Box: drag a rectangle round parts");
        lasso.setTooltip("Lasso: draw round parts");
        connected.setTooltip("Pieces: click to pick a whole piece, such as a letter");
        box.setToggleState(true, juce::dontSendNotification);
        setGroups({{&box, &lasso, &connected}});
    }
    Tool box {"Box pick", motion::icons::Icon::box}, lasso {"Lasso pick", motion::icons::Icon::lasso}, connected {"Pick pieces", motion::icons::Icon::connected};
};

// The Scope's controls: sharing its picture with other apps (lit while it
// is shared), its settings, then the popout and full screen. Docked, they
// run across the Scope's header on its own fill; on a Scope too narrow for
// its header they float down the picture's top right as the Scene's strip
// does (full screen, the transport takes their place). The canvas is the
// composition's (its Format). Video is made by Export, drawn offline, never
// recorded live.
class MotionScopeToolbar final : public motion::icons::ToolStrip {
public:
    using Tool = motion::icons::Button;

    MotionScopeToolbar() {
        setName("Scope tools");
        textureOutput.setTooltip("Share the Scope's picture with other apps (Syphon on macOS, Spout on Windows)");
        // On or off, as the visualiser has it, for screen readers too.
        textureOutput.setToggleable(true);
        settings.setTooltip("Scope settings: picture, display and jumps");
        popout.setTooltip("Open the Scope in its own window");
        fullScreen.setTooltip("Full screen");
        setGroups({{&textureOutput}, {&settings}, {&popout, &fullScreen}});
    }
    Tool textureOutput {"Share picture", motion::icons::Icon::cast};
    Tool settings {"Scope settings", motion::icons::Icon::settings};
    Tool popout {"Pop out Scope", motion::icons::Icon::openInNew}, fullScreen {"Full screen Scope", motion::icons::Icon::fullscreen};
    void setDocked(bool value) {
        if (docked == value) { return; }
        docked = value;
        horizontal = value;
        resized();
    }
    // How long it runs, docked or floating.
    int extent() const { return horizontal ? preferredWidth() : preferredHeight(); }
    void resized() override {
        ToolStrip::resized();
        if (!docked) { return; }
        // As tall as the header's other controls (the undo buttons, the
        // recording stopwatch).
        for (auto* tool : tools()) {
            if (tool->isVisible()) { tool->setBounds(tool->getX(), 3, cell, getHeight() - 6); }
        }
    }
    void paint(juce::Graphics& g) override {
        if (!docked) {
            ToolStrip::paint(g);
            return;
        }
        // The header shows through; hairlines part the groups.
        g.setColour(motion::style::hairline());
        juce::Component* previous = nullptr;
        for (auto* tool : tools()) {
            if (!tool->isVisible()) { continue; }
            if (previous != nullptr && tool->getX() - previous->getRight() >= groupGap) { g.fillRect(static_cast<float>(previous->getRight() + groupGap / 2), tool->getY() + 4.0f, 1.0f, tool->getHeight() - 8.0f); }
            previous = tool;
        }
    }

private:
    std::array<juce::Component*, 4> tools() { return {&textureOutput, &settings, &popout, &fullScreen}; }
    bool docked = false;
};

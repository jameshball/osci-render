#pragma once

#include <JuceHeader.h>
#include "MotionIcons.h"

// Blender-style tools inside the Scene: a vertical strip with the transform
// tools, the motion path, fly navigation and framing.
class MotionSceneToolbar final : public juce::Component {
public:
    using Tool = motion::icons::Button;

    MotionSceneToolbar() {
        setName("Scene tools");
        for (auto* tool : {&move, &rotate, &scale}) { tool->setRadioGroupId(91); tool->setClickingTogglesState(true); }
        path.setClickingTogglesState(true);
        move.setTooltip("Move (G): drag an arrow, or the centre to move in the view plane");
        rotate.setTooltip("Rotate (R): drag a coloured ring");
        scale.setTooltip("Scale (S): drag an axis, or the centre for all axes");
        path.setTooltip("Motion path (P): show the selected object's position path; click a path key to seek");
        fly.setTooltip("Fly (N): look around with the mouse and WASD or arrow keys; Esc finishes. The output camera is unchanged.");
        frame.setTooltip("Frame (F): fit the selection, or everything visible. 0 resets the view.");
        for (auto* tool : {&move, &rotate, &scale, &path, &fly, &frame}) { addAndMakeVisible(tool); }
    }
    Tool move {"Move tool", motion::icons::Icon::move}, rotate {"Rotate tool", motion::icons::Icon::rotate}, scale {"Scale tool", motion::icons::Icon::scale};
    Tool path {"Show motion path", motion::icons::Icon::path}, fly {"Navigate composition view", motion::icons::Icon::fly}, frame {"Frame composition selection", motion::icons::Icon::frame};
    static constexpr int toolSize = 30;
    int preferredHeight(bool compact = false) const { return compact ? toolSize * 3 + 6 : toolSize * 6 + 14; }
    // A short Scene keeps only the transform tools.
    void setCompact(bool value) {
        compact = value;
        for (auto* tool : {&path, &fly, &frame}) { tool->setVisible(!compact); }
        resized();
        repaint();
    }
    bool compact = false;

    void paint(juce::Graphics& g) override {
        g.setColour(osci::Colours::veryDark().withAlpha(.82f));
        g.fillRoundedRectangle(getLocalBounds().toFloat(), 6);
        g.setColour(juce::Colours::white.withAlpha(.08f));
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(.5f), 6, 1);
        if (!compact) { g.drawHorizontalLine(3 + toolSize * 3 + 4, 6, static_cast<float>(getWidth() - 6)); }
    }
    void resized() override {
        auto area = getLocalBounds().reduced(1, 3);
        for (auto* tool : {&move, &rotate, &scale}) { tool->setBounds(area.removeFromTop(toolSize)); }
        area.removeFromTop(8);
        for (auto* tool : {&path, &fly, &frame}) { tool->setBounds(area.removeFromTop(toolSize)); }
    }
};

#pragma once

#include <JuceHeader.h>
#include "MotionStyle.h"

// Blender-style tools inside the Scene: a vertical strip with the transform
// tools, the motion path, fly navigation and framing. Icons are drawn here so
// the strip stays crisp at any scale.
class MotionSceneToolbar final : public juce::Component {
public:
    enum class Glyph { move, rotate, scale, path, fly, frame };
    class Tool final : public juce::Button {
    public:
        Tool(const juce::String& name, Glyph shape) : juce::Button(name), glyph(shape) { setTitle(name); setWantsKeyboardFocus(false); setMouseClickGrabsKeyboardFocus(false); }
        void paintButton(juce::Graphics& g, bool highlighted, bool down) override {
            const auto bounds = getLocalBounds().toFloat().reduced(2);
            if (getToggleState()) {
                g.setColour(motion::style::accent().withAlpha(.35f));
                g.fillRoundedRectangle(bounds, 4);
            } else if (highlighted || down) {
                g.setColour(juce::Colours::white.withAlpha(down ? .16f : .09f));
                g.fillRoundedRectangle(bounds, 4);
            }
            g.setColour(getToggleState() ? juce::Colours::white : osci::Colours::text().withAlpha(highlighted ? 1.0f : .78f));
            const auto c = bounds.getCentre();
            const auto r = std::min(bounds.getWidth(), bounds.getHeight()) * .32f;
            juce::Path path;
            const juce::PathStrokeType stroke(1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
            switch (glyph) {
                case Glyph::move:
                    path.startNewSubPath(c.x - r, c.y); path.lineTo(c.x + r, c.y);
                    path.startNewSubPath(c.x, c.y - r); path.lineTo(c.x, c.y + r);
                    g.strokePath(path, stroke);
                    for (const auto [dx, dy] : std::initializer_list<std::pair<float, float>> {{1, 0}, {-1, 0}, {0, 1}, {0, -1}}) {
                        juce::Path head;
                        const auto tip = c + juce::Point<float>(dx * r, dy * r);
                        head.addTriangle(tip, tip + juce::Point<float>(-dx * 4 - dy * 3, -dy * 4 - dx * 3), tip + juce::Point<float>(-dx * 4 + dy * 3, -dy * 4 + dx * 3));
                        g.fillPath(head);
                    }
                    break;
                case Glyph::rotate:
                    path.addCentredArc(c.x, c.y, r, r, 0, 0.6f, juce::MathConstants<float>::twoPi - 0.4f, true);
                    g.strokePath(path, stroke);
                    {
                        juce::Path head;
                        const auto tip = c + juce::Point<float>(std::sin(0.6f) * r, -std::cos(0.6f) * r);
                        head.addTriangle(tip + juce::Point<float>(-4, -2), tip + juce::Point<float>(2, -5), tip + juce::Point<float>(2, 2));
                        g.fillPath(head);
                    }
                    break;
                case Glyph::scale:
                    g.drawRect(juce::Rectangle<float>(c.x - r, c.y - r * .2f, r * 1.2f, r * 1.2f), 1.5f);
                    path.startNewSubPath(c.x - r * .2f, c.y + r * .2f); path.lineTo(c.x + r, c.y - r);
                    path.startNewSubPath(c.x + r * .3f, c.y - r); path.lineTo(c.x + r, c.y - r); path.lineTo(c.x + r, c.y - r * .3f);
                    g.strokePath(path, stroke);
                    break;
                case Glyph::path:
                    path.startNewSubPath(c.x - r, c.y + r * .7f);
                    path.cubicTo(c.x - r * .2f, c.y - r * 1.4f, c.x + r * .2f, c.y + r * 1.4f, c.x + r, c.y - r * .7f);
                    g.strokePath(path, stroke);
                    g.fillEllipse(c.x - r - 2, c.y + r * .7f - 2, 4, 4);
                    g.fillEllipse(c.x + r - 2, c.y - r * .7f - 2, 4, 4);
                    break;
                case Glyph::fly:
                    // A paper plane: fly through the scene.
                    path.startNewSubPath(c.x - r, c.y);
                    path.lineTo(c.x + r, c.y - r * .8f);
                    path.lineTo(c.x + r * .2f, c.y + r);
                    path.lineTo(c.x - r * .05f, c.y + r * .15f);
                    path.closeSubPath();
                    path.startNewSubPath(c.x - r * .05f, c.y + r * .15f);
                    path.lineTo(c.x + r, c.y - r * .8f);
                    g.strokePath(path, stroke);
                    break;
                case Glyph::frame:
                    for (const auto [sx, sy] : std::initializer_list<std::pair<float, float>> {{-1, -1}, {1, -1}, {-1, 1}, {1, 1}}) {
                        path.startNewSubPath(c.x + sx * r, c.y + sy * r * .4f);
                        path.lineTo(c.x + sx * r, c.y + sy * r);
                        path.lineTo(c.x + sx * r * .4f, c.y + sy * r);
                    }
                    g.strokePath(path, stroke);
                    break;
            }
        }
    private:
        Glyph glyph;
    };

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
    Tool move {"Move tool", Glyph::move}, rotate {"Rotate tool", Glyph::rotate}, scale {"Scale tool", Glyph::scale};
    Tool path {"Show motion path", Glyph::path}, fly {"Navigate composition view", Glyph::fly}, frame {"Frame composition selection", Glyph::frame};
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

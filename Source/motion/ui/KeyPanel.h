#pragma once

#include "ModulatorPanels.h"

// The one selected key's time and value as numbers, above Routing beside the
// Graph (like Blender's Active Keyframe). Drags and typing both edit it.
class MotionKeyPanel final : public juce::Component {
public:
    MotionKeyPanel() {
        setName("Active key");
        time.setup(*this, "Time", "Key time", timeSpec);
        value.setup(*this, "Value", "Key value", motion::ui::amountSpec);
        time.field.onChange = [this](double seconds) { if (onTime) { onTime(seconds); } };
        time.field.onCommit = time.field.onChange;
        value.field.onChange = [this](double number) { if (onValue) { onValue(number); } };
        value.field.onCommit = value.field.onChange;
    }

    std::function<void(double)> onTime, onValue;

    // Fields being edited keep what the user is typing or dragging.
    void show(double seconds, double number, const motion::PropertySpec& spec, bool editable) {
        if (!time.field.isEditing()) { time.field.setValue(seconds); }
        if (spec.id != valueSpecId) {
            valueSpecId = std::string(spec.id);
            value.field.setSpec(spec);
        }
        if (!value.field.isEditing()) { value.field.setValue(number); }
        time.field.setEnabled(editable);
        value.field.setEnabled(editable);
    }
    bool isEditing() const { return time.field.isEditing() || value.field.isEditing(); }

    static int preferredHeight() { return headerHeight + motion::style::gap + 2 * (motion::style::controlHeight + motion::style::gap) + motion::style::padding; }

    void paint(juce::Graphics& g) override {
        g.fillAll(osci::Colours::surfaceSunken());
        auto header = getLocalBounds().removeFromTop(headerHeight);
        g.setColour(osci::Colours::surfaceRaised());
        g.fillRect(header);
        g.setColour(osci::Colours::text());
        g.setFont(motion::style::title());
        g.drawText("Key", header.reduced(10, 0), juce::Justification::centredLeft);
    }
    void resized() override {
        auto area = getLocalBounds().reduced(8, 0);
        area.removeFromTop(headerHeight + motion::style::gap);
        time.layout(area);
        value.layout(area);
    }

private:
    static constexpr int headerHeight = 26;
    static constexpr motion::PropertySpec timeSpec {"time", "Time", "", "", 0, motion::unbounded, 0, .01, 3, "s"};
    motion::ui::LabelledScrub time, value;
    std::string valueSpecId;
};

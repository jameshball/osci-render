#pragma once

#include "InterpolationBar.h"
#include "ModulatorPanels.h"

// The selected keys beside the Graph (like Blender's Active Keyframe): how
// many, their interpolation and Easy ease, and, for one key, its time and
// value as fields that drags and typing both edit.
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
        interpolation.onInterpolation = [this](motion::Interpolation choice) { if (onInterpolation) { onInterpolation(choice); } };
        interpolation.onEase = [this] { if (onEase) { onEase(); } };
        addAndMakeVisible(interpolation);
    }

    std::function<void(double)> onTime, onValue;
    std::function<void(motion::Interpolation)> onInterpolation;
    std::function<void()> onEase;

    // `single` is the one selected key's time and value, when only one is.
    // Fields being edited keep what the user is typing or dragging.
    void show(std::size_t keys, const std::array<bool, 4>& present, bool editable, std::optional<std::pair<double, double>> single, const motion::PropertySpec& spec) {
        title = keys == 1 ? juce::String("Key") : juce::String(static_cast<int>(keys)) + " keys";
        interpolation.show(present, editable);
        fields = single.has_value();
        time.setVisible(fields);
        value.setVisible(fields);
        if (fields) {
            if (!time.field.isEditing()) { time.field.setValue(single->first); }
            if (spec.id != valueSpecId) {
                valueSpecId = std::string(spec.id);
                value.field.setSpec(spec);
            }
            if (!value.field.isEditing()) { value.field.setValue(single->second); }
            time.field.setEnabled(editable);
            value.field.setEnabled(editable);
        }
        resized();
        repaint();
    }

    int preferredHeight() const {
        return headerHeight + motion::style::gap + MotionInterpolationBar::height + motion::style::gap
            + (fields ? 2 * (motion::style::controlHeight + motion::style::gap) : 0) + motion::style::padding;
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(osci::Colours::surface());
        auto header = getLocalBounds().removeFromTop(headerHeight);
        g.setColour(osci::Colours::surfaceRaised());
        g.fillRect(header);
        g.setColour(osci::Colours::text());
        g.setFont(motion::style::title());
        g.drawText(title, header.reduced(10, 0), juce::Justification::centredLeft);
    }
    void resized() override {
        auto area = getLocalBounds().reduced(8, 0);
        area.removeFromTop(headerHeight + motion::style::gap);
        interpolation.setBounds(area.removeFromTop(MotionInterpolationBar::height));
        area.removeFromTop(motion::style::gap);
        if (fields) {
            time.layout(area);
            value.layout(area);
        }
    }

private:
    static constexpr int headerHeight = 26;
    static constexpr motion::PropertySpec timeSpec {"time", "Time", "", "", 0, motion::unbounded, 0, .01, 3, "s"};
    MotionInterpolationBar interpolation;
    motion::ui::LabelledScrub time, value;
    juce::String title {"Key"};
    std::string valueSpecId;
    bool fields = false;
};

#pragma once

#include "../../visualiser/RecordingSettings.h"
#include "MotionStyle.h"
#include <optional>

// Shared framing controls for the output canvas and video export.
class MotionCanvasSizeEditor final : public juce::Component {
public:
    static constexpr int preferredHeight = 2 * motion::style::dialog::row + motion::style::dialog::rowGap + 20;
    explicit MotionCanvasSizeEditor(VisualiserRenderSize size) {
        for (auto* component : std::initializer_list<juce::Component*> { &preset, &widthLabel, &heightLabel, &width, &height }) { addAndMakeVisible(component); }
        preset.setName("Canvas preset");
        preset.addItem("Custom canvas", 1);
        preset.addItem("Square / 1024 x 1024", 2);
        preset.addItem("Landscape / 1920 x 1080", 3);
        preset.addItem("Portrait / 1080 x 1920", 4);
        width.setName("Video width");
        height.setName("Video height");
        motion::style::dialog::caption(widthLabel);
        motion::style::dialog::caption(heightLabel);
        for (auto* field : { &width, &height }) {
            field->setFont(motion::style::body());
            field->setJustification(juce::Justification::centredLeft);
            field->setInputRestrictions(4, "0123456789");
            field->onTextChange = [this] { refreshPreset(); };
        }
        setSizeValue(size);
        preset.onChange = [this] {
            switch (preset.getSelectedId()) {
                case 2: setSizeValue({ 1024, 1024 }); break;
                case 3: setSizeValue({ 1920, 1080 }); break;
                case 4: setSizeValue({ 1080, 1920 }); break;
                default: break;
            }
        };
    }
    std::function<void()> onChange;
    std::optional<VisualiserRenderSize> value() const {
        const auto w = width.getText().getIntValue(), h = height.getText().getIntValue();
        if (w < 128 || w > 4096 || h < 128 || h > 4096 || w % 2 != 0 || h % 2 != 0) { return std::nullopt; }
        return VisualiserRenderSize { w, h };
    }
    void resized() override {
        auto bounds = getLocalBounds();
        preset.setBounds(bounds.removeFromTop(motion::style::dialog::row));
        bounds.removeFromTop(motion::style::dialog::rowGap);
        auto labels = bounds.removeFromTop(20);
        const auto half = (bounds.getWidth() - 8) / 2;
        widthLabel.setBounds(labels.removeFromLeft(half));
        labels.removeFromLeft(8);
        heightLabel.setBounds(labels);
        auto row = bounds.removeFromTop(motion::style::dialog::row);
        width.setBounds(row.removeFromLeft(half));
        row.removeFromLeft(8);
        height.setBounds(row);
    }
private:
    void setSizeValue(VisualiserRenderSize size) {
        width.setText(juce::String(size.width), false);
        height.setText(juce::String(size.height), false);
        refreshPreset();
    }
    void refreshPreset() {
        const auto w = width.getText().getIntValue(), h = height.getText().getIntValue();
        const auto id = w == 1024 && h == 1024 ? 2 : w == 1920 && h == 1080 ? 3 : w == 1080 && h == 1920 ? 4 : 1;
        preset.setSelectedId(id, juce::dontSendNotification);
        if (onChange) { onChange(); }
    }
    juce::ComboBox preset;
    juce::Label widthLabel { "Video width caption", "Width (pixels)" }, heightLabel { "Video height caption", "Height (pixels)" };
    juce::TextEditor width, height;
};

class MotionCanvasSettings final : public juce::Component {
public:
    MotionCanvasSettings(VisualiserRenderSize size, double fps) : canvas(size) {
        for (auto* component : std::initializer_list<juce::Component*> { &canvas, &note, &error, &apply }) { addAndMakeVisible(component); }
        juce::ignoreUnused(fps);
        error.setFont(motion::style::body());
        error.setBorderSize({});
        error.setColour(juce::Label::textColourId, motion::style::danger());
        canvas.onChange = [this] { error.setText({}, juce::dontSendNotification); };
        apply.onClick = [this] {
            const auto size = canvas.value();
            if (!size.has_value()) { error.setText("Use even dimensions from 128 to 4096 pixels.", juce::dontSendNotification); return; }
            if (onApply) { onApply(*size); }
        };
    }
    std::function<void(VisualiserRenderSize)> onApply;
    void resized() override {
        auto bounds = getLocalBounds().reduced(motion::style::dialog::margin);
        error.setBounds(motion::style::dialog::footer(bounds, { &apply }));
        canvas.setBounds(bounds.removeFromTop(MotionCanvasSizeEditor::preferredHeight));
    }
private:
    MotionCanvasSizeEditor canvas;
    juce::Label note, error;
    juce::TextButton apply { "Apply canvas" };
};

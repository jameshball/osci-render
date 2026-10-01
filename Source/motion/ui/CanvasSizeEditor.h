#pragma once

#include "../../visualiser/RecordingSettings.h"
#include <optional>

// Shared framing controls for the output canvas and video export.
class MotionCanvasSizeEditor final : public juce::Component {
public:
    explicit MotionCanvasSizeEditor(VisualiserRenderSize size) {
        for (auto* component : std::initializer_list<juce::Component*> { &preset, &widthLabel, &heightLabel, &width, &height }) { addAndMakeVisible(component); }
        preset.setName("Canvas preset");
        preset.addItem("Custom canvas", 1);
        preset.addItem("Square / 1024 x 1024", 2);
        preset.addItem("Landscape / 1920 x 1080", 3);
        preset.addItem("Portrait / 1080 x 1920", 4);
        width.setName("Video width");
        height.setName("Video height");
        for (auto* field : { &width, &height }) {
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
        preset.setBounds(bounds.removeFromTop(28));
        bounds.removeFromTop(8);
        auto labels = bounds.removeFromTop(20);
        const auto half = (bounds.getWidth() - 12) / 2;
        widthLabel.setBounds(labels.removeFromLeft(half));
        labels.removeFromLeft(12);
        heightLabel.setBounds(labels);
        auto row = bounds.removeFromTop(28);
        width.setBounds(row.removeFromLeft(half));
        row.removeFromLeft(12);
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
        note.setText("Frames the output preview and sets the default video size.\n" + juce::String(fps, 3) + " fps (change it in the Timing menu).", juce::dontSendNotification);
        error.setColour(juce::Label::textColourId, juce::Colour(0xffe98080));
        canvas.onChange = [this] { error.setText({}, juce::dontSendNotification); };
        apply.onClick = [this] {
            const auto size = canvas.value();
            if (!size.has_value()) { error.setText("Use even dimensions from 128 to 4096 pixels.", juce::dontSendNotification); return; }
            if (onApply) { onApply(*size); }
        };
    }
    std::function<void(VisualiserRenderSize)> onApply;
    void resized() override {
        auto bounds = getLocalBounds().reduced(12);
        canvas.setBounds(bounds.removeFromTop(84));
        note.setBounds(bounds.removeFromTop(54));
        error.setBounds(bounds.removeFromTop(24));
        apply.setBounds(bounds.removeFromTop(30));
    }
private:
    MotionCanvasSizeEditor canvas;
    juce::Label note, error;
    juce::TextButton apply { "Apply canvas" };
};

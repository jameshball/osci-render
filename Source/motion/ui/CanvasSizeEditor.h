#pragma once

#include "../../visualiser/RecordingSettings.h"
#include "Sheet.h"
#include "ScrubField.h"
#include <optional>

// Shared framing controls for the output canvas and video export: a preset,
// then width and height, as form rows with the caption column the owner uses.
class MotionCanvasSizeEditor final : public juce::Component {
public:
    static constexpr int rows = 3;
    MotionCanvasSizeEditor(VisualiserRenderSize size, int captionWidth, int rowHeight, int rowGap) : caption(captionWidth), row(rowHeight), gap(rowGap) {
        for (auto* component : std::initializer_list<juce::Component*> { &preset, &presetLabel, &widthLabel, &heightLabel, &width, &height }) { addAndMakeVisible(component); }
        preset.setName("Canvas preset");
        preset.addItem("Custom", 1);
        preset.addItem("Square" + motion::style::dot() + "1024 " + juce::String::fromUTF8("\xc3\x97") + " 1024", 2);
        preset.addItem("Landscape" + motion::style::dot() + "1920 " + juce::String::fromUTF8("\xc3\x97") + " 1080", 3);
        preset.addItem("Portrait" + motion::style::dot() + "1080 " + juce::String::fromUTF8("\xc3\x97") + " 1920", 4);
        width.setName("Video width");
        height.setName("Video height");
        for (auto* label : { &presetLabel, &widthLabel, &heightLabel }) {
            label->setFont(motion::style::body());
            label->setColour(juce::Label::textColourId, osci::Colours::textMuted());
            label->setBorderSize({});
        }
        for (auto* field : { &width, &height }) {
            field->setSpec(sizeSpec);
            field->setFill(motion::style::fieldFill(*this));
            field->onChange = [this](double) { refreshPreset(); };
            field->onCommit = field->onChange;
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
    int preferredHeight() const { return rows * row + (rows - 1) * gap; }
    std::function<void()> onChange;
    // Fields recess into whichever panel holds the editor.
    void parentHierarchyChanged() override {
        for (auto* field : { &width, &height }) { field->setFill(motion::style::fieldFill(*this)); }
    }
    std::optional<VisualiserRenderSize> value() const {
        const auto w = juce::roundToInt(width.getValue()), h = juce::roundToInt(height.getValue());
        if (w < 128 || w > 4096 || h < 128 || h > 4096 || w % 2 != 0 || h % 2 != 0) { return std::nullopt; }
        return VisualiserRenderSize { w, h };
    }
    void resized() override {
        auto bounds = getLocalBounds();
        auto line = bounds.removeFromTop(row);
        presetLabel.setBounds(line.removeFromLeft(caption));
        preset.setBounds(line);
        for (auto [label, field] : { std::pair { &widthLabel, &width }, std::pair { &heightLabel, &height } }) {
            bounds.removeFromTop(gap);
            line = bounds.removeFromTop(row);
            label->setBounds(line.removeFromLeft(caption));
            field->setBounds(line.removeFromLeft(motion::ui::Sheet::number));
        }
    }
private:
    void setSizeValue(VisualiserRenderSize size) {
        width.setValue(size.width);
        height.setValue(size.height);
        refreshPreset();
    }
    void refreshPreset() {
        const auto w = juce::roundToInt(width.getValue()), h = juce::roundToInt(height.getValue());
        const auto id = w == 1024 && h == 1024 ? 2 : w == 1920 && h == 1080 ? 3 : w == 1080 && h == 1920 ? 4 : 1;
        preset.setSelectedId(id, juce::dontSendNotification);
        if (onChange) { onChange(); }
    }
    int caption, row, gap;
    juce::ComboBox preset;
    static constexpr motion::PropertySpec sizeSpec {"size", "Size", "", "", 2, 8192, 1024, 2, 0, " px"};
    juce::Label presetLabel { "Canvas preset caption", "Preset" }, widthLabel { "Video width caption", "Width" }, heightLabel { "Video height caption", "Height" };
    motion::ui::ScrubField width, height;
};

// The output canvas, from the Scope's tools.
class MotionCanvasSettings final : public motion::ui::Popover {
public:
    explicit MotionCanvasSettings(VisualiserRenderSize size) : Popover("Output canvas", "Apply", "Apply canvas"), canvas(size, caption, row, rowGap) {
        addAndMakeVisible(canvas);
        canvas.onChange = [this] { setError({}); };
        primary.onClick = [this] {
            const auto value = canvas.value();
            if (!value.has_value()) { setError("Even sizes, 128-4096 px"); return; }
            if (onApply) { onApply(*value); }
        };
        setSize(widthFor(284), heightFor(MotionCanvasSizeEditor::rows));
    }
    std::function<void(VisualiserRenderSize)> onApply;

protected:
    void layoutBody(juce::Rectangle<int> area) override { canvas.setBounds(area.removeFromTop(canvas.preferredHeight())); }

private:
    MotionCanvasSizeEditor canvas;
};

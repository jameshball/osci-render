#pragma once

#include "../import/FractalPreparation.h"
#include "Sheet.h"
#include "ScrubField.h"
#include "SourcePreview.h"
#include <algorithm>
#include <functional>

// How deep an L-system is grown, with the result drawn beside the setting.
class MotionFractalSettingsPanel final : public motion::ui::Sheet {
public:
    MotionFractalSettingsPanel(int initialDepth, const juce::String& fileName, juce::String sourceText = {})
        : Sheet("Prepare fractal", fileName, "Prepare"), source(std::move(sourceText)) {
        setName("Fractal preparation settings");
        nameAction("Prepare fractal");
        depth.setName("Fractal depth");
        depth.setSpec(depthSpec);
        depth.setValue(std::clamp(initialDepth, 0, 15));
        depth.setFill(fieldFill());
        depth.setTooltip("Higher values add L-system detail and increase preparation time.");
        depth.onChange = [this](double) { refresh(); };
        depth.onCommit = depth.onChange;
        styleCaption(depthLabel);
        primary.onClick = [this] {
            if (onPrepare) { onPrepare(juce::roundToInt(depth.getValue())); }
        };
        addAndMakeVisible(depthLabel);
        addAndMakeVisible(depth);
        if (source.isNotEmpty()) { addAndMakeVisible(preview); }
        // One row beside a preview as tall as the image sheet's.
        setSize(widthFor(source.isNotEmpty() ? previewSize : 0), source.isNotEmpty() ? heightFor(1, previewSize - row) : heightFor(1));
        refresh();
    }

    std::function<void(int)> onPrepare;

protected:
    void layoutBody(juce::Rectangle<int> area) override {
        if (source.isNotEmpty()) {
            preview.setBounds(area.removeFromLeft(previewSize).withHeight(previewSize));
            area.removeFromLeft(24);
        }
        formRow(area, depthLabel, depth, number);
    }

private:
    static constexpr int previewSize = 172;
    static constexpr motion::PropertySpec depthSpec {"depth", "Depth", "", "", 0, 15, 3, 1, 0, ""};
    void refresh() {
        if (source.isEmpty()) { return; }
        preview.request([text = source, level = juce::roundToInt(depth.getValue())](const std::atomic<bool>& cancel) {
            motion::ui::SourcePreview::Result result;
            const auto prepared = motion::fractal::prepare(text, level, &cancel);
            if (!prepared) {
                result.error = prepared.error;
                return result;
            }
            float left = 1e9f, right = -1e9f, top = 1e9f, bottom = -1e9f;
            for (const auto& segment : prepared.segments) {
                for (const auto [x, y] : {std::pair {segment[0], segment[1]}, std::pair {segment[2], segment[3]}}) {
                    left = std::min(left, static_cast<float>(x)); right = std::max(right, static_cast<float>(x));
                    top = std::min(top, static_cast<float>(y)); bottom = std::max(bottom, static_cast<float>(y));
                }
            }
            const auto size = std::max({right - left, bottom - top, 1e-6f});
            const auto map = [&](double x, double y) {
                return juce::Point<float>(.5f + (static_cast<float>(x) - (left + right) * .5f) / size, .5f - (static_cast<float>(y) - (top + bottom) * .5f) / size);
            };
            juce::Point<float> last {-1e9f, -1e9f};
            for (const auto& segment : prepared.segments) {
                const auto from = map(segment[0], segment[1]), to = map(segment[2], segment[3]);
                if (from.getDistanceFrom(last) > 1e-5f) { result.path.startNewSubPath(from); }
                result.path.lineTo(to);
                last = to;
            }
            const auto count = static_cast<int>(prepared.segments.size());
            result.caption = juce::String(count) + (count == 1 ? " segment" : " segments");
            return result;
        });
    }

    juce::String source;
    motion::ui::ScrubField depth;
    motion::ui::SourcePreview preview;
    juce::Label depthLabel {"Fractal depth caption", "Depth"};
};

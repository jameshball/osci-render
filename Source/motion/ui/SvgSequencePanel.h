#pragma once

#include "../import/SourceDecoding.h"
#include "../import/SvgSequence.h"
#include "Sheet.h"
#include "ScrubField.h"
#include "SourcePreview.h"
#include <functional>

// Several SVGs chosen together: one animation of them, a frame each in name
// order, or a source each. The animation plays beside the choice.
class MotionSvgSequencePanel final : public motion::ui::Sheet {
public:
    explicit MotionSvgSequencePanel(juce::Array<juce::File> chosen)
        : Sheet("Add " + juce::String(chosen.size()) + " SVGs", chosen.getFirst().getFileName() + juce::String::fromUTF8(" \xe2\x80\x93 ") + chosen.getLast().getFileName(), "Add"),
          files(std::move(chosen)) {
        setName("SVG sequence import");
        nameAction("Add SVGs");
        mode.setTooltip("Animation plays the SVGs as frames, in name order; Separate adds a source for each");
        mode.setSelected(motion::svgsequence::numbered(files) ? 0 : 1);
        mode.onChange = [this](int) { refresh(); };
        frameRate.setName("Sequence frame rate");
        frameRate.setSpec(frameRateSpec);
        frameRate.setValue(motion::svgsequence::defaultFrameRate);
        frameRate.setFill(fieldFill());
        frameRate.onChange = [this](double value) { preview.setFrameRate(animation() ? value : 0); };
        frameRate.onCommit = frameRate.onChange;
        styleCaption(modeLabel);
        styleCaption(frameRateLabel);
        primary.onClick = [this] {
            if (onImport) { onImport(animation(), frameRate.getValue()); }
        };
        for (auto* component : std::initializer_list<juce::Component*> {&preview, &modeLabel, &mode, &frameRateLabel, &frameRate}) { addAndMakeVisible(component); }
        setSize(widthFor(), heightFor(2, previewSize + rowGap));
        // Traced once; the rate only changes how fast it plays.
        preview.request([files = files](const std::atomic<bool>& cancel) {
            motion::ui::SourcePreview::Result result;
            motion::svgsequence::Frames frames;
            for (const auto& file : files) { frames.svgs.push_back(file.loadFileAsString()); }
            motion::Asset asset;
            asset.extension = motion::svgsequence::extension;
            asset.data = motion::svgsequence::encode(frames);
            const auto decoded = motion::decodeAsset(asset, &cancel);
            if (decoded.failed() || asset.source == nullptr) {
                result.error = decoded.getErrorMessage();
                return result;
            }
            // Long sequences play a share of their frames, at the same pace.
            const auto count = asset.source->frameCount();
            const auto shown = std::min<std::size_t>(count, 240);
            for (std::size_t index = 0; index < shown && !cancel.load(); ++index) {
                // The page every frame is placed on, fitted to the screen (y up).
                result.frames.push_back(motion::ui::traceSource(*asset.source, index * count / shown, 1500, nullptr, juce::Rectangle<float>(-.9f, -.9f, 1.8f, 1.8f)));
            }
            result.frameStep = static_cast<double>(count) / static_cast<double>(std::max<std::size_t>(shown, 1));
            return result;
        });
        refresh();
    }

    std::function<void(bool animation, double frameRate)> onImport;

protected:
    void layoutBody(juce::Rectangle<int> area) override {
        preview.setBounds(area.removeFromTop(previewSize));
        area.removeFromTop(rowGap);
        formRow(area, modeLabel, mode);
        formRow(area, frameRateLabel, frameRate, number);
    }

private:
    static constexpr int previewSize = 196;
    static constexpr motion::PropertySpec frameRateSpec {"frameRate", "Frame rate", "", "", 1, 120, motion::svgsequence::defaultFrameRate, .5, 1, " fps"};
    bool animation() const { return mode.getSelected() == 0; }
    void refresh() {
        const auto count = files.size();
        frameRate.setEnabled(animation());
        frameRateLabel.setAlpha(animation() ? 1.0f : .4f);
        preview.setFrameRate(animation() ? frameRate.getValue() : 0);
        preview.setCaption(animation() ? juce::String(count) + " frames" + motion::style::dot() + "in name order" : juce::String(count) + " sources, one for each SVG");
    }

    juce::Array<juce::File> files;
    motion::ui::SourcePreview preview;
    motion::ui::SegmentedControl mode {"Import as", {"Animation", "Separate"}};
    motion::ui::ScrubField frameRate;
    juce::Label modeLabel {"Import as caption", "Import as"}, frameRateLabel {"Frame rate caption", "Frame rate"};
};

#pragma once

#include "MotionStyle.h"
#include "../model/Animation.h"
#include <array>
#include <optional>

// The selected keys' interpolation in plain sight: a small bar with the
// selection count, one glyph button per interpolation (lit when every
// selected key uses it) and Easy ease. It sits at the end of the Graph's
// ruler band while keys are selected, never over the curves.
class MotionKeyBar final : public juce::Component {
public:
    MotionKeyBar() {
        setName("Key interpolation");
        const std::array<const char*, 4> names {"Hold", "Linear", "Auto", "Bezier"};
        const std::array<const char*, 4> tips {"Hold: keep the value until the next key", "Linear: a straight line to the next key",
            "Auto: a smooth curve through the keys that never overshoots", "Bezier: a curve shaped by the key's handles"};
        for (std::size_t index = 0; index < buttons.size(); ++index) {
            auto& button = buttons[index];
            button.interpolation = static_cast<motion::Interpolation>(index);
            button.setName(juce::String(names[index]) + " interpolation");
            button.setTitle(button.getName());
            button.setTooltip(tips[index]);
            button.onClick = [this, index] { if (onInterpolation) { onInterpolation(static_cast<motion::Interpolation>(index)); } };
            addAndMakeVisible(button);
        }
        ease.setName("Easy ease");
        ease.setTitle(ease.getName());
        ease.setTooltip("Easy ease: slow in and out of the selected keys (F9)");
        ease.onClick = [this] { if (onEase) { onEase(); } };
        addAndMakeVisible(ease);
    }

    std::function<void(motion::Interpolation)> onInterpolation;
    std::function<void()> onEase;

    // `common` is the interpolation every selected key shares, if any.
    void show(std::size_t keys, std::optional<motion::Interpolation> common, bool editable) {
        count = keys == 1 ? "1 key" : juce::String(static_cast<int>(keys)) + " keys";
        for (auto& button : buttons) {
            button.setToggleState(common == button.interpolation, juce::dontSendNotification);
            button.setEnabled(editable);
        }
        ease.setEnabled(editable);
        setSize(preferredWidth(), height);
        repaint();
    }

    int preferredWidth() const { return padding + countWidth() + gap + static_cast<int>(buttons.size()) * buttonWidth + gap + easeWidth + padding; }
    static constexpr int height = 22;

    void paint(juce::Graphics& g) override {
        const auto bounds = getLocalBounds().toFloat().reduced(.5f);
        motion::style::fillFloatingPanel(g, bounds, osci::Colours::surfaceRaised().brighter(.12f));
        g.setColour(osci::Colours::textMuted());
        g.setFont(motion::style::caption());
        g.drawText(count, getLocalBounds().withTrimmedLeft(padding).withWidth(countWidth()), juce::Justification::centredLeft, false);
        // Hairline dividers between the count, the interpolations and Ease.
        g.setColour(juce::Colours::white.withAlpha(.08f));
        for (const auto x : {buttons.front().getX() - gap / 2, ease.getX() - gap / 2}) { g.fillRect(static_cast<float>(x), 5.0f, 1.0f, height - 10.0f); }
    }

    void resized() override {
        auto area = getLocalBounds().reduced(padding, 2);
        area.removeFromLeft(countWidth() + gap);
        for (auto& button : buttons) { button.setBounds(area.removeFromLeft(buttonWidth)); }
        area.removeFromLeft(gap);
        ease.setBounds(area.removeFromLeft(easeWidth));
    }

private:
    // A glyph of the interpolation's segment shape, lit while selected.
    struct InterpolationButton final : juce::Button {
        InterpolationButton() : juce::Button({}) { setWantsKeyboardFocus(false); }
        motion::Interpolation interpolation = motion::Interpolation::linear;
        void paintButton(juce::Graphics& g, bool over, bool down) override {
            const auto bounds = getLocalBounds().toFloat().reduced(1.0f, 0.0f);
            const auto lit = getToggleState();
            if (lit || over || down) {
                g.setColour(lit ? osci::Colours::accentColor().withAlpha(down ? .45f : .32f) : juce::Colours::white.withAlpha(down ? .12f : .07f));
                g.fillRoundedRectangle(bounds, motion::style::radius);
            }
            const auto colour = !isEnabled() ? osci::Colours::textMuted().withAlpha(.35f) : lit ? juce::Colours::white : osci::Colours::text().withAlpha(over ? 1.0f : .7f);
            const auto box = bounds.withSizeKeepingCentre(14.0f, 12.0f);
            const auto x0 = box.getX(), x1 = box.getRight(), y0 = box.getBottom(), y1 = box.getY();
            juce::Path glyph;
            glyph.startNewSubPath(x0, y0);
            switch (interpolation) {
                case motion::Interpolation::hold:
                    glyph.lineTo(box.getCentreX(), y0);
                    glyph.lineTo(box.getCentreX(), y1);
                    glyph.lineTo(x1, y1);
                    break;
                case motion::Interpolation::linear: glyph.lineTo(x1, y1); break;
                // Auto rises and settles; Bezier is an S with its handles.
                case motion::Interpolation::smooth: glyph.cubicTo(x0 + 3.0f, y1 + 1.0f, x1 - 6.0f, y1, x1, y1); break;
                case motion::Interpolation::cubic: glyph.cubicTo(x1 - 2.0f, y0, x0 + 2.0f, y1, x1, y1); break;
            }
            g.setColour(colour);
            g.strokePath(glyph, juce::PathStrokeType(1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            if (interpolation == motion::Interpolation::cubic) {
                g.setColour(colour.withMultipliedAlpha(.55f));
                g.drawLine(x0, y0, x0 + 5.0f, y0, 1.0f);
                g.drawLine(x1, y1, x1 - 5.0f, y1, 1.0f);
            }
            // Both ends are keys, as on the curve.
            for (const auto end : {juce::Point<float>(x0, y0), juce::Point<float>(x1, y1)}) { g.fillEllipse(juce::Rectangle<float>(3.2f, 3.2f).withCentre(end)); }
        }
    };
    struct EaseButton final : juce::Button {
        EaseButton() : juce::Button({}) { setWantsKeyboardFocus(false); }
        void paintButton(juce::Graphics& g, bool over, bool down) override {
            const auto bounds = getLocalBounds().toFloat().reduced(1.0f, 0.0f);
            if (over || down) {
                g.setColour(juce::Colours::white.withAlpha(down ? .12f : .07f));
                g.fillRoundedRectangle(bounds, motion::style::radius);
            }
            g.setColour(!isEnabled() ? osci::Colours::textMuted().withAlpha(.35f) : osci::Colours::text().withAlpha(over ? 1.0f : .8f));
            g.setFont(motion::style::caption());
            g.drawText("Ease", bounds, juce::Justification::centred, false);
        }
    };
    int countWidth() const { return juce::GlyphArrangement::getStringWidthInt(motion::style::caption(), "88 keys"); }
    static constexpr int padding = 9, gap = 9, buttonWidth = 26, easeWidth = 38;
    std::array<InterpolationButton, 4> buttons;
    EaseButton ease;
    juce::String count;
};

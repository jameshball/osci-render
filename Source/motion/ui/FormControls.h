#pragma once

#include "MotionStyle.h"

namespace motion::ui {
// A choice among a few options shown at once (Outlines | Scanlines). The
// chosen segment is a raised neutral tab, not the accent: green stays for
// selection and the one primary action.
class SegmentedControl final : public juce::Component, public juce::SettableTooltipClient {
public:
    SegmentedControl(const juce::String& name, juce::StringArray options) : labels(std::move(options)) {
        setName(name);
        setTitle(name);
        setWantsKeyboardFocus(true);
        setRepaintsOnMouseActivity(true);
    }

    std::function<void(int)> onChange;
    int getSelected() const { return selected; }
    void setSelected(int index, juce::NotificationType notification = juce::dontSendNotification) {
        index = std::clamp(index, 0, labels.size() - 1);
        if (index == selected) { return; }
        selected = index;
        repaint();
        if (notification != juce::dontSendNotification && onChange) { onChange(selected); }
    }
    int preferredWidth() const {
        auto width = 0;
        for (const auto& label : labels) { width += juce::roundToInt(juce::TextLayout::getStringWidth(style::body(), label)) + 2 * segmentPadding; }
        return width + 2 * inset;
    }

    void paint(juce::Graphics& g) override {
        const auto bounds = getLocalBounds().toFloat();
        g.setColour(fill);
        g.fillRoundedRectangle(bounds, style::radius + 1);
        const auto hover = isMouseOver(true) ? segmentAt(getMouseXYRelative().x) : -1;
        for (int index = 0; index < labels.size(); ++index) {
            const auto segment = segmentBounds(index).toFloat();
            if (index == selected) {
                g.setColour(osci::Colours::surfaceRaised().brighter(.12f));
                g.fillRoundedRectangle(segment, style::radius);
                g.setColour(juce::Colours::white.withAlpha(.07f));
                g.drawRoundedRectangle(segment.reduced(.5f), style::radius, 1.0f);
            } else if (index == hover && isEnabled()) {
                g.setColour(juce::Colours::white.withAlpha(.05f));
                g.fillRoundedRectangle(segment, style::radius);
            }
            g.setFont(compact ? style::caption() : style::body());
            const auto alpha = !isEnabled() ? .35f : index == selected ? 1.0f : index == hover ? .9f : .62f;
            g.setColour(osci::Colours::text().withAlpha(alpha));
            g.drawText(labels[index], segment, juce::Justification::centred, false);
        }
        if (hasKeyboardFocus(false)) {
            g.setColour(osci::Colours::accentColor().withAlpha(.6f));
            g.drawRoundedRectangle(bounds.reduced(.5f), style::radius + 1, 1.0f);
        }
    }
    void mouseDown(const juce::MouseEvent& event) override {
        if (!isEnabled()) { return; }
        const auto index = segmentAt(event.x);
        if (index >= 0) { setSelected(index, juce::sendNotification); }
    }
    bool keyPressed(const juce::KeyPress& key) override {
        if (key.getKeyCode() == juce::KeyPress::leftKey) { setSelected(selected - 1, juce::sendNotification); return true; }
        if (key.getKeyCode() == juce::KeyPress::rightKey) { setSelected(selected + 1, juce::sendNotification); return true; }
        return false;
    }
    // Fields recess into the panel they sit on; dialogs sit on a darker one.
    juce::Colour fill = osci::Colours::veryDark();
    // Many short options (six presets in a narrow column) read in captions.
    bool compact = false;

    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override {
        // Read and set as text, like a combo box.
        struct Value final : juce::AccessibilityTextValueInterface {
            explicit Value(SegmentedControl& owner) : control(owner) {}
            bool isReadOnly() const override { return false; }
            juce::String getCurrentValueAsString() const override { return control.labels[control.selected]; }
            void setValueAsString(const juce::String& text) override {
                const auto index = control.labels.indexOf(text, true);
                if (index >= 0) { control.setSelected(index, juce::sendNotification); }
            }
            SegmentedControl& control;
        };
        return std::make_unique<juce::AccessibilityHandler>(*this, juce::AccessibilityRole::comboBox, juce::AccessibilityActions(),
            juce::AccessibilityHandler::Interfaces {std::make_unique<Value>(*this)});
    }

private:
    static constexpr int inset = 2, segmentPadding = 12;
    juce::Rectangle<int> segmentBounds(int index) const {
        const auto area = getLocalBounds().reduced(inset);
        const auto width = static_cast<float>(area.getWidth()) / static_cast<float>(labels.size());
        const auto left = area.getX() + juce::roundToInt(width * static_cast<float>(index));
        const auto right = area.getX() + juce::roundToInt(width * static_cast<float>(index + 1));
        return {left, area.getY(), right - left, area.getHeight()};
    }
    int segmentAt(int x) const {
        for (int index = 0; index < labels.size(); ++index) {
            if (x < segmentBounds(index).getRight()) { return index; }
        }
        return labels.size() - 1;
    }
    juce::StringArray labels;
    int selected = 0;
};

// An on/off setting in a form: a small pill whose knob slides across, lit in
// the accent when on.
class Switch final : public juce::Button {
public:
    explicit Switch(const juce::String& name) : juce::Button(name) {
        setClickingTogglesState(true);
        setTitle(name);
        slide.setValueChangedCallback([this](float) { repaint(); });
    }
    static constexpr int width = 28, height = 16;
    void buttonStateChanged() override { fade.follow(*this); }
    void clicked() override { slide.animateTo(getToggleState(), 140, juce::Easings::createEaseOut()); }
    void paintButton(juce::Graphics& g, bool, bool) override {
        if (slide.getTargetState() != getToggleState()) { slide.snapTo(getToggleState()); }
        const auto pill = getLocalBounds().toFloat().withSizeKeepingCentre(static_cast<float>(width), static_cast<float>(height));
        const auto on = slide.getProgress();
        const auto off = osci::Colours::surfaceRaised().brighter(.25f + .15f * fade.value());
        g.setColour(off.interpolatedWith(osci::Colours::accentColor().withAlpha(.75f), on).withMultipliedAlpha(isEnabled() ? 1.0f : .45f));
        g.fillRoundedRectangle(pill, pill.getHeight() * .5f);
        const auto knob = pill.getHeight() - 4.0f;
        const auto x = pill.getX() + 2.0f + (pill.getWidth() - 4.0f - knob) * on;
        g.setColour(juce::Colours::white.withAlpha(isEnabled() ? .95f : .5f));
        g.fillEllipse(x, pill.getY() + 2.0f, knob, knob);
    }
private:
    style::Fade fade {*this};
    osci::ToggleAnimationController slide {this};
};
}

#pragma once

#include <JuceHeader.h>
#include "../LookAndFeel.h"

// Rotary knob that extends juce::Slider with RotaryHorizontalVerticalDrag style.
// Custom arc + marker rendering is handled by the LookAndFeel's drawRotarySlider.
// Supports modulation display via slider properties (same convention as linear sliders).
class RotaryKnobComponent : public juce::Slider {
public:
    RotaryKnobComponent() {
        setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
        setPopupDisplayEnabled(true, false, nullptr);
        setColour(juce::Slider::rotarySliderFillColourId, Colours::accentColor());

        constexpr float kArcGap = juce::MathConstants<float>::pi / 3.0f;
        float startAngle = juce::MathConstants<float>::pi + kArcGap * 0.5f;
        float endAngle   = juce::MathConstants<float>::pi * 3.0f - kArcGap * 0.5f;
        setRotaryParameters({ startAngle, endAngle, true });
    }

    void setAccentColour(juce::Colour c) {
        setColour(juce::Slider::rotarySliderFillColourId, c);
    }

    void mouseDown(const juce::MouseEvent& event) override {
        if (event.mods.isRightButtonDown()) {
            lastRightClickScreenPos = event.getScreenPosition();
            showContextMenu();
            return;
        }
        juce::Slider::mouseDown(event);
    }

private:
    juce::Point<int> lastRightClickScreenPos;
    std::unique_ptr<juce::TextEditor> valueEditor;

    void showContextMenu() {
        juce::PopupMenu menu;
        menu.addItem(1, "Reset to Default Value", isDoubleClickReturnEnabled());
        menu.addItem(2, "Set Value...");

        auto options = juce::PopupMenu::Options().withTargetScreenArea(
            juce::Rectangle<int>(lastRightClickScreenPos.x, lastRightClickScreenPos.y, 1, 1));

        menu.showMenuAsync(options,
            [safeThis = juce::Component::SafePointer<RotaryKnobComponent>(this)](int result) {
                if (safeThis == nullptr) return;
                if (result == 1) {
                    safeThis->setValue(safeThis->getDoubleClickReturnValue(), juce::sendNotificationSync);
                } else if (result == 2) {
                    safeThis->showValueEditor();
                }
            });
    }

    void showValueEditor() {
        valueEditor = std::make_unique<juce::TextEditor>();
        valueEditor->setMultiLine(false);
        valueEditor->setJustification(juce::Justification::centred);
        valueEditor->setText(juce::String(getValue()), false);
        valueEditor->selectAll();

        int editorW = juce::jmax(getWidth(), 60);
        int editorH = 22;
        int x = (getWidth() - editorW) / 2;
        int y = (getHeight() - editorH) / 2;
        valueEditor->setBounds(x, y, editorW, editorH);

        addAndMakeVisible(valueEditor.get());
        valueEditor->grabKeyboardFocus();

        valueEditor->onReturnKey = [this]() { applyValueFromEditor(); };
        valueEditor->onEscapeKey = [this]() { dismissValueEditor(); };
        valueEditor->onFocusLost = [this]() { applyValueFromEditor(); };
    }

    void applyValueFromEditor() {
        if (!valueEditor) return;
        auto text = valueEditor->getText();
        double newValue = text.getDoubleValue();
        newValue = juce::jlimit(getMinimum(), getMaximum(), newValue);
        setValue(newValue, juce::sendNotificationSync);
        dismissValueEditor();
    }

    void dismissValueEditor() {
        if (valueEditor) {
            removeChildComponent(valueEditor.get());
            valueEditor.reset();
        }
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RotaryKnobComponent)
};

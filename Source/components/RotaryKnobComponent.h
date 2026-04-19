#pragma once

#include <JuceHeader.h>
#include "../LookAndFeel.h"
#include "InlineValueEditor.h"

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

    // Optional: parent can take full ownership of the context menu.
    // When set, the knob will call this instead of building a default menu.
    std::function<void(juce::Point<int> screenPos)> onShowContextMenu;

    void mouseDown(const juce::MouseEvent& event) override {
        if (event.mods.isRightButtonDown()) {
            showContextMenu(event.getScreenPosition());
            return;
        }
        juce::Slider::mouseDown(event);
    }

    // Show the context menu. Delegates to onShowContextMenu if set,
    // otherwise shows a minimal default menu.
    void showContextMenu(juce::Point<int> screenPos = {}) {
        if (onShowContextMenu) {
            onShowContextMenu(screenPos);
            return;
        }

        // Default fallback menu (Reset + Set Value)
        juce::PopupMenu menu;
        menu.addItem(1, "Reset to Default Value", isDoubleClickReturnEnabled());
        menu.addItem(2, "Set Value...");

        auto options = juce::PopupMenu::Options().withTargetScreenArea(
            juce::Rectangle<int>(screenPos.x, screenPos.y, 1, 1));

        menu.showMenuAsync(options,
            [safeThis = juce::Component::SafePointer<RotaryKnobComponent>(this)](int result) {
                if (safeThis == nullptr) return;
                if (result == 1)
                    safeThis->setValue(safeThis->getDoubleClickReturnValue(), juce::sendNotificationSync);
                else if (result == 2)
                    safeThis->showValueEditor();
            });
    }

    void showValueEditor() {
        InlineValueEditor::show(*this, *this, valueEditor, getLocalBounds());
    }

private:

    std::shared_ptr<juce::TextEditor> valueEditor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RotaryKnobComponent)
};

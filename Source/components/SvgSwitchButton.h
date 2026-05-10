#pragma once

#include <JuceHeader.h>
#include "SwitchButton.h"

class SvgSwitchButton : public juce::Component {
public:
    SvgSwitchButton(juce::String name, juce::String svg, juce::Colour colour, osci::BooleanParameter* parameterToUse, bool showParameterTooltip = false)
        : juce::Component(std::move(name)),
          parameter(parameterToUse),
          switchButton(parameterToUse, showParameterTooltip),
          drawable(osci::Svg::createDrawable(std::move(svg), colour)) {
        addAndMakeVisible(switchButton);
    }

    SvgSwitchButton(juce::String name, juce::String svg, osci::BooleanParameter* parameterToUse)
        : SvgSwitchButton(std::move(name), std::move(svg), juce::Colours::white, parameterToUse) {}

    void resized() override {
        constexpr int groupWidth = 30;
        constexpr int iconSize = 18;
        constexpr int gap = 2;
        constexpr int switchHeight = 20;
        constexpr int groupHeight = iconSize + gap + switchHeight;

        auto group = getLocalBounds().withSizeKeepingCentre(groupWidth, groupHeight);
        iconBounds = group.removeFromTop(iconSize).withSizeKeepingCentre(iconSize, iconSize);
        group.removeFromTop(gap);
        switchButton.setBounds(group.removeFromTop(switchHeight).withSizeKeepingCentre(groupWidth, switchHeight));
    }

    void paint(juce::Graphics& g) override {
        if (drawable == nullptr) {
            return;
        }

        drawable->drawWithin(g, iconBounds.toFloat(),
                             juce::RectanglePlacement::centred | juce::RectanglePlacement::onlyReduceInSize,
                             1.0f);
    }

    void mouseDown(const juce::MouseEvent& e) override {
        if (e.mods.isPopupMenu() || parameter == nullptr) {
            return;
        }

        parameter->setBoolValueNotifyingHost(!parameter->getBoolValue());
    }

    juce::MouseCursor getMouseCursor() override {
        return juce::MouseCursor::PointingHandCursor;
    }

private:
    osci::BooleanParameter* parameter = nullptr;
    jux::SwitchButton switchButton;
    std::unique_ptr<juce::Drawable> drawable;
    juce::Rectangle<int> iconBounds;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SvgSwitchButton)
};

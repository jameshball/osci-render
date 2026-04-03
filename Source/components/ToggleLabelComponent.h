#pragma once

#include <JuceHeader.h>
#include "../LookAndFeel.h"
#include <osci_render_core/effect/osci_EffectParameter.h>

// A toggleable rounded-label pill that wraps a BooleanParameter.
// When enabled, the label text is bright and the pill has an accent highlight.
// When disabled, the text and pill are dimmed.
// Clicking toggles the parameter and notifies the DAW host.
class ToggleLabelComponent : public juce::Component,
                             public juce::AudioProcessorParameter::Listener,
                             public juce::AsyncUpdater {
public:
    ToggleLabelComponent(osci::BooleanParameter* param)
        : parameter(param)
    {
        jassert(param != nullptr);
        parameter->addListener(this);
        setRepaintsOnMouseActivity(true);
    }

    ~ToggleLabelComponent() override {
        parameter->removeListener(this);
    }

    void paint(juce::Graphics& g) override {
        auto bounds = getLocalBounds().toFloat().reduced(1.0f, 0.0f);
        bool on = parameter->getBoolValue();
        bool hovering = isEnabled() && isMouseOver();

        // Background pill
        auto bgColour = on ? Colours::accentColor().withAlpha(0.25f)
                           : Colours::darkerer();
        if (hovering)
            bgColour = bgColour.brighter(0.3f);
        g.setColour(bgColour);
        g.fillRoundedRectangle(bounds, Colours::kPillRadius);

        // Text
        auto textColour = on ? juce::Colours::white
                             : juce::Colours::white.withAlpha(0.35f);
        g.setColour(textColour);
        g.setFont(juce::Font(10.0f));
        g.drawText(parameter->name.toUpperCase(), bounds, juce::Justification::centred, false);
    }

    void mouseEnter(const juce::MouseEvent&) override {
        if (isEnabled())
            setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }

    void mouseExit(const juce::MouseEvent&) override {
        setMouseCursor(juce::MouseCursor::NormalCursor);
    }

    void mouseUp(const juce::MouseEvent& e) override {
        if (!isEnabled()) return;
        if (e.mouseWasClicked() && getLocalBounds().toFloat().contains(e.position)) {
            parameter->beginChangeGesture();
            parameter->setBoolValueNotifyingHost(!parameter->getBoolValue());
            parameter->endChangeGesture();
        }
    }

    void parameterValueChanged(int, float) override { triggerAsyncUpdate(); }
    void parameterGestureChanged(int, bool) override {}
    void handleAsyncUpdate() override { repaint(); }

private:
    osci::BooleanParameter* parameter;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ToggleLabelComponent)
};

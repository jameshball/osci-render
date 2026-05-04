#pragma once

#include <JuceHeader.h>
#include "LabelledBarComponent.h"
#include "../LookAndFeel.h"
#include "../audio/GraphNode.h"
#include <osci_render_core/effect/osci_EffectParameter.h>

// A LabelledBarComponent that draws a response curve from (0,0) to (1,1)
// shaped by a FloatParameter's value used as the power/curve argument.
// Reuses the existing evaluateGraphCurve / powerScale infrastructure.
// Drag vertically to change the curve parameter.
class SlopeGraphComponent : public LabelledBarComponent,
                            public juce::AudioProcessorParameter::Listener,
                            public juce::AsyncUpdater {
public:
    SlopeGraphComponent(osci::FloatParameter* param, const juce::String& label)
        : parameter(param), labelText(label)
    {
        jassert(param != nullptr);
        setDraggable(true);
        parameter->addListener(this);
    }

    ~SlopeGraphComponent() override {
        parameter->removeListener(this);
    }

    juce::String getLabelText() const override { return labelText; }

    void paintContent(juce::Graphics& g, juce::Rectangle<int> area) override {
        if (area.isEmpty()) return;

        float power = parameter->getValueUnnormalised();

        // Build a two-node curve: (0,0) -> (1,1) with the power as curve
        const std::vector<GraphNode> nodes = {
            { 0.0, 0.0, 0.0f },
            { 1.0, 1.0, power }
        };

        auto bounds = area.reduced(4, 3).toFloat();

        // Draw curve
        juce::Path path;
        constexpr int steps = 64;
        for (int i = 0; i <= steps; ++i) {
            float t = (float)i / (float)steps;
            float v = evaluateGraphCurve(nodes, t);
            float x = bounds.getX() + t * bounds.getWidth();
            float y = bounds.getBottom() - v * bounds.getHeight();
            if (i == 0)
                path.startNewSubPath(x, y);
            else
                path.lineTo(x, y);
        }

        // Stroke curve
        g.setColour(osci::Colours::accentColor().withAlpha(0.9f));
        g.strokePath(path, juce::PathStrokeType(1.5f, juce::PathStrokeType::beveled, juce::PathStrokeType::rounded));
    }

    void mouseDown(const juce::MouseEvent& e) override {
        if (!isEnabled()) return;
        dragStartValue = parameter->getValueUnnormalised();
        dragStartPos = e.getPosition();
        parameter->beginChangeGesture();
    }

    void mouseDrag(const juce::MouseEvent& e) override {
        if (!isEnabled()) return;
        int dy = e.getPosition().getY() - dragStartPos.getY();
        float range = (float)(parameter->max.load() - parameter->min.load());
        float sensitivity = range / 150.0f;
        float newValue = juce::jlimit((float)parameter->min.load(),
                                      (float)parameter->max.load(),
                                      dragStartValue + dy * sensitivity);
        parameter->setUnnormalisedValueNotifyingHost(newValue);
        repaint();
    }

    void mouseUp(const juce::MouseEvent&) override {
        parameter->endChangeGesture();
    }

    void mouseDoubleClick(const juce::MouseEvent&) override {
        if (!isEnabled()) return;
        parameter->beginChangeGesture();
        parameter->setUnnormalisedValueNotifyingHost(parameter->defaultValue.load());
        parameter->endChangeGesture();
        repaint();
    }

    void parameterValueChanged(int, float) override { triggerAsyncUpdate(); }
    void parameterGestureChanged(int, bool) override {}
    void handleAsyncUpdate() override { repaint(); }

private:
    osci::FloatParameter* parameter;
    juce::String labelText;
    float dragStartValue = 0.0f;
    juce::Point<int> dragStartPos;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SlopeGraphComponent)
};

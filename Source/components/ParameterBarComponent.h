#pragma once

#include <JuceHeader.h>
#include "LabelledBarComponent.h"
#include "../LookAndFeel.h"
#include "modulation/InlineEditorHelper.h"
#include <osci_render_core/effect/osci_EffectParameter.h>

// A concrete LabelledBarComponent that wraps an osci::FloatParameter or
// osci::IntParameter.  Drag vertically to change the value; double-click
// to type.  Display format and drag sensitivity are inferred automatically.
class ParameterBarComponent : public LabelledBarComponent,
                              public juce::AudioProcessorParameter::Listener,
                              public juce::AsyncUpdater {
public:
    ParameterBarComponent(osci::FloatParameter* param, const juce::String& label)
        : labelText(label)
    {
        jassert(param != nullptr);
        rawParam = param;
        getValue  = [param]() { return param->getValueUnnormalised(); };
        setValue   = [param](float v) { param->setUnnormalisedValueNotifyingHost(v); };
        getMin    = [param]() { return (float)param->min.load(); };
        getMax    = [param]() { return (float)param->max.load(); };
        integerMode = param->step.load() >= 1.0f;
        init();
    }

    ParameterBarComponent(osci::IntParameter* param, const juce::String& label)
        : labelText(label), integerMode(true)
    {
        jassert(param != nullptr);
        rawParam = param;
        getValue  = [param]() { return (float)param->getValueUnnormalised(); };
        setValue   = [param](float v) { param->setUnnormalisedValueNotifyingHost(v); };
        getMin    = [param]() { return (float)param->min.load(); };
        getMax    = [param]() { return (float)param->max.load(); };
        init();
    }

    ~ParameterBarComponent() override {
        rawParam->removeListener(this);
    }

    juce::String getLabelText() const override { return labelText; }

    void paintContent(juce::Graphics& g, juce::Rectangle<int> area) override {
        if (inlineEditor != nullptr)
            return;

        float value = getValue();

        g.setFont(juce::Font(juce::FontOptions(13.0f)));
        g.setColour(juce::Colours::white.withAlpha(0.9f));

        juce::String text = integerMode ? juce::String((int)value)
                                        : juce::String(value, 1);
        g.drawText(text, area.toFloat(), juce::Justification::centred);
    }

    void resizedContent(juce::Rectangle<int> area) override {
        if (inlineEditor != nullptr)
            inlineEditor->setBounds(area.reduced(2));
    }

    void mouseDown(const juce::MouseEvent& e) override {
        if (!isEnabled()) return;
        dragStartValue = getValue();
        dragStartPos = e.getPosition();
        dragging = false;
    }

    void mouseDrag(const juce::MouseEvent& e) override {
        if (!isEnabled()) return;
        dragging = true;
        int dy = dragStartPos.getY() - e.getPosition().getY();
        float minVal = getMin();
        float maxVal = getMax();
        float newValue = juce::jlimit(minVal, maxVal, dragStartValue + dy * sensitivity);
        if (integerMode)
            newValue = std::round(newValue);
        setValue(newValue);
        repaint();
    }

    void mouseUp(const juce::MouseEvent&) override {
        dragging = false;
    }

    void mouseDoubleClick(const juce::MouseEvent&) override {
        if (!isEnabled()) return;
        showInlineEditor();
    }

private:
    void init() {
        setDraggable(true);
        rawParam->addListener(this);
        float range = getMax() - getMin();
        sensitivity = range / 200.0f;
    }

    void parameterValueChanged(int, float) override { triggerAsyncUpdate(); }
    void parameterGestureChanged(int, bool) override {}
    void handleAsyncUpdate() override { repaint(); }

    void showInlineEditor() {
        float value = getValue();
        float minVal = getMin();
        float maxVal = getMax();

        juce::String initialText = integerMode ? juce::String((int)value)
                                               : juce::String(value, 1);

        auto commitFn = [this, minVal, maxVal](const juce::String& text) {
            float val = text.getFloatValue();
            if (val >= minVal && val <= maxVal) {
                if (integerMode)
                    val = std::round(val);
                setValue(val);
            }
            inlineEditor.reset();
            repaint();
        };
        auto cancelFn = [this]() {
            inlineEditor.reset();
            repaint();
        };
        inlineEditor = InlineEditorHelper::create(
            initialText, getContentArea().reduced(2),
            { commitFn, cancelFn },
            osci::Colours::evenDarker(), juce::Colours::white,
            juce::Colours::transparentBlack, 13.0f);
        addAndMakeVisible(*inlineEditor);
        inlineEditor->grabKeyboardFocus();
        repaint();
    }

    juce::AudioProcessorParameter* rawParam = nullptr;
    std::function<float()> getValue;
    std::function<void(float)> setValue;
    std::function<float()> getMin;
    std::function<float()> getMax;

    juce::String labelText;
    float sensitivity = 0.5f;
    bool integerMode = false;

    float dragStartValue = 0.0f;
    juce::Point<int> dragStartPos;
    bool dragging = false;

    std::unique_ptr<juce::TextEditor> inlineEditor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ParameterBarComponent)
};

#include "TempoComponent.h"
#include "../../LookAndFeel.h"
#include "InlineEditorHelper.h"

TempoComponent::TempoComponent(OscirenderAudioProcessor& p) : audioProcessor(p) {
    setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
    audioProcessor.standaloneBpm->addListener(this);
}

TempoComponent::~TempoComponent() {
    audioProcessor.standaloneBpm->removeListener(this);
}

void TempoComponent::handleAsyncUpdate() {
    repaint();
}

void TempoComponent::paintContent(juce::Graphics& g, juce::Rectangle<int> area) {
    if (inlineEditor != nullptr)
        return;

    float bpm = audioProcessor.standaloneBpm->getValueUnnormalised();

    g.setFont(juce::Font(13.0f));
    g.setColour(juce::Colours::white.withAlpha(0.9f));

    juce::String text = juce::String(bpm, 1);

    g.drawText(text, area.toFloat(), juce::Justification::centred);
}

void TempoComponent::resizedContent(juce::Rectangle<int> area) {
    if (inlineEditor != nullptr) {
        inlineEditor->setBounds(area.reduced(2));
    }
}

void TempoComponent::mouseDown(const juce::MouseEvent& e) {
    dragStartValue = audioProcessor.standaloneBpm->getValueUnnormalised();
    dragStartPos = e.getPosition();
    dragging = false;
}

void TempoComponent::mouseDrag(const juce::MouseEvent& e) {
    dragging = true;
    int dy = dragStartPos.getY() - e.getPosition().getY();
    float sensitivity = 0.5f;
    float newBpm = juce::jlimit(20.0f, 300.0f, dragStartValue + dy * sensitivity);
    audioProcessor.standaloneBpm->setUnnormalisedValueNotifyingHost(newBpm);
    repaint();
}

void TempoComponent::mouseUp(const juce::MouseEvent&) {
    dragging = false;
}

void TempoComponent::mouseDoubleClick(const juce::MouseEvent&) {
    showInlineEditor();
}

void TempoComponent::showInlineEditor() {
    float bpm = audioProcessor.standaloneBpm->getValueUnnormalised();

    auto commitFn = [this](const juce::String& text) {
        float val = text.getFloatValue();
        if (val >= 20.0f && val <= 300.0f)
            audioProcessor.standaloneBpm->setUnnormalisedValueNotifyingHost(val);
        inlineEditor.reset();
        repaint();
    };
    auto cancelFn = [this]() {
        inlineEditor.reset();
        repaint();
    };
    inlineEditor = InlineEditorHelper::create(
        juce::String(bpm, 1), getContentArea().reduced(2),
        { commitFn, cancelFn },
        Colours::evenDarker(), juce::Colours::white,
        juce::Colours::transparentBlack, 13.0f);
    addAndMakeVisible(*inlineEditor);
    inlineEditor->grabKeyboardFocus();
    repaint();
}

void TempoComponent::commitInlineEditor() {
    auto editor = std::move(inlineEditor);
    if (editor == nullptr)
        return;

    float val = editor->getText().getFloatValue();
    if (val >= 20.0f && val <= 300.0f) {
        audioProcessor.standaloneBpm->setUnnormalisedValueNotifyingHost(val);
    }

    repaint();
}

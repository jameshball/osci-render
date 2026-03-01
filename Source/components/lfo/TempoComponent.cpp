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

void TempoComponent::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().toFloat();

    // Dark rounded bar background
    g.setColour(juce::Colour(0xFF1A1A1A));
    g.fillRoundedRectangle(bounds, 4.0f);

    // Subtle border
    g.setColour(juce::Colours::white.withAlpha(0.08f));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 4.0f, 1.0f);

    if (inlineEditor != nullptr)
        return;

    float bpm = audioProcessor.standaloneBpm->getValueUnnormalised();

    // BPM value
    g.setFont(juce::Font(13.0f));
    g.setColour(juce::Colours::white.withAlpha(0.9f));

    juce::String text;
    if (bpm == std::round(bpm))
        text = juce::String((int)bpm) + " BPM";
    else
        text = juce::String(bpm, 1) + " BPM";

    g.drawText(text, bounds.reduced(6.0f, 0), juce::Justification::centred);
}

void TempoComponent::resized() {
    if (inlineEditor != nullptr) {
        inlineEditor->setBounds(getLocalBounds().reduced(2));
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
        juce::String(bpm, 1), getLocalBounds().reduced(2),
        { commitFn, cancelFn },
        juce::Colour(0xFF1A1A1A), juce::Colours::white,
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

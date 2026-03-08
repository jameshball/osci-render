#pragma once

#include <JuceHeader.h>
#include <memory>
#include <functional>

// Factory for creating consistently styled inline text editors used across
// the LFO subsystem (DepthIndicator, LfoRateComponent, TempoComponent).
namespace InlineEditorHelper {

struct Callbacks {
    std::function<void(const juce::String& text)> onCommit;
    std::function<void()> onCancel;
};

// Create a styled inline text editor with consistent look, commit/cancel wiring.
// The caller owns the returned editor and must add it to a parent component.
inline std::unique_ptr<juce::TextEditor> create(
        const juce::String& initialText,
        juce::Rectangle<int> bounds,
        const Callbacks& callbacks,
        juce::Colour backgroundColour = juce::Colour(0xFF1A1A1A),
        juce::Colour textColour = juce::Colours::white,
        juce::Colour outlineColour = juce::Colours::white.withAlpha(0.3f),
        float fontSize = 12.0f) {
    auto editor = std::make_unique<juce::TextEditor>();
    editor->setJustification(juce::Justification::centred);
    editor->setFont(juce::Font(fontSize, juce::Font::bold));
    editor->setColour(juce::TextEditor::backgroundColourId, backgroundColour);
    editor->setColour(juce::TextEditor::textColourId, textColour);
    editor->setColour(juce::TextEditor::outlineColourId, outlineColour);
    editor->setColour(juce::TextEditor::focusedOutlineColourId, outlineColour.withAlpha(
        juce::jmin(1.0f, outlineColour.getFloatAlpha() + 0.2f)));
    editor->setBounds(bounds);
    editor->setText(initialText, false);
    editor->selectAll();

    // Use raw pointer captures — the caller guarantees the editor stays alive
    // while it has focus, and both commit/cancel destroy it.
    auto* raw = editor.get();
    editor->onReturnKey = [raw, callbacks]() {
        if (callbacks.onCommit)
            callbacks.onCommit(raw->getText());
    };
    editor->onEscapeKey = [callbacks]() {
        if (callbacks.onCancel)
            callbacks.onCancel();
    };
    editor->onFocusLost = [raw, callbacks]() {
        if (callbacks.onCommit)
            callbacks.onCommit(raw->getText());
    };

    return editor;
}

} // namespace InlineEditorHelper

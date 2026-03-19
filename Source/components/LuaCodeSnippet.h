#pragma once

#include <JuceHeader.h>
#include "../LookAndFeel.h"

// A read-only Lua code snippet component with syntax highlighting.
// Uses the same Dracula colour scheme as the main code editor but
// has no line numbers, no caret, and a compact dark background.
class LuaCodeSnippet : public juce::Component {
public:
    LuaCodeSnippet() {
        editor = std::make_unique<juce::CodeEditorComponent>(document, &tokeniser);
        editor->setReadOnly(true);
        editor->setLineNumbersShown(false);
        editor->setScrollbarThickness(0);

        editor->setColourScheme(OscirenderLookAndFeel::getDefaultColourScheme());

        editor->setColour(juce::CodeEditorComponent::backgroundColourId, Colours::veryDark);
        editor->setColour(juce::CodeEditorComponent::defaultTextColourId, Dracula::foreground);
        editor->setColour(juce::CodeEditorComponent::lineNumberBackgroundId, Colours::veryDark);
        editor->setColour(juce::CaretComponent::caretColourId, juce::Colours::transparentBlack);

        addAndMakeVisible(*editor);
    }

    void setText(const juce::String& code) {
        document.replaceAllContent(code);
        updateHeight();
    }

    void paint(juce::Graphics& g) override {
        auto bounds = getLocalBounds().toFloat();
        // Fill rounded background so the editor's rectangular bg doesn't peek through corners
        g.setColour(Colours::veryDark);
        g.fillRoundedRectangle(bounds, 6.0f);
        // Draw the border
        g.setColour(juce::Colours::white.withAlpha(0.15f));
        g.drawRoundedRectangle(bounds.reduced(0.5f), 6.0f, 1.0f);
    }

    void resized() override {
        // Inset enough to keep the rectangular editor clear of the rounded corners
        editor->setBounds(getLocalBounds().reduced(4, 2));
    }

    // Returns the ideal height for the current content.
    int getIdealHeight() const {
        return idealHeight;
    }

private:
    void updateHeight() {
        int numLines = document.getNumLines();
        float lineHeight = editor->getFont().getHeight();
        idealHeight = juce::jmax(24, (int)(numLines * lineHeight + 16));
    }

    juce::CodeDocument document;
    juce::LuaTokeniser tokeniser;
    std::unique_ptr<juce::CodeEditorComponent> editor;
    int idealHeight = 40;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LuaCodeSnippet)
};

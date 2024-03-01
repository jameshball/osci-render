#include "LuaConsole.h"
#include "../PluginEditor.h"

LuaConsole::LuaConsole() {
	setText("Lua Console");

	console.setMultiLine(true);
	console.setReadOnly(true);
	console.setCaretVisible(false);
	
	console.setColour(juce::TextEditor::backgroundColourId, juce::Colours::black);
	console.setColour(juce::TextEditor::textColourId, juce::Colours::white);
	console.setColour(juce::TextEditor::outlineColourId, juce::Colours::white);
	console.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::white);
	console.setColour(juce::TextEditor::highlightColourId, juce::Colours::white);
	console.setColour(juce::TextEditor::highlightedTextColourId, juce::Colours::black);
	console.setColour(juce::TextEditor::shadowColourId, juce::Colours::black);

	addAndMakeVisible(console);
}

LuaConsole::~LuaConsole() {}

void LuaConsole::print(const juce::String& text) {
	console.moveCaretToEnd();
	console.insertTextAtCaret(text);

	// clear start of console if it gets too long
	if (console.getTotalNumChars() > 10000) {
		console.setHighlightedRegion(juce::Range<int>(0, 1000));
		console.deleteBackwards(true);
		console.moveCaretToEnd();
	}
	
}

void LuaConsole::clear() {
	console.clear();
}

void LuaConsole::resized() {
	auto area = getLocalBounds().withTrimmedTop(20);
	console.setBounds(area);
}

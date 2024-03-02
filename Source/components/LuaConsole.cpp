#include "LuaConsole.h"
#include "../PluginEditor.h"

LuaConsole::LuaConsole() {
	setText("Lua Console");

	console.setReadOnly(true);
	console.setLineNumbersShown(false);
	console.setScrollbarThickness(0);
	document.getUndoManager().setMaxNumberOfStoredUnits(0, 0);

	startTimerHz(10);

	clearConsoleButton.onClick = [this] {
        clear();
    };

	addAndMakeVisible(console);
	addAndMakeVisible(clearConsoleButton);
	addAndMakeVisible(pauseConsoleButton);

	pauseConsoleButton.onClick = [this] {
        console.setScrollbarThickness(pauseConsoleButton.getToggleState() ? 10 : 0);
    };
}

LuaConsole::~LuaConsole() {}

void LuaConsole::print(const std::string& text) {
	juce::SpinLock::ScopedLockType l(lock);

	if (consoleOpen && !pauseConsoleButton.getToggleState()) {
		buffer += text + "\n";
		consoleLines++;
	}
}

void LuaConsole::clear() {
	juce::SpinLock::ScopedLockType l(lock);

	document.replaceAllContent("");
	document.clearUndoHistory();
	consoleLines = 0;
	buffer.clear();
}

void LuaConsole::timerCallback() {
	juce::SpinLock::ScopedLockType l(lock);

	if (consoleOpen && !pauseConsoleButton.getToggleState()) {
		document.insertText(juce::CodeDocument::Position(document, std::numeric_limits<int>::max(), std::numeric_limits<int>::max()), buffer);
		buffer.clear();

		// clear console if it gets too long
		if (consoleLines > 100000) {
			// soft-clear console
			int linesToClear = consoleLines * 0.9;
			document.deleteSection(juce::CodeDocument::Position(document, 0, 0), juce::CodeDocument::Position(document, linesToClear, 0));
			consoleLines -= linesToClear;
		}

		console.moveCaretToTop(false);
		console.moveCaretToEnd(false);
		console.scrollDown();
	}
}

void LuaConsole::setConsoleOpen(bool open) {
	juce::SpinLock::ScopedLockType l(lock);

	consoleOpen = open;
	console.setVisible(open);
	if (open) {
		startTimerHz(10);
    } else {
		stopTimer();
    }
}

void LuaConsole::resized() {
	auto topBar = getLocalBounds().removeFromTop(30);
	auto area = getLocalBounds().withTrimmedTop(30);
	console.setBounds(area);

	clearConsoleButton.setBounds(topBar.removeFromRight(30).withSizeKeepingCentre(20, 20));
	pauseConsoleButton.setBounds(topBar.removeFromRight(30).withSizeKeepingCentre(20, 20));
}

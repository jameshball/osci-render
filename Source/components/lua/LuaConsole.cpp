#include "LuaConsole.h"
#include "../../PluginEditor.h"

LuaConsole::LuaConsole() {
	setOpaque(false);
	setText("Lua Console");

	console.setReadOnly(true);
	console.setLineNumbersShown(false);
	console.setScrollbarThickness(0);
	document.getUndoManager().setMaxNumberOfStoredUnits(0, 0);

	startTimerHz(10);

	clearConsoleButton.onClick = [this] {
        clear(true);
    };

	addAndMakeVisible(console);
	addAndMakeVisible(clearConsoleButton);
	addAndMakeVisible(pauseConsoleButton);
	addAndMakeVisible(emptyConsoleLabel);

	clearConsoleButton.setTooltip("Clear console output. You can clear the console from Lua with the clear() function.");

	pauseConsoleButton.setTooltip("Pause console output, and show a scrollbar to navigate through the console history.");

	pauseConsoleButton.setClickingTogglesState(true);
	pauseConsoleButton.onClick = [this] {
        console.setScrollbarThickness(pauseConsoleButton.getToggleState() ? 10 : 0);
    };

	emptyConsoleLabel.setJustificationType(juce::Justification::centred);
	updateColours();
}

LuaConsole::~LuaConsole() {}

void LuaConsole::print(const std::string& text) {
	juce::SpinLock::ScopedLockType l(lock);

	if (consoleOpen && !pauseConsoleButton.getToggleState()) {
		buffer += text + "\n";
		consoleLines++;
	}
}

void LuaConsole::clear(bool forceClear) {
	juce::SpinLock::ScopedLockType l(lock);

	if (forceClear || !pauseConsoleButton.getToggleState()) {
		document.replaceAllContent("");
		document.clearUndoHistory();
		consoleLines = 0;
		buffer.clear();

		juce::MessageManager::callAsync([this] {
			console.setVisible(false);
			emptyConsoleLabel.setVisible(true);
		});
	}
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

		if (consoleLines > 0) {
            console.setVisible(true);
            emptyConsoleLabel.setVisible(false);
        }
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

void LuaConsole::paint(juce::Graphics& g) {
	auto bounds = getLocalBounds().toFloat();
	auto alpha = isEnabled() ? 1.0f : 0.5f;
	auto radius = static_cast<float>(osci::LookAndFeel::RECT_RADIUS);

	g.setColour(findColour(osci::groupComponentBackgroundColourId).withMultipliedAlpha(alpha));
	g.fillRoundedRectangle(bounds, radius);

	auto header = bounds;
	header.setHeight(30.0f);

	juce::Path headerPath;
	if (getHeight() <= 30) {
		headerPath.addRoundedRectangle(header.getX(), header.getY(), header.getWidth(), header.getHeight(), radius, radius);
	} else {
		headerPath.addRoundedRectangle(header.getX(), header.getY(), header.getWidth(), header.getHeight(), radius, radius, true, true, false, false);
	}

	g.setColour(findColour(osci::groupComponentHeaderColourId).withMultipliedAlpha(alpha));
	g.fillPath(headerPath);

	g.setColour(findColour(juce::GroupComponent::textColourId).withMultipliedAlpha(alpha));
	g.setFont(juce::Font(juce::FontOptions(15.0f)));
	g.drawText(getText(), header.reduced(32.0f, 0.0f).withY(header.getY() + 7.0f).withHeight(15.0f), juce::Justification::centredLeft, true);
}

void LuaConsole::resized() {
	auto topBar = getLocalBounds().removeFromTop(30);
	auto area = getLocalBounds().withTrimmedTop(30);
	area.removeFromBottom(juce::jmin(area.getHeight(), osci::LookAndFeel::RECT_RADIUS));
	console.setBounds(area);
	emptyConsoleLabel.setBounds(area);

	clearConsoleButton.setBounds(topBar.removeFromRight(30).withSizeKeepingCentre(20, 20));
	pauseConsoleButton.setBounds(topBar.removeFromRight(30).withSizeKeepingCentre(20, 20));
}

void LuaConsole::lookAndFeelChanged() {
	updateColours();
}

void LuaConsole::updateColours() {
	console.setColour(juce::CodeEditorComponent::backgroundColourId, findColour(osci::groupComponentBackgroundColourId, true));
	console.setColour(juce::CodeEditorComponent::defaultTextColourId, findColour(juce::CodeEditorComponent::defaultTextColourId, true));
	console.setColour(juce::CodeEditorComponent::highlightColourId, findColour(juce::CodeEditorComponent::highlightColourId, true));
	emptyConsoleLabel.setColour(juce::Label::textColourId, findColour(juce::GroupComponent::textColourId, true).withMultipliedAlpha(0.8f));
}

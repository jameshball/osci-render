#include "TxtComponent.h"
#include "PluginEditor.h"

TxtComponent::TxtComponent(OscirenderAudioProcessor& p, OscirenderAudioProcessorEditor& editor) : audioProcessor(p), pluginEditor(editor) {
	setText(".txt File Settings");

	addAndMakeVisible(font);
	addAndMakeVisible(bold);
	addAndMakeVisible(italic);

	for (int i = 0; i < installedFonts.size(); i++) {
        font.addItem(installedFonts[i], i + 1);
    }

	{
		juce::SpinLock::ScopedLockType lock(audioProcessor.fontLock);
		juce::String defaultFont = audioProcessor.font.getTypefaceName();
		int index = installedFonts.indexOf(defaultFont);
		if (index == -1) {
			index = 0;
		}
		font.setSelectedItemIndex(index);
		bold.setToggleState(audioProcessor.font.isBold(), juce::dontSendNotification);
		italic.setToggleState(audioProcessor.font.isItalic(), juce::dontSendNotification);
	}

	auto updateFont = [this]() {
		juce::SpinLock::ScopedLockType lock1(audioProcessor.parsersLock);
		juce::SpinLock::ScopedLockType lock2(audioProcessor.effectsLock);
		{
			juce::SpinLock::ScopedLockType lock3(audioProcessor.fontLock);
			audioProcessor.font.setTypefaceName(installedFonts[font.getSelectedItemIndex()]);
			audioProcessor.font.setBold(bold.getToggleState());
			audioProcessor.font.setItalic(italic.getToggleState());
		}
		
		audioProcessor.openFile(audioProcessor.currentFile);
    };

	font.onChange = updateFont;
	bold.onClick = updateFont;
	italic.onClick = updateFont;
}

void TxtComponent::resized() {
	auto area = getLocalBounds().withTrimmedTop(20).reduced(20);
	double rowHeight = 30;
	font.setBounds(area.removeFromTop(rowHeight));
	bold.setBounds(area.removeFromTop(rowHeight));
	italic.setBounds(area.removeFromTop(rowHeight));
}

#include "TxtComponent.h"
#include "PluginEditor.h"

TxtComponent::TxtComponent(OscirenderAudioProcessor& p, OscirenderAudioProcessorEditor& editor) : audioProcessor(p), pluginEditor(editor) {
	for (int i = 0; i < installedFonts.size(); i++) {
        addItem(installedFonts[i], i + 1);
    }

	update();

	onChange = [this]() {
		juce::SpinLock::ScopedLockType lock1(audioProcessor.parsersLock);
		juce::SpinLock::ScopedLockType lock2(audioProcessor.effectsLock);
        audioProcessor.font = juce::Font(installedFonts[getSelectedItemIndex()], audioProcessor.FONT_SIZE, juce::Font::plain);
    };
}

void TxtComponent::update() {
    juce::String defaultFont = audioProcessor.font.getTypefaceName();
    int index = installedFonts.indexOf(defaultFont);
	if (index == -1) {
		// If the processor's current font isn't in the list (name mismatch), try to find a monospace font.
		juce::String monoName = juce::Font::getDefaultMonospacedFontName();
		int monoIndex = installedFonts.indexOf(monoName);
		if (monoIndex != -1) {
			index = monoIndex;
		} else {
			// Heuristic: pick the first font whose name suggests monospace/code.
			static const char* monoHints[] = { "mono", "code", "consolas", "courier", "menlo", "andale", "lucida" };
			for (int i = 0; i < installedFonts.size() && index == -1; ++i) {
				auto name = installedFonts[i];
				for (auto hint : monoHints) {
					if (name.containsIgnoreCase(hint)) { index = i; break; }
				}
			}
			if (index == -1) {
				// Fall back to first font.
				index = 0;
			}
		}
    }
    setSelectedItemIndex(index);
}

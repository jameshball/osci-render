#include "TxtComponent.h"
#include "PluginEditor.h"

TxtComponent::TxtComponent(OscirenderAudioProcessor& p, OscirenderAudioProcessorEditor& editor) : audioProcessor(p), pluginEditor(editor) {
	setLookAndFeel(&fontPreviewLookAndFeel);

	for (int i = 0; i < installedFonts.size(); i++) {
        addItem(installedFonts[i], i + 1);
    }

	update();

	onChange = [this]() {
		juce::SpinLock::ScopedLockType lock1(audioProcessor.parsersLock);
		juce::SpinLock::ScopedLockType lock2(audioProcessor.effectsLock);
		if (installedFonts.isEmpty()) {
			return;
		}

		const auto selectedIndex = getSelectedItemIndex();
		if (selectedIndex < 0 || selectedIndex >= installedFonts.size()) {
			return;
		}

		audioProcessor.font = juce::Font(installedFonts[selectedIndex], audioProcessor.FONT_SIZE, juce::Font::plain);
    };
}

TxtComponent::~TxtComponent() {
	setLookAndFeel(nullptr);
}

TxtComponent::FontPreviewLookAndFeel::FontPreviewLookAndFeel() {
	OscirenderLookAndFeel::applyOscirenderColours(*this);
}

void TxtComponent::FontPreviewLookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool, int, int, int, int,
														juce::ComboBox& box) {
	OscirenderLookAndFeel::drawOscirenderComboBox(g, width, height, box);
}

void TxtComponent::FontPreviewLookAndFeel::positionComboBoxText(juce::ComboBox& box, juce::Label& label) {
	OscirenderLookAndFeel::positionOscirenderComboBoxText(*this, box, label);
}

void TxtComponent::FontPreviewLookAndFeel::drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator,
															 bool isActive, bool isHighlighted, bool isTicked, bool hasSubMenu,
															 const juce::String& text, const juce::String& shortcutKeyText,
															 const juce::Drawable* icon, const juce::Colour* textColour) {
	if (isSeparator) {
		auto r = area.reduced(8, 0);
		g.setColour(findColour(juce::PopupMenu::textColourId).withMultipliedAlpha(0.3f));
		g.drawLine((float)r.getX(), (float)r.getCentreY(), (float)r.getRight(), (float)r.getCentreY());
		return;
	}

	auto background = findColour(juce::PopupMenu::backgroundColourId);
	if (isHighlighted && isActive)
		background = findColour(juce::PopupMenu::highlightedBackgroundColourId);

	g.setColour(background);
	g.fillRect(area);

	auto r = area.reduced(8, 0);
	auto iconArea = r.removeFromLeft(r.getHeight());

	if (icon != nullptr) {
		icon->drawWithin(g, iconArea.toFloat(), juce::RectanglePlacement::centred, isActive ? 1.0f : 0.3f);
	} else if (isTicked) {
		g.setColour(findColour(juce::PopupMenu::textColourId));
		auto tick = getTickShape(0.5f);
		g.fillPath(tick, tick.getTransformToScaleToFit(iconArea.reduced(3).toFloat(), false));
	}

	auto baseFont = juce::LookAndFeel_V4::getPopupMenuFont();
	auto fontHeight = juce::jmin(baseFont.getHeight(), (float)r.getHeight() - 2.0f);
	juce::Font itemFont(text, fontHeight, juce::Font::plain);

	auto defaultTextColour = findColour(isHighlighted ? juce::PopupMenu::highlightedTextColourId
													  : juce::PopupMenu::textColourId);
	auto colour = (textColour != nullptr) ? *textColour : defaultTextColour;
	if (! isActive)
		colour = colour.withMultipliedAlpha(0.5f);

	g.setColour(colour);
	g.setFont(itemFont);

	if (shortcutKeyText.isNotEmpty()) {
		auto shortcutWidth = juce::jmin(100, r.getWidth() / 3);
		auto shortcutArea = r.removeFromRight(shortcutWidth);
		g.drawFittedText(shortcutKeyText, shortcutArea, juce::Justification::centredRight, 1);
	}

	if (hasSubMenu) {
		auto arrowArea = r.removeFromRight(r.getHeight());
		juce::Path path;
		path.startNewSubPath((float)arrowArea.getX(), (float)arrowArea.getY() + 3.0f);
		path.lineTo((float)arrowArea.getRight(), (float)arrowArea.getCentreY());
		path.lineTo((float)arrowArea.getX(), (float)arrowArea.getBottom() - 3.0f);
		g.strokePath(path, juce::PathStrokeType(1.5f));
	}

	g.drawFittedText(text, r, juce::Justification::centredLeft, 1);
}

void TxtComponent::update() {
    juce::String defaultFont = audioProcessor.font.getTypefaceName();
    int index = installedFonts.indexOf(defaultFont);
	if (installedFonts.isEmpty()) {
		clear(juce::dontSendNotification);
		setSelectedItemIndex(-1, juce::dontSendNotification);
		return;
	}
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
	setSelectedItemIndex(index, juce::dontSendNotification);
}

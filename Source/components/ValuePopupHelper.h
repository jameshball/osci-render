#pragma once

#include <JuceHeader.h>

// Reusable desktop tooltip popup — styled like the modulation depth popups.
// Shows a small floating label near a component. Call show() to display and
// hide() to dismiss. The popup is added to the desktop as a temporary window.
class ValuePopupHelper {
public:
    void show(juce::Component& anchor, const juce::String& text,
              juce::Colour bg = juce::Colour(0xff111111),
              juce::Colour fg = juce::Colours::white) {
        if (!popup) {
            popup = std::make_unique<juce::Label>();
            popup->setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
            popup->setJustificationType(juce::Justification::centred);
            popup->setInterceptsMouseClicks(false, false);
            popup->addToDesktop(juce::ComponentPeer::windowIsTemporary
                                | juce::ComponentPeer::windowIgnoresKeyPresses);
            popup->setAlwaysOnTop(true);
        }

        popup->setColour(juce::Label::backgroundColourId, bg);
        popup->setColour(juce::Label::textColourId, fg);
        popup->setText(text, juce::dontSendNotification);

        int popupW = juce::GlyphArrangement::getStringWidthInt(popup->getFont(), text) + 14;
        int popupH = 20;
        auto screenPos = anchor.localPointToGlobal(
            juce::Point<int>(anchor.getWidth() / 2, 0));
        popup->setBounds(screenPos.x - popupW / 2, screenPos.y - popupH - 4,
                         popupW, popupH);
        popup->setVisible(true);
    }

    void hide() { popup.reset(); }

    bool isVisible() const { return popup != nullptr && popup->isVisible(); }

private:
    std::unique_ptr<juce::Label> popup;
};

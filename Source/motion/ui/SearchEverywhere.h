#pragma once

#include <JuceHeader.h>
#include "MotionStyle.h"
#include "MotionIcons.h"

namespace motion::ui {
// Where a library's own search found nothing: the same words, searched
// across everything osci-motion does.
class SearchEverywhereButton final : public juce::TextButton {
public:
    SearchEverywhereButton() {
        setName("Search everything");
        style::makeQuiet(*this);
        // A link to the search field, which takes the keys itself.
        setWantsKeyboardFocus(false);
    }
    void setQuery(const juce::String& query) {
        setButtonText(juce::String::fromUTF8("Search everything for \xe2\x80\x9c") + query + juce::String::fromUTF8("\xe2\x80\x9d"));
    }
    // As wide as its glass and words, within `room`.
    int idealWidth(int room) const { return std::min(room, juce::roundToInt(juce::TextLayout::getStringWidth(style::body(), getButtonText())) + 40); }
    // A link: the glass and its words in the accent, a wash under the pointer.
    void paintButton(juce::Graphics& g, bool highlighted, bool down) override {
        auto area = getLocalBounds();
        if (highlighted || down) {
            g.setColour(down ? style::restFill() : style::hoverFill());
            g.fillRoundedRectangle(area.toFloat(), style::radiusControl);
        }
        area.reduce(8, 0);
        icons::draw(g, icons::Icon::search, area.removeFromLeft(16).toFloat(), style::accent(), style::iconSmall);
        area.removeFromLeft(6);
        g.setFont(style::body());
        g.setColour(style::accent());
        g.drawText(getButtonText(), area, juce::Justification::centredLeft, true);
    }
};
}

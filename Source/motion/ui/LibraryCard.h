#pragma once

#include "MotionStyle.h"

namespace motion::ui {
// The one card every library shows its things as (Sources, Effects,
// Modulators), so they read alike and as objects to pick up and drag: a box
// at rest (style::paintCard, as a picture tile is), brighter under the
// pointer, lit in the accent when selected. Inside, the same padding all
// round a 36 px picture in its well, then the name, with any caption under
// it. A card every `pitch` px, its face `face` px tall; a subclass paints
// the picture, and anything over the card (a badge, a row's buttons).
class LibraryCard : public juce::Component {
public:
    static constexpr int pitch = 48, face = pitch - style::gap, picture = 36, inset = (face - picture) / 2;
    // Where a face's picture and text go.
    static juce::Rectangle<int> pictureIn(juce::Rectangle<int> faceBounds) { return faceBounds.withTrimmedLeft(inset).withWidth(picture).withSizeKeepingCentre(picture, picture); }
    static juce::Rectangle<int> textIn(juce::Rectangle<int> faceBounds) { return faceBounds.withTrimmedLeft(inset + picture + style::padding).withTrimmedRight(inset); }

    LibraryCard() { setMouseCursor(juce::MouseCursor::DraggingHandCursor); }

    void setText(const juce::String& name, const juce::String& caption = {}, juce::Colour captionColour = {}) {
        if (cardName == name && cardCaption == caption && cardCaptionColour == captionColour) { return; }
        cardName = name;
        cardCaption = caption;
        cardCaptionColour = captionColour;
        repaint();
    }
    void setSelected(bool on) {
        if (selected == on) { return; }
        selected = on;
        repaint();
    }
    bool isSelected() const { return selected; }
    // A card taking no mouse events of its own (a list's row) is told when
    // the pointer is over it.
    void setHovered(bool on) { fade.setTarget(on); }
    // Space around the face within the card's bounds (a list's row fills
    // its whole slot).
    void setMargin(juce::BorderSize<int> around) {
        margin = around;
        repaint();
    }
    juce::Rectangle<int> faceBounds() const { return margin.subtractedFrom(getLocalBounds()); }
    juce::Rectangle<int> pictureArea() const { return pictureIn(faceBounds()); }

    void mouseEnter(const juce::MouseEvent&) override { fade.setTarget(true); }
    void mouseExit(const juce::MouseEvent&) override { fade.setTarget(false); }

    void paint(juce::Graphics& g) override final {
        const auto hover = fade.value();
        const auto bounds = faceBounds();
        style::paintCard(g, bounds.toFloat(), hover, selected);
        const auto well = pictureIn(bounds).toFloat();
        style::fillWell(g, well);
        paintPicture(g, well, selected ? 1.0f : hover);
        auto text = textIn(bounds);
        text.setRight(std::min(text.getRight(), getWidth() - textClearance()));
        g.setColour(osci::Colours::text());
        g.setFont(style::body());
        if (cardCaption.isEmpty()) {
            // The name alone sits centred; with a caption, the two lines
            // share the face.
            g.drawText(cardName, text, juce::Justification::centredLeft, true);
        } else {
            g.drawText(cardName, text.removeFromTop(text.getHeight() / 2 + 2), juce::Justification::bottomLeft, true);
            g.setColour(cardCaptionColour);
            g.setFont(style::caption());
            g.drawText(cardCaption, text, juce::Justification::topLeft, true);
        }
        paintOver(g, hover);
    }

protected:
    // The picture inside its well; `lit` from 0 to 1, as hovered or selected.
    virtual void paintPicture(juce::Graphics& g, juce::Rectangle<float> well, float lit) = 0;
    // How much of the card's right end the text keeps clear of.
    virtual int textClearance() const { return 0; }
    // Drawn over the rest: a badge, a row's buttons.
    virtual void paintOver(juce::Graphics&, float) {}

private:
    juce::String cardName, cardCaption;
    juce::Colour cardCaptionColour;
    bool selected = false;
    juce::BorderSize<int> margin;
    style::Fade fade {*this};
};
}

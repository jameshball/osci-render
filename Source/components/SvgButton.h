#pragma once
#include <JuceHeader.h>

class SvgButton : public juce::DrawableButton {
 public:
    SvgButton(juce::String name, juce::String svg, juce::String colour, juce::String colourOn) : juce::DrawableButton(name, juce::DrawableButton::ButtonStyle::ImageFitted) {
        auto doc = juce::XmlDocument::parse(svg);
        changeSvgColour(doc.get(), colour);
        normalImage = juce::Drawable::createFromSVG(*doc);
        changeSvgColour(doc.get(), colourOn);
        normalImageOn = juce::Drawable::createFromSVG(*doc);

        getLookAndFeel().setColour(juce::DrawableButton::backgroundOnColourId, juce::Colours::transparentWhite);

        if (colour != colourOn) {
            setClickingTogglesState(true);
        }
        setImages(normalImage.get(), nullptr, nullptr, nullptr, normalImageOn.get());
    }

    SvgButton(juce::String name, juce::String svg, juce::String colour) : SvgButton(name, svg, colour, colour) {}

    ~SvgButton() override {}
private:
    std::unique_ptr<juce::Drawable> normalImage;
    std::unique_ptr<juce::Drawable> normalImageOn;

    void changeSvgColour(juce::XmlElement* xml, juce::String colour) {
        forEachXmlChildElement(*xml, xmlnode) {
            xmlnode->setAttribute("fill", colour);
        }
    }
};

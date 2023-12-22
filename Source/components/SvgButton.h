#pragma once
#include <JuceHeader.h>

class SvgButton : public juce::DrawableButton, public juce::AudioProcessorParameter::Listener, public juce::AsyncUpdater {
 public:
    SvgButton(juce::String name, juce::String svg, juce::String colour, juce::String colourOn, BooleanParameter* toggle = nullptr) : juce::DrawableButton(name, juce::DrawableButton::ButtonStyle::ImageFitted), toggle(toggle) {
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

        if (toggle != nullptr) {
            toggle->addListener(this);
            setToggleState(toggle->getBoolValue(), juce::NotificationType::dontSendNotification);
        }
    }

    SvgButton(juce::String name, juce::String svg, juce::String colour) : SvgButton(name, svg, colour, colour) {}

    ~SvgButton() override {
        if (toggle != nullptr) {
            toggle->removeListener(this);
        }
    }

    void parameterValueChanged(int parameterIndex, float newValue) override {
        triggerAsyncUpdate();
    }

    void parameterGestureChanged(int parameterIndex, bool gestureIsStarting) override {}

    void handleAsyncUpdate() override {
        setToggleState(toggle->getBoolValue(), juce::NotificationType::dontSendNotification);
    }
private:
    std::unique_ptr<juce::Drawable> normalImage;
    std::unique_ptr<juce::Drawable> normalImageOn;
    BooleanParameter* toggle;

    void changeSvgColour(juce::XmlElement* xml, juce::String colour) {
        forEachXmlChildElement(*xml, xmlnode) {
            xmlnode->setAttribute("fill", colour);
        }
    }
};

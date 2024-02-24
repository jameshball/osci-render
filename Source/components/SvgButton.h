#pragma once
#include <JuceHeader.h>

class SvgButton : public juce::DrawableButton, public juce::AudioProcessorParameter::Listener, public juce::AsyncUpdater {
 public:
    SvgButton(juce::String name, juce::String svg, juce::Colour colour, juce::Colour colourOn, BooleanParameter* toggle = nullptr) : juce::DrawableButton(name, juce::DrawableButton::ButtonStyle::ImageFitted), toggle(toggle) {
        auto doc = juce::XmlDocument::parse(svg);

		setMouseCursor(juce::MouseCursor::PointingHandCursor);
        
        changeSvgColour(doc.get(), colour);
        normalImage = juce::Drawable::createFromSVG(*doc);
		changeSvgColour(doc.get(), colour.withBrightness(0.7f));
		overImage = juce::Drawable::createFromSVG(*doc);
		changeSvgColour(doc.get(), colour.withBrightness(0.5f));
		downImage = juce::Drawable::createFromSVG(*doc);
		changeSvgColour(doc.get(), colour.withBrightness(0.3f));
		disabledImage = juce::Drawable::createFromSVG(*doc);
        
        changeSvgColour(doc.get(), colourOn);
        normalImageOn = juce::Drawable::createFromSVG(*doc);
		changeSvgColour(doc.get(), colourOn.withBrightness(0.7f));
		overImageOn = juce::Drawable::createFromSVG(*doc);
		changeSvgColour(doc.get(), colourOn.withBrightness(0.5f));
		downImageOn = juce::Drawable::createFromSVG(*doc);
		changeSvgColour(doc.get(), colourOn.withBrightness(0.3f));
		disabledImageOn = juce::Drawable::createFromSVG(*doc);

        getLookAndFeel().setColour(juce::DrawableButton::backgroundOnColourId, juce::Colours::transparentWhite);

        if (colour != colourOn) {
            setClickingTogglesState(true);
        }
		setImages(normalImage.get(), overImage.get(), downImage.get(), disabledImage.get(), normalImageOn.get(), overImageOn.get(), downImageOn.get(), disabledImageOn.get());

        if (toggle != nullptr) {
            toggle->addListener(this);
            setToggleState(toggle->getBoolValue(), juce::NotificationType::dontSendNotification);
        }
    }

    SvgButton(juce::String name, juce::String svg, juce::Colour colour) : SvgButton(name, svg, colour, colour) {}

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
	std::unique_ptr<juce::Drawable> overImage;
	std::unique_ptr<juce::Drawable> downImage;
	std::unique_ptr<juce::Drawable> disabledImage;

    std::unique_ptr<juce::Drawable> normalImageOn;
	std::unique_ptr<juce::Drawable> overImageOn;
	std::unique_ptr<juce::Drawable> downImageOn;
    std::unique_ptr<juce::Drawable> disabledImageOn;

    BooleanParameter* toggle;

    void changeSvgColour(juce::XmlElement* xml, juce::Colour colour) {
        forEachXmlChildElement(*xml, xmlnode) {
            xmlnode->setAttribute("fill", '#' + colour.toDisplayString(false));
        }
    }
};

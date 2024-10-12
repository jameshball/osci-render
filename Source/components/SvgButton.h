#pragma once
#include <JuceHeader.h>
#include "../audio/BooleanParameter.h"

class SvgButton : public juce::DrawableButton, public juce::AudioProcessorParameter::Listener, public juce::AsyncUpdater {
 public:
    SvgButton(juce::String name, juce::String svg, juce::Colour colour, juce::Colour colourOn, BooleanParameter* toggle = nullptr) : juce::DrawableButton(name, juce::DrawableButton::ButtonStyle::ImageFitted), toggle(toggle) {
        auto doc = juce::XmlDocument::parse(svg);
        
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
        
        path = normalImage->getOutlineAsPath();

        getLookAndFeel().setColour(juce::DrawableButton::backgroundOnColourId, juce::Colours::transparentWhite);

        if (colour != colourOn) {
            setClickingTogglesState(true);
        }
		setImages(normalImage.get(), overImage.get(), downImage.get(), disabledImage.get(), normalImageOn.get(), overImageOn.get(), downImageOn.get(), disabledImageOn.get());

        if (toggle != nullptr) {
            toggle->addListener(this);
            setToggleState(toggle->getBoolValue(), juce::NotificationType::dontSendNotification);
            setTooltip(toggle->getDescription());
        }
        
        updater.addAnimator(pulse);
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

    void mouseEnter(const juce::MouseEvent& e) override {
        juce::DrawableButton::mouseEnter(e);

        if (isEnabled()) {
            setMouseCursor(juce::MouseCursor::PointingHandCursor);
        }
    }

    void mouseExit(const juce::MouseEvent& e) override {
        juce::DrawableButton::mouseExit(e);
        setMouseCursor(juce::MouseCursor::NormalCursor);
    }
    
    void setPulseAnimation(bool pulseUsed) {
        this->pulseUsed = pulseUsed;
    }
    
    void paintOverChildren(juce::Graphics& g) override {
        if (pulseUsed && getToggleState()) {
            g.setColour(juce::Colours::black.withAlpha(colourFade / 1.5f));
            g.fillPath(path);
        }
    }
    
    void buttonStateChanged() override {
        juce::DrawableButton::buttonStateChanged();
        if (pulseUsed && getToggleState() != prevToggleState) {
            if (getToggleState()) {
                pulse.start();
            } else {
                pulse.complete();
                colourFade = 1.0;
            }
            prevToggleState = getToggleState();
        }
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
    
    juce::VBlankAnimatorUpdater updater{this};
    float colourFade = 0.0;
    bool pulseUsed = false;
    bool prevToggleState = false;
    juce::Path path;
    juce::Animator pulse = juce::ValueAnimatorBuilder {}
        .withEasing([] (float t) { return std::sin(3.14159 * t) / 2 + 0.5; })
        .withDurationMs(500)
        .runningInfinitely()
        .withValueChangedCallback([this] (auto value) {
            colourFade = value;
            repaint();
        })
        .build();

    void changeSvgColour(juce::XmlElement* xml, juce::Colour colour) {
        forEachXmlChildElement(*xml, xmlnode) {
            xmlnode->setAttribute("fill", '#' + colour.toDisplayString(false));
        }
    }
};

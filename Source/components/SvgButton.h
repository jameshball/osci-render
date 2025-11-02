#pragma once
#include <JuceHeader.h>

class SvgButton : public juce::DrawableButton, public juce::AudioProcessorParameter::Listener, public juce::AsyncUpdater {
 public:
    SvgButton(juce::String name, juce::String svg, juce::Colour colour, juce::Colour colourOn, osci::BooleanParameter* toggle = nullptr, juce::String toggledSvg = "") : juce::DrawableButton(name, juce::DrawableButton::ButtonStyle::ImageFitted), toggle(toggle) {
        auto doc = juce::XmlDocument::parse(svg);
        
        changeSvgColour(doc.get(), colour);
        normalImage = juce::Drawable::createFromSVG(*doc);
        changeSvgColour(doc.get(), colour.withBrightness(0.7f));
        overImage = juce::Drawable::createFromSVG(*doc);
        changeSvgColour(doc.get(), colour.withBrightness(0.5f));
        downImage = juce::Drawable::createFromSVG(*doc);
        changeSvgColour(doc.get(), colour.withBrightness(0.3f));
        disabledImage = juce::Drawable::createFromSVG(*doc);
        
        // If a toggled SVG is provided, use it for the "on" state images
        if (toggledSvg.isNotEmpty()) {
            auto toggledDoc = juce::XmlDocument::parse(toggledSvg);
            changeSvgColour(toggledDoc.get(), colourOn);
            normalImageOn = juce::Drawable::createFromSVG(*toggledDoc);
            changeSvgColour(toggledDoc.get(), colourOn.withBrightness(0.7f));
            overImageOn = juce::Drawable::createFromSVG(*toggledDoc);
            changeSvgColour(toggledDoc.get(), colourOn.withBrightness(0.5f));
            downImageOn = juce::Drawable::createFromSVG(*toggledDoc);
            changeSvgColour(toggledDoc.get(), colourOn.withBrightness(0.3f));
            disabledImageOn = juce::Drawable::createFromSVG(*toggledDoc);
        } else {
            changeSvgColour(doc.get(), colourOn);
            normalImageOn = juce::Drawable::createFromSVG(*doc);
            changeSvgColour(doc.get(), colourOn.withBrightness(0.7f));
            overImageOn = juce::Drawable::createFromSVG(*doc);
            changeSvgColour(doc.get(), colourOn.withBrightness(0.5f));
            downImageOn = juce::Drawable::createFromSVG(*doc);
            changeSvgColour(doc.get(), colourOn.withBrightness(0.3f));
            disabledImageOn = juce::Drawable::createFromSVG(*doc);
        }
        
        basePath = normalImage->getOutlineAsPath();

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
    
    void resized() override {
        juce::DrawableButton::resized();
        if (pulseUsed) {
            resizedPath = basePath;
            resizedPath.applyTransform(getImageTransform());
            repaint();
        }
    }
    
    void paintOverChildren(juce::Graphics& g) override {
        if (pulseUsed && getToggleState()) {
            g.setColour(juce::Colours::black.withAlpha(juce::jlimit(0.0f, 1.0f, colourFade / 1.5f)));
            g.fillPath(resizedPath);
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

    osci::BooleanParameter* toggle;
    
    juce::VBlankAnimatorUpdater updater{this};
    float colourFade = 0.0;
    bool pulseUsed = false;
    bool prevToggleState = false;
    juce::Path basePath;
    juce::Path resizedPath;
    juce::AffineTransform imageTransform; // Transform applied to all state images
    juce::Animator pulse = juce::ValueAnimatorBuilder {}
        .withEasing([] (float t) { return juce::dsp::FastMathApproximations::sin(3.14159 * t) / 2 + 0.5; })
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

public:
    // Allows callers to adjust the placement/scale/rotation of the SVG within the button.
    void setImageTransform(const juce::AffineTransform& t) {
        imageTransform = juce::RectanglePlacement(juce::RectanglePlacement::centred).getTransformToFit(normalImage->getDrawableBounds(), getImageBounds()).followedBy(t);
        if (getLocalBounds().isEmpty()) {
            return;
        }
        setButtonStyle(juce::DrawableButton::ButtonStyle::ImageRaw);
        applyImageTransform();
        // Keep the pulse overlay in sync
        resized();
    }

    juce::AffineTransform getImageTransform() const { return imageTransform; }

private:
    void applyImageTransform() {
        auto apply = [this](std::unique_ptr<juce::Drawable>& d) {
            if (d != nullptr) {
                d->setTransform(imageTransform);
            }
        };
        apply(normalImage);
        apply(overImage);
        apply(downImage);
        apply(disabledImage);
        apply(normalImageOn);
        apply(overImageOn);
        apply(downImageOn);
        apply(disabledImageOn);

        setImages(normalImage.get(), overImage.get(), downImage.get(), disabledImage.get(), normalImageOn.get(), overImageOn.get(), downImageOn.get(), disabledImageOn.get());
    }
};

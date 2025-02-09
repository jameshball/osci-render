#pragma once

#define VERSION_HINT 2

#include <JuceHeader.h>
#include "../components/EffectComponent.h"
#include "../components/SvgButton.h"
#include "../LookAndFeel.h"
#include "../components/SwitchButton.h"
#include "../audio/SmoothEffect.h"
#include "../audio/StereoEffect.h"

enum class ScreenOverlay : int {
    INVALID = -1,
    Empty = 1,
    Graticule = 2,
    Smudged = 3,
    SmudgedGraticule = 4,
#if SOSCI_FEATURES
    Real = 5,
    VectorDisplay = 6,
    MAX = 6,
#else
    MAX = 4,
#endif
};

class ScreenOverlayParameter : public IntParameter {
public:
    ScreenOverlayParameter(juce::String name, juce::String id, int versionHint, ScreenOverlay value) : IntParameter(name, id, versionHint, (int) value, 1, (int)ScreenOverlay::MAX) {}

    juce::String getText(float value, int maximumStringLength = 100) const override {
        switch ((ScreenOverlay)(int)getUnnormalisedValue(value)) {
            case ScreenOverlay::Empty:
                return "Empty";
            case ScreenOverlay::Graticule:
                return "Graticule";
            case ScreenOverlay::Smudged:
                return "Smudged";
            case ScreenOverlay::SmudgedGraticule:
                return "Smudged Graticule";
#if SOSCI_FEATURES
            case ScreenOverlay::Real:
                return "Real Oscilloscope";
            case ScreenOverlay::VectorDisplay:
                return "Vector Display";
#endif
            default:
                return "Unknown";
        }
    }

    float getValueForText(const juce::String& text) const override {
        int unnormalisedValue;
        if (text == "Empty") {
            unnormalisedValue = (int)ScreenOverlay::Empty;
        } else if (text == "Graticule") {
            unnormalisedValue = (int)ScreenOverlay::Graticule;
        } else if (text == "Smudged") {
            unnormalisedValue = (int)ScreenOverlay::Smudged;
        } else if (text == "Smudged Graticule") {
            unnormalisedValue = (int)ScreenOverlay::SmudgedGraticule;
#if SOSCI_FEATURES
        } else if (text == "Real Oscilloscope") {
            unnormalisedValue = (int)ScreenOverlay::Real;
        } else if (text == "Vector Display") {
            unnormalisedValue = (int)ScreenOverlay::VectorDisplay;
#endif
        } else {
            unnormalisedValue = (int)ScreenOverlay::Empty;
        }
        return getNormalisedValue(unnormalisedValue);
    }

    void save(juce::XmlElement* xml) {
        xml->setAttribute("screenOverlay", getText(getValue()));
    }

    void load(juce::XmlElement* xml) {
        setValueNotifyingHost(getValueForText(xml->getStringAttribute("screenOverlay")));
    }
    
#if SOSCI_FEATURES
    bool isRealisticDisplay() {
        ScreenOverlay type = (ScreenOverlay)(int)getValueUnnormalised();
        return type == ScreenOverlay::Real || type == ScreenOverlay::VectorDisplay;
    }
#endif
};

class VisualiserParameters {
public:
    ScreenOverlayParameter* screenOverlay = new ScreenOverlayParameter("Screen Overlay", "screenOverlay", VERSION_HINT, ScreenOverlay::SmudgedGraticule);
    BooleanParameter* upsamplingEnabled = new BooleanParameter("Upsample Audio", "upsamplingEnabled", VERSION_HINT, true, "Upsamples the audio before visualising it to make it appear more realistic, at the expense of performance.");
    BooleanParameter* sweepEnabled = new BooleanParameter("Sweep", "sweepEnabled", VERSION_HINT, false, "Plots the audio signal over time, sweeping from left to right");
    BooleanParameter* visualiserFullScreen = new BooleanParameter("Visualiser Fullscreen", "visualiserFullScreen", VERSION_HINT, false, "Makes the software visualiser fullscreen.");

#if SOSCI_FEATURES
    BooleanParameter* flipVertical = new BooleanParameter("Flip Vertical", "flipVertical", VERSION_HINT, false, "Flips the visualiser vertically.");
    BooleanParameter* flipHorizontal = new BooleanParameter("Flip Horizontal", "flipHorizontal", VERSION_HINT, false, "Flips the visualiser horizontally.");
    BooleanParameter* goniometer = new BooleanParameter("Goniometer", "goniometer", VERSION_HINT, false, "Rotates the visualiser to replicate a goniometer display to show the phase relationship between two channels.");

    std::shared_ptr<Effect> screenSaturationEffect = std::make_shared<Effect>(
        new EffectParameter(
            "Screen Saturation",
            "Controls how saturated the colours are on the oscilloscope screen.",
            "screenSaturation",
            VERSION_HINT, 1.0, 0.0, 5.0
        )
    );
    std::shared_ptr<Effect> screenHueEffect = std::make_shared<Effect>(
        new EffectParameter(
            "Screen Hue",
            "Controls the hue shift of the oscilloscope screen.",
            "screenHue",
            VERSION_HINT, 0, 0, 359, 1
        )
    );
    std::shared_ptr<Effect> afterglowEffect = std::make_shared<Effect>(
        new EffectParameter(
            "Afterglow",
            "Controls how quickly the image disappears after glowing brightly. Closely related to persistence.",
            "afterglow",
            VERSION_HINT, 1.0, 0.0, 10.0
        )
    );
    std::shared_ptr<Effect> overexposureEffect = std::make_shared<Effect>(
        new EffectParameter(
            "Overexposure",
            "Controls at which point the line becomes overexposed and clips, turning white.",
            "overexposure",
            VERSION_HINT, 0.5, 0.0, 1.0
        )
    );
    std::shared_ptr<StereoEffect> stereoEffectApplication = std::make_shared<StereoEffect>();
    std::shared_ptr<Effect> stereoEffect = std::make_shared<Effect>(
        stereoEffectApplication,
        new EffectParameter(
            "Stereo",
            "Turns mono audio that is uninteresting to visualise into stereo audio that is interesting to visualise.",
            "stereo",
            VERSION_HINT, 0.0, 0.0, 1.0
        )
    );
    std::shared_ptr<Effect> scaleEffect = std::make_shared<Effect>(
        [this](int index, OsciPoint input, const std::vector<std::atomic<double>>& values, double sampleRate) {
            input.scale(values[0].load(), values[1].load(), 1.0);
            return input;
        }, std::vector<EffectParameter*>{
        new EffectParameter(
            "X Scale",
            "Controls the horizontal scale of the oscilloscope display.",
            "xScale",
            VERSION_HINT, 1.0, -3.0, 3.0
        ),
            new EffectParameter(
                "Y Scale",
                "Controls the vertical scale of the oscilloscope display.",
                "yScale",
                VERSION_HINT, 1.0, -3.0, 3.0
            ),
    });
    std::shared_ptr<Effect> offsetEffect = std::make_shared<Effect>(
        [this](int index, OsciPoint input, const std::vector<std::atomic<double>>& values, double sampleRate) {
            input.translate(values[0].load(), values[1].load(), 0.0);
            return input;
        }, std::vector<EffectParameter*>{
        new EffectParameter(
            "X Offset",
            "Controls the horizontal position offset of the oscilloscope display.",
            "xOffset",
            VERSION_HINT, 0.0, -1.0, 1.0
        ),
        new EffectParameter(
            "Y Offset",
            "Controls the vertical position offset of the oscilloscope display.",
            "yOffset",
            VERSION_HINT, 0.0, -1.0, 1.0
        ),
    });
    std::shared_ptr<Effect> shutterLengthEffect = std::make_shared<Effect>(
        new EffectParameter(
            "Shutter Length",
            "Controls the percentage of time the camera shutter is open over a frame. This can be beneficial when the drawing frequency and frame rate are in sync.",
            "shutterLength",
            VERSION_HINT, 0.0, 0.0, 1.0
        )
    );
#endif

    std::shared_ptr<Effect> persistenceEffect = std::make_shared<Effect>(
        new EffectParameter(
            "Persistence",
            "Controls how long the light glows for on the oscilloscope display.",
            "persistence",
            VERSION_HINT, 0.5, 0, 6.0
        )
    );
    std::shared_ptr<Effect> hueEffect = std::make_shared<Effect>(
        new EffectParameter(
            "Line Hue",
            "Controls the hue of the beam of the oscilloscope.",
            "hue",
            VERSION_HINT, 125, 0, 359, 1
        )
    );
    std::shared_ptr<Effect> intensityEffect = std::make_shared<Effect>(
        new EffectParameter(
            "Line Intensity",
            "Controls how bright the electron beam of the oscilloscope is.",
            "intensity",
            VERSION_HINT, 5.0, 0.0, 10.0
        )
    );
    std::shared_ptr<Effect> lineSaturationEffect = std::make_shared<Effect>(
        new EffectParameter(
            "Line Saturation",
            "Controls how saturated the colours are on the oscilloscope lines.",
            "lineSaturation",
            VERSION_HINT, 1.0, 0.0, 5.0
        )
    );
    std::shared_ptr<Effect> focusEffect = std::make_shared<Effect>(
        new EffectParameter(
            "Focus",
            "Controls how focused the electron beam of the oscilloscope is.",
            "focus",
            VERSION_HINT, 1.0, 0.3, 10.0
        )
    );
    std::shared_ptr<Effect> noiseEffect = std::make_shared<Effect>(
        new EffectParameter(
            "Noise",
            "Controls how much noise/grain is added to the oscilloscope display.",
            "noise",
            VERSION_HINT, 0.0, 0.0, 1.0
        )
    );
    std::shared_ptr<Effect> glowEffect = std::make_shared<Effect>(
        new EffectParameter(
            "Glow",
            "Controls how much the light glows on the oscilloscope display.",
            "glow",
            VERSION_HINT, 0.3, 0.0, 1.0
        )
    );
    std::shared_ptr<Effect> ambientEffect = std::make_shared<Effect>(
        new EffectParameter(
            "Ambient Light",
            "Controls how much ambient light is added to the oscilloscope display.",
            "ambient",
            VERSION_HINT, 0.7, 0.0, 5.0
        )
    );
    std::shared_ptr<Effect> smoothEffect = std::make_shared<Effect>(
        std::make_shared<SmoothEffect>(),
        new EffectParameter(
            "Smoothing",
            "This works as a low-pass frequency filter, effectively reducing the sample rate of the audio being visualised.",
            "visualiserSmoothing",
            VERSION_HINT, 0, 0.0, 1.0
        )
    );
    std::shared_ptr<Effect> sweepMsEffect = std::make_shared<Effect>(
        new EffectParameter(
            "Sweep (ms)",
            "The number of milliseconds it takes for the oscilloscope to sweep from left to right.",
            "sweepMs",
            VERSION_HINT, 10.0, 0.0, 1000.0
        )
    );
    std::shared_ptr<Effect> triggerValueEffect = std::make_shared<Effect>(
        new EffectParameter(
            "Trigger Value",
            "The trigger value sets the signal level that starts waveform capture to display a stable waveform.",
            "triggerValue",
            VERSION_HINT, 0.0, -1.0, 1.0
        )
    );
    
    std::vector<std::shared_ptr<Effect>> effects = {
        persistenceEffect,
        hueEffect,
        intensityEffect,
        lineSaturationEffect,
        focusEffect,
        noiseEffect,
        glowEffect,
        ambientEffect,
        sweepMsEffect,
        triggerValueEffect,
#if SOSCI_FEATURES
        afterglowEffect,
        screenSaturationEffect,
        screenHueEffect,
        overexposureEffect,
        shutterLengthEffect,
#endif
    };
    std::vector<std::shared_ptr<Effect>> audioEffects = {
        smoothEffect,
#if SOSCI_FEATURES
        stereoEffect,
        scaleEffect,
        offsetEffect,
#endif
    };
    std::vector<BooleanParameter*> booleans = {
        upsamplingEnabled,
        visualiserFullScreen,
        sweepEnabled,
#if SOSCI_FEATURES
        flipVertical,
        flipHorizontal,
        goniometer,
#endif
    };
    std::vector<IntParameter*> integers = {
        screenOverlay,
    };
};

class GroupedSettings : public juce::GroupComponent {
public:
    GroupedSettings(std::vector<std::shared_ptr<EffectComponent>> effects, juce::String label) : effects(effects), juce::GroupComponent(label, label) {
        for (auto effect : effects) {
            addAndMakeVisible(effect.get());
            effect->setSliderOnValueChange();
        }
        
        setColour(groupComponentBackgroundColourId, Colours::veryDark.withMultipliedBrightness(3.0));
    }
    
    void resized() override {
        auto area = getLocalBounds();
        area.removeFromTop(35);
        double rowHeight = 30;
        
        for (auto effect : effects) {
            effect->setBounds(area.removeFromTop(rowHeight));
        }
    }
    
    int getHeight() {
        return 40 + effects.size() * 30;
    }

private:
    std::vector<std::shared_ptr<EffectComponent>> effects;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GroupedSettings)
};

class VisualiserSettings : public juce::Component, public juce::AudioProcessorParameter::Listener {
public:
    VisualiserSettings(VisualiserParameters&, int numChannels = 2);
    ~VisualiserSettings();

    void paint(juce::Graphics& g) override;
    void resized() override;
    void parameterValueChanged(int parameterIndex, float newValue) override;
    void parameterGestureChanged(int parameterIndex, bool gestureIsStarting) override;
    
    double getIntensity() {
        return parameters.intensityEffect->getActualValue() / 100;
    }
    
    double getPersistence() {
        return parameters.persistenceEffect->getActualValue() - 1.33;
    }
    
    double getHue() {
        return parameters.hueEffect->getActualValue();
    }
    
    double getLineSaturation() {
        return parameters.lineSaturationEffect->getActualValue();
    }

#if SOSCI_FEATURES
    double getScreenSaturation() {
        return parameters.screenSaturationEffect->getActualValue();
    }
    
    double getScreenHue() {
        return parameters.screenHueEffect->getActualValue();
    }
    
    double getAfterglow() {
        return parameters.afterglowEffect->getActualValue();
    }
    
    double getOverexposure() {
        return parameters.overexposureEffect->getActualValue();
    }

    bool isFlippedVertical() {
        return parameters.flipVertical->getBoolValue();
    }

    bool isFlippedHorizontal() {
        return parameters.flipHorizontal->getBoolValue();
    }
    
    bool isGoniometer() {
        return parameters.goniometer->getBoolValue();
    }
    
    double getShutterLength() {
        return parameters.shutterLengthEffect->getActualValue();
    }
#endif
    
    double getFocus() {
        return parameters.focusEffect->getActualValue() / 100;
    }
    
    double getNoise() {
        return parameters.noiseEffect->getActualValue() / 5;
    }
    
    double getGlow() {
        return parameters.glowEffect->getActualValue() * 3;
    }
    
    double getAmbient() {
        return parameters.ambientEffect->getActualValue();
    }
    
    ScreenOverlay getScreenOverlay() {
        return (ScreenOverlay)parameters.screenOverlay->getValueUnnormalised();
    }
    
    bool getUpsamplingEnabled() {
        return parameters.upsamplingEnabled->getBoolValue();
    }
    
    bool isSweepEnabled() {
        return parameters.sweepEnabled->getBoolValue();
    }
    
    double getSweepSeconds() {
        return parameters.sweepMsEffect->getActualValue() / 1000.0;
    }
    
    double getTriggerValue() {
        return parameters.triggerValueEffect->getActualValue();
    }

    VisualiserParameters& parameters;
    int numChannels;

private:
    GroupedSettings lineColour{
        std::vector<std::shared_ptr<EffectComponent>>{
            std::make_shared<EffectComponent>(*parameters.hueEffect),
            std::make_shared<EffectComponent>(*parameters.lineSaturationEffect),
            std::make_shared<EffectComponent>(*parameters.intensityEffect),
        },
        "Line Colour"
    };
    
#if SOSCI_FEATURES
    GroupedSettings screenColour{
        std::vector<std::shared_ptr<EffectComponent>>{
            std::make_shared<EffectComponent>(*parameters.screenHueEffect),
            std::make_shared<EffectComponent>(*parameters.screenSaturationEffect),
            std::make_shared<EffectComponent>(*parameters.ambientEffect),
        },
        "Screen Colour"
    };
#endif
    
    GroupedSettings lightEffects{
        std::vector<std::shared_ptr<EffectComponent>>{
            std::make_shared<EffectComponent>(*parameters.persistenceEffect),
            std::make_shared<EffectComponent>(*parameters.focusEffect),
            std::make_shared<EffectComponent>(*parameters.glowEffect),
#if SOSCI_FEATURES
            std::make_shared<EffectComponent>(*parameters.afterglowEffect),
            std::make_shared<EffectComponent>(*parameters.overexposureEffect),
            std::make_shared<EffectComponent>(*parameters.shutterLengthEffect),
#else
            std::make_shared<EffectComponent>(*parameters.ambientEffect),
#endif
        },
        "Light Effects"
    };
    
    GroupedSettings videoEffects{
        std::vector<std::shared_ptr<EffectComponent>>{
            std::make_shared<EffectComponent>(*parameters.noiseEffect),
        },
        "Video Effects"
    };
    
    GroupedSettings lineEffects{
        std::vector<std::shared_ptr<EffectComponent>>{
            std::make_shared<EffectComponent>(*parameters.smoothEffect),
#if SOSCI_FEATURES
            std::make_shared<EffectComponent>(*parameters.stereoEffect),
#endif
        },
        "Line Effects"
    };
    
    EffectComponent sweepMs{*parameters.sweepMsEffect};
    EffectComponent triggerValue{*parameters.triggerValueEffect};
    
    juce::Label screenOverlayLabel{"Screen Overlay", "Screen Overlay"};
    juce::ComboBox screenOverlay;
    
    jux::SwitchButton upsamplingToggle{parameters.upsamplingEnabled};
    jux::SwitchButton sweepToggle{parameters.sweepEnabled};

#if SOSCI_FEATURES
    GroupedSettings positionSize{
        std::vector<std::shared_ptr<EffectComponent>>{
            std::make_shared<EffectComponent>(*parameters.scaleEffect, 0),
            std::make_shared<EffectComponent>(*parameters.scaleEffect, 1),
            std::make_shared<EffectComponent>(*parameters.offsetEffect, 0),
            std::make_shared<EffectComponent>(*parameters.offsetEffect, 1),
        },
        "Line Position & Scale"
    };

    jux::SwitchButton flipVerticalToggle{parameters.flipVertical};
    jux::SwitchButton flipHorizontalToggle{parameters.flipHorizontal};
    jux::SwitchButton goniometerToggle{parameters.goniometer};
#endif

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VisualiserSettings)
};

class ScrollableComponent : public juce::Component {
public:
    ScrollableComponent(juce::Component& component) : component(component) {
        addAndMakeVisible(viewport);
        viewport.setViewedComponent(&component, false);
        viewport.setScrollBarsShown(true, false, true, false);
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(Colours::darker);
    }

    void resized() override {
        viewport.setBounds(getLocalBounds());
    }

private:
    juce::Viewport viewport;
    juce::Component& component;
};

class SettingsWindow : public juce::DialogWindow {
public:
    SettingsWindow(juce::String name, juce::Component& component) : juce::DialogWindow(name, Colours::darker, true, true), component(component) {
        setContentComponent(&viewport);
        centreWithSize(550, 500);
        setResizeLimits(getWidth(), 300, getWidth(), 1080);
        setResizable(true, false);
        viewport.setColour(juce::ScrollBar::trackColourId, juce::Colours::white);
        viewport.setViewedComponent(&component, false);
        viewport.setScrollBarsShown(true, false, true, false);
        setAlwaysOnTop(true);
    }

    void closeButtonPressed() override {
        setVisible(false);
    }

private:
    juce::Viewport viewport;
    juce::Component& component;
};

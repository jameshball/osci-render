#pragma once

#define VERSION_HINT 2

#include <JuceHeader.h>
#include "../components/EffectComponent.h"
#include "../components/SvgButton.h"
#include "../LookAndFeel.h"
#include "../components/SwitchButton.h"
#include "../audio/SmoothEffect.h"

enum class ScreenOverlay : int {
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
            "Hue",
            "Controls the hue/colour of the oscilloscope display.",
            "hue",
            VERSION_HINT, 125, 0, 359, 1
        )
    );
    std::shared_ptr<Effect> intensityEffect = std::make_shared<Effect>(
        new EffectParameter(
            "Intensity",
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
    std::shared_ptr<Effect> screenSaturationEffect = std::make_shared<Effect>(
        new EffectParameter(
            "Screen Saturation",
            "Controls how saturated the colours are on the oscilloscope screen.",
            "screenSaturation",
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
    
    std::vector<std::shared_ptr<Effect>> effects = {persistenceEffect, hueEffect, intensityEffect, lineSaturationEffect, screenSaturationEffect, focusEffect, noiseEffect, glowEffect, ambientEffect, sweepMsEffect, triggerValueEffect};
    std::vector<BooleanParameter*> booleans = {upsamplingEnabled, visualiserFullScreen, sweepEnabled};
    std::vector<IntParameter*> integers = {screenOverlay};
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

    double getScreenSaturation() {
        return parameters.screenSaturationEffect->getActualValue();
    }
    
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
    EffectComponent intensity{*parameters.intensityEffect};
    EffectComponent persistence{*parameters.persistenceEffect};
    EffectComponent hue{*parameters.hueEffect};
    EffectComponent lineSaturation{*parameters.lineSaturationEffect};
    EffectComponent screenSaturation{*parameters.screenSaturationEffect};
    EffectComponent focus{*parameters.focusEffect};
    EffectComponent noise{*parameters.noiseEffect};
    EffectComponent glow{*parameters.glowEffect};
    EffectComponent ambient{*parameters.ambientEffect};
    EffectComponent smooth{*parameters.smoothEffect};
    EffectComponent sweepMs{*parameters.sweepMsEffect};
    EffectComponent triggerValue{*parameters.triggerValueEffect};
    
    juce::Label screenOverlayLabel{"Screen Overlay", "Screen Overlay"};
    juce::ComboBox screenOverlay;
    
    jux::SwitchButton upsamplingToggle{parameters.upsamplingEnabled};
    jux::SwitchButton sweepToggle{parameters.sweepEnabled};

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
        setResizable(false, false);
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

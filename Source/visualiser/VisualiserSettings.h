#pragma once

#define VERSION_HINT 2

#include <JuceHeader.h>
#include "../components/EffectComponent.h"
#include "../components/SvgButton.h"
#include "../LookAndFeel.h"
#include "../components/SwitchButton.h"
#include "../audio/SmoothEffect.h"

enum class ScreenType : int {
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

class ScreenTypeParameter : public IntParameter {
public:
    ScreenTypeParameter(juce::String name, juce::String id, int versionHint, ScreenType value) : IntParameter(name, id, versionHint, (int) value, 1, (int)ScreenType::MAX) {}

    juce::String getText(float value, int maximumStringLength = 100) const override {
        switch ((ScreenType)(int)getUnnormalisedValue(value)) {
            case ScreenType::Empty:
                return "Empty";
            case ScreenType::Graticule:
                return "Graticule";
            case ScreenType::Smudged:
                return "Smudged";
            case ScreenType::SmudgedGraticule:
                return "Smudged Graticule";
#if SOSCI_FEATURES
            case ScreenType::Real:
                return "Real Oscilloscope";
            case ScreenType::VectorDisplay:
                return "Vector Display";
#endif
            default:
                return "Unknown";
        }
    }

    float getValueForText(const juce::String& text) const override {
        int unnormalisedValue;
        if (text == "Empty") {
            unnormalisedValue = (int)ScreenType::Empty;
        } else if (text == "Graticule") {
            unnormalisedValue = (int)ScreenType::Graticule;
        } else if (text == "Smudged") {
            unnormalisedValue = (int)ScreenType::Smudged;
        } else if (text == "Smudged Graticule") {
            unnormalisedValue = (int)ScreenType::SmudgedGraticule;
#if SOSCI_FEATURES
        } else if (text == "Real Oscilloscope") {
            unnormalisedValue = (int)ScreenType::Real;
        } else if (text == "Vector Display") {
            unnormalisedValue = (int)ScreenType::VectorDisplay;
#endif
        } else {
            unnormalisedValue = (int)ScreenType::Empty;
        }
        return getNormalisedValue(unnormalisedValue);
    }

    void save(juce::XmlElement* xml) {
        xml->setAttribute("screenType", getText(getValue()));
    }

    void load(juce::XmlElement* xml) {
        setValueNotifyingHost(getValueForText(xml->getStringAttribute("screenType")));
    }
    
#if SOSCI_FEATURES
    bool isRealisticDisplay() {
        ScreenType type = (ScreenType)(int)getValueUnnormalised();
        return type == ScreenType::Real || type == ScreenType::VectorDisplay;
    }
#endif
};

class VisualiserParameters {
public:
    ScreenTypeParameter* screenType = new ScreenTypeParameter("Screen Type", "screenType", VERSION_HINT, ScreenType::SmudgedGraticule);
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
    std::shared_ptr<Effect> saturationEffect = std::make_shared<Effect>(
        new EffectParameter(
            "Saturation",
            "Controls how saturated the colours are on the oscilloscope.",
            "saturation",
            VERSION_HINT, 1.0, 0.0, 5.0
        )
    );
    std::shared_ptr<Effect> focusEffect = std::make_shared<Effect>(
        new EffectParameter(
            "Focus",
            "Controls how focused the electron beam of the oscilloscope is.",
            "focus",
            VERSION_HINT, 1.0, 0.01, 10.0
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
            VERSION_HINT, 0.3, 0.0, 1.0
        )
    );
    
    std::vector<std::shared_ptr<Effect>> effects = {persistenceEffect, hueEffect, intensityEffect, saturationEffect, focusEffect, noiseEffect, glowEffect, ambientEffect, sweepMsEffect};
    std::vector<BooleanParameter*> booleans = {upsamplingEnabled, visualiserFullScreen, sweepEnabled};
    std::vector<IntParameter*> integers = {screenType};
};

class VisualiserSettings : public juce::Component {
public:
    VisualiserSettings(VisualiserParameters&, int numChannels = 2);
    ~VisualiserSettings();

    void resized() override;
    
    double getIntensity() {
        return parameters.intensityEffect->getActualValue() / 100;
    }
    
    double getPersistence() {
        return parameters.persistenceEffect->getActualValue() - 1.33;
    }
    
    double getHue() {
        return parameters.hueEffect->getActualValue();
    }
    
    double getSaturation() {
        return parameters.saturationEffect->getActualValue();
    }
    
    double getFocus() {
        return parameters.focusEffect->getActualValue() / 100;
    }
    
    double getNoise() {
        return parameters.noiseEffect->getActualValue();
    }
    
    double getGlow() {
        return parameters.glowEffect->getActualValue() * 3;
    }
    
    double getAmbient() {
        return parameters.ambientEffect->getActualValue();
    }
    
    ScreenType getScreenType() {
        return (ScreenType)parameters.screenType->getValueUnnormalised();
    }
    
    bool getUpsamplingEnabled() {
        return parameters.upsamplingEnabled->getBoolValue();
    }

    VisualiserParameters& parameters;
    int numChannels;

private:
    EffectComponent intensity{*parameters.intensityEffect};
    EffectComponent persistence{*parameters.persistenceEffect};
    EffectComponent hue{*parameters.hueEffect};
    EffectComponent saturation{*parameters.saturationEffect};
    EffectComponent focus{*parameters.focusEffect};
    EffectComponent noise{*parameters.noiseEffect};
    EffectComponent glow{*parameters.glowEffect};
    EffectComponent ambient{*parameters.ambientEffect};
    EffectComponent smooth{*parameters.smoothEffect};
    EffectComponent sweepMs{*parameters.sweepMsEffect};
    
    juce::Label screenTypeLabel{"Screen Type", "Screen Type"};
    juce::ComboBox screenType;
    
    jux::SwitchButton upsamplingToggle{parameters.upsamplingEnabled};
    jux::SwitchButton sweepToggle{parameters.sweepEnabled};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VisualiserSettings)
};

class SettingsWindow : public juce::DocumentWindow {
public:
    SettingsWindow(juce::String name) : juce::DocumentWindow(name, Colours::darker, juce::DocumentWindow::TitleBarButtons::closeButton) {
        setResizable(false, false);
    }
    
    void closeButtonPressed() override {
        setVisible(false);
    }
};

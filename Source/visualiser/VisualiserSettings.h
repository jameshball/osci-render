#pragma once

#define VERSION_HINT 2

#include <JuceHeader.h>
#include "../components/EffectComponent.h"
#include "../components/SvgButton.h"
#include "../LookAndFeel.h"
#include "../components/SwitchButton.h"

class VisualiserParameters {
public:
    BooleanParameter* graticuleEnabled = new BooleanParameter("Show Graticule", "graticuleEnabled", VERSION_HINT, true, "Show the graticule or grid lines over the oscilloscope display.");
    BooleanParameter* smudgesEnabled = new BooleanParameter("Show Smudges", "smudgesEnabled", VERSION_HINT, true, "Adds a subtle layer of dirt/smudges to the oscilloscope display to make it look more realistic.");
    BooleanParameter* upsamplingEnabled = new BooleanParameter("Upsample Audio", "upsamplingEnabled", VERSION_HINT, false, "Upsamples the audio before visualising it to make it appear more realistic, at the expense of performance.");
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
    std::shared_ptr<Effect> brightnessEffect = std::make_shared<Effect>(
        new EffectParameter(
            "Brightness",
            "Controls how bright the light glows for on the oscilloscope display.",
            "brightness",
            VERSION_HINT, 3.0, 0.0, 10.0
        )
    );
    std::shared_ptr<Effect> intensityEffect = std::make_shared<Effect>(
        new EffectParameter(
            "Intensity",
            "Controls how bright the electron beam of the oscilloscope is.",
            "intensity",
            VERSION_HINT, 3.0, 0.0, 10.0
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
            VERSION_HINT, 1.0, 0.01, 1.0
        )
    );
};

class VisualiserSettings : public juce::Component {
public:
    VisualiserSettings(VisualiserParameters&, int numChannels = 2);
    ~VisualiserSettings();

    void resized() override;
    
    double getBrightness() {
        return parameters.brightnessEffect->getActualValue() - 2;
    }
    
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
    
    bool getGraticuleEnabled() {
        return parameters.graticuleEnabled->getBoolValue();
    }
    
    bool getSmudgesEnabled() {
        return parameters.smudgesEnabled->getBoolValue();
    }
    
    bool getUpsamplingEnabled() {
        return parameters.upsamplingEnabled->getBoolValue();
    }

    VisualiserParameters& parameters;
    int numChannels;

private:
    EffectComponent brightness{*parameters.brightnessEffect};
    EffectComponent intensity{*parameters.intensityEffect};
    EffectComponent persistence{*parameters.persistenceEffect};
    EffectComponent hue{*parameters.hueEffect};
    EffectComponent saturation{*parameters.saturationEffect};
    EffectComponent focus{*parameters.focusEffect};
    
    jux::SwitchButton graticuleToggle{parameters.graticuleEnabled};
    jux::SwitchButton smudgeToggle{parameters.smudgesEnabled};
    jux::SwitchButton upsamplingToggle{parameters.upsamplingEnabled};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VisualiserSettings)
};

class SettingsWindow : public juce::DocumentWindow {
public:
    SettingsWindow(juce::String name) : juce::DocumentWindow(name, Colours::darker, juce::DocumentWindow::TitleBarButtons::closeButton) {}
    
    void closeButtonPressed() override {
        setVisible(false);
    }
};

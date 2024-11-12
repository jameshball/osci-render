#pragma once

#define VERSION_HINT 2

#include <JuceHeader.h>
#include "../components/EffectComponent.h"
#include "../components/SvgButton.h"
#include "../LookAndFeel.h"
#include "../components/SwitchButton.h"
#include "../audio/SmoothEffect.h"

class VisualiserParameters {
public:
    BooleanParameter* graticuleEnabled = new BooleanParameter("Show Graticule", "graticuleEnabled", VERSION_HINT, true, "Show the graticule or grid lines over the oscilloscope display.");
    BooleanParameter* smudgesEnabled = new BooleanParameter("Show Smudges", "smudgesEnabled", VERSION_HINT, true, "Adds a subtle layer of dirt/smudges to the oscilloscope display to make it look more realistic.");
    BooleanParameter* upsamplingEnabled = new BooleanParameter("Upsample Audio", "upsamplingEnabled", VERSION_HINT, true, "Upsamples the audio before visualising it to make it appear more realistic, at the expense of performance.");
    BooleanParameter* sweepEnabled = new BooleanParameter("Sweep", "sweepEnabled", VERSION_HINT, true, "Plots the audio signal over time, sweeping from left to right");
    BooleanParameter* visualiserFullScreen = new BooleanParameter("Visualiser Fullscreen", "visualiserFullScreen", VERSION_HINT, false, "Makes the software visualiser fullscreen.");

    std::shared_ptr<Effect> persistenceEffect = std::make_shared<Effect>(
        new EffectParameter(
            "Persistence",
            "Controls how long the phosphor remains lit after being hit by the beam.",
            "persistence",
            VERSION_HINT, 0.5, 0, 6.0
        )
    );
    std::shared_ptr<Effect> hueEffect = std::make_shared<Effect>(
        new EffectParameter(
            "Hue",
            "Controls the hue of the phosphor screen's colour.",
            "hue",
            VERSION_HINT, 125, 0, 359, 1
        )
    );
    std::shared_ptr<Effect> brightnessEffect = std::make_shared<Effect>(
        new EffectParameter(
            "Intensity",
            "Controls the brightness of the activated phosphor.",
            "brightness",
            VERSION_HINT, 1.0, 0.0, 2.0
        )
    );
    std::shared_ptr<Effect> saturationEffect = std::make_shared<Effect>(
        new EffectParameter(
            "Saturation",
            "Controls the saturation of the phosphor screen's colour.",
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
            "Controls how much visual noise is added to the oscilloscope display.",
            "noise",
            VERSION_HINT, 0.0, 0.0, 1.0
        )
    );
    std::shared_ptr<Effect> glowEffect = std::make_shared<Effect>(
        new EffectParameter(
            "Frontlight",
            "Controls how brightly the screen is lit from the front.",
            "glow",
            VERSION_HINT, 0.3, 0.0, 1.0
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
    
    std::vector<std::shared_ptr<Effect>> effects = {persistenceEffect, hueEffect, brightnessEffect, saturationEffect, focusEffect, noiseEffect, glowEffect};
    std::vector<BooleanParameter*> booleans = {graticuleEnabled, smudgesEnabled, upsamplingEnabled, visualiserFullScreen, sweepEnabled};
};

class VisualiserSettings : public juce::Component {
public:
    VisualiserSettings(VisualiserParameters&, int numChannels = 2);
    ~VisualiserSettings();

    void resized() override;
    
    double getBrightness() {
        return parameters.brightnessEffect->getActualValue();
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
    EffectComponent persistence{*parameters.persistenceEffect};
    EffectComponent hue{*parameters.hueEffect};
    EffectComponent saturation{*parameters.saturationEffect};
    EffectComponent focus{*parameters.focusEffect};
    EffectComponent noise{*parameters.noiseEffect};
    EffectComponent glow{*parameters.glowEffect};
    EffectComponent smooth{*parameters.smoothEffect};
    
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

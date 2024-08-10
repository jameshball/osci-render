#pragma once

#include <JuceHeader.h>
#include "VisualiserComponent.h"
#include "EffectComponent.h"
#include "SvgButton.h"

class VisualiserSettings : public juce::Component {
public:
    VisualiserSettings(OscirenderAudioProcessor&, VisualiserComponent&);
    ~VisualiserSettings();

    void resized() override;
private:
    OscirenderAudioProcessor& audioProcessor;
    VisualiserComponent& visualiser;

    EffectComponent intensity{audioProcessor, *audioProcessor.intensityEffect};
    EffectComponent persistence{audioProcessor, *audioProcessor.persistenceEffect};
    EffectComponent hue{audioProcessor, *audioProcessor.hueEffect};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VisualiserSettings)
};

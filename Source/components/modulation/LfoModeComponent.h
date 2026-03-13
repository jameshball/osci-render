#pragma once

#include <JuceHeader.h>
#include "../../audio/LfoState.h"

class OscirenderAudioProcessor;

// Vital-inspired dark-bar selector for LFO playback mode.
// Layout mirrors LfoRateComponent: value text + bottom label strip.
// Click the value area to cycle through modes, or right-click for a popup menu.
class LfoModeComponent : public juce::Component {
public:
    LfoModeComponent(OscirenderAudioProcessor& processor, int lfoIndex);
    ~LfoModeComponent() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;

    void setLfoIndex(int index);
    int getLfoIndex() const { return lfoIndex; }

    LfoMode getMode() const { return mode; }
    void setMode(LfoMode m);

    void syncFromProcessor();

    juce::String getDisplayText() const;

private:
    OscirenderAudioProcessor& audioProcessor;
    int lfoIndex = 0;
    LfoMode mode = LfoMode::Free;

    juce::Rectangle<int> valueArea;
    juce::Rectangle<int> labelArea;

    void showModePopup();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LfoModeComponent)
};

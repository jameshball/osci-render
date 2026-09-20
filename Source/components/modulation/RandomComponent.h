#pragma once

#include <JuceHeader.h>
#include "RandomGraphComponent.h"
#include "ModulationRateComponent.h"
#include "ModulationModeComponent.h"
#include "../../audio/modulation/RandomState.h"
#include "ModulationSourceComponent.h"
#include <osci_gui/osci_gui.h>

class OscirenderAudioProcessor;

// Random modulator panel: subclass of ModulationSourceComponent that adds a
// time-series graph, rate control, and style/mode selector.
class RandomComponent : public ModulationSourceComponent {
public:
    RandomComponent(OscirenderAudioProcessor& processor);
    ~RandomComponent() override;

    void resized() override;
    void displaySampleArrived(int index, const ModulationDisplayBuffer::Sample& sample) override;

    int getActiveRandomIndex() const { return getActiveSourceIndex(); }

    static juce::Colour getRandomColour(int randomIndex);

    void syncFromProcessorState() override;

protected:
    void onActiveSourceChanged(int newIndex) override;

private:
    OscirenderAudioProcessor& audioProcessor;

    RandomGraphComponent graph;
    ModulationRateComponent rateControl;
    ModulationModeComponent styleControl;

    // Layout constants
    static constexpr int kRateHeight  = 48;
    static constexpr int kRateGap     = 4;
    static constexpr int kMaxRateWidth  = 130;
    static constexpr int kMaxStyleWidth = 130;


    ParameterSyncHelper paramSync { [this] { syncFromProcessorState(); } };

    void syncGraphColours();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RandomComponent)
};

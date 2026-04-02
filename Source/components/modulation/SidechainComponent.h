#pragma once

#include <JuceHeader.h>
#include "NodeGraphComponent.h"
#include "ModulationSourceComponent.h"
#include "../KnobContainerComponent.h"
#include "../../audio/modulation/SidechainState.h"

class OscirenderAudioProcessor;

// Sidechain modulation panel: single-source modulation component that converts
// audio input level into a modulation signal via an envelope follower with
// attack/release controls and a transfer curve graph.
//
// The graph has:
//  - A bottom-left handle (input min threshold) and top-right handle (input max)
//  - A single curve handle controlling the power response between them
//  - A marker showing the current input level
//
// Attack/Release knobs control the envelope follower smoothing.
// Direction (unipolar/bipolar) is set via right-click on the tab (standard).
// Strength is controlled by the modulation depth dial on the tab.
class SidechainComponent : public ModulationSourceComponent {
public:
    SidechainComponent(OscirenderAudioProcessor& processor);

    void resized() override;
    void paint(juce::Graphics& g) override;
    void paintOverChildren(juce::Graphics& g) override;
    void timerCallback() override;
    void lookAndFeelChanged() override;

    static juce::Colour getSidechainColour(int index = 0);

    void syncFromProcessorState() override;

protected:
    void onActiveSourceChanged(int newIndex) override;

private:
    OscirenderAudioProcessor& audioProcessor;

    NodeGraphComponent graph;
    KnobContainerComponent attackKnob { "ATTACK" };
    KnobContainerComponent releaseKnob { "RELEASE" };

    // Layout constants
    static constexpr int kKnobSize = 55;
    static constexpr int kKnobGap = 3;
    static constexpr int kMaxKnobGap = 12;
    static constexpr int kMaxTabHeight = 70;
    static constexpr int kMinTabHeight = 40;

    // Bounds computed in resized() for painting the knob background
    juce::Rectangle<int> knobAreaBounds;

    // Guard against re-entrant onNodesChanged
    bool isSyncingGraph = false;

    void syncGraphFromProcessor();
    void syncGraphColours();
    void syncProcessorFromGraph();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SidechainComponent)
};

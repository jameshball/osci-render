#include "QuickControlsBar.h"
#include "../../PluginEditor.h"
#include "../../LookAndFeel.h"

QuickControlsBar::QuickControlsBar(OscirenderAudioProcessor& p, OscirenderAudioProcessorEditor& editor)
    : audioProcessor(p), pluginEditor(editor) {
    addAndMakeVisible(frequencyKnob);
    addAndMakeVisible(perspectiveKnob);
    addAndMakeVisible(fovKnob);

    // Bind knobs to their EffectParameters
    auto* freqParam = audioProcessor.frequencyEffect->parameters[0];
    frequencyKnob.bindToEffectParam(freqParam, 500.0, 1);
    frequencyKnob.getKnob().setTextValueSuffix("Hz");

    auto* perspParam = audioProcessor.perspective->parameters[0];
    perspectiveKnob.bindToEffectParam(perspParam, 0.0, 2);

    auto* fovParam = audioProcessor.perspective->parameters[1];
    fovKnob.bindToEffectParam(fovParam, 0.0, 1);
    fovKnob.getKnob().setTextValueSuffix(juce::CharPointer_UTF8("\xc2\xb0")); // degree symbol

    // Wire modulation
    frequencyKnob.wireModulation(audioProcessor);
    perspectiveKnob.wireModulation(audioProcessor);
    fovKnob.wireModulation(audioProcessor);
}

QuickControlsBar::~QuickControlsBar() {}

void QuickControlsBar::resized() {
    auto area = getLocalBounds().reduced(5, 3);

    constexpr int gap = 3;
    constexpr int numCols = 3;
    int totalGaps = (numCols - 1) * gap;
    int available = area.getWidth() - totalGaps;
    float colWidth = (float)available / (float)numCols;

    int startX = area.getX();
    int y = area.getY();
    int h = area.getHeight();
    float cum = 0.0f;
    int gapsSoFar = 0;

    auto nextCol = [&](float w, int vReduce = 0) -> juce::Rectangle<int> {
        int left = startX + juce::roundToInt(cum) + gapsSoFar;
        cum += w;
        int right = startX + juce::roundToInt(cum) + gapsSoFar;
        gapsSoFar += gap;
        return juce::Rectangle<int>(left, y, right - left, h).reduced(0, vReduce);
    };

    frequencyKnob.setBounds(nextCol(colWidth, 1));
    perspectiveKnob.setBounds(nextCol(colWidth, 1));
    fovKnob.setBounds(nextCol(colWidth, 1));
}

void QuickControlsBar::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().toFloat();
    g.setColour(Colours::darker());
    g.fillRoundedRectangle(bounds, Colours::kPillRadius);
}

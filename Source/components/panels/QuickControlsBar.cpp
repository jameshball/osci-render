#include "QuickControlsBar.h"
#include "../../PluginEditor.h"
#include "../../LookAndFeel.h"

QuickControlsBar::QuickControlsBar(OscirenderAudioProcessor& p, OscirenderAudioProcessorEditor& editor)
    : audioProcessor(p), pluginEditor(editor) {
    addAndMakeVisible(frequencyKnob);
    addAndMakeVisible(perspectiveKnob);
    addAndMakeVisible(fovKnob);
    addChildComponent(animationSpeedKnob);

    // Bind knobs to their EffectParameters
    auto* freqParam = audioProcessor.frequencyEffect->parameters[0];
    frequencyKnob.bindToEffectParam(freqParam, 500.0, 1);
    frequencyKnob.getKnob().setTextValueSuffix("Hz");

    auto* perspParam = audioProcessor.perspective->parameters[0];
    perspectiveKnob.bindToEffectParam(perspParam, 0.0, 2);

    auto* fovParam = audioProcessor.perspective->parameters[1];
    fovKnob.bindToEffectParam(fovParam, 0.0, 1);
    fovKnob.getKnob().setTextValueSuffix(juce::CharPointer_UTF8("\xc2\xb0")); // degree symbol

    animationSpeedKnob.bindToEffectParam(audioProcessor.animationSpeed->parameters[0], 1.0, 2);
    animationSpeedKnob.getKnob().setTextValueSuffix("x");

    // Wire modulation
    frequencyKnob.wireModulation(audioProcessor);
    perspectiveKnob.wireModulation(audioProcessor);
    fovKnob.wireModulation(audioProcessor);
    animationSpeedKnob.wireModulation(audioProcessor);

#if !OSCI_PREMIUM
    addAndMakeVisible(midiToggle);
    addAndMakeVisible(midiLabel);
    midiLabel.setFont(juce::Font(13.0f));
    midiLabel.setJustificationType(juce::Justification::centredLeft);
    midiLabel.setColour(juce::Label::textColourId, juce::Colours::white);

    addAndMakeVisible(voicesBar);
    voicesBar.setVisible(audioProcessor.midiEnabled->getBoolValue());
#endif

    audioProcessor.midiEnabled->addListener(this);
    frequencyKnob.setVisible(!audioProcessor.midiEnabled->getBoolValue());
}

QuickControlsBar::~QuickControlsBar() {
    audioProcessor.midiEnabled->removeListener(this);
}

void QuickControlsBar::parameterValueChanged(int, float) {
    triggerAsyncUpdate();
}

void QuickControlsBar::parameterGestureChanged(int, bool) {}

void QuickControlsBar::handleAsyncUpdate() {
    bool midiOn = audioProcessor.midiEnabled->getBoolValue();
    frequencyKnob.setVisible(!midiOn);
#if !OSCI_PREMIUM
    voicesBar.setVisible(midiOn);
#endif
    resized();
    repaint();
}

void QuickControlsBar::resized() {
    auto area = getLocalBounds().reduced(5, 3);

    constexpr int gap = 3;

#if !OSCI_PREMIUM
    // Layout: [toggle][label][voices] | [freq][persp][fov]
    constexpr int toggleW = 30;
    constexpr int labelW = 75;
    constexpr int voicesW = 80;

    auto midiArea = area.removeFromLeft(toggleW);
    midiToggle.setBounds(midiArea.withSizeKeepingCentre(toggleW, 22));

    midiLabel.setBounds(area.removeFromLeft(labelW));
    area.removeFromLeft(gap);

    bool midiOn = audioProcessor.midiEnabled->getBoolValue();
    if (midiOn) {
        voicesBar.setBounds(area.removeFromLeft(voicesW).reduced(0, 1));
        area.removeFromLeft(gap);
    }
#endif

    int numCols = frequencyKnob.isVisible() ? 3 : 2;
    if (animationSpeedKnob.isVisible()) ++numCols;
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

    if (frequencyKnob.isVisible())
        frequencyKnob.setBounds(nextCol(colWidth, 1));
    perspectiveKnob.setBounds(nextCol(colWidth, 1));
    fovKnob.setBounds(nextCol(colWidth, 1));
    if (animationSpeedKnob.isVisible())
        animationSpeedKnob.setBounds(nextCol(colWidth, 1));
}

void QuickControlsBar::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().toFloat();
    g.setColour(Colours::darker());
    g.fillRoundedRectangle(bounds, Colours::kPillRadius);
}

void QuickControlsBar::setAnimated(bool animated) {
    if (animationSpeedKnob.isVisible() == animated) return;
    animationSpeedKnob.setVisible(animated);
    resized();
    repaint();
}

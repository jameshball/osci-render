#include "MidiComponent.h"
#include "../../PluginEditor.h"
#include "../../LookAndFeel.h"

static constexpr int kToggleSectionWidth = 36;

MidiComponent::MidiComponent(OscirenderAudioProcessor& p, OscirenderAudioProcessorEditor& editor) : audioProcessor(p), pluginEditor(editor) {

    addAndMakeVisible(midiToggle);
    addAndMakeVisible(voicesBar);
    addAndMakeVisible(bendBar);
    addAndMakeVisible(velTrkKnob);
    addAndMakeVisible(glideKnob);
    addAndMakeVisible(slopeGraph);
    addAndMakeVisible(alwaysGlideToggle);
    addAndMakeVisible(legatoToggle);
    addAndMakeVisible(octaveScaleToggle);

    velTrkKnob.bindToParam(audioProcessor.velocityTracking);
    glideKnob.bindToParam(audioProcessor.glideTime, 1.0);

    // Velocity: show as percentage
    velTrkKnob.getKnob().setTextValueSuffix("%");
    velTrkKnob.getKnob().textFromValueFunction = [](double v) { return juce::String(v * 100.0, 1); };
    velTrkKnob.getKnob().valueFromTextFunction = [](const juce::String& s) { return s.getDoubleValue() / 100.0; };

    // Glide: show as seconds
    glideKnob.getKnob().setTextValueSuffix(" s");

    audioProcessor.midiEnabled->addListener(this);
    audioProcessor.velocityTracking->addListener(this);
    audioProcessor.glideTime->addListener(this);

    if (juce::JUCEApplicationBase::isStandaloneApp()) {
        addAndMakeVisible(tempoBar);
    }

    handleAsyncUpdate();
}

MidiComponent::~MidiComponent() {
    audioProcessor.midiEnabled->removeListener(this);
    audioProcessor.velocityTracking->removeListener(this);
    audioProcessor.glideTime->removeListener(this);
}

void MidiComponent::parameterValueChanged(int parameterIndex, float newValue) {
    triggerAsyncUpdate();
}

void MidiComponent::parameterGestureChanged(int parameterIndex, bool gestureIsStarting) {}

void MidiComponent::handleAsyncUpdate() {
    updateEnabledState();
    repaint();
}

void MidiComponent::updateEnabledState() {
    const bool midiOn = audioProcessor.midiEnabled->getBoolValue();
    voicesBar.setEnabled(midiOn);
    bendBar.setEnabled(midiOn);
    velTrkKnob.setEnabled(midiOn);
    glideKnob.setEnabled(midiOn);
    slopeGraph.setEnabled(midiOn);
    alwaysGlideToggle.setEnabled(midiOn);
    legatoToggle.setEnabled(midiOn);
    octaveScaleToggle.setEnabled(midiOn);
    tempoBar.setEnabled(midiOn);

    float alpha = midiOn ? 1.0f : 0.4f;
    voicesBar.setAlpha(alpha);
    bendBar.setAlpha(alpha);
    velTrkKnob.setAlpha(alpha);
    glideKnob.setAlpha(alpha);
    slopeGraph.setAlpha(alpha);
    alwaysGlideToggle.setAlpha(alpha);
    legatoToggle.setAlpha(alpha);
    octaveScaleToggle.setAlpha(alpha);
    tempoBar.setAlpha(alpha);
}

void MidiComponent::resized() {
    auto area = getLocalBounds();

    // Toggle section on the left
    auto toggleSection = area.removeFromLeft(kToggleSectionWidth);
    midiToggle.setBounds(toggleSection.withSizeKeepingCentre(30, 20));

    // Settings section — cumulative boundary layout to avoid jitter
    auto settingsArea = area.reduced(5, 3);

    constexpr int gap = 3;
    static constexpr int kMinToggleColWidth = 80;
    bool hasTempo = tempoBar.isVisible();

    int numCols = hasTempo ? 7 : 6;
    int totalGaps = (numCols - 1) * gap;
    int available = settingsArea.getWidth() - totalGaps;

    float colWidth = (float)available / (float)numCols;
    float toggleColWidth = colWidth;
    if (colWidth < (float)kMinToggleColWidth) {
        toggleColWidth = (float)kMinToggleColWidth;
        colWidth = ((float)available - toggleColWidth) / (float)(numCols - 1);
    }

    // Cumulative float widths → integer boundaries (gaps are exact integers)
    int startX = settingsArea.getX();
    int y = settingsArea.getY();
    int h = settingsArea.getHeight();
    float cum = 0.0f;
    int gapsSoFar = 0;

    auto nextCol = [&](float w, int vReduce = 0) -> juce::Rectangle<int> {
        int left = startX + juce::roundToInt(cum) + gapsSoFar;
        cum += w;
        int right = startX + juce::roundToInt(cum) + gapsSoFar;
        gapsSoFar += gap;
        return juce::Rectangle<int>(left, y, right - left, h).reduced(0, vReduce);
    };

    voicesBar.setBounds(nextCol(colWidth, 2));
    bendBar.setBounds(nextCol(colWidth, 2));
    velTrkKnob.setBounds(nextCol(colWidth, 1));
    glideKnob.setBounds(nextCol(colWidth, 1));
    slopeGraph.setBounds(nextCol(colWidth, 2));

    // Toggle labels column (3 stacked)
    {
        auto toggleCol = nextCol(toggleColWidth);
        constexpr int tGap = 2;
        int tAvail = toggleCol.getHeight() - tGap * 2;
        float tH = (float)tAvail / 3.0f;
        float tCum = 0.0f;
        int tGapsSoFar = 0;

        auto nextToggle = [&]() -> juce::Rectangle<int> {
            int top = toggleCol.getY() + juce::roundToInt(tCum) + tGapsSoFar;
            tCum += tH;
            int bottom = toggleCol.getY() + juce::roundToInt(tCum) + tGapsSoFar;
            tGapsSoFar += tGap;
            return juce::Rectangle<int>(toggleCol.getX(), top, toggleCol.getWidth(), bottom - top).reduced(1, 1);
        };

        alwaysGlideToggle.setBounds(nextToggle());
        legatoToggle.setBounds(nextToggle());
        octaveScaleToggle.setBounds(nextToggle());
    }

    if (hasTempo)
        tempoBar.setBounds(nextCol(colWidth, 2));
}

void MidiComponent::paint(juce::Graphics& g) {
    auto area = getLocalBounds();

    auto toggleSection = area.removeFromLeft(kToggleSectionWidth).toFloat();
    auto settingsSection = area.toFloat();

    // Toggle section: rounded on left, flat on right
    juce::Path togglePath;
    togglePath.addRoundedRectangle(toggleSection.getX(), toggleSection.getY(),
                                   toggleSection.getWidth(), toggleSection.getHeight(),
                                   Colours::kPillRadius, Colours::kPillRadius,
                                   true, false, true, false);
    g.setColour(Colours::veryDark());
    g.fillPath(togglePath);

    // Settings section: rounded on right, flat on left
    juce::Path settingsPath;
    settingsPath.addRoundedRectangle(settingsSection.getX(), settingsSection.getY(),
                                     settingsSection.getWidth(), settingsSection.getHeight(),
                                     Colours::kPillRadius, Colours::kPillRadius,
                                     false, true, false, true);
    g.setColour(Colours::darker());
    g.fillPath(settingsPath);
}

void MidiComponent::paintOverChildren(juce::Graphics& g) {
    if (!audioProcessor.midiEnabled->getBoolValue()) {
        auto area = getLocalBounds();
        area.removeFromLeft(kToggleSectionWidth);
        auto settingsSection = area.toFloat();

        auto centre = settingsSection.getCentre();
        float rx = settingsSection.getWidth() * 0.4f;
        float ry = settingsSection.getHeight();

        juce::ColourGradient gradient(
            Colours::darker(), centre.x, centre.y,
            Colours::darker().withAlpha(0.0f), centre.x + rx, centre.y, true);
        g.setGradientFill(gradient);
        g.fillEllipse(centre.x - rx, centre.y - ry, rx * 2.0f, ry * 2.0f);

        g.setColour(juce::Colours::white.withAlpha(0.7f));
        g.setFont(juce::Font(13.0f).boldened());
        g.drawText("MIDI DISABLED", settingsSection, juce::Justification::centred, false);
    }
}

#include "MidiComponent.h"
#include "../../PluginEditor.h"
#include "../../LookAndFeel.h"

static constexpr int kToggleSectionWidth = 36;

MidiComponent::MidiComponent(OscirenderAudioProcessor& p, OscirenderAudioProcessorEditor& editor) : audioProcessor(p), pluginEditor(editor) {

    addAndMakeVisible(midiToggle);
    addAndMakeVisible(voicesBar);
#if !OSCI_PREMIUM
    midiLabel.setFont(juce::Font(12.0f));
    midiLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    midiLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(midiLabel);
#endif
#if OSCI_PREMIUM
    addAndMakeVisible(bendBar);
    addAndMakeVisible(velTrkKnob);
    addAndMakeVisible(glideKnob);
    addAndMakeVisible(slopeGraph);
    addAndMakeVisible(alwaysGlideToggle);
    addAndMakeVisible(legatoToggle);
    addAndMakeVisible(octaveScaleToggle);
#endif

#if OSCI_PREMIUM
    velTrkKnob.bindToParam(audioProcessor.velocityTracking);
    glideKnob.bindToParam(audioProcessor.glideTime, 1.0);

    // Wire modulation for knobs
    velTrkKnob.wireModulation(audioProcessor);
    glideKnob.wireModulation(audioProcessor);

    // Velocity: show as percentage
    velTrkKnob.getKnob().setTextValueSuffix("%");
    velTrkKnob.getKnob().textFromValueFunction = [](double v) { return juce::String(v * 100.0, 1); };
    velTrkKnob.getKnob().valueFromTextFunction = [](const juce::String& s) { return s.getDoubleValue() / 100.0; };

    // Glide: show as seconds
    glideKnob.getKnob().setTextValueSuffix(" s");
#endif

    audioProcessor.midiEnabled->addListener(this);
#if OSCI_PREMIUM
    audioProcessor.velocityTracking->addListener(this);
    audioProcessor.glideTime->addListener(this);

    if (juce::JUCEApplicationBase::isStandaloneApp()) {
        addAndMakeVisible(tempoBar);
    }
#endif

    handleAsyncUpdate();
}

MidiComponent::~MidiComponent() {
    audioProcessor.midiEnabled->removeListener(this);
#if OSCI_PREMIUM
    audioProcessor.velocityTracking->removeListener(this);
    audioProcessor.glideTime->removeListener(this);
#endif
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
#if OSCI_PREMIUM
    velTrkKnob.setEnabled(midiOn);
    bendBar.setEnabled(midiOn);
    glideKnob.setEnabled(midiOn);
    slopeGraph.setEnabled(midiOn);
    alwaysGlideToggle.setEnabled(midiOn);
    legatoToggle.setEnabled(midiOn);
    octaveScaleToggle.setEnabled(midiOn);
    tempoBar.setEnabled(midiOn);
#endif

    float alpha = midiOn ? 1.0f : 0.4f;
    voicesBar.setAlpha(alpha);
#if OSCI_PREMIUM
    velTrkKnob.setAlpha(alpha);
    bendBar.setAlpha(alpha);
    glideKnob.setAlpha(alpha);
    slopeGraph.setAlpha(alpha);
    alwaysGlideToggle.setAlpha(alpha);
    legatoToggle.setAlpha(alpha);
    octaveScaleToggle.setAlpha(alpha);
    tempoBar.setAlpha(alpha);
#endif
}

void MidiComponent::resized() {
    auto area = getLocalBounds();

#if !OSCI_PREMIUM
    // Free mode: toggle + "Enable MIDI" label + voices bar, no backgrounds
    {
        auto row = area.reduced(4, 0);
        midiToggle.setBounds(row.removeFromLeft(30).withSizeKeepingCentre(30, 20));
        row.removeFromLeft(4);
        midiLabel.setBounds(row.removeFromLeft(80));

        bool midiOn = audioProcessor.midiEnabled->getBoolValue();
        voicesBar.setVisible(midiOn);
        if (midiOn) {
            row.removeFromLeft(4);
            constexpr int kMaxVoicesWidth = 120;
            auto voicesBounds = row.withWidth(juce::jmin(row.getWidth(), kMaxVoicesWidth));
            voicesBar.setBounds(voicesBounds);
        }
    }
    return;
#endif

    // Toggle section on the left
    auto toggleSection = area.removeFromLeft(kToggleSectionWidth);
    midiToggle.setBounds(toggleSection.withSizeKeepingCentre(30, 20));

    // Settings section — cumulative boundary layout to avoid jitter
    auto settingsArea = area.reduced(5, 3);

    constexpr int gap = 3;
#if OSCI_PREMIUM
    static constexpr int kMinToggleColWidth = 80;
    bool hasTempo = tempoBar.isVisible();
    int numCols = hasTempo ? 7 : 6;
#else
    int numCols = 1;
#endif
    int totalGaps = (numCols - 1) * gap;
    int available = settingsArea.getWidth() - totalGaps;

    float colWidth = (float)available / (float)numCols;
#if OSCI_PREMIUM
    float toggleColWidth = colWidth;
    if (colWidth < (float)kMinToggleColWidth) {
        toggleColWidth = (float)kMinToggleColWidth;
        colWidth = ((float)available - toggleColWidth) / (float)(numCols - 1);
    }
#endif

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
#if OSCI_PREMIUM
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
#endif
}

void MidiComponent::paint(juce::Graphics& g) {
#if OSCI_PREMIUM
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
#endif
}

void MidiComponent::paintOverChildren(juce::Graphics& g) {
#if OSCI_PREMIUM
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
#endif
}

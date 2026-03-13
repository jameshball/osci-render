#include "LfoModeComponent.h"
#include "../../PluginProcessor.h"
#include "../../LookAndFeel.h"
#include "../DarkBarPainter.h"

LfoModeComponent::LfoModeComponent(OscirenderAudioProcessor& processor, int index)
    : audioProcessor(processor), lfoIndex(index)
{
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    setRepaintsOnMouseActivity(true);
}

juce::String LfoModeComponent::getDisplayText() const {
    return lfoModeToString(mode);
}

void LfoModeComponent::setLfoIndex(int index) {
    lfoIndex = juce::jlimit(0, NUM_LFOS - 1, index);
    repaint();
}

void LfoModeComponent::setMode(LfoMode m) {
    if (mode == m) return;
    mode = m;
    audioProcessor.setLfoMode(lfoIndex, m);
    repaint();
    if (auto* p = getParentComponent()) p->repaint();
}

void LfoModeComponent::syncFromProcessor() {
    mode = audioProcessor.getLfoMode(lfoIndex);
    repaint();
}

// ============================================================================
// Paint — matches LfoRateComponent style
// ============================================================================

void LfoModeComponent::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().toFloat();
    bool hovering = isMouseOver(true);

    DarkBarPainter::paintBackground(g, bounds, labelArea, "MODE", hovering);

    // Value text
    g.setColour(juce::Colours::white.withAlpha(0.9f));
    g.setFont(juce::Font(12.0f, juce::Font::bold));
    g.drawText(getDisplayText(), valueArea.toFloat(), juce::Justification::centred);
}

void LfoModeComponent::resized() {
    auto bounds = getLocalBounds();
    static constexpr int labelH = 14;
    labelArea = bounds.removeFromBottom(labelH);
    valueArea = bounds;
}

// ============================================================================
// Mouse interaction
// ============================================================================

void LfoModeComponent::mouseDown(const juce::MouseEvent&) {
    showModePopup();
}

void LfoModeComponent::showModePopup() {
    juce::PopupMenu menu;
    menu.addItem(static_cast<int>(LfoMode::Free) + 1,            "Free",             true, mode == LfoMode::Free);
    menu.addItem(static_cast<int>(LfoMode::Trigger) + 1,         "Trigger",          true, mode == LfoMode::Trigger);
    menu.addItem(static_cast<int>(LfoMode::Sync) + 1,            "Sync",             true, mode == LfoMode::Sync);
    menu.addItem(static_cast<int>(LfoMode::Envelope) + 1,        "Envelope",         true, mode == LfoMode::Envelope);
    menu.addItem(static_cast<int>(LfoMode::SustainEnvelope) + 1, "Sustain Envelope", true, mode == LfoMode::SustainEnvelope);
    menu.addItem(static_cast<int>(LfoMode::LoopPoint) + 1,       "Loop Point",       true, mode == LfoMode::LoopPoint);
    menu.addItem(static_cast<int>(LfoMode::LoopHold) + 1,        "Loop Hold",        true, mode == LfoMode::LoopHold);

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),
        [this](int result) {
            if (result > 0)
                setMode(static_cast<LfoMode>(result - 1));
        });
}

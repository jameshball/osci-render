#include "ModulationModeComponent.h"
#include "../../LookAndFeel.h"
#include "../DarkBarPainter.h"

ModulationModeComponent::ModulationModeComponent(const ModulationModeConfig& cfg, int index)
    : config(cfg), sourceIndex(index)
{
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    setRepaintsOnMouseActivity(true);

    // Set initial mode from config if available
    if (!config.modes.empty())
        mode = config.modes[0].first;
}

juce::String ModulationModeComponent::getDisplayText() const {
    for (const auto& [value, name] : config.modes) {
        if (value == mode)
            return name;
    }
    return config.modes.empty() ? "" : config.modes[0].second;
}

void ModulationModeComponent::setSourceIndex(int index) {
    sourceIndex = juce::jlimit(0, config.maxIndex - 1, index);
    repaint();
}

void ModulationModeComponent::setMode(int m) {
    if (mode == m) return;
    mode = m;
    config.setMode(sourceIndex, m);
    repaint();
    if (auto* p = getParentComponent()) p->repaint();
}

void ModulationModeComponent::syncFromProcessor() {
    mode = config.getMode(sourceIndex);
    repaint();
}

// ============================================================================
// Paint — matches ModulationRateComponent style
// ============================================================================

void ModulationModeComponent::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().toFloat();
    bool hovering = isMouseOver(true);

    DarkBarPainter::paintBackground(g, bounds, labelArea, config.labelText, hovering);

    // Value text
    g.setColour(juce::Colours::white.withAlpha(0.9f));
    g.setFont(juce::Font(12.0f, juce::Font::bold));
    g.drawText(getDisplayText(), valueArea.toFloat(), juce::Justification::centred);
}

void ModulationModeComponent::resized() {
    auto bounds = getLocalBounds();
    static constexpr int labelH = 14;
    labelArea = bounds.removeFromBottom(labelH);
    valueArea = bounds;
}

// ============================================================================
// Mouse interaction
// ============================================================================

void ModulationModeComponent::mouseDown(const juce::MouseEvent&) {
    showModePopup();
}

void ModulationModeComponent::showModePopup() {
    juce::PopupMenu menu;
    for (const auto& [value, name] : config.modes) {
        menu.addItem(value + 1, name, true, value == mode);
    }

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),
        [this](int result) {
            if (result > 0)
                setMode(result - 1);
        });
}

void ModulationModeComponent::setModes(std::vector<std::pair<int, juce::String>> newModes) {
    config.modes = std::move(newModes);

    // If the current mode is no longer in the new list, reset to the first valid mode.
    bool found = false;
    for (const auto& [value, name] : config.modes) {
        if (value == mode) { found = true; break; }
    }
    if (!found && !config.modes.empty()) {
        mode = config.modes[0].first;
        config.setMode(sourceIndex, mode);
    }

    repaint();
}

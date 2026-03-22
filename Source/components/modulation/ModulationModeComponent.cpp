#include "ModulationModeComponent.h"

ModulationModeComponent::ModulationModeComponent(const ModulationModeConfig& cfg, int index)
    : config(cfg)
{
    sourceIndex = index;
    maxIndex = cfg.maxIndex;

    if (!config.modes.empty())
        mode = config.modes[0].first;
}

juce::MouseCursor ModulationModeComponent::getMouseCursor() {
    if (contentArea.contains(getMouseXYRelative()))
        return juce::MouseCursor::PointingHandCursor;
    return juce::MouseCursor::NormalCursor;
}

juce::String ModulationModeComponent::getDisplayText() const {
    for (const auto& [value, name] : config.modes) {
        if (value == mode)
            return name;
    }
    return config.modes.empty() ? "" : config.modes[0].second;
}

juce::String ModulationModeComponent::getLabelText() const {
    return config.labelText;
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
// Mouse interaction
// ============================================================================

void ModulationModeComponent::mouseDown(const juce::MouseEvent& e) {
    if (!contentArea.contains(e.getPosition())) return;
    showModePopup();
}

void ModulationModeComponent::mouseMove(const juce::MouseEvent& e) {
    ModulationControlComponent::mouseMove(e);
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

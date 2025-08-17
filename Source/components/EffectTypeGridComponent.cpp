#include "EffectTypeGridComponent.h"
#include <JuceHeader.h>
#include "../LookAndFeel.h"
#include "GridComponent.h"
#include "GridItemComponent.h"
#include <unordered_set>
#include <algorithm>
#include <numeric>

// ===================== EffectTypeGridComponent (wrapper) =====================

EffectTypeGridComponent::EffectTypeGridComponent(OscirenderAudioProcessor& processor)
    : audioProcessor(processor)
{
    addAndMakeVisible(grid);
    setSize(400, 200);
    addAndMakeVisible(cancelButton);
    cancelButton.onClick = [this]() {
        if (onCanceled) onCanceled();
    };
    setupEffectItems();
    refreshDisabledStates();
}

EffectTypeGridComponent::~EffectTypeGridComponent() = default;

void EffectTypeGridComponent::setupEffectItems()
{
    grid.clearItems();

    // Get effect types directly from the audio processor's toggleableEffects
    juce::SpinLock::ScopedLockType lock(audioProcessor.effectsLock);
    const int n = (int) audioProcessor.toggleableEffects.size();
    std::vector<int> order(n);
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [this](int a, int b) {
        auto ea = audioProcessor.toggleableEffects[a];
        auto eb = audioProcessor.toggleableEffects[b];
        const int cmp = ea->getName().compareIgnoreCase(eb->getName());
        if (cmp != 0)
            return cmp < 0; // ascending alphabetical, case-insensitive
        // Stable tie-breaker to ensure deterministic layout
        return ea->getId().compare(eb->getId()) < 0;
    });

    for (int idx : order)
    {
        auto effect = audioProcessor.toggleableEffects[idx];
        // Extract effect name from the effect
        juce::String effectName = effect->getName();

        // Create new generic item component
        auto* item = new GridItemComponent(effectName, effect->getIcon(), effect->getId());

        // Set up callback to forward selection
        item->onItemSelected = [this](const juce::String& effectId) {
            if (onEffectSelected)
                onEffectSelected(effectId);
        };
        // Hover preview: request temporary preview of this effect while hovered
        item->onHoverStart = [this](const juce::String& effectId) {
            if (audioProcessor.getGlobalBoolValue("previewEffectOnHover", true)) {
                juce::SpinLock::ScopedLockType lock(audioProcessor.effectsLock);
                audioProcessor.setPreviewEffectId(effectId);
            }
        };
        item->onHoverEnd = [this]() {
            if (audioProcessor.getGlobalBoolValue("previewEffectOnHover", true)) {
                juce::SpinLock::ScopedLockType lock(audioProcessor.effectsLock);
                audioProcessor.clearPreviewEffect();
            }
        };

        grid.addItem(item);
    }
}

void EffectTypeGridComponent::refreshDisabledStates()
{
    juce::SpinLock::ScopedLockType lock(audioProcessor.effectsLock);
    // Build a quick lookup of selected ids
    std::unordered_set<std::string> selectedIds;
    selectedIds.reserve((size_t) audioProcessor.toggleableEffects.size());
    bool anySelected = false;
    for (const auto& eff : audioProcessor.toggleableEffects) {
        const bool isSelected = (eff->selected == nullptr) ? true : eff->selected->getBoolValue();
        if (isSelected) {
            anySelected = true;
            selectedIds.insert(eff->getId().toStdString());
        }
    }
    for (auto* item : grid.getItems()) {
        const bool disable = selectedIds.find(item->getId().toStdString()) != selectedIds.end();
        item->setEnabled(! disable);
    }
    cancelButton.setVisible(anySelected);
}

void EffectTypeGridComponent::paint(juce::Graphics& g)
{
    // No background - make component transparent
}

void EffectTypeGridComponent::resized()
{
    auto bounds = getLocalBounds();
    auto topBar = bounds.removeFromTop(30);
    cancelButton.setBounds(topBar.removeFromRight(80).reduced(4));
    grid.setBounds(bounds);
}

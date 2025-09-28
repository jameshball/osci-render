#include "EffectTypeGridComponent.h"
#include <JuceHeader.h>
#include "../LookAndFeel.h"
#include "GridComponent.h"
#include "GridItemComponent.h"
#include "../PluginEditor.h"
#include <unordered_set>
#include <algorithm>
#include <numeric>

// ===================== EffectTypeGridComponent (wrapper) =====================

EffectTypeGridComponent::EffectTypeGridComponent(OscirenderAudioProcessor& processor, OscirenderAudioProcessorEditor& editorRef)
    : audioProcessor(processor), editor(editorRef)
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

    std::vector<std::shared_ptr<osci::Effect>> effectsToDisplay;
    effectsToDisplay.reserve(audioProcessor.toggleableEffects.size());

    effectsToDisplay.insert(effectsToDisplay.end(), audioProcessor.toggleableEffects.begin(), audioProcessor.toggleableEffects.end());

    std::sort(effectsToDisplay.begin(), effectsToDisplay.end(), [](const auto& a, const auto& b) {
#if !OSCI_PREMIUM
        const bool aPremium = a->isPremiumOnly();
        const bool bPremium = b->isPremiumOnly();
        if (aPremium != bPremium)
            return !aPremium; // non-premium effects first
#endif

        const int cmp = a->getName().compareIgnoreCase(b->getName());
        if (cmp != 0)
            return cmp < 0; // ascending alphabetical, case-insensitive
        // Stable tie-breaker to ensure deterministic layout
        return a->getId().compare(b->getId()) < 0;
    });

    for (const auto& effect : effectsToDisplay)
    {
        // Extract effect name from the effect
        juce::String effectName = effect->getName();

        // Create new generic item component
        auto* item = new GridItemComponent(effectName, effect->getIcon(), effect->getId());

        const bool isLockedPremium =
#if !OSCI_PREMIUM
            effect->isPremiumOnly();
#else
            false;
#endif
        ;

#if !OSCI_PREMIUM
        if (isLockedPremium) {
            item->setLocked(true);
            item->onLockedClick = [this]() {
                editor.showPremiumSplashScreen();
            };
        }
#endif

        if (! isLockedPremium) {
            item->onItemSelected = [this](const juce::String& effectId) {
                if (onEffectSelected)
                    onEffectSelected(effectId);
            };
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
        }

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
#if !OSCI_PREMIUM
        if (eff->isPremiumOnly())
            continue;
#endif
        const bool isSelected = (eff->selected == nullptr) ? true : eff->selected->getBoolValue();
        if (isSelected) {
            anySelected = true;
            selectedIds.insert(eff->getId().toStdString());
        }
    }
    for (auto* item : grid.getItems()) {
        if (item->isLocked()) {
            item->setEnabled(true);
            continue;
        }
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

#pragma once
#include "../DraggableListBox.h"
#include <JuceHeader.h>
#include "../../PluginProcessor.h"
#include "EffectComponent.h"
#include "../ComponentList.h"
#include "../SwitchButton.h"
#include "EffectTypeGridComponent.h"
#include <osci_gui/osci_gui.h>
#include <random>
#include <unordered_map>

// Application-specific data container
class OscirenderAudioProcessorEditor;
struct AudioEffectListBoxItemData : public DraggableListBoxItemData
{
    std::vector<std::shared_ptr<osci::Effect>> data;
    OscirenderAudioProcessor& audioProcessor;
    OscirenderAudioProcessorEditor& editor;
    std::function<void()> onAddNewEffectRequested; // callback hooked by parent to open the grid

    AudioEffectListBoxItemData(OscirenderAudioProcessor& p, OscirenderAudioProcessorEditor& editor) : audioProcessor(p), editor(editor) {
        resetData();
    }

    void randomise() {
        std::random_device rd;
        std::mt19937 g(rd());
        
        // Group the entire randomise into a single undo transaction
        audioProcessor.getUndoManager().beginNewTransaction("Randomise Effects");
        CommonAudioProcessor::ScopedFlag grouping(audioProcessor.undoGrouping);

        // Decide which effects to pick (indices into toggleableEffects)
        int total;
        std::vector<int> pickedIndices;
        {
            juce::SpinLock::ScopedLockType lock(audioProcessor.effectsLock);
            total = (int)audioProcessor.toggleableEffects.size();
        }
        int maxPick = juce::jmin(5, total);
        int numPick = juce::jmax(1, juce::Random::getSystemRandom().nextInt({1, maxPick + 1}));
        std::vector<int> indices(total);
        std::iota(indices.begin(), indices.end(), 0);
        std::shuffle(indices.begin(), indices.end(), g);
        for (int k = 0; k < numPick && k < (int)indices.size(); ++k)
            pickedIndices.push_back(indices[k]);
        std::sort(pickedIndices.begin(), pickedIndices.end());

        // Deselect and disable ALL effects (outside lock — ValueTree/UndoManager alloc)
        for (auto& effect : audioProcessor.toggleableEffects) {
            if (effect->selected != nullptr)
                effect->selected->setBoolValueNotifyingHost(false);
            if (effect->enabled != nullptr)
                effect->enabled->setBoolValueNotifyingHost(false);
        }
        
        // Enable the picked effects and randomise their params (outside lock)
        for (int k : pickedIndices) {
            auto& effect = audioProcessor.toggleableEffects[k];
            if (effect->selected != nullptr)
                effect->selected->setBoolValueNotifyingHost(true);
            if (effect->enabled != nullptr)
                effect->enabled->setBoolValueNotifyingHost(true);
            
            auto id = effect->getId().toLowerCase();
            if (id.contains("scale") || id.contains("translate") || id.contains("trace"))
                continue;
            
            for (auto& parameter : effect->parameters) {
                float normVal = juce::Random::getSystemRandom().nextFloat();
                parameter->setUnnormalisedValueNotifyingHost(parameter->getUnnormalisedValue(normVal));
                if (parameter->lfo != nullptr) {
                    parameter->lfo->setUnnormalisedValueNotifyingHost((int) osci::LfoType::Static);
                    parameter->lfoRate->setUnnormalisedValueNotifyingHost(1);
                    if (juce::Random::getSystemRandom().nextFloat() > 0.8) {
                        parameter->lfo->setUnnormalisedValueNotifyingHost((int)(juce::Random::getSystemRandom().nextFloat() * (int) osci::LfoType::Noise));
                        float normRate = juce::Random::getSystemRandom().nextFloat() * 0.1f;
                        parameter->lfoRate->setUnnormalisedValueNotifyingHost(parameter->lfoRate->getUnnormalisedValue(normRate));
                    }
                }
            }
        }

        // Refresh local data with only selected effects
        resetData();
        
        {
            juce::SpinLock::ScopedLockType lock(audioProcessor.effectsLock);
            // shuffle precedence of the selected subset
            std::shuffle(data.begin(), data.end(), g);
            for (int i = 0; i < (int)data.size(); i++) {
                data[i]->setPrecedence(i);
            }
            audioProcessor.updateEffectPrecedence();
        }
    }

    void resetData() {
        juce::SpinLock::ScopedLockType lock(audioProcessor.effectsLock);
        data.clear();
        for (int i = 0; i < audioProcessor.toggleableEffects.size(); i++) {
            auto effect = audioProcessor.toggleableEffects[i];
#if !OSCI_PREMIUM
            if (effect->isPremiumOnly()) continue; // skip premium effects in free version
#endif
            // Ensure 'selected' exists and defaults to true for older projects
            effect->markSelectable(effect->selected == nullptr ? true : effect->selected->getBoolValue());
            if (effect->selected == nullptr || effect->selected->getBoolValue()) {
                data.push_back(effect);
            }
        }
        // Sort by precedence so the list matches the user's custom ordering
        std::sort(data.begin(), data.end(), [](const auto& a, const auto& b) {
            return a->getPrecedence() < b->getPrecedence();
        });
    }

    int getNumItems() override {
    // Only the effects themselves are rows; the "+ Add new effect" button is a separate control below the list
    return (int) data.size();
    }

    // CURRENTLY NOT USED
    void deleteItem(int indexOfItemToDelete) override {
        // data.erase(data.begin() + indexOfItemToDelete);
    }

    // CURRENTLY NOT USED
    void addItemAtEnd() override {
        // data.push_back(juce::String("Yahoo"));
    }

    // Capture current ordering as a vector of effect IDs
    std::vector<juce::String> captureOrder() const {
        std::vector<juce::String> order;
        for (auto& e : data)
            order.push_back(e->getId());
        return order;
    }

    // Apply a saved ordering (updates local data + processor precedences)
    void applyOrder(const std::vector<juce::String>& order) {
        audioProcessor.applyEffectOrder(order);
        resetData();
    }

    // UndoableAction for effect reordering — references the processor (which
    // always outlives the UndoManager it owns) rather than the editor-scoped
    // AudioEffectListBoxItemData, so undo after editor close is safe.
    struct PrecedenceChangeAction : public juce::UndoableAction {
        PrecedenceChangeAction(OscirenderAudioProcessor& p,
                               std::vector<juce::String> before,
                               std::vector<juce::String> after)
            : processor(p), beforeOrder(std::move(before)), afterOrder(std::move(after)) {}

        bool perform() override {
            processor.applyEffectOrder(afterOrder);
            return true;
        }
        bool undo() override {
            processor.applyEffectOrder(beforeOrder);
            return true;
        }

        OscirenderAudioProcessor& processor;
        std::vector<juce::String> beforeOrder, afterOrder;
    };

    void performReorder(int fromIndex, int toIndex) {
        auto beforeOrder = captureOrder();
        auto dataCopy = data;
        move(dataCopy, fromIndex, toIndex);
        std::vector<juce::String> afterOrder;
        for (auto& e : dataCopy)
            afterOrder.push_back(e->getId());
        audioProcessor.getUndoManager().beginNewTransaction("Reorder Effects");
        audioProcessor.getUndoManager().perform(new PrecedenceChangeAction(audioProcessor, std::move(beforeOrder), std::move(afterOrder)));
    }

    void moveBefore(int indexOfItemToMove, int indexOfItemToPlaceBefore) override {
        int dest = (indexOfItemToMove < indexOfItemToPlaceBefore)
                       ? indexOfItemToPlaceBefore - 1
                       : indexOfItemToPlaceBefore;
        performReorder(indexOfItemToMove, dest);
    }

    void moveAfter(int indexOfItemToMove, int indexOfItemToPlaceAfter) override {
        int dest = (indexOfItemToMove <= indexOfItemToPlaceAfter)
                       ? indexOfItemToPlaceAfter
                       : indexOfItemToPlaceAfter + 1;
        performReorder(indexOfItemToMove, dest);
    }

    template <typename t> void move(std::vector<t>& v, size_t oldIndex, size_t newIndex) {
        if (oldIndex > newIndex) {
            std::rotate(v.rend() - oldIndex - 1, v.rend() - oldIndex, v.rend() - newIndex);
        } else {
            std::rotate(v.begin() + oldIndex, v.begin() + oldIndex + 1, v.begin() + newIndex + 1);
        }
    }
    
    void setSelected(int itemIndex, bool selected) {
        data[itemIndex]->enabled->setBoolValueNotifyingHost(selected);
    }

    std::shared_ptr<osci::Effect> getEffect(int itemIndex) {
        return data[itemIndex];
    }
};

// Custom list-item Component (which includes item-delete button)
class EffectsListComponent : public DraggableListBoxItem
{
public:
    EffectsListComponent(DraggableListBox& lb, AudioEffectListBoxItemData& data, int rn, osci::Effect& effect);
    ~EffectsListComponent();

    void paint(juce::Graphics& g) override;
    void paintOverChildren(juce::Graphics& g) override;
    void resized() override;
    
    static const int LEFT_BAR_WIDTH = 50;
    static const int RIGHT_BAR_WIDTH = 15; // space for close button
    static const int ROW_HEIGHT = 30;
    static const int PADDING = 4;

protected:
    osci::Effect& effect;
    ComponentListModel listModel { ROW_HEIGHT };
    VListBox list;
    jux::SwitchButton enabled = { effect.enabled };
    osci::SvgButton closeButton = osci::SvgButton("closeEffect", juce::String::createStringFromData(BinaryData::close_svg, BinaryData::close_svgSize), juce::Colours::white, juce::Colours::white);
private:
    OscirenderAudioProcessor& audioProcessor;
    OscirenderAudioProcessorEditor& editor;

    std::shared_ptr<juce::Component> createComponent(osci::EffectParameter* parameter);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EffectsListComponent)
};

// Customized DraggableListBoxModel overrides refreshComponentForRow() to ensure that every
// list-item Component is a EffectsListComponent.
class EffectsListBoxModel : public DraggableListBoxModel
{
public:
    EffectsListBoxModel(DraggableListBox& lb, DraggableListBoxItemData& md)
        : DraggableListBoxModel(lb, md) {
        OscirenderAudioProcessor& audioProcessor = ((AudioEffectListBoxItemData&)md).audioProcessor;
        juce::SpinLock::ScopedLockType lock(audioProcessor.effectsLock);
        audioProcessor.updateEffectPrecedence();
    }

    int getRowHeight(int row) override;
    bool hasVariableHeightRows() const override;

    juce::Component* refreshComponentForRow(int, bool, juce::Component*) override;
};

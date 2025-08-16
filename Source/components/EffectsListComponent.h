#pragma once
#include "DraggableListBox.h"
#include <JuceHeader.h>
#include "../PluginProcessor.h"
#include "EffectComponent.h"
#include "ComponentList.h"
#include "SwitchButton.h"
#include "EffectTypeGridComponent.h"
#include "SvgButton.h"
#include <random>

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
        juce::SpinLock::ScopedLockType lock(audioProcessor.effectsLock);
        // Decide how many effects to select (1..5 or up to available)
        int total = (int) audioProcessor.toggleableEffects.size();
        int maxPick = juce::jmin(5, total);
        int numPick = juce::jmax(1, juce::Random::getSystemRandom().nextInt({1, maxPick + 1}));

        // Build indices [0..total)
        std::vector<int> indices(total);
        std::iota(indices.begin(), indices.end(), 0);
        std::random_device rd;
        std::mt19937 g(rd());
        std::shuffle(indices.begin(), indices.end(), g);

        // First, deselect and disable all
        for (auto& effect : audioProcessor.toggleableEffects) {
            effect->markSelectable(false);
            effect->markEnableable(false);
        }

        // Pick numPick to select & enable, and randomise params
        for (int k = 0; k < numPick && k < indices.size(); ++k) {
            auto& effect = audioProcessor.toggleableEffects[indices[k]];
            effect->markSelectable(true);
            effect->markEnableable(true);

            auto id = effect->getId().toLowerCase();
            if (id.contains("scale") || id.contains("translate") || id.contains("trace")) {
                continue;
            }

            for (auto& parameter : effect->parameters) {
                parameter->setValueNotifyingHost(juce::Random::getSystemRandom().nextFloat());
                if (parameter->lfo != nullptr) {
                    parameter->lfo->setUnnormalisedValueNotifyingHost((int) osci::LfoType::Static);
                    parameter->lfoRate->setUnnormalisedValueNotifyingHost(1);
                    if (juce::Random::getSystemRandom().nextFloat() > 0.8) {
                        parameter->lfo->setUnnormalisedValueNotifyingHost((int)(juce::Random::getSystemRandom().nextFloat() * (int) osci::LfoType::Noise));
                        parameter->lfoRate->setValueNotifyingHost(juce::Random::getSystemRandom().nextFloat() * 0.1);
                    }
                }
            }
        }

        // Refresh local data with only selected effects
        resetData();
        
        // shuffle precedence of the selected subset
        std::shuffle(data.begin(), data.end(), g);
        for (int i = 0; i < data.size(); i++) {
            data[i]->setPrecedence(i);
        }
        audioProcessor.updateEffectPrecedence();
    }

    void resetData() {
        juce::SpinLock::ScopedLockType lock(audioProcessor.effectsLock);
        data.clear();
        for (int i = 0; i < audioProcessor.toggleableEffects.size(); i++) {
            auto effect = audioProcessor.toggleableEffects[i];
            effect->setValue(effect->getValue());
            // Ensure 'selected' exists and defaults to true for older projects
            effect->markSelectable(effect->selected == nullptr ? true : effect->selected->getBoolValue());
            if (effect->selected == nullptr || effect->selected->getBoolValue()) {
                data.push_back(effect);
            }
        }
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

    void moveBefore(int indexOfItemToMove, int indexOfItemToPlaceBefore) override {
        juce::SpinLock::ScopedLockType lock(audioProcessor.effectsLock);

        auto effect = data[indexOfItemToMove];

        if (indexOfItemToMove < indexOfItemToPlaceBefore) {
            move(data, indexOfItemToMove, indexOfItemToPlaceBefore - 1);
        } else {
            move(data, indexOfItemToMove, indexOfItemToPlaceBefore);
        }
        
        for (int i = 0; i < data.size(); i++) {
            data[i]->setPrecedence(i);
        }

        audioProcessor.updateEffectPrecedence();
    }

    void moveAfter(int indexOfItemToMove, int indexOfItemToPlaceAfter) override {
        juce::SpinLock::ScopedLockType lock(audioProcessor.effectsLock);

        auto temp = data[indexOfItemToMove];
        
        if (indexOfItemToMove <= indexOfItemToPlaceAfter) {
            move(data, indexOfItemToMove, indexOfItemToPlaceAfter);
        } else {
            move(data, indexOfItemToMove, indexOfItemToPlaceAfter + 1);
        }
        
        for (int i = 0; i < data.size(); i++) {
            data[i]->setPrecedence(i);
        }

        audioProcessor.updateEffectPrecedence();
    }

    template <typename t> void move(std::vector<t>& v, size_t oldIndex, size_t newIndex) {
        if (oldIndex > newIndex) {
            std::rotate(v.rend() - oldIndex - 1, v.rend() - oldIndex, v.rend() - newIndex);
        } else {
            std::rotate(v.begin() + oldIndex, v.begin() + oldIndex + 1, v.begin() + newIndex + 1);
        }
    }
    
    void setSelected(int itemIndex, bool selected) {
        if (selected) {
            data[itemIndex]->enabled->setValueNotifyingHost(true);
        } else {
            data[itemIndex]->enabled->setValueNotifyingHost(false);
        }
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
    ComponentListModel listModel;
    juce::ListBox list;
    jux::SwitchButton enabled = { effect.enabled };
    SvgButton closeButton = SvgButton("closeEffect", juce::String::createStringFromData(BinaryData::close_svg, BinaryData::close_svgSize), juce::Colours::white, juce::Colours::white);
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

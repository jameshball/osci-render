#pragma once
#include "DraggableListBox.h"
#include <JuceHeader.h>
#include "../PluginProcessor.h"
#include "../audio/Effect.h"
#include "EffectComponent.h"
#include "ComponentList.h"

// Application-specific data container
struct AudioEffectListBoxItemData : public DraggableListBoxItemData
{
    std::vector<std::shared_ptr<Effect>> data;
    OscirenderAudioProcessor& audioProcessor;

    AudioEffectListBoxItemData(OscirenderAudioProcessor& p) : audioProcessor(p) {}

    int getNumItems() override {
        return data.size();
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

    std::shared_ptr<Effect> getEffect(int itemIndex) {
        return data[itemIndex];
    }
};

// Custom list-item Component (which includes item-delete button)
class EffectsListComponent : public DraggableListBoxItem
{
public:
    EffectsListComponent(DraggableListBox& lb, AudioEffectListBoxItemData& data, int rn, Effect& effect);
    ~EffectsListComponent();

    void paint(juce::Graphics& g) override;
    void paintOverChildren(juce::Graphics& g) override;
    void resized() override;

protected:
    Effect& effect;
    ComponentListModel listModel;
    juce::ListBox list;
private:
    OscirenderAudioProcessor& audioProcessor;

    std::shared_ptr<juce::Component> createComponent(EffectParameter* parameter);

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

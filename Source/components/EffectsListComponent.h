#pragma once
#include "DraggableListBox.h"
#include <JuceHeader.h>
#include "../PluginProcessor.h"
#include "../audio/Effect.h"
#include "EffectComponent.h"

// Application-specific data container
struct AudioEffectListBoxItemData : public DraggableListBoxItemData
{
    std::vector<std::shared_ptr<Effect>> data;
    OscirenderAudioProcessor& audioProcessor;

    AudioEffectListBoxItemData(OscirenderAudioProcessor& p) : audioProcessor(p) {}

    int getNumItems() override {
        return data.size();
    }

    void deleteItem(int indexOfItemToDelete) override {
		data.erase(data.begin() + indexOfItemToDelete);
    }

    void addItemAtEnd() override {
        // data.push_back(juce::String("Yahoo"));
    }

	void moveBefore(int indexOfItemToMove, int indexOfItemToPlaceBefore) override {
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
    
    void setValue(int itemIndex, double value) {
		data[itemIndex]->setValue(value);
    }
    
    void setSelected(int itemIndex, bool selected) {
        if (selected) {
			audioProcessor.enableEffect(data[itemIndex]);
        } else {
			audioProcessor.disableEffect(data[itemIndex]);
        }
    }

	juce::String getText(int itemIndex) {
		return data[itemIndex]->getName();
	}

	double getValue(int itemIndex) {
		return data[itemIndex]->getValue();
	}

    juce::String getId(int itemIndex) {
        return data[itemIndex]->getId();
    }
};

// Custom list-item Component (which includes item-delete button)
class EffectsListComponent : public DraggableListBoxItem
{
public:
    EffectsListComponent(DraggableListBox& lb, AudioEffectListBoxItemData& data, int rn, std::shared_ptr<EffectComponent> effectComponent);
    ~EffectsListComponent();

    void paint(juce::Graphics& g) override;
    void resized() override;

protected:
    std::shared_ptr<EffectComponent> effectComponent;
private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EffectsListComponent)
};

// Customized DraggableListBoxModel overrides refreshComponentForRow() to ensure that every
// list-item Component is a EffectsListComponent.
class EffectsListBoxModel : public DraggableListBoxModel
{
public:
    EffectsListBoxModel(DraggableListBox& lb, DraggableListBoxItemData& md)
        : DraggableListBoxModel(lb, md) {}

    juce::Component* refreshComponentForRow(int, bool, juce::Component*) override;
};

#pragma once
#include "DraggableListBox.h"
#include <JuceHeader.h>
#include "../PluginProcessor.h"
#include "../audio/Effect.h"
#include "EffectComponent.h"
#include "ComponentList.h"
#include <random>

// Application-specific data container
class OscirenderAudioProcessorEditor;
struct AudioEffectListBoxItemData : public DraggableListBoxItemData
{
    std::vector<std::shared_ptr<Effect>> data;
    OscirenderAudioProcessor& audioProcessor;
    OscirenderAudioProcessorEditor& editor;

    AudioEffectListBoxItemData(OscirenderAudioProcessor& p, OscirenderAudioProcessorEditor& editor) : audioProcessor(p), editor(editor) {
        resetData();
    }

    void randomise() {
        juce::SpinLock::ScopedLockType lock(audioProcessor.effectsLock);
		for (int i = 0; i < data.size(); i++) {
			auto effect = data[i];
			auto id = effect->getId();
            
            if (id.contains("scale") || id.contains("translate")) {
                continue;
            }

            for (auto& parameter : effect->parameters) {
                parameter->setValueNotifyingHost(juce::Random::getSystemRandom().nextFloat());
                if (parameter->lfo != nullptr) {
                    parameter->lfo->setUnnormalisedValueNotifyingHost((int) LfoType::Static);
                    parameter->lfoRate->setUnnormalisedValueNotifyingHost(1);
                    
                    if (juce::Random::getSystemRandom().nextFloat() > 0.8) {
                        parameter->lfo->setUnnormalisedValueNotifyingHost((int)(juce::Random::getSystemRandom().nextFloat() * (int)LfoType::Noise));
                        parameter->lfoRate->setValueNotifyingHost(juce::Random::getSystemRandom().nextFloat() * 0.1);
                    }
                }
            }
			effect->enabled->setValueNotifyingHost(juce::Random::getSystemRandom().nextFloat() > 0.8);
		}

        // shuffle precedence
        std::random_device rd;
        std::mt19937 g(rd());
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
            data.push_back(effect);
        }
    }

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
    
    static const int ROW_HEIGHT = 30;

protected:
    Effect& effect;
    ComponentListModel listModel;
    juce::ListBox list;
    juce::ToggleButton selected;
private:
    OscirenderAudioProcessor& audioProcessor;
    OscirenderAudioProcessorEditor& editor;

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

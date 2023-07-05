#include "EffectsListComponent.h"

EffectsListComponent::EffectsListComponent(DraggableListBox& lb, AudioEffectListBoxItemData& data, int rn, std::shared_ptr<EffectComponent> effectComponent) : DraggableListBoxItem(lb, data, rn), effectComponent(effectComponent) {
	addAndMakeVisible(*effectComponent);

	effectComponent->slider.setValue(data.getValue(rn), juce::dontSendNotification);
	effectComponent->slider.onValueChange = [this] {
        ((AudioEffectListBoxItemData&)modelData).setValue(rowNum, this->effectComponent->slider.getValue());
	};

	bool isSelected = false;

	{
		juce::SpinLock::ScopedLockType lock(data.audioProcessor.effectsLock);
		// check if effect is in audioProcessor enabled effects
		for (auto effect : data.audioProcessor.enabledEffects) {
			if (effect->getId() == data.getId(rn)) {
				isSelected = true;
				break;
			}
		}
	}
	effectComponent->selected.setToggleState(isSelected, juce::dontSendNotification);
	effectComponent->selected.onClick = [this] {
		auto data = (AudioEffectListBoxItemData&)modelData;
		juce::SpinLock::ScopedLockType lock(data.audioProcessor.effectsLock);
		((AudioEffectListBoxItemData&)modelData).setSelected(rowNum, this->effectComponent->selected.getToggleState());
	};
}

EffectsListComponent::~EffectsListComponent() {}

void EffectsListComponent::paint(juce::Graphics& g) {
	DraggableListBoxItem::paint(g);
	auto bounds = getLocalBounds();
	bounds.removeFromLeft(20);
	// draw drag and drop handle using circles
	g.setColour(juce::Colours::white);
	double size = 4;
	double leftPad = 4;
	double spacing = 7;
	double topPad = 7;
	g.fillEllipse(leftPad, topPad, size, size);
	g.fillEllipse(leftPad, topPad + spacing, size, size);
	g.fillEllipse(leftPad, topPad + 2 * spacing, size, size);
	g.fillEllipse(leftPad + spacing, topPad, size, size);
	g.fillEllipse(leftPad + spacing, topPad + spacing, size, size);
	g.fillEllipse(leftPad + spacing, topPad + 2 * spacing, size, size);
}

void EffectsListComponent::resized() {
	auto area = getLocalBounds();
	area.removeFromLeft(20);
	effectComponent->setBounds(area);
}

juce::Component* EffectsListBoxModel::refreshComponentForRow(int rowNumber, bool isRowSelected, juce::Component *existingComponentToUpdate) {
    std::unique_ptr<EffectsListComponent> item(dynamic_cast<EffectsListComponent*>(existingComponentToUpdate));
    if (juce::isPositiveAndBelow(rowNumber, modelData.getNumItems())) {
		auto data = (AudioEffectListBoxItemData&)modelData;
		std::shared_ptr<EffectComponent> effectComponent = std::make_shared<EffectComponent>(0, 1, 0.01, 0, data.getText(rowNumber), data.getId(rowNumber));
        item = std::make_unique<EffectsListComponent>(listBox, (AudioEffectListBoxItemData&)modelData, rowNumber, effectComponent);
    }
    return item.release();
}
